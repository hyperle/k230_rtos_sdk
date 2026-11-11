#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

extern "C" {
#include "mpp_vb_mgmt.h"
}

namespace {
constexpr std::size_t kImageBytes = 640 * 480 * 3;
constexpr k_u64 kPhysicalAddress = 0x100000;
std::vector<std::uint8_t> physical(kImageBytes);
std::vector<std::uint8_t> cached(kImageBytes);
bool owned;
bool mapped;
std::size_t dirty_bytes;
int invalidate_error;
int flush_error;
int unmap_error;
int release_error;
int map_error;
int dump_calls;
int release_calls;
int flush_calls;
int unmap_calls;

// Model a noncoherent DMA buffer: CPU cache and device memory are separate.
// This deliberately preserves cached contents across mappings of the same VB.
class VicapBufferTest : public ::testing::Test {
protected:
    vb_mgmt_dump_vicap_config cfg{};
    vb_mgmt_vicap_image image{};

    void SetUp() override {
        invalidate_error = flush_error = unmap_error = release_error = map_error = 0;
        vb_mgmt_init();
        owned = mapped = false;
        dirty_bytes = 0;
        dump_calls = release_calls = flush_calls = unmap_calls = 0;
        std::fill(physical.begin(), physical.end(), 0x24);
        std::fill(cached.begin(), cached.end(), 0x81);
        cfg.dev_num = VICAP_DEV_ID_0;
        cfg.chn_num = VICAP_CHN_ID_0;
        cfg.foramt = VICAP_DUMP_YUV;
        cfg.milli_sec = 1000;
    }

    void TearDown() override {
        invalidate_error = flush_error = unmap_error = release_error = map_error = 0;
        vb_mgmt_deinit();
    }

    void WriteConvertedPixels() {
        dirty_bytes = 640 * 480 * 2;
        std::fill(cached.begin(), cached.begin() + dirty_bytes, 0xF8);
    }

    void SimulateNextDmaAndLateCacheEviction() {
        ASSERT_FALSE(owned);
        std::fill(physical.begin(), physical.end(), 0x55);
        if (dirty_bytes) {
            std::copy_n(cached.begin(), dirty_bytes, physical.begin());
            dirty_bytes = 0;
        }
    }
};
} // namespace

extern "C" {
k_u32 calc_video_size(k_pixel_format format, k_u16 width, k_u16 height) {
    return format == PIXEL_FORMAT_RGB_888 ? static_cast<k_u32>(width) * height * 3 : 0;
}

k_s32 kd_mpi_vicap_dump_frame(k_vicap_dev dev, k_vicap_chn chn,
                            k_vicap_dump_format format, k_video_frame_info *frame,
                            k_u32 timeout) {
    ++dump_calls;
    if (owned) return -10;
    owned = true;
    std::memset(frame, 0, sizeof(*frame));
    frame->v_frame.width = 640;
    frame->v_frame.height = 480;
    frame->v_frame.stride[0] = 640 * 3;
    frame->v_frame.pixel_format = PIXEL_FORMAT_RGB_888;
    frame->v_frame.phys_addr[0] = kPhysicalAddress;
    return 0;
}

void *kd_mpi_sys_mmap_cached(k_u64 address, k_u32 size) {
    if (map_error) return nullptr;
    EXPECT_TRUE(owned);
    EXPECT_EQ(address, kPhysicalAddress);
    EXPECT_EQ(size, kImageBytes);
    mapped = true;
    return cached.data();
}

k_s32 kd_mpi_sys_mmz_invalidate_cache(k_u64 address, void *data, k_u32 size) {
    EXPECT_TRUE(owned);
    EXPECT_TRUE(mapped);
    EXPECT_EQ(address, kPhysicalAddress);
    EXPECT_EQ(data, cached.data());
    EXPECT_EQ(size, kImageBytes);
    if (invalidate_error) return invalidate_error;
    std::copy(physical.begin(), physical.end(), cached.begin());
    dirty_bytes = 0;
    return 0;
}

k_s32 kd_mpi_sys_mmz_flush_cache(k_u64 address, void *data, k_u32 size) {
    ++flush_calls;
    EXPECT_TRUE(owned);
    EXPECT_TRUE(mapped);
    EXPECT_EQ(address, kPhysicalAddress);
    EXPECT_EQ(data, cached.data());
    EXPECT_EQ(size, kImageBytes);
    if (flush_error) return flush_error;
    std::copy_n(cached.begin(), dirty_bytes, physical.begin());
    dirty_bytes = 0;
    return 0;
}

k_s32 kd_mpi_sys_munmap(void *data, k_u32 size) {
    ++unmap_calls;
    EXPECT_EQ(data, cached.data());
    EXPECT_EQ(size, kImageBytes);
    if (unmap_error) return unmap_error;
    if (!mapped) return -11;
    mapped = false;
    return 0;
}

k_s32 kd_mpi_vicap_dump_release(k_vicap_dev dev, k_vicap_chn chn,
                              const k_video_frame_info *frame) {
    ++release_calls;
    EXPECT_EQ(frame->v_frame.phys_addr[0], kPhysicalAddress);
    if (release_error) return release_error;
    if (!owned) return -12;
    owned = false;
    return 0;
}

k_s32 kd_mpi_vicap_stop_stream(k_vicap_dev dev) { return 0; }
k_s32 kd_mpi_vicap_deinit(k_vicap_dev dev) { return 0; }
}

TEST_F(VicapBufferTest, NewDmaPixelsReplacePreviousCachedFrame) {
    ASSERT_EQ(vb_mgmt_dump_vicap_frame(&cfg, &image), 0);
    const auto *pixels = reinterpret_cast<const std::uint8_t *>(image.vf_info.v_frame.virt_addr[0]);
    EXPECT_TRUE(std::equal(physical.begin(), physical.end(), pixels));
    EXPECT_EQ(vb_mgmt_release_vicap_frame(&image), 0);
}

TEST_F(VicapBufferTest, ConvertedPixelsCannotOverwriteTheNextDmaFrame) {
    ASSERT_EQ(vb_mgmt_dump_vicap_frame(&cfg, &image), 0);
    WriteConvertedPixels();
    ASSERT_EQ(vb_mgmt_release_vicap_frame(&image), 0);
    SimulateNextDmaAndLateCacheEviction();
    EXPECT_EQ(std::count(physical.begin(), physical.end(), 0x55), kImageBytes);
}

TEST_F(VicapBufferTest, ReusingOneBlockFor120FramesPreservesEveryNewFrame) {
    for (unsigned frame = 1; frame <= 120; ++frame) {
        const auto expected = static_cast<std::uint8_t>(frame);
        std::fill(physical.begin(), physical.end(), expected);
        ASSERT_EQ(vb_mgmt_dump_vicap_frame(&cfg, &image), 0);
        ASSERT_EQ(std::count(cached.begin(), cached.end(), expected), kImageBytes);
        WriteConvertedPixels();
        ASSERT_EQ(vb_mgmt_release_vicap_frame(&image), 0);
        ASSERT_EQ(dirty_bytes, 0U);
    }
}

TEST_F(VicapBufferTest, InvalidateFailureDoesNotPublishAnUnreadableFrame) {
    invalidate_error = -20;
    EXPECT_NE(vb_mgmt_dump_vicap_frame(&cfg, &image), 0);
    EXPECT_EQ(image.magic, 0U);
    EXPECT_FALSE(mapped);
    EXPECT_FALSE(owned);
    EXPECT_EQ(release_calls, 1);
}

TEST_F(VicapBufferTest, MapFailureReturnsTheAcquiredFrame) {
    map_error = 1;
    EXPECT_NE(vb_mgmt_dump_vicap_frame(&cfg, &image), 0);
    EXPECT_EQ(image.magic, 0U);
    EXPECT_FALSE(owned);
    EXPECT_EQ(release_calls, 1);
}

TEST_F(VicapBufferTest, FlushFailureKeepsDirtyFrameOwnedForRetry) {
    ASSERT_EQ(vb_mgmt_dump_vicap_frame(&cfg, &image), 0);
    WriteConvertedPixels();
    flush_error = -21;
    EXPECT_NE(vb_mgmt_release_vicap_frame(&image), 0);
    EXPECT_TRUE(owned);
    EXPECT_TRUE(mapped);
    EXPECT_EQ(unmap_calls, 0);
    EXPECT_EQ(release_calls, 0);
    flush_error = 0;
    EXPECT_EQ(vb_mgmt_release_vicap_frame(&image), 0);
}

TEST_F(VicapBufferTest, UnmapFailureDoesNotReturnAStillMappedFrame) {
    ASSERT_EQ(vb_mgmt_dump_vicap_frame(&cfg, &image), 0);
    unmap_error = -22;
    EXPECT_NE(vb_mgmt_release_vicap_frame(&image), 0);
    EXPECT_TRUE(owned);
    EXPECT_TRUE(mapped);
    EXPECT_EQ(release_calls, 0);
    unmap_error = 0;
    EXPECT_EQ(vb_mgmt_release_vicap_frame(&image), 0);
}

TEST_F(VicapBufferTest, DriverReleaseFailureRetriesWithoutUsingTheUnmappedAddress) {
    ASSERT_EQ(vb_mgmt_dump_vicap_frame(&cfg, &image), 0);
    release_error = -23;
    EXPECT_NE(vb_mgmt_release_vicap_frame(&image), 0);
    EXPECT_FALSE(mapped);
    EXPECT_TRUE(owned);
    release_error = 0;
    EXPECT_EQ(vb_mgmt_release_vicap_frame(&image), 0);
    EXPECT_EQ(unmap_calls, 1);
    EXPECT_EQ(flush_calls, 1);
}

TEST_F(VicapBufferTest, ReleasingTheSameHandleTwiceDoesNotTouchTheReusedBlock) {
    ASSERT_EQ(vb_mgmt_dump_vicap_frame(&cfg, &image), 0);
    ASSERT_EQ(vb_mgmt_release_vicap_frame(&image), 0);
    EXPECT_EQ(image.magic, 0U);
    EXPECT_NE(vb_mgmt_release_vicap_frame(&image), 0);
    EXPECT_EQ(release_calls, 1);
    EXPECT_EQ(unmap_calls, 1);
}
