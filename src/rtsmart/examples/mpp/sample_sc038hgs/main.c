/* SPDX-License-Identifier: Apache-2.0 */
/* MIPI -> MCM -> RAW memory, without ISP or a calibration database. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "mpi_sensor_api.h"
#include "mpi_sys_api.h"
#include "mpi_vb_api.h"
#include "mpi_vicap_api.h"
#include "k_sensor_sc038hgs.h"

#define RAW_BYTES (SC038HGS_WIDTH * SC038HGS_HEIGHT * 2U)
#define BUFFER_BYTES ((RAW_BYTES + 4095U) & ~4095U)
#define CAPTURE_DEV VICAP_DEV_ID_0
#define CAPTURE_CHN VICAP_CHN_ID_0

static int number(const char *str, unsigned limit, unsigned *out)
{
    char *end;
    unsigned long value = strtoul(str, &end, 10);
    if (!*str || *end || *str == '-' || value > limit)
        return -1;
    *out = value;
    return 0;
}

static double now_seconds(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now))
        return 0;
    return now.tv_sec + now.tv_nsec / 1e9;
}

static int save_raw(const char *path, const k_video_frame *frame)
{
    /* MCM RAW10 occupies a 16-bit word per sample in this SDK. */
    if (frame->width != SC038HGS_WIDTH || frame->height != SC038HGS_HEIGHT ||
        frame->pixel_format != PIXEL_FORMAT_RGB_BAYER_10BPP ||
        (frame->stride[0] && frame->stride[0] != SC038HGS_WIDTH * 2U)) {
        fprintf(stderr, "Unexpected RAW format, dimensions or stride\n");
        return -1;
    }
    void *data = kd_mpi_sys_mmap(frame->phys_addr[0], RAW_BYTES);
    if (!data)
        return -1;
    FILE *file = fopen(path, "wb");
    int ret = -1;
    if (file) {
        size_t written = fwrite(data, 1, RAW_BYTES, file);
        int closed = fclose(file);
        ret = written == RAW_BYTES && !closed ? 0 : -1;
    }
    if (kd_mpi_sys_munmap(data, RAW_BYTES))
        ret = -1;
    return ret;
}

int main(int argc, char **argv)
{
    unsigned csi = 2, pattern = 1, frames = 120;
    k_vicap_sensor_type type = SENSOR_TYPE_MAX;
    k_vicap_sensor_info info = {0};
    k_vicap_dev_attr attr = {0};
    k_vicap_chn_attr channel = {0};
    k_vicap_sensor_attr sensor = {.dev_num = CAPTURE_DEV};
    k_vb_config vb = {.max_pool_cnt = 16};
    k_bool vb_ready = K_FALSE, initialized = K_FALSE, started = K_FALSE;
    k_bool sensor_ready = K_FALSE;
    k_s32 ret = -1;
    k_u32 hts_hi, hts_lo, vts_hi, vts_lo;
    double first_time = 0, last_time = 0;
    k_u64 first_pts = 0, last_pts = 0;
    k_u32 first_ref = 0, last_ref = 0;

    if (argc > 5 || (argc > 1 && number(argv[1], 2, &csi)) ||
        (argc > 2 && number(argv[2], 1, &pattern)) ||
        (argc > 3 && number(argv[3], 100000, &frames)) || frames < 4) {
        fprintf(stderr, "Usage: %s [csi=2] [gradient=1|image=0] [frames=120] [raw-file]\n", argv[0]);
        return 1;
    }
    switch (csi) {
#ifdef CONFIG_MPP_ENABLE_CSI_DEV_0
    case 0: type = SC038HGS_MIPI_CSI0_640X480_10BIT_LINEAR; break;
#endif
#ifdef CONFIG_MPP_ENABLE_CSI_DEV_1
    case 1: type = SC038HGS_MIPI_CSI1_640X480_10BIT_LINEAR; break;
#endif
#ifdef CONFIG_MPP_ENABLE_CSI_DEV_2
    case 2: type = SC038HGS_MIPI_CSI2_640X480_10BIT_LINEAR; break;
#endif
    default: fprintf(stderr, "CSI%u is disabled in this build\n", csi); return 1;
    }

#define CHECK(call) do { ret = (call); if (ret) { \
    fprintf(stderr, "%s failed: %d\n", #call, ret); goto done; } } while (0)
    CHECK(kd_mpi_vicap_get_sensor_info(type, &info));
    CHECK(kd_mpi_vb_set_config(&vb));
    CHECK(kd_mpi_vb_init());
    vb_ready = K_TRUE;
    attr.acq_win.width = info.width;
    attr.acq_win.height = info.height;
    attr.input_type = VICAP_INPUT_TYPE_SENSOR;
    attr.mode = VICAP_WORK_ONLY_MCM_MODE;
    attr.sensor_info = info;
    attr.buffer_num = 6;
    attr.buffer_size = BUFFER_BYTES;
    attr.buffer_pool_id = VB_INVALID_POOLID;
    CHECK(kd_mpi_vicap_set_dev_attr(CAPTURE_DEV, attr));
    channel.out_win = attr.acq_win;
    channel.crop_win = channel.scale_win = channel.out_win;
    channel.chn_enable = K_TRUE;
    channel.pix_format = PIXEL_FORMAT_RGB_BAYER_10BPP;
    channel.buffer_num = 6;
    channel.buffer_size = BUFFER_BYTES;
    channel.buffer_pool_id = VB_INVALID_POOLID;
    channel.alignment = 12;
    CHECK(kd_mpi_vicap_set_chn_attr(CAPTURE_DEV, CAPTURE_CHN, channel));
    CHECK(kd_mpi_vicap_init(CAPTURE_DEV));
    initialized = K_TRUE;
    CHECK(kd_mpi_vicap_get_sensor_fd(&sensor));
    sensor_ready = K_TRUE;
    /* The SDK's ONLY_MCM path powers the sensor down and skips sensor_init.
     * Configure it explicitly after the receiver has been initialized. */
    CHECK(kd_mpi_sensor_power_set(sensor.sensor_fd, K_TRUE));
    k_sensor_mode mode = {.sensor_type = type};
    CHECK(kd_mpi_sensor_init(sensor.sensor_fd, mode));
    k_sensor_test_pattern tpg = {.enable = pattern};
    CHECK(kd_mpi_sensor_tpg_set(sensor.sensor_fd, tpg));
    CHECK(kd_mpi_sensor_reg_read(sensor.sensor_fd, 0x320c, &hts_hi));
    CHECK(kd_mpi_sensor_reg_read(sensor.sensor_fd, 0x320d, &hts_lo));
    CHECK(kd_mpi_sensor_reg_read(sensor.sensor_fd, 0x320e, &vts_hi));
    CHECK(kd_mpi_sensor_reg_read(sensor.sensor_fd, 0x320f, &vts_lo));
    printf("SC038HGS CSI%u, %s, HTS=%u VTS=%u\n", csi,
           pattern ? "gradient" : "image", ((hts_hi & 15) << 8) | hts_lo,
           (vts_hi << 8) | vts_lo);
    CHECK(kd_mpi_vicap_start_stream(CAPTURE_DEV));
    started = K_TRUE;
    CHECK(kd_mpi_sensor_stream_enable(sensor.sensor_fd, K_TRUE));
    for (unsigned i = 0; i < frames; ++i) {
        k_video_frame_info frame = {0};
        CHECK(kd_mpi_vicap_dump_frame(CAPTURE_DEV, CAPTURE_CHN, VICAP_DUMP_RAW, &frame, 3000));
        double received = now_seconds();
        if (!i) {
            first_time = received;
            first_pts = frame.v_frame.pts;
            first_ref = frame.v_frame.time_ref;
        }
        last_time = received;
        last_pts = frame.v_frame.pts;
        last_ref = frame.v_frame.time_ref;
        /* Keep per-frame logging out of the timing measurement. */
        if (i == frames - 1) {
            printf("RAW %ux%u format=%u stride=%u pts=%llu time_ref=%u\n",
                   frame.v_frame.width, frame.v_frame.height, frame.v_frame.pixel_format,
                   frame.v_frame.stride[0], (unsigned long long)frame.v_frame.pts,
                   frame.v_frame.time_ref);
            if (argc == 5)
                ret = save_raw(argv[4], &frame.v_frame);
        }
        k_s32 released = kd_mpi_vicap_dump_release(CAPTURE_DEV, CAPTURE_CHN, &frame);
        if (ret || released) {
            if (!ret) ret = released;
            goto done;
        }
    }
    printf("PTS %llu -> %llu, time_ref %u -> %u\n",
           (unsigned long long)first_pts, (unsigned long long)last_pts, first_ref, last_ref);
    if (last_time > first_time) {
        double fps = (frames - 1) / (last_time - first_time);
        double pclk = fps * (((hts_hi & 15) << 8) | hts_lo) * ((vts_hi << 8) | vts_lo);
        printf("Received %u frames, arrival rate %.3f fps, estimated Pixel CLK %.0f Hz\n",
               frames, fps, pclk);
        printf("Repeat with longer runs to check frame drops before using the clock estimate.\n");
    }
done:
    if (sensor_ready) {
        k_s32 stopped = kd_mpi_sensor_stream_enable(sensor.sensor_fd, K_FALSE);
        if (!ret) ret = stopped;
    }
    if (started) {
        k_s32 stopped = kd_mpi_vicap_stop_stream(CAPTURE_DEV);
        if (!ret) ret = stopped;
    }
    if (sensor_ready) {
        k_sensor_test_pattern normal = {0};
        k_s32 restored = kd_mpi_sensor_tpg_set(sensor.sensor_fd, normal);
        if (!ret) ret = restored;
        k_s32 powered_off = kd_mpi_sensor_power_set(sensor.sensor_fd, K_FALSE);
        if (!ret) ret = powered_off;
    }
    if (initialized) {
        k_s32 deinitialized = kd_mpi_vicap_deinit(CAPTURE_DEV);
        if (!ret) ret = deinitialized;
    }
    if (vb_ready) {
        k_s32 exited = kd_mpi_vb_exit();
        if (!ret) ret = exited;
    }
    return ret ? 1 : 0;
}
