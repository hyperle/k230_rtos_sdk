#include "k_type.h"
#include <assert.h>

#undef CONFIG_MPP_ENABLE_SENSOR_SC132GS
#define CONFIG_MPP_ENABLE_SENSOR_SC132GS 1
#undef CONFIG_MPP_ENABLE_CSI_DEV_0
#undef CONFIG_MPP_ENABLE_CSI_DEV_1
#undef CONFIG_MPP_ENABLE_CSI_DEV_2
#define CONFIG_MPP_ENABLE_CSI_DEV_0 1
#define CONFIG_MPP_ENABLE_CSI_DEV_1 1
#define CONFIG_MPP_ENABLE_CSI_DEV_2 1
#undef CONFIG_MPP_SC132GS_CSI0_4LANE
#if TEST_FOUR_LANE
#define CONFIG_MPP_SC132GS_CSI0_4LANE 1
#endif

#include "../../userapps/src/sensor/mpi_sensor.c"

int main(void)
{
    const k_vicap_data_source sources[] = {
        VICAP_SOURCE_CSI0, VICAP_SOURCE_CSI1, VICAP_SOURCE_CSI2,
    };
    const k_vicap_sensor_type types[][2] = {
        {SC132GS_MIPI_CSI0_1080X1280_120FPS_10BIT_LINEAR,
         SC132GS_MIPI_CSI0_640X480_240FPS_10BIT_LINEAR},
        {SC132GS_MIPI_CSI1_1080X1280_120FPS_10BIT_LINEAR,
         SC132GS_MIPI_CSI1_640X480_240FPS_10BIT_LINEAR},
        {SC132GS_MIPI_CSI2_1080X1280_120FPS_10BIT_LINEAR,
         SC132GS_MIPI_CSI2_640X480_240FPS_10BIT_LINEAR},
    };
    unsigned matches = 0;
    for (unsigned csi = 0; csi < 3; ++csi) {
        for (unsigned mode = 0; mode < 2; ++mode) {
            k_vicap_sensor_info info;
            k_s32 ret = kd_mpi_vicap_get_sensor_info(types[csi][mode], &info);
            if (TEST_FOUR_LANE && csi == 1) {
                assert(ret != 0);
                continue;
            }
            assert(ret == 0);
            assert(info.csi_num == VICAP_CSI0 + csi);
            assert(info.source_id == sources[csi]);
            assert(info.mipi_lanes == (TEST_FOUR_LANE && csi == 0 ?
                                      VICAP_MIPI_4LANE : VICAP_MIPI_2LANE));
            assert(info.data_type == VICAP_CSI_DATA_TYPE_RAW10);
            assert(info.width == (mode ? 640 : 1080));
            assert(info.height == (mode ? 480 : 1280));
            assert(info.fps == (mode ? 240 : 120));
            assert(info.phy_freq == (mode ? VICAP_MIPI_PHY_800M : VICAP_MIPI_PHY_1200M));
            ++matches;
        }
    }
    assert(matches == (TEST_FOUR_LANE ? 4 : 6));
    printf("SC132GS MPI four_lane=%d: PASS\n", TEST_FOUR_LANE);
    return 0;
}
