#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "py/obj.h"
#include "py/runtime.h"

#include "mpthread.h"

#include "mpp_vb_mgmt.h"

#include "mpi_vicap_api.h"

#define VB_MGMT_VICAP_STOP_STREAM_DELAY_US (1000 * 100)
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
// #define VB_MGMT_RECORD_MAX_CNT (128)
// #define VB_MGMT_RECORD_MAGIC_IN_USE (0x1234ABCD)

// struct vb_record_t
// {
//     void *virt_addr; // self.virt_addr = virt_addr
//     k_u32 magic;
//     k_u32 size;             // self.size = size, user set
//     k_vb_blk_handle handle; // self.handle = handle
// };

// static struct vb_record_t vb_records[VB_MGMT_RECORD_MAX_CNT];
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
/* vicap mgmt */
static k_u8 vicap_dev_stat[VICAP_MAX_DEV_NUMS];

static k_s32 vb_mgmt_stop_vicap_stream(k_u32 id)
{
    if (0x00 == vicap_dev_stat[id])
    {
        return 0;
    }

    if (0x00 != kd_mpi_vicap_stop_stream(id))
    {
        printf("vb_mgmt stop vicap %d stream failed.\n", id);
        return 1;
    }

    return 0;
}

static k_s32 vb_mgmt_deinit_vicap(k_u32 id)
{
    if (0x00 == vicap_dev_stat[id])
    {
        return 0;
    }

    if (0x00 != kd_mpi_vicap_deinit(id))
    {
        printf("vb_mgmt deinit vicap %d failed.\n", id);
        vicap_dev_stat[id] = 0;
        return 1;
    }

    vicap_dev_stat[id] = 0;

    return 0;
}

k_s32 vb_mgmt_vicap_dev_inited(k_u32 id)
{
    if (id > VICAP_MAX_DEV_NUMS)
    {
        return 1;
    }

    vicap_dev_stat[id] = 1;

    return 0;
}

k_s32 vb_mgmt_vicap_dev_deinited(k_u32 id)
{
    if (id > VICAP_MAX_DEV_NUMS)
    {
        return 1;
    }

    vicap_dev_stat[id] = 0;

    return 0;
}
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
/* mpp link mgmt */
#define VB_MGMT_VICAP_IMAGE_MAX_CNT (32)
#define VB_MGMT_VICAP_IMAGE_MAGIC_IN_USE (0x1234DEAD)

static vb_mgmt_vicap_image vicap_images[VB_MGMT_VICAP_IMAGE_MAX_CNT];
/* Set once a record's rows have been compacted; cleared on dump and release. */
static k_u8 vicap_images_packed[VB_MGMT_VICAP_IMAGE_MAX_CNT];

/* The VICAP ISP writes RGB888 and YUV420 rows at a 16-pixel aligned pitch,
 * while k_video_frame.stride reports the visible width. Board-measured on
 * K230D: 1080-wide rows are 1088 pixels apart; 528- and 544-wide rows are
 * not padded; the YUV420 UV plane starts at phys_addr[1] = pitch * height. */
#define VB_MGMT_VICAP_PITCH_ALIGN (16U)

typedef struct {
    k_u32 planes;
    k_u32 row_bytes[3];   /* visible bytes per row */
    k_u32 pitch_bytes[3]; /* hardware bytes per row */
    k_u32 rows[3];
    k_u64 src_offset[3];  /* plane start in the hardware frame */
    k_u64 dst_offset[3];  /* plane start once rows are packed */
    k_u64 map_size;       /* bytes the hardware frame occupies */
} vb_mgmt_frame_layout;

static k_u64 vb_mgmt_plane_offset(const k_video_frame *frame, int plane, k_u64 fallback)
{
    if (frame->phys_addr[plane] > frame->phys_addr[0]) {
        return frame->phys_addr[plane] - frame->phys_addr[0];
    }
    return fallback;
}

/* Returns 0 with the padded layout of formats the ISP pads, -1 otherwise. */
static k_s32 vb_mgmt_frame_layout_get(const k_video_frame *frame, vb_mgmt_frame_layout *layout)
{
    const k_u64 width = frame->width;
    const k_u64 height = frame->height;
    const k_u64 pitch = (width + VB_MGMT_VICAP_PITCH_ALIGN - 1) & ~(k_u64)(VB_MGMT_VICAP_PITCH_ALIGN - 1);

    memset(layout, 0, sizeof(*layout));
    switch (frame->pixel_format) {
    case PIXEL_FORMAT_RGB_888:
    case PIXEL_FORMAT_BGR_888:
        layout->planes = 1;
        layout->row_bytes[0] = width * 3;
        layout->pitch_bytes[0] = pitch * 3;
        layout->rows[0] = height;
        break;
    case PIXEL_FORMAT_YUV_SEMIPLANAR_420:
    case PIXEL_FORMAT_YVU_SEMIPLANAR_420:
        layout->planes = 2;
        layout->row_bytes[0] = layout->row_bytes[1] = width;
        layout->pitch_bytes[0] = layout->pitch_bytes[1] = pitch;
        layout->rows[0] = height;
        layout->rows[1] = height / 2;
        layout->src_offset[1] = vb_mgmt_plane_offset(frame, 1, pitch * height);
        layout->dst_offset[1] = width * height;
        break;
    case PIXEL_FORMAT_RGB_888_PLANAR:
    case PIXEL_FORMAT_BGR_888_PLANAR:
        layout->planes = 3;
        for (int plane = 0; plane < 3; plane++) {
            layout->row_bytes[plane] = width;
            layout->pitch_bytes[plane] = pitch;
            layout->rows[plane] = height;
            layout->src_offset[plane] = vb_mgmt_plane_offset(frame, plane, plane * pitch * height);
            layout->dst_offset[plane] = plane * width * height;
        }
        break;
    default:
        return -1;
    }

    for (k_u32 plane = 0; plane < layout->planes; plane++) {
        k_u64 end = layout->src_offset[plane] + (k_u64)layout->pitch_bytes[plane] * layout->rows[plane];
        if (end > layout->map_size) {
            layout->map_size = end;
        }
    }
    return 0;
}

static int vb_mgmt_find_vicap_record(const vb_mgmt_vicap_image *image)
{
    if ((NULL == image) || (VB_MGMT_VICAP_IMAGE_MAGIC_IN_USE != image->magic)) {
        return -1;
    }

    for (int i = 0; i < VB_MGMT_VICAP_IMAGE_MAX_CNT; i++) {
        if ((VB_MGMT_VICAP_IMAGE_MAGIC_IN_USE == vicap_images[i].magic) &&
            (image->vf_info.v_frame.phys_addr[0] == vicap_images[i].vf_info.v_frame.phys_addr[0]) &&
            (image->vf_info.v_frame.virt_addr[0] == vicap_images[i].vf_info.v_frame.virt_addr[0]) &&
            (image->cfg.dev_num == vicap_images[i].cfg.dev_num) &&
            (image->cfg.chn_num == vicap_images[i].cfg.chn_num)) {
            return i;
        }
    }
    return -1;
}

k_s32 vb_mgmt_dump_vicap_frame(vb_mgmt_dump_vicap_config *cfg, vb_mgmt_vicap_image *image)
{
    vb_mgmt_vicap_image *_image = NULL;

    if((NULL == cfg) || (NULL == image)) {
        return 1;
    }

    memset(image, 0, sizeof(*image));

    for (int i = 0; i < VB_MGMT_VICAP_IMAGE_MAX_CNT; i++)
    {
        if (VB_MGMT_VICAP_IMAGE_MAGIC_IN_USE != vicap_images[i].magic)
        {
            _image = &vicap_images[i];

            _image->magic = VB_MGMT_VICAP_IMAGE_MAGIC_IN_USE;
            vicap_images_packed[i] = 0;

            break;
        }
    }

    if(NULL == _image) {
        printf("no space to record vicap_image\n");

        return 2;
    }

    memcpy(&_image->cfg, cfg, sizeof(*cfg));

    MP_THREAD_GIL_EXIT();

    if(0x00 != kd_mpi_vicap_dump_frame(cfg->dev_num, cfg->chn_num, cfg->foramt, &_image->vf_info, cfg->milli_sec)) {
        MP_THREAD_GIL_ENTER();

        printf("vicap dump dev %u chn %u failed.\n", cfg->dev_num, cfg->chn_num);

        _image->magic = 0x00;

        return 3;
    }
    MP_THREAD_GIL_ENTER();

    k_u32 img_width = _image->vf_info.v_frame.width;
    k_u32 img_height = _image->vf_info.v_frame.height;

    extern k_u32 calc_video_size(k_pixel_format video_fmt, k_u16 width, k_u16 height);
    _image->image_size = calc_video_size(_image->vf_info.v_frame.pixel_format, img_width, img_height);

    if(0x00 == _image->image_size) {
        printf("unsupport image format %u\n", _image->vf_info.v_frame.pixel_format);

        kd_mpi_vicap_dump_release(_image->cfg.dev_num, _image->cfg.chn_num, &_image->vf_info);
        _image->magic = 0x00;

        return 4;
    }

    /* Map, invalidate and later flush the whole padded frame: padded rows
     * extend past width * height * bytes-per-pixel. */
    vb_mgmt_frame_layout layout;
    if ((0x00 == vb_mgmt_frame_layout_get(&_image->vf_info.v_frame, &layout)) &&
        (layout.map_size > _image->image_size)) {
        _image->image_size = layout.map_size;
    }

    _image->vf_info.v_frame.virt_addr[0] = (k_u64)kd_mpi_sys_mmap_cached(_image->vf_info.v_frame.phys_addr[0], _image->image_size);

    if(0x00 == _image->vf_info.v_frame.virt_addr[0])
    {
        printf("mmap failed.\n");

        kd_mpi_vicap_dump_release(_image->cfg.dev_num, _image->cfg.chn_num, &_image->vf_info);
        _image->magic = 0x00;

        return 5;
    }

    /* DMA has produced a new frame. Mapping cached memory does not discard
     * cache lines left by the previous owner of this physical VB block. */
    if (0x00 != kd_mpi_sys_mmz_invalidate_cache(
            _image->vf_info.v_frame.phys_addr[0],
            (void *)_image->vf_info.v_frame.virt_addr[0], _image->image_size)) {
        printf("invalidate vicap image cache failed.\n");
        vb_mgmt_release_vicap_frame(_image);
        return 6;
    }

    memcpy(image, _image, sizeof(*_image));

    return 0;
}

k_s32 vb_mgmt_pack_vicap_frame(vb_mgmt_vicap_image *image)
{
    int index = vb_mgmt_find_vicap_record(image);
    vb_mgmt_frame_layout layout;

    if ((index < 0) || (0 == vicap_images[index].vf_info.v_frame.virt_addr[0])) {
        return 1;
    }
    if (vicap_images_packed[index] ||
        (0x00 != vb_mgmt_frame_layout_get(&vicap_images[index].vf_info.v_frame, &layout))) {
        vicap_images_packed[index] = 1;
        return 0;
    }

    /* Rows move to lower addresses only: each destination row ends at or
     * before the next source row, and plane k + 1 starts after plane k. */
    k_u8 *base = (k_u8 *)(uintptr_t)vicap_images[index].vf_info.v_frame.virt_addr[0];
    for (k_u32 plane = 0; plane < layout.planes; plane++) {
        if ((layout.row_bytes[plane] == layout.pitch_bytes[plane]) &&
            (layout.dst_offset[plane] == layout.src_offset[plane])) {
            continue;
        }
        for (k_u32 row = 0; row < layout.rows[plane]; row++) {
            memmove(base + layout.dst_offset[plane] + (k_u64)row * layout.row_bytes[plane],
                    base + layout.src_offset[plane] + (k_u64)row * layout.pitch_bytes[plane],
                    layout.row_bytes[plane]);
        }
    }
    vicap_images_packed[index] = 1;
    return 0;
}

k_s32 vb_mgmt_release_vicap_frame(vb_mgmt_vicap_image *image)
{
    vb_mgmt_vicap_image *record = NULL;
    k_s32 ret;

    int index = vb_mgmt_find_vicap_record(image);
    if (index < 0) {
        return 1;
    }
    record = &vicap_images[index];

    if (record->vf_info.v_frame.virt_addr[0]) {
        /* RGB888 -> RGB565 and drawing can dirty this borrowed VB mapping.
         * Clean and invalidate it while the frame is still owned, before DMA
         * is allowed to reuse it. A late writeback would corrupt a new frame. */
        ret = kd_mpi_sys_mmz_flush_cache(record->vf_info.v_frame.phys_addr[0],
                (void *)record->vf_info.v_frame.virt_addr[0], record->image_size);
        if (ret) {
            printf("flush vicap image cache failed.\n");
            return ret;
        }
        ret = kd_mpi_sys_munmap((void *)record->vf_info.v_frame.virt_addr[0], record->image_size);
        if (ret) {
            printf("release image failed(1).\n");
            return ret;
        }
        /* Preserve this state if the driver release needs to be retried. */
        record->vf_info.v_frame.virt_addr[0] = 0;
        image->vf_info.v_frame.virt_addr[0] = 0;
    }

    ret = kd_mpi_vicap_dump_release(record->cfg.dev_num, record->cfg.chn_num, &record->vf_info);
    if (ret) {
        printf("release image failed(2).\n");
        return ret;
    }

    record->magic = 0;
    vicap_images_packed[index] = 0;
    image->magic = 0;
    return 0;
}
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
k_s32 vb_mgmt_init(void)
{
    for (int i = 0; i < VB_MGMT_VICAP_IMAGE_MAX_CNT; i++)
    {
        if (VB_MGMT_VICAP_IMAGE_MAGIC_IN_USE == vicap_images[i].magic)
        {
            printf("maybe not call vb_mgmt_deinit, the vicap images record %d is in use\n", i);

            vb_mgmt_release_vicap_frame(&vicap_images[i]);
        }

        vicap_images[i].magic = 0;
    }

    return 0;
}

k_s32 vb_mgmt_deinit(void)
{
    for (int i = 0; i < VB_MGMT_VICAP_IMAGE_MAX_CNT; i++)
    {
        if (VB_MGMT_VICAP_IMAGE_MAGIC_IN_USE == vicap_images[i].magic)
        {
            vb_mgmt_release_vicap_frame(&vicap_images[i]);
        }
    }

    for (int i = 0; i < VICAP_MAX_DEV_NUMS; i++)
    {
        vb_mgmt_stop_vicap_stream(i);
    }

    usleep(VB_MGMT_VICAP_STOP_STREAM_DELAY_US);

    for (int i = 0; i < VICAP_MAX_DEV_NUMS; i++)
    {
        vb_mgmt_deinit_vicap(i);
    }

    return 0;
}
