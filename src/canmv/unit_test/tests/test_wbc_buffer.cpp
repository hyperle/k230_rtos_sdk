#include <gtest/gtest.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

extern "C" {
#include "kd_display.h"
#include "mpi_gsdma_api.h"
#include "mpi_sys_api.h"
#include "mpi_vb_api.h"
#include "mpi_venc_api.h"
void ide_dbg_vo_wbc_start(int enable);
void ide_dbg_vo_wbc_stop(void);
int ide_dbg_vo_wbc_dump_and_encode(void **buffer, size_t *size,
                                   uint32_t *width, uint32_t *height);
}

namespace {
std::vector<unsigned char> output(640 * 480 * 2);
std::vector<unsigned char> cached_output(output.size());
const unsigned char jpeg[] = {0xff, 0xd8, 0x12, 0x34, 0xff, 0xd9};
k_u64 held_frame;
k_u64 next_frame;
k_u64 submitted_frame;
int dump_calls, send_calls, stop_calls, request_error, query_error, stream_error;
bool encoder_reading;
int dma_error, invalidate_error, dma_calls, invalidate_calls, release_stream_calls;
bool partial_dma;

class WbcBufferTest : public ::testing::Test {
protected:
    void SetUp() override {
        ide_dbg_vo_wbc_stop();
        held_frame = submitted_frame = 0;
        next_frame = 0x10000;
        dump_calls = send_calls = stop_calls = request_error = query_error = stream_error = 0;
        encoder_reading = false;
        dma_error = invalidate_error = dma_calls = invalidate_calls = release_stream_calls = 0;
        partial_dma = false;
        std::fill(output.begin(), output.end(), 0x31);
        std::fill(cached_output.begin(), cached_output.end(), 0x81);
        ide_dbg_vo_wbc_start(1);
    }
    void TearDown() override { ide_dbg_vo_wbc_stop(); }

    void ExpectValidJpeg() {
        void *buffer = nullptr;
        size_t size = 0;
        uint32_t width = 0, height = 0;
        ASSERT_EQ(ide_dbg_vo_wbc_dump_and_encode(&buffer, &size, &width, &height), 0);
        ASSERT_NE(buffer, nullptr);
        ASSERT_EQ(size, sizeof(jpeg));
        EXPECT_EQ(std::memcmp(buffer, jpeg, size), 0);
        EXPECT_EQ(width, 640u);
        EXPECT_EQ(height, 480u);
        EXPECT_EQ(held_frame, 0u);
        EXPECT_FALSE(encoder_reading);
    }
};
} // namespace

extern "C" {
void ide_dbg_enable_vo_wbc(void) {}
int py_display_wbc_quality(void) { return 80; }

// Model the production display helper: acquiring another WBC frame releases
// the previously held frame. A frame descriptor does not retain ownership.
int py_display_wbc_dump(k_video_frame_info *frame, int timeout, int force) {
    EXPECT_FALSE(encoder_reading);
    ++dump_calls;
    held_frame = next_frame++;
    std::memset(frame, 0, sizeof(*frame));
    frame->v_frame.width = 640;
    frame->v_frame.height = 480;
    frame->v_frame.phys_addr[0] = held_frame;
    return 0;
}
int py_display_wbc_dump_relase(void) {
    EXPECT_FALSE(encoder_reading) << "WBC buffer returned while VENC still reads it";
    held_frame = 0;
    return 0;
}
k_s32 kd_display_get_resolution(k_u32 *width, k_u32 *height) { *width = 640; *height = 480; return 0; }
k_s32 kd_mpi_vo_get_dev_attr(k_vo_dev_attr *attr) { std::memset(attr, 0, sizeof(*attr)); return 0; }
k_s32 kd_mpi_venc_request_chn(k_u32 *channel) { if (request_error) return request_error; *channel = 1; return 0; }
k_s32 kd_mpi_venc_release_chn(k_u32 channel) { return 0; }
k_s32 kd_mpi_venc_create_chn(k_u32 channel, k_venc_chn_attr *attr) { return 0; }
k_s32 kd_mpi_venc_start_chn(k_u32 channel) { return 0; }
k_s32 kd_mpi_venc_stop_chn(k_u32 channel) { ++stop_calls; encoder_reading = false; return 0; }
k_s32 kd_mpi_venc_destroy_chn(k_u32 channel) { return 0; }
k_s32 kd_mpi_venc_attach_vb_pool_ex(k_u32 channel, k_u32 pool, k_s32 count) { return 0; }
k_s32 kd_mpi_venc_detach_vb_pool(k_u32 channel) { return 0; }
k_s32 kd_mpi_venc_set_rotation(k_u32 channel, const k_rotation rotation) { return 0; }
k_s32 kd_mpi_venc_send_frame(k_u32 channel, k_video_frame_info *frame, k_s32 timeout) {
    ++send_calls;
    submitted_frame = frame->v_frame.phys_addr[0];
    if (held_frame == 0 || submitted_frame != held_frame) return -31;
    encoder_reading = true;
    return 0;
}
k_s32 kd_mpi_venc_query_status(k_u32 channel, k_venc_chn_status *status) {
    if (query_error) return query_error;
    std::memset(status, 0, sizeof(*status)); status->cur_packs = 1; return 0;
}
k_s32 kd_mpi_venc_get_stream(k_u32 channel, k_venc_stream *stream, k_s32 timeout) {
    EXPECT_TRUE(encoder_reading);
    EXPECT_EQ(held_frame, submitted_frame);
    if (stream_error) return stream_error;
    stream->pack_cnt = 1;
    stream->pack[0].phys_addr = reinterpret_cast<uintptr_t>(jpeg);
    stream->pack[0].len = sizeof(jpeg);
    encoder_reading = false;
    return 0;
}
k_s32 kd_mpi_venc_release_stream(k_u32 channel, k_venc_stream *stream) { ++release_stream_calls; return 0; }
k_s32 kd_mpi_vb_create_pool(k_vb_pool_config *config) { return 2; }
k_s32 kd_mpi_vb_destory_pool(k_u32 pool) { return 0; }
k_vb_blk_handle kd_mpi_vb_get_block(k_u32 pool, k_u64 size, const k_char *name) {
    EXPECT_LE(size, output.size()); return 3;
}
k_s32 kd_mpi_vb_release_block(k_vb_blk_handle block) { return 0; }
k_u64 kd_mpi_vb_handle_to_phyaddr(k_vb_blk_handle block) { return reinterpret_cast<uintptr_t>(output.data()); }
void *kd_mpi_sys_mmap_cached(k_u64 address, k_u32 size) {
    EXPECT_EQ(address, reinterpret_cast<uintptr_t>(output.data()));
    return cached_output.data();
}
k_s32 kd_mpi_sys_munmap(void *address, k_u32 size) { return 0; }
k_s32 kd_mpi_sys_mmz_invalidate_cache(k_u64 physical, void *address, k_u32 size) {
    ++invalidate_calls;
    EXPECT_EQ(physical, reinterpret_cast<uintptr_t>(output.data()));
    EXPECT_EQ(address, cached_output.data());
    EXPECT_LE(size, output.size());
    if (invalidate_error) return invalidate_error;
    std::memcpy(address, output.data(), size);
    return 0;
}
void *kd_mpi_dma_memcpy(void *dst, const void *src, size_t size) {
    ++dma_calls;
    if (dma_error) {
        if (partial_dma) std::memcpy(dst, src, size / 2);
        return nullptr;
    }
    return std::memcpy(dst, src, size);
}
}

TEST_F(WbcBufferTest, FirstEncodeKeepsItsInputOwnedAfterInitializationProbe) {
    ExpectValidJpeg();
    EXPECT_EQ(dump_calls, 2); // Initialization probe and the actual encoder input.
    EXPECT_EQ(send_calls, 1);
}

TEST_F(WbcBufferTest, SubsequentEncodesHoldEachInputUntilTheStreamIsReady) {
    for (int frame = 0; frame < 4; ++frame) ExpectValidJpeg();
    EXPECT_EQ(dump_calls, 5);
    EXPECT_EQ(send_calls, 4);
}

TEST_F(WbcBufferTest, InitializationFailureDoesNotSubmitOrRetainAnInputFrame) {
    request_error = -32;
    void *buffer = reinterpret_cast<void *>(1);
    size_t size = 123;
    uint32_t width = 1, height = 1;
    EXPECT_NE(ide_dbg_vo_wbc_dump_and_encode(&buffer, &size, &width, &height), 0);
    EXPECT_EQ(send_calls, 0);
    EXPECT_EQ(held_frame, 0u);
    EXPECT_EQ(buffer, nullptr);
    EXPECT_EQ(size, 0u);
}

TEST_F(WbcBufferTest, StatusFailureStopsEncoderBeforeReturningItsInput) {
    ExpectValidJpeg();
    query_error = -33;
    int stops_before = stop_calls;
    void *buffer = nullptr;
    size_t size = 0;
    uint32_t width = 0, height = 0;
    EXPECT_NE(ide_dbg_vo_wbc_dump_and_encode(&buffer, &size, &width, &height), 0);
    EXPECT_GT(stop_calls, stops_before);
    EXPECT_FALSE(encoder_reading);
    EXPECT_EQ(held_frame, 0u);
    query_error = 0;
    ExpectValidJpeg(); // A failed encode must rebuild the channel for retry.
}

TEST_F(WbcBufferTest, StreamTimeoutStopsEncoderBeforeReturningItsInput) {
    ExpectValidJpeg();
    stream_error = -34;
    int stops_before = stop_calls;
    void *buffer = nullptr;
    size_t size = 0;
    uint32_t width = 0, height = 0;
    EXPECT_NE(ide_dbg_vo_wbc_dump_and_encode(&buffer, &size, &width, &height), 0);
    EXPECT_GT(stop_calls, stops_before);
    EXPECT_FALSE(encoder_reading);
    EXPECT_EQ(held_frame, 0u);
    stream_error = 0;
    ExpectValidJpeg();
}


TEST_F(WbcBufferTest, FailedDmaDoesNotPublishPreviousOutput) {
    dma_error = -34;
    void *buffer = reinterpret_cast<void *>(1);
    size_t size = 123;
    uint32_t width = 1, height = 1;
    EXPECT_NE(ide_dbg_vo_wbc_dump_and_encode(&buffer, &size, &width, &height), 0);
    EXPECT_EQ(buffer, nullptr);
    EXPECT_EQ(size, 0u);
    EXPECT_EQ(width, 0u);
    EXPECT_EQ(height, 0u);
    EXPECT_EQ(dma_calls, 1);
    EXPECT_EQ(invalidate_calls, 0);
    EXPECT_EQ(release_stream_calls, 1);
    EXPECT_EQ(held_frame, 0u);
}

TEST_F(WbcBufferTest, PartialDmaDoesNotPublishMixedJpegAndOldBytes) {
    dma_error = -34;
    partial_dma = true;
    void *buffer = nullptr;
    size_t size = 0;
    uint32_t width = 0, height = 0;
    EXPECT_NE(ide_dbg_vo_wbc_dump_and_encode(&buffer, &size, &width, &height), 0);
    EXPECT_EQ(buffer, nullptr);
    EXPECT_EQ(size, 0u);
    EXPECT_EQ(invalidate_calls, 0);
    EXPECT_EQ(release_stream_calls, 1);
    EXPECT_EQ(std::memcmp(output.data(), jpeg, sizeof(jpeg) / 2), 0);
    EXPECT_EQ(output[sizeof(jpeg) - 1], 0x31);
}

TEST_F(WbcBufferTest, InvalidateFailureDoesNotPublishStaleCachedJpeg) {
    invalidate_error = -35;
    void *buffer = reinterpret_cast<void *>(1);
    size_t size = 123;
    uint32_t width = 1, height = 1;
    EXPECT_NE(ide_dbg_vo_wbc_dump_and_encode(&buffer, &size, &width, &height), 0);
    EXPECT_EQ(buffer, nullptr);
    EXPECT_EQ(size, 0u);
    EXPECT_EQ(width, 0u);
    EXPECT_EQ(height, 0u);
    EXPECT_EQ(dma_calls, 1);
    EXPECT_EQ(invalidate_calls, 1);
    EXPECT_EQ(release_stream_calls, 1);
    EXPECT_EQ(cached_output[0], 0x81);
    EXPECT_EQ(std::memcmp(output.data(), jpeg, sizeof(jpeg)), 0);
}

TEST_F(WbcBufferTest, OutputFailureCanRecoverWithoutReinitializingEncoder) {
    ExpectValidJpeg();
    invalidate_error = -35;
    void *buffer = nullptr;
    size_t size = 0;
    uint32_t width = 0, height = 0;
    EXPECT_NE(ide_dbg_vo_wbc_dump_and_encode(&buffer, &size, &width, &height), 0);
    EXPECT_EQ(buffer, nullptr);
    EXPECT_EQ(size, 0u);
    invalidate_error = 0;
    ExpectValidJpeg();
    EXPECT_EQ(dump_calls, 4); // One initial probe and three encode attempts.
    EXPECT_EQ(send_calls, 3);
    EXPECT_EQ(release_stream_calls, 3);
}
