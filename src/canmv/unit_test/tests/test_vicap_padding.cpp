#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <vector>

extern "C" {
#include "mpp_vb_mgmt.h"
}

// The VICAP ISP writes rows at a 16-pixel pitch but reports the visible width
// as the stride (board-measured: 1080-wide RGB888/YUV420 rows are 1088 apart).
// These host tests model that layout; they do not prove hardware timing.
namespace {
constexpr k_u64 kPhysicalAddress = 0x200000;
std::vector<std::uint8_t> memory;
k_pixel_format frame_format;
k_u32 frame_width;
k_u32 frame_height;
k_u64 uv_offset;
bool owned;
bool mapped;
k_u32 mapped_size;
k_u32 invalidated_size;
k_u32 flushed_size;
k_u32 unmapped_size;

std::uint8_t Pixel(k_u32 plane, k_u32 row, k_u32 column) {
    return static_cast<std::uint8_t>(plane * 101 + row * 7 + column * 3 + 1);
}

class VicapPaddingTest : public ::testing::Test {
protected:
    vb_mgmt_dump_vicap_config cfg{};
    vb_mgmt_vicap_image image{};

    void SetUp() override {
        vb_mgmt_init();
        owned = mapped = false;
        mapped_size = invalidated_size = flushed_size = unmapped_size = 0;
        uv_offset = 0;
        cfg.dev_num = VICAP_DEV_ID_0;
        cfg.chn_num = VICAP_CHN_ID_0;
        cfg.foramt = VICAP_DUMP_YUV;
        cfg.milli_sec = 1000;
    }

    void TearDown() override { vb_mgmt_deinit(); }

    // Fill each plane row by row at the hardware pitch; padding bytes are 0xEE.
    void Configure(k_pixel_format format, k_u32 width, k_u32 height) {
        frame_format = format;
        frame_width = width;
        frame_height = height;
        const k_u32 pitch = (width + 15) & ~15U;
        memory.assign(8u << 20, 0xEE);
        if (format == PIXEL_FORMAT_RGB_888) {
            for (k_u32 row = 0; row < height; ++row)
                for (k_u32 column = 0; column < width * 3; ++column)
                    memory[row * pitch * 3 + column] = Pixel(0, row, column);
        } else {
            uv_offset = static_cast<k_u64>(pitch) * height;
            for (k_u32 row = 0; row < height; ++row)
                for (k_u32 column = 0; column < width; ++column)
                    memory[row * pitch + column] = Pixel(0, row, column);
            for (k_u32 row = 0; row < height / 2; ++row)
                for (k_u32 column = 0; column < width; ++column)
                    memory[uv_offset + row * pitch + column] = Pixel(1, row, column);
        }
    }

    const std::uint8_t *Mapped() const {
        return reinterpret_cast<const std::uint8_t *>(image.vf_info.v_frame.virt_addr[0]);
    }
};
} // namespace

extern "C" {
k_u32 calc_video_size(k_pixel_format format, k_u16 width, k_u16 height) {
    if (format == PIXEL_FORMAT_RGB_888) return static_cast<k_u32>(width) * height * 3;
    if (format == PIXEL_FORMAT_YUV_SEMIPLANAR_420) return static_cast<k_u32>(width) * height * 3 / 2;
    return 0;
}

k_s32 kd_mpi_vicap_dump_frame(k_vicap_dev, k_vicap_chn, k_vicap_dump_format,
                            k_video_frame_info *frame, k_u32) {
    if (owned) return -10;
    owned = true;
    std::memset(frame, 0, sizeof(*frame));
    frame->v_frame.width = frame_width;
    frame->v_frame.height = frame_height;
    frame->v_frame.stride[0] = frame_width;  // what the driver reports
    frame->v_frame.pixel_format = frame_format;
    frame->v_frame.phys_addr[0] = kPhysicalAddress;
    if (uv_offset) {
        frame->v_frame.stride[1] = frame_width;
        frame->v_frame.phys_addr[1] = kPhysicalAddress + uv_offset;
    }
    return 0;
}

void *kd_mpi_sys_mmap_cached(k_u64 address, k_u32 size) {
    EXPECT_EQ(address, kPhysicalAddress);
    EXPECT_LE(size, memory.size());
    mapped = true;
    mapped_size = size;
    return memory.data();
}

k_s32 kd_mpi_sys_mmz_invalidate_cache(k_u64, void *, k_u32 size) {
    invalidated_size = size;
    return 0;
}

k_s32 kd_mpi_sys_mmz_flush_cache(k_u64, void *, k_u32 size) {
    flushed_size = size;
    return 0;
}

k_s32 kd_mpi_sys_munmap(void *, k_u32 size) {
    unmapped_size = size;
    mapped = false;
    return 0;
}

k_s32 kd_mpi_vicap_dump_release(k_vicap_dev, k_vicap_chn, const k_video_frame_info *) {
    if (!owned) return -12;
    owned = false;
    return 0;
}

k_s32 kd_mpi_vicap_stop_stream(k_vicap_dev) { return 0; }
k_s32 kd_mpi_vicap_deinit(k_vicap_dev) { return 0; }
}

TEST_F(VicapPaddingTest, NativeWidthRgbMapsAndFlushesThePaddedFrame) {
    Configure(PIXEL_FORMAT_RGB_888, 1080, 1280);
    ASSERT_EQ(vb_mgmt_dump_vicap_frame(&cfg, &image), 0);
    EXPECT_EQ(mapped_size, 1088u * 1280 * 3);
    EXPECT_EQ(invalidated_size, 1088u * 1280 * 3);
    EXPECT_EQ(image.image_size, 1088u * 1280 * 3);
    ASSERT_EQ(vb_mgmt_release_vicap_frame(&image), 0);
    EXPECT_EQ(flushed_size, 1088u * 1280 * 3);
    EXPECT_EQ(unmapped_size, 1088u * 1280 * 3);
}

TEST_F(VicapPaddingTest, PackingMakesNativeWidthRgbRowsDense) {
    Configure(PIXEL_FORMAT_RGB_888, 1080, 1280);
    ASSERT_EQ(vb_mgmt_dump_vicap_frame(&cfg, &image), 0);
    ASSERT_EQ(vb_mgmt_pack_vicap_frame(&image), 0);
    const std::uint8_t *pixels = Mapped();
    for (k_u32 row = 0; row < 1280; row += 127)
        for (k_u32 column = 0; column < 1080 * 3; column += 53)
            ASSERT_EQ(pixels[row * 1080 * 3 + column], Pixel(0, row, column)) << row << "," << column;
    // The last visible byte of the last row lands at the end of the dense image.
    EXPECT_EQ(pixels[1080 * 3 * 1280 - 1], Pixel(0, 1279, 1080 * 3 - 1));
    ASSERT_EQ(vb_mgmt_release_vicap_frame(&image), 0);
}

TEST_F(VicapPaddingTest, PackingTwiceDoesNotShiftRowsAgain) {
    Configure(PIXEL_FORMAT_RGB_888, 1080, 64);
    ASSERT_EQ(vb_mgmt_dump_vicap_frame(&cfg, &image), 0);
    ASSERT_EQ(vb_mgmt_pack_vicap_frame(&image), 0);
    ASSERT_EQ(vb_mgmt_pack_vicap_frame(&image), 0);
    EXPECT_EQ(Mapped()[63 * 1080 * 3 + 5], Pixel(0, 63, 5));
    ASSERT_EQ(vb_mgmt_release_vicap_frame(&image), 0);
}

TEST_F(VicapPaddingTest, PackingNativeWidthYuvMovesTheUvPlane) {
    Configure(PIXEL_FORMAT_YUV_SEMIPLANAR_420, 1080, 1280);
    ASSERT_EQ(vb_mgmt_dump_vicap_frame(&cfg, &image), 0);
    EXPECT_EQ(mapped_size, 1088u * 1280 * 3 / 2);
    ASSERT_EQ(vb_mgmt_pack_vicap_frame(&image), 0);
    const std::uint8_t *pixels = Mapped();
    for (k_u32 row = 0; row < 1280; row += 211)
        for (k_u32 column = 0; column < 1080; column += 37)
            ASSERT_EQ(pixels[row * 1080 + column], Pixel(0, row, column));
    const std::uint8_t *uv = pixels + 1080 * 1280;
    for (k_u32 row = 0; row < 640; row += 97)
        for (k_u32 column = 0; column < 1080; column += 41)
            ASSERT_EQ(uv[row * 1080 + column], Pixel(1, row, column));
    EXPECT_EQ(uv[639 * 1080 + 1079], Pixel(1, 639, 1079));
    ASSERT_EQ(vb_mgmt_release_vicap_frame(&image), 0);
}

TEST_F(VicapPaddingTest, AlignedWidthsKeepTheirSizeAndBytes) {
    Configure(PIXEL_FORMAT_RGB_888, 544, 640);
    const std::vector<std::uint8_t> before(memory.begin(), memory.begin() + 544 * 640 * 3);
    ASSERT_EQ(vb_mgmt_dump_vicap_frame(&cfg, &image), 0);
    EXPECT_EQ(mapped_size, 544u * 640 * 3);
    ASSERT_EQ(vb_mgmt_pack_vicap_frame(&image), 0);
    EXPECT_TRUE(std::equal(before.begin(), before.end(), Mapped()));
    ASSERT_EQ(vb_mgmt_release_vicap_frame(&image), 0);
    EXPECT_EQ(flushed_size, 544u * 640 * 3);
}

TEST_F(VicapPaddingTest, PackingRequiresAnOwnedMappedFrame) {
    Configure(PIXEL_FORMAT_RGB_888, 1080, 16);
    EXPECT_NE(vb_mgmt_pack_vicap_frame(&image), 0);
    ASSERT_EQ(vb_mgmt_dump_vicap_frame(&cfg, &image), 0);
    vb_mgmt_vicap_image released = image;
    ASSERT_EQ(vb_mgmt_release_vicap_frame(&image), 0);
    EXPECT_NE(vb_mgmt_pack_vicap_frame(&released), 0);
}

TEST_F(VicapPaddingTest, AReusedRecordIsPackedAgainForTheNextFrame) {
    Configure(PIXEL_FORMAT_RGB_888, 1080, 32);
    ASSERT_EQ(vb_mgmt_dump_vicap_frame(&cfg, &image), 0);
    ASSERT_EQ(vb_mgmt_pack_vicap_frame(&image), 0);
    ASSERT_EQ(vb_mgmt_release_vicap_frame(&image), 0);
    Configure(PIXEL_FORMAT_RGB_888, 1080, 32);
    ASSERT_EQ(vb_mgmt_dump_vicap_frame(&cfg, &image), 0);
    ASSERT_EQ(vb_mgmt_pack_vicap_frame(&image), 0);
    EXPECT_EQ(Mapped()[31 * 1080 * 3 + 7], Pixel(0, 31, 7));
    ASSERT_EQ(vb_mgmt_release_vicap_frame(&image), 0);
}
