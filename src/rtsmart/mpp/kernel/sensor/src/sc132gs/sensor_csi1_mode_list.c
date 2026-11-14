static k_sensor_ae_info sensor_csi1_ae_info[] = {
    SC132GS_AE_DEFAULTS(1350, 120),
    SC132GS_AE_DEFAULTS(675, 240),
};

static const k_sensor_mode sensor_csi1_mode_list[] = {
    {
        .index = 0,
        .sensor_type = SC132GS_MIPI_CSI1_1080X1280_120FPS_10BIT_LINEAR,
        .size = {
            .bounds_width = 1080,
            .bounds_height = 1280,
            .top = 0,
            .left = 0,
            .width = 1080,
            .height = 1280,
        },
        .fps = 120000,
        .hdr_mode = SENSOR_MODE_LINEAR,
        .bit_width = 10,
        .bayer_pattern = BAYER_PAT_BGGR,//BAYER_PAT_GRBG, //BAYER_PAT_RGGB,
        .mipi_info = {
            .csi_id = 1,
            .mipi_lanes = 2,
            .data_type = 0x2B,
        },
        .reg_list = sc132gs_mipi_1080x1280_120fps_init,
#if defined (CONFIG_MPP_ENABLE_SENSOR_SC132GS_ON_CSI1_USE_CHIP_CLK)
        .mclk_setting = {
            {
                .mclk_setting_en = K_TRUE,
                .setting.id = CONFIG_MPP_CSI_DEV1_MCLK_NUM,
                .setting.mclk_sel = SENSOR_PLL1_CLK_DIV4,
                .setting.mclk_div = 25,
            },
            {K_FALSE},
            {K_FALSE},
        },
        .sensor_ae_info = &sensor_csi1_ae_info[0],
#else
        .mclk_setting = {
            {K_FALSE},
            {K_FALSE},
            {K_FALSE},
        },
        .sensor_ae_info = &sensor_csi1_ae_info[0],
#endif
    },

    {
        .index = 1,
        .sensor_type = SC132GS_MIPI_CSI1_640X480_240FPS_10BIT_LINEAR,
        .size = {
            .bounds_width = 640,
            .bounds_height = 480,
            .top = 0,
            .left = 0,
            .width = 640,
            .height = 480,
        },
        .fps = 240000,
        .hdr_mode = SENSOR_MODE_LINEAR,
        .bit_width = 10,
        .bayer_pattern = BAYER_PAT_BGGR,//BAYER_PAT_GRBG, //BAYER_PAT_RGGB,
        .mipi_info = {
            .csi_id = 1,
            .mipi_lanes = 2,
            .data_type = 0x2B,
        },
        .reg_list = sc132gs_mipi_640x480_240fps_init,
#if defined (CONFIG_MPP_ENABLE_SENSOR_SC132GS_ON_CSI1_USE_CHIP_CLK)
        .mclk_setting = {
            {
                .mclk_setting_en = K_TRUE,
                .setting.id = CONFIG_MPP_CSI_DEV1_MCLK_NUM,
                .setting.mclk_sel = SENSOR_PLL1_CLK_DIV4,
                .setting.mclk_div = 25,
            },
            {K_FALSE},
            {K_FALSE},
        },
        .sensor_ae_info = &sensor_csi1_ae_info[1],
#else
        .mclk_setting = {
            {K_FALSE},
            {K_FALSE},
            {K_FALSE},
        },
        .sensor_ae_info = &sensor_csi1_ae_info[1],
#endif
    },
};

#if defined (CONFIG_MPP_ENABLE_SENSOR_SC132GS_ON_CSI1_USE_CHIP_CLK)
_Static_assert(CONFIG_MPP_CSI_DEV1_MCLK_NUM >= 1 && (CONFIG_MPP_CSI_DEV1_MCLK_NUM <= 3), "Invalid CONFIG_MPP_CSI_DEV1_MCLK_NUM");
#endif
