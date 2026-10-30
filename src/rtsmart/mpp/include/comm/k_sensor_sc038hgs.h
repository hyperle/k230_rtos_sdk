/* SPDX-License-Identifier: Apache-2.0 */
#ifndef K_SENSOR_SC038HGS_H
#define K_SENSOR_SC038HGS_H

/* SC038HGS V1.1, tables 2-12 and 2-13. EXTCLK is NOT Pixel CLK. */
#define SC038HGS_WIDTH       640U
#define SC038HGS_HEIGHT      480U
#define SC038HGS_HTS         750U
#define SC038HGS_VTS         600U
#define SC038HGS_MAX_VTS     0xfff0U
#ifndef CONFIG_MPP_SC038HGS_PIXEL_CLOCK_HZ
#define CONFIG_MPP_SC038HGS_PIXEL_CLOCK_HZ 0
#endif
#define SC038HGS_DEFAULT_FPS_MILLI \
    ((k_u32)((k_u64)CONFIG_MPP_SC038HGS_PIXEL_CLOCK_HZ * 1000U / \
             (SC038HGS_HTS * SC038HGS_VTS)))

#endif
