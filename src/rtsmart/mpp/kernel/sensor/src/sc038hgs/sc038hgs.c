/* SPDX-License-Identifier: Apache-2.0 */
#include "sensor_dev.h"
#include "drv_gpio.h"
#include "k_sensor_sc038hgs.h"
#include <float.h>

#define SC038HGS_CHIP_ID 0x1a79U
#define SC038HGS_MIN_EXP_LINES 4U
#define SC038HGS_EXP_MARGIN 11U
#define SC038HGS_INITIAL_EXP (128U * 16U)

#include "sensor_reg_table.c"

static k_sensor_ae_info sc038hgs_ae_defaults = {
    .frame_length = SC038HGS_VTS,
    .cur_frame_length = SC038HGS_VTS,
    .gain_accuracy = 1024,
    .min_gain = 1.0f,
    .max_gain = 39.375f * 7.875f,
    .gain_increment = 1.0f / 32.0f,
    .max_integraion_line = SC038HGS_VTS - SC038HGS_EXP_MARGIN,
    .min_integraion_line = SC038HGS_MIN_EXP_LINES,
    .cur_again = 1.0f, .cur_dgain = 1.0f, .cur_gain = 1.0f,
    .gain = {1.0f, 39.375f * 7.875f, 1.0f / 32.0f},
    .a_gain = {1.0f, 39.375f, 1.0f / 32.0f},
    .d_gain = {1.0f, 7.875f, 1.0f / 32.0f},
    .int_time_delay_frame = 2,
    .gain_delay_frame = 2,
#ifdef CONFIG_MPP_SC038HGS_MONO
    .color_type = SENSOR_MONO,
#else
    .color_type = SENSOR_COLOR,
#endif
};

#define SC038HGS_MODE(n) { \
    .index = 0, \
    .sensor_type = SC038HGS_MIPI_CSI##n##_640X480_10BIT_LINEAR, \
    .size = {.bounds_width = 640, .bounds_height = 480, .width = 640, .height = 480}, \
    .fps = SC038HGS_DEFAULT_FPS_MILLI, \
    .hdr_mode = SENSOR_MODE_LINEAR, .bit_width = 10, \
    .bayer_pattern = BAYER_PAT_BGGR, \
    .mipi_info = {.csi_id = n, .mipi_lanes = 1, .data_type = 0x2b}, \
    .reg_list = sc038hgs_default_regs, \
    .sensor_ae_info = &sc038hgs_ae_defaults, \
}

static const k_sensor_mode sc038hgs_modes[] = {
#ifdef CONFIG_MPP_ENABLE_CSI_DEV_0
    SC038HGS_MODE(0),
#endif
#ifdef CONFIG_MPP_ENABLE_CSI_DEV_1
    SC038HGS_MODE(1),
#endif
#ifdef CONFIG_MPP_ENABLE_CSI_DEV_2
    SC038HGS_MODE(2),
#endif
};

static k_s32 sc038hgs_update_bits(struct sensor_driver_dev *dev, k_u16 addr,
                                k_u16 mask, k_u16 value)
{
    k_u16 old;
    k_s32 ret = sensor_reg_read(&dev->i2c_info, addr, &old);
    if (ret)
        return ret;
    return sensor_reg_write(&dev->i2c_info, addr, (old & ~mask) | (value & mask));
}

static k_s32 sc038hgs_read16(struct sensor_driver_dev *dev, k_u16 addr, k_u32 *value)
{
    k_u16 hi, lo;
    k_s32 ret = sensor_reg_read(&dev->i2c_info, addr, &hi);
    if (!ret)
        ret = sensor_reg_read(&dev->i2c_info, addr + 1, &lo);
    if (!ret)
        *value = (hi << 8) | lo;
    return ret;
}

/* Group 0 holds up to eight registers by default (datasheet p. 29). */
static k_s32 sc038hgs_group_write(struct sensor_driver_dev *dev,
                                const k_sensor_reg *regs, k_u32 count)
{
    k_s32 ret, end_ret;
    if (count > 8)
        return -EINVAL;
    ret = sensor_reg_write(&dev->i2c_info, 0x3800, 0x00);
    if (ret) {
        /* A failed transfer may still have reached the sensor. */
        dev->mode_init_flag = K_FALSE;
        dev->init_flag = K_FALSE;
        return ret;
    }
    for (k_u32 i = 0; i < count; ++i) {
        ret = sensor_reg_write(&dev->i2c_info, regs[i].addr, regs[i].val);
        if (ret)
            break;
    }
    end_ret = sensor_reg_write(&dev->i2c_info, 0x3800, 0x10);
    if (ret || end_ret) {
        /* Never launch an incomplete group; require initialization again. */
        dev->mode_init_flag = K_FALSE;
        dev->init_flag = K_FALSE;
        return ret ? ret : end_ret;
    }
    ret = sensor_reg_write(&dev->i2c_info, 0x3800, 0x60);
    if (ret) {
        dev->mode_init_flag = K_FALSE;
        dev->init_flag = K_FALSE;
    }
    return ret;
}

static k_s32 sc038hgs_gpio(k_s32 pin, k_s32 value)
{
    k_s32 ret;
    if (pin < 0)
        return 0;
    ret = kd_pin_mode(pin, GPIO_DM_OUTPUT);
    return ret ? ret : kd_pin_write(pin, value);
}

static k_s32 sc038hgs_power(void *ctx, k_s32 on)
{
    struct sensor_driver_dev *dev = ctx;
    k_s32 ret = 0, gpio_ret;
    if (on && dev->power_flag)
        return 0;
    dev->init_flag = K_FALSE;
    dev->mode_init_flag = K_FALSE;
    if (on) {
        ret = sc038hgs_gpio(dev->reset_gpio, GPIO_PV_LOW);
        if (!ret)
            ret = sc038hgs_gpio(dev->pwd_gpio, GPIO_PV_LOW);
        if (ret)
            return ret;
        rt_thread_mdelay(5);
        ret = sc038hgs_gpio(dev->pwd_gpio, GPIO_PV_HIGH);
        if (!ret)
            ret = sc038hgs_gpio(dev->reset_gpio, GPIO_PV_HIGH);
        if (ret) {
            (void)sc038hgs_gpio(dev->pwd_gpio, GPIO_PV_LOW);
            return ret;
        }
        /* XSHUTDN/PWDN low = shutdown. T4 >= 4 ms (p. 8). */
        rt_thread_mdelay(20);
        dev->power_flag = K_TRUE;
        return 0;
    }
    if (dev->power_flag) {
        ret = sc038hgs_update_bits(dev, 0x2100, 0x01, 0);
        rt_thread_mdelay(1); /* >= 6 EXTCLK periods before shutdown (p. 9). */
    }
    gpio_ret = sc038hgs_gpio(dev->reset_gpio, GPIO_PV_LOW);
    if (!ret)
        ret = gpio_ret;
    gpio_ret = sc038hgs_gpio(dev->pwd_gpio, GPIO_PV_LOW);
    if (!ret)
        ret = gpio_ret;
    dev->power_flag = K_FALSE;
    return ret;
}

static k_s32 sc038hgs_get_id(void *ctx, k_u32 *id)
{
    struct sensor_driver_dev *dev = ctx;
    k_s32 ret;
    if (!id)
        return -EINVAL;
    *id = 0;
    if (!dev->power_flag)
        return -EIO;
    ret = sc038hgs_read16(dev, 0x3107, id);
    return ret ? ret : (*id == SC038HGS_CHIP_ID ? 0 : -ENODEV);
}

static void sc038hgs_timing(struct sensor_driver_dev *dev, k_u32 hts, k_u32 vts)
{
    k_sensor_mode *mode = &dev->current_sensor_mode;
    k_sensor_ae_info *ae = &mode->ae_info;
    const k_u32 pclk = CONFIG_MPP_SC038HGS_PIXEL_CLOCK_HZ;
    ae->frame_length = vts;
    ae->cur_frame_length = vts;
    ae->max_integraion_line = vts - SC038HGS_EXP_MARGIN;
    ae->one_line_exp_time = pclk ? (float)hts / pclk : 0.0f;
    ae->integration_time_increment = ae->one_line_exp_time / 16.0f;
    ae->min_integraion_time = ae->one_line_exp_time * SC038HGS_MIN_EXP_LINES;
    ae->max_integraion_time = ae->one_line_exp_time * ae->max_integraion_line;
    ae->max_fps = SC038HGS_DEFAULT_FPS_MILLI;
    ae->min_fps = pclk ? (k_u64)pclk * 1000U / (hts * SC038HGS_MAX_VTS) : 0;
    ae->cur_fps = pclk ? (k_u64)pclk * 1000U / (hts * vts) : 0;
    mode->fps = ae->cur_fps;
}

static k_s32 sc038hgs_get_mode(void *ctx, k_sensor_mode *mode)
{
    struct sensor_driver_dev *dev = ctx;
    if (!mode || mode->sensor_type != dev->sensor_mode_list[0].sensor_type)
        return -EINVAL;
    *mode = dev->current_sensor_mode;
    return 0;
}

static k_s32 sc038hgs_mirror(void *ctx, k_vicap_mirror_mode mirror)
{
    struct sensor_driver_dev *dev = ctx;
    static const k_u8 bits[] = {0, 0x06, 0x60, 0x66};
    static const k_sensor_bayer_pattern patterns[] = {
        BAYER_PAT_BGGR, BAYER_PAT_GBRG, BAYER_PAT_GRBG, BAYER_PAT_RGGB,
    };
    k_s32 ret = 0;
    if (mirror.sensor_type != dev->sensor_mode_list[0].sensor_type ||
        (k_u32)mirror.mirror > VICAP_MIRROR_BOTH)
        return -EINVAL;
    if (dev->init_flag)
        ret = sc038hgs_update_bits(dev, 0x3221, 0x66, bits[mirror.mirror]);
    if (!ret) {
        dev->mirror_setting = mirror;
        dev->current_sensor_mode.bayer_pattern =
            dev->current_sensor_mode.ae_info.color_type == SENSOR_MONO ?
            BAYER_PAT_BGGR : patterns[mirror.mirror];
    }
    return ret;
}

static k_s32 sc038hgs_init(void *ctx, k_sensor_mode requested)
{
    struct sensor_driver_dev *dev = ctx;
    k_u32 id, hts, vts;
    k_s32 ret;
    if (requested.sensor_type != dev->sensor_mode_list[0].sensor_type)
        return -EINVAL;
    ret = sc038hgs_power(dev, 1);
    if (ret)
        return ret;
    dev->init_flag = K_FALSE;
    dev->mode_init_flag = K_FALSE;
    ret = sensor_reg_write(&dev->i2c_info, 0x2103, 1);
    if (ret)
        goto failed;
    rt_thread_mdelay(20);
    ret = sc038hgs_get_id(dev, &id);
    for (k_u32 i = 0; !ret && i < sizeof(sc038hgs_default_fields) /
                                      sizeof(sc038hgs_default_fields[0]); ++i)
        ret = sc038hgs_update_bits(dev, sc038hgs_default_fields[i].addr,
                                  sc038hgs_default_fields[i].mask,
                                  sc038hgs_default_fields[i].value);
    if (!ret)
        ret = sensor_reg_list_write(&dev->i2c_info, sc038hgs_default_regs);
    /* Check the selected interface as well as timing before allowing output. */
    for (k_u32 i = 0; !ret && i < sizeof(sc038hgs_default_fields) /
                                      sizeof(sc038hgs_default_fields[0]); ++i) {
        k_u16 value;
        ret = sensor_reg_read(&dev->i2c_info, sc038hgs_default_fields[i].addr, &value);
        if (!ret && (value & sc038hgs_default_fields[i].mask) !=
                    sc038hgs_default_fields[i].value)
            ret = -EIO;
    }
    for (const k_sensor_reg *reg = sc038hgs_default_regs;
         !ret && reg->addr != REG_NULL; ++reg) {
        k_u16 value;
        ret = sensor_reg_read(&dev->i2c_info, reg->addr, &value);
        if (!ret && value != reg->val)
            ret = -EIO;
    }
    if (!ret)
        ret = sc038hgs_read16(dev, 0x320c, &hts);
    if (!ret)
        ret = sc038hgs_read16(dev, 0x320e, &vts);
    if (ret)
        goto failed;
    if ((hts & 0xfff) != SC038HGS_HTS || vts != SC038HGS_VTS) {
        ret = -EIO;
        goto failed;
    }
    dev->current_sensor_mode = dev->sensor_mode_list[0];
    dev->current_sensor_mode.ae_info = sc038hgs_ae_defaults;
    dev->current_sensor_mode.et_line = 128;
    sc038hgs_timing(dev, SC038HGS_HTS, SC038HGS_VTS);
    dev->current_sensor_mode.ae_info.cur_integration_time =
        128.0f * dev->current_sensor_mode.ae_info.one_line_exp_time;
    dev->init_flag = K_TRUE;
    ret = sc038hgs_mirror(dev, dev->mirror_setting);
    if (ret)
        goto failed;
    dev->mode_init_flag = K_TRUE;
    return 0;
failed:
    dev->init_flag = K_FALSE;
    dev->mode_init_flag = K_FALSE;
    (void)sc038hgs_update_bits(dev, 0x2100, 1, 0);
    return ret;
}

static k_s32 sc038hgs_enum_mode(void *ctx, k_sensor_enum_mode *entry)
{
    struct sensor_driver_dev *dev = ctx;
    if (!entry || entry->index >= dev->mode_count)
        return -EINVAL;
    entry->mode = dev->current_sensor_mode;
    return 0;
}

static k_s32 sc038hgs_caps(void *ctx, k_sensor_caps *caps)
{
    struct sensor_driver_dev *dev = ctx;
    if (!caps)
        return -EINVAL;
    memset(caps, 0, sizeof(*caps));
    caps->bit_width = 10;
    caps->mode = SENSOR_MODE_LINEAR;
    caps->bayer_pattern = dev->current_sensor_mode.bayer_pattern;
    caps->resolution.width = SC038HGS_WIDTH;
    caps->resolution.height = SC038HGS_HEIGHT;
    caps->mipi_lanes = 1;
    return 0;
}

static k_s32 sc038hgs_connected(void *ctx, k_s32 *connected)
{
    k_u32 id;
    k_s32 ret;
    if (!connected)
        return -EINVAL;
    ret = sc038hgs_get_id(ctx, &id);
    *connected = !ret;
    return ret;
}

static k_s32 sc038hgs_stream(void *ctx, k_s32 enable)
{
    struct sensor_driver_dev *dev = ctx;
    if (!dev->power_flag || (enable && !dev->init_flag))
        return -EIO;
    return sc038hgs_update_bits(dev, 0x2100, 1, !!enable);
}

static const struct { k_u8 code; float base; k_u8 max_fine; } sc038hgs_again[] = {
    {0x00, 1.0f, 0x3f}, {0x01, 2.0f, 0x27}, {0x80, 2.5f, 0x3f},
    {0x81, 5.0f, 0x3f}, {0x83, 10.0f, 0x3f}, {0x87, 20.0f, 0x3f},
};

static k_s32 sc038hgs_get_gain(void *ctx, k_sensor_gain *gain, k_bool digital)
{
    struct sensor_driver_dev *dev = ctx;
    k_u32 raw;
    float value = 0.0f;
    k_s32 ret;
    if (!gain)
        return -EINVAL;
    if (!dev->init_flag)
        return -EIO;
    ret = sc038hgs_read16(dev, digital ? 0x3e06 : 0x3e08, &raw);
    if (ret)
        return ret;
    k_u8 coarse = raw >> 8, fine = raw & 0xff;
    if (digital) {
        if ((coarse != 0 && coarse != 1 && coarse != 3) ||
            fine < 0x80 || fine > 0xfc || (fine & 3))
            return -EIO;
        value = (coarse + 1) * fine / 128.0f;
        dev->current_sensor_mode.ae_info.cur_dgain = value;
    } else {
        for (k_u32 i = 0; i < sizeof(sc038hgs_again) / sizeof(sc038hgs_again[0]); ++i)
            if (sc038hgs_again[i].code == coarse && fine >= 0x20 &&
                fine <= sc038hgs_again[i].max_fine)
                value = sc038hgs_again[i].base * fine / 32.0f;
        if (!value)
            return -EIO;
        dev->current_sensor_mode.ae_info.cur_again = value;
    }
    memset(gain, 0, sizeof(*gain));
    gain->gain[SENSOR_LINEAR_PARAS] = value;
    dev->current_sensor_mode.ae_info.cur_gain =
        dev->current_sensor_mode.ae_info.cur_again * dev->current_sensor_mode.ae_info.cur_dgain;
    return 0;
}

static k_s32 sc038hgs_quantize_gain(float requested, k_bool digital,
                                   k_sensor_reg regs[2], float *actual)
{
    if (!(requested > 0.0f) || requested > FLT_MAX)
        return -EINVAL;
    *actual = 1.0f;
    regs[0] = (k_sensor_reg){digital ? 0x3e06 : 0x3e08, 0};
    regs[1] = (k_sensor_reg){digital ? 0x3e07 : 0x3e09, digital ? 0x80 : 0x20};
    /* Quantize down using only entries present in tables 2-7 / 2-8. */
    for (k_u32 i = 0; i < (digital ? 3U : 6U); ++i) {
        k_u8 coarse = digital ? ((1U << i) - 1U) : sc038hgs_again[i].code;
        k_u32 limit = digital ? 63U : sc038hgs_again[i].max_fine;
        float base = digital ? (float)(1U << i) : sc038hgs_again[i].base;
        for (k_u32 fine = 32; fine <= limit; ++fine) {
            float candidate = base * fine / 32.0f;
            if (candidate <= requested && candidate >= *actual) {
                *actual = candidate;
                regs[0].val = coarse;
                regs[1].val = digital ? fine * 4U : fine;
            }
        }
    }
    return 0;
}

static k_s32 sc038hgs_set_gain(void *ctx, k_sensor_gain gain, k_bool digital)
{
    struct sensor_driver_dev *dev = ctx;
    k_sensor_reg regs[2];
    float actual;
    k_s32 ret;
    if (!dev->init_flag)
        return -EIO;
    ret = sc038hgs_quantize_gain(gain.gain[SENSOR_LINEAR_PARAS], digital, regs, &actual);
    if (ret)
        return ret;
    ret = sc038hgs_group_write(dev, regs, 2);
    if (!ret) {
        k_sensor_ae_info *ae = &dev->current_sensor_mode.ae_info;
        if (digital)
            ae->cur_dgain = actual;
        else
            ae->cur_again = actual;
        ae->cur_gain = ae->cur_again * ae->cur_dgain;
    }
    return ret;
}

static k_s32 sc038hgs_get_again(void *ctx, k_sensor_gain *gain)
{ return sc038hgs_get_gain(ctx, gain, K_FALSE); }
static k_s32 sc038hgs_set_again(void *ctx, k_sensor_gain gain)
{ return sc038hgs_set_gain(ctx, gain, K_FALSE); }
static k_s32 sc038hgs_get_dgain(void *ctx, k_sensor_gain *gain)
{ return sc038hgs_get_gain(ctx, gain, K_TRUE); }
static k_s32 sc038hgs_set_dgain(void *ctx, k_sensor_gain gain)
{ return sc038hgs_set_gain(ctx, gain, K_TRUE); }

static k_s32 sc038hgs_read_exposure(struct sensor_driver_dev *dev, k_u32 *raw)
{
    k_u16 high;
    k_s32 ret = sensor_reg_read(&dev->i2c_info, 0x3e00, &high);
    if (!ret)
        ret = sc038hgs_read16(dev, 0x3e01, raw);
    if (!ret)
        *raw |= (high & 0x0f) << 16;
    return ret;
}

static k_s32 sc038hgs_get_exposure(void *ctx, k_sensor_intg_time *time)
{
    struct sensor_driver_dev *dev = ctx;
    k_u32 raw;
    k_s32 ret;
    if (!time)
        return -EINVAL;
    if (!dev->init_flag)
        return -EIO;
    if (!CONFIG_MPP_SC038HGS_PIXEL_CLOCK_HZ)
        return -ENOSYS;
    ret = sc038hgs_read_exposure(dev, &raw);
    if (!ret) {
        memset(time, 0, sizeof(*time));
        time->intg_time[SENSOR_LINEAR_PARAS] = raw / 16.0f *
            dev->current_sensor_mode.ae_info.one_line_exp_time;
        dev->current_sensor_mode.ae_info.cur_integration_time = time->intg_time[0];
    }
    return ret;
}

static k_s32 sc038hgs_exposure_regs(struct sensor_driver_dev *dev, float seconds,
                                   k_sensor_reg regs[3], k_u32 *raw)
{
    k_sensor_ae_info *ae = &dev->current_sensor_mode.ae_info;
    k_u16 high;
    k_s32 ret;
    if (!dev->init_flag)
        return -EIO;
    if (!CONFIG_MPP_SC038HGS_PIXEL_CLOCK_HZ)
        return -ENOSYS;
    if (!(seconds >= 0.0f) || seconds > FLT_MAX)
        return -EINVAL;
    if (seconds < ae->min_integraion_time)
        seconds = ae->min_integraion_time;
    if (seconds > ae->max_integraion_time)
        seconds = ae->max_integraion_time;
    *raw = (k_u32)(seconds / ae->one_line_exp_time * 16.0f + 0.5f);
    if (*raw > (k_u32)ae->max_integraion_line * 16U)
        *raw = ae->max_integraion_line * 16U;
    ret = sensor_reg_read(&dev->i2c_info, 0x3e00, &high);
    if (ret)
        return ret;
    regs[0] = (k_sensor_reg){0x3e00, (high & 0xf0) | ((*raw >> 16) & 0x0f)};
    regs[1] = (k_sensor_reg){0x3e01, (*raw >> 8) & 0xff};
    regs[2] = (k_sensor_reg){0x3e02, *raw & 0xff};
    return 0;
}

static k_s32 sc038hgs_set_exposure(void *ctx, k_sensor_intg_time time)
{
    struct sensor_driver_dev *dev = ctx;
    k_sensor_ae_info *ae = &dev->current_sensor_mode.ae_info;
    k_sensor_reg regs[3];
    k_u32 raw;
    k_s32 ret = sc038hgs_exposure_regs(dev, time.intg_time[SENSOR_LINEAR_PARAS], regs, &raw);
    if (ret)
        return ret;
    ret = sc038hgs_group_write(dev, regs, 3);
    if (!ret) {
        dev->current_sensor_mode.et_line = raw / 16U;
        ae->cur_integration_time = raw / 16.0f * ae->one_line_exp_time;
    }
    return ret;
}

static k_s32 sc038hgs_get_exp_parm(void *ctx, k_sensor_exposure_param *param)
{
    k_sensor_intg_time time;
    k_sensor_gain again, dgain;
    k_s32 ret;
    if (!param)
        return -EINVAL;
    ret = sc038hgs_get_exposure(ctx, &time);
    if (!ret)
        ret = sc038hgs_get_again(ctx, &again);
    if (!ret)
        ret = sc038hgs_get_dgain(ctx, &dgain);
    if (!ret) {
        memset(param, 0, sizeof(*param));
        param->exp_time[0] = time.intg_time[0];
        param->gain[0] = again.gain[0] * dgain.gain[0];
    }
    return ret;
}

static k_s32 sc038hgs_set_exp_parm(void *ctx, k_sensor_exposure_param param)
{
    struct sensor_driver_dev *dev = ctx;
    k_sensor_ae_info *ae = &dev->current_sensor_mode.ae_info;
    k_sensor_reg regs[7];
    k_u32 raw;
    float again, dgain;
    k_s32 ret;
    if (!dev->init_flag)
        return -EIO;
    /* Prefer analogue gain, then add digital gain (datasheet p. 23). */
    ret = sc038hgs_quantize_gain(param.gain[0], K_FALSE, regs + 3, &again);
    if (!ret)
        ret = sc038hgs_quantize_gain(param.gain[0] / again, K_TRUE, regs + 5, &dgain);
    if (!ret)
        ret = sc038hgs_exposure_regs(dev, param.exp_time[0], regs, &raw);
    if (ret)
        return ret;
    ret = sc038hgs_group_write(dev, regs, 7);
    if (!ret) {
        dev->current_sensor_mode.et_line = raw / 16U;
        ae->cur_integration_time = raw / 16.0f * ae->one_line_exp_time;
        ae->cur_again = again;
        ae->cur_dgain = dgain;
        ae->cur_gain = again * dgain;
    }
    return ret;
}

static k_s32 sc038hgs_get_fps(void *ctx, k_u32 *fps)
{
    struct sensor_driver_dev *dev = ctx;
    k_u32 hts, vts;
    k_s32 ret;
    if (!fps)
        return -EINVAL;
    if (!dev->init_flag)
        return -EIO;
    if (!CONFIG_MPP_SC038HGS_PIXEL_CLOCK_HZ)
        return -ENOSYS;
    ret = sc038hgs_read16(dev, 0x320c, &hts);
    if (!ret)
        ret = sc038hgs_read16(dev, 0x320e, &vts);
    if (ret)
        return ret;
    hts &= 0xfff;
    if (!hts || vts <= SC038HGS_EXP_MARGIN || vts > SC038HGS_MAX_VTS)
        return -EIO;
    sc038hgs_timing(dev, hts, vts);
    *fps = dev->current_sensor_mode.fps; /* milli-fps, like mode.fps */
    return 0;
}

static k_s32 sc038hgs_set_fps(void *ctx, k_u32 fps)
{
    struct sensor_driver_dev *dev = ctx;
    k_u32 hts, vts, exposure;
    k_u16 high;
    k_s32 ret;
    if (!dev->init_flag)
        return -EIO;
    if (!CONFIG_MPP_SC038HGS_PIXEL_CLOCK_HZ)
        return -ENOSYS;
    if (!fps || fps > 240000U)
        return -EINVAL;
    ret = sc038hgs_read16(dev, 0x320c, &hts);
    if (ret)
        return ret;
    hts &= 0xfff;
    if (!hts)
        return -EIO;
    k_u64 divisor = (k_u64)hts * fps;
    k_u64 frame_lines = ((k_u64)CONFIG_MPP_SC038HGS_PIXEL_CLOCK_HZ * 1000U + divisor - 1) / divisor;
    if (frame_lines < SC038HGS_VTS || frame_lines > SC038HGS_MAX_VTS)
        return -EINVAL;
    vts = frame_lines;
    ret = sc038hgs_read_exposure(dev, &exposure);
    if (!ret)
        ret = sensor_reg_read(&dev->i2c_info, 0x3e00, &high);
    if (ret)
        return ret;
    if (exposure > (vts - SC038HGS_EXP_MARGIN) * 16U)
        exposure = (vts - SC038HGS_EXP_MARGIN) * 16U;
    k_sensor_reg regs[] = {
        {0x320e, vts >> 8}, {0x320f, vts & 0xff},
        {0x3e00, (high & 0xf0) | ((exposure >> 16) & 0xf)},
        {0x3e01, (exposure >> 8) & 0xff}, {0x3e02, exposure & 0xff},
    };
    ret = sc038hgs_group_write(dev, regs, 5);
    if (!ret) {
        sc038hgs_timing(dev, hts, vts);
        dev->current_sensor_mode.et_line = exposure / 16U;
        dev->current_sensor_mode.ae_info.cur_integration_time = exposure / 16.0f *
            dev->current_sensor_mode.ae_info.one_line_exp_time;
    }
    return ret;
}

static k_s32 sc038hgs_get_tpg(void *ctx, k_sensor_test_pattern *tpg)
{
    struct sensor_driver_dev *dev = ctx;
    k_u16 value;
    k_s32 ret;
    if (!tpg)
        return -EINVAL;
    if (!dev->init_flag)
        return -EIO;
    ret = sensor_reg_read(&dev->i2c_info, 0x4501, &value);
    if (!ret) {
        tpg->enable = !!(value & 8);
        tpg->pattern = 0; /* The only documented pattern: grey gradient. */
    }
    return ret;
}

static k_s32 sc038hgs_set_tpg(void *ctx, k_sensor_test_pattern tpg)
{
    struct sensor_driver_dev *dev = ctx;
    if (tpg.pattern || tpg.enable > 1)
        return -EINVAL;
    if (!dev->init_flag)
        return -EIO;
    return sc038hgs_update_bits(dev, 0x4501, 8, tpg.enable ? 8 : 0);
}

static k_s32 sc038hgs_exposure_range(void *ctx, k_sensor_exposure_time_range *range)
{
    struct sensor_driver_dev *dev = ctx;
    if (!range)
        return -EINVAL;
    if (!dev->init_flag)
        return -EIO;
    if (!CONFIG_MPP_SC038HGS_PIXEL_CLOCK_HZ)
        return -ENOSYS;
    range->min_intg_time_us = dev->current_sensor_mode.ae_info.min_integraion_time * 1000000.0f;
    range->max_intg_time_us = dev->current_sensor_mode.ae_info.max_integraion_time * 1000000.0f;
    return 0;
}

static const k_sensor_function sc038hgs_functions = {
    .sensor_power = sc038hgs_power,
    .sensor_init = sc038hgs_init,
    .sensor_get_chip_id = sc038hgs_get_id,
    .sensor_get_mode = sc038hgs_get_mode,
    .sensor_set_mode = sc038hgs_init,
    .sensor_enum_mode = sc038hgs_enum_mode,
    .sensor_get_caps = sc038hgs_caps,
    .sensor_conn_check = sc038hgs_connected,
    .sensor_set_stream = sc038hgs_stream,
    .sensor_get_again = sc038hgs_get_again,
    .sensor_set_again = sc038hgs_set_again,
    .sensor_get_dgain = sc038hgs_get_dgain,
    .sensor_set_dgain = sc038hgs_set_dgain,
    .sensor_get_intg_time = sc038hgs_get_exposure,
    .sensor_set_intg_time = sc038hgs_set_exposure,
    .sensor_get_exp_parm = sc038hgs_get_exp_parm,
    .sensor_set_exp_parm = sc038hgs_set_exp_parm,
    .sensor_get_fps = sc038hgs_get_fps,
    .sensor_set_fps = sc038hgs_set_fps,
    .sensor_get_tpg = sc038hgs_get_tpg,
    .sensor_set_tpg = sc038hgs_set_tpg,
    .sensor_mirror_set = sc038hgs_mirror,
    .sensor_get_exposure_time_range = sc038hgs_exposure_range,
};

k_s32 sensor_sc038hgs_probe(struct k_sensor_probe_cfg *cfg, struct sensor_driver_dev *dev)
{
    const k_sensor_mode *mode = NULL;
    k_u32 id;
    k_s32 ret;
    for (k_u32 i = 0; i < sizeof(sc038hgs_modes) / sizeof(sc038hgs_modes[0]); ++i)
        if (sc038hgs_modes[i].mipi_info.csi_id == cfg->csi_num)
            mode = &sc038hgs_modes[i];
    if (!mode)
        return -EINVAL;
    memset(dev, 0, sizeof(*dev));
    dev->pwd_gpio = cfg->pwd_gpio;
    dev->reset_gpio = cfg->reset_gpio;
    dev->i2c_info.i2c_bus = rt_i2c_bus_device_find(cfg->i2c_name);
    if (!dev->i2c_info.i2c_bus)
        return -ENODEV;
    snprintf(dev->i2c_info.i2c_name, sizeof(dev->i2c_info.i2c_name), "%s", cfg->i2c_name);
    dev->i2c_info.reg_addr_size = SENSOR_REG_VALUE_16BIT;
    dev->i2c_info.reg_val_size = SENSOR_REG_VALUE_8BIT;
    /* External oscillator: do not enable, disable or retune SoC MCLKs. */
    ret = sc038hgs_power(dev, 1);
    if (ret)
        goto failed;
    for (k_u16 addr = 0x30; addr <= 0x33; ++addr) {
        dev->i2c_info.slave_addr = addr;
        ret = sc038hgs_get_id(dev, &id);
        if (!ret)
            break;
    }
    if (ret)
        goto failed;
    dev->sensor_mode_list = mode;
    dev->mode_count = 1;
    dev->current_sensor_mode = *mode;
    dev->current_sensor_mode.ae_info = sc038hgs_ae_defaults;
    sc038hgs_timing(dev, SC038HGS_HTS, SC038HGS_VTS);
    dev->mirror_setting.sensor_type = mode->sensor_type;
    dev->mirror_setting.mirror = VICAP_MIRROR_NONE;
    dev->sensor_func = sc038hgs_functions;
    snprintf(dev->sensor_name, sizeof(dev->sensor_name), "sc038hgs_csi%d", cfg->csi_num);
    rt_kprintf("SC038HGS: id=0x%04x, %s addr=0x%02x, external 24 MHz\n",
               id, cfg->i2c_name, dev->i2c_info.slave_addr);
    if (!CONFIG_MPP_SC038HGS_PIXEL_CLOCK_HZ)
        rt_kprintf("SC038HGS: reset-default PLL; Pixel CLK unknown, fps/time controls unavailable\n");
    return 0;
failed:
    (void)sc038hgs_power(dev, 0);
    memset(dev, 0, sizeof(*dev));
    return ret;
}
