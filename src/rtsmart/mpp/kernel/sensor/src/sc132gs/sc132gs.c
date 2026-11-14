/* Copyright (c) 2023, Canaan Bright Sight Co., Ltd
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
 * CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "rtthread.h"
#include "sensor_dev.h"
#include "drv_gpio.h"
#include "drv_fpioa.h"
#include <float.h>

#define SC132GS_CHIP_ID 0x0132U
#define SC132GS_EXP_MARGIN 8U
#define SC132GS_MAX_VTS 0xffffU
#define SC132GS_MAX_AGAIN 28.546875f
#define SC132GS_MAX_DGAIN 31.875f

#ifdef CONFIG_MPP_SC132GS_SKIP_CHIP_ID_CHECK
_Static_assert(CONFIG_MPP_SC132GS_I2C_ADDRESS >= 0x30 &&
               CONFIG_MPP_SC132GS_I2C_ADDRESS <= 0x33,
               "SC132GS fixed I2C address must be 0x30..0x33");
#endif

#ifdef CONFIG_MPP_SC132GS_CSI0_4LANE
#define SC132GS_CSI0_LANES 4
#else
#define SC132GS_CSI0_LANES 2
#endif

#ifdef CONFIG_MPP_SC132GS_CSI0_CLOCK_INVERT
#define SC132GS_CSI0_CLOCK_INVERT 0x08U
#else
#define SC132GS_CSI0_CLOCK_INVERT 0U
#endif

#ifndef CONFIG_MPP_SC132GS_EXTCLK_HZ
#define CONFIG_MPP_SC132GS_EXTCLK_HZ 24000000U
#endif

#ifdef CONFIG_MPP_SC132GS_MONO
#define SC132GS_COLOR_TYPE SENSOR_MONO
#else
#define SC132GS_COLOR_TYPE SENSOR_COLOR
#endif

#define SC132GS_AE_DEFAULTS(lines, rate) { \
    .frame_length = (lines), .cur_frame_length = (lines), \
    .one_line_exp_time = 1.0f / ((rate) * (lines)), \
    .gain_accuracy = 1024, .min_gain = 1.0f, \
    .max_gain = SC132GS_MAX_AGAIN * SC132GS_MAX_DGAIN, \
    .int_time_delay_frame = 2, .gain_delay_frame = 2, \
    .color_type = SC132GS_COLOR_TYPE, \
    .integration_time_increment = 1.0f / ((rate) * (lines) * 16.0f), \
    .gain_increment = 1.0f / 32.0f, \
    .max_integraion_line = (lines) - SC132GS_EXP_MARGIN, \
    .min_integraion_line = 0, \
    .max_integraion_time = ((lines) - SC132GS_EXP_MARGIN) / ((rate) * (lines) * 1.0f), \
    .cur_again = 1.0f, .cur_dgain = 1.0f, .cur_gain = 1.0f, \
    .gain = {1.0f, SC132GS_MAX_AGAIN * SC132GS_MAX_DGAIN, 1.0f / 32.0f}, \
    .a_gain = {1.0f, SC132GS_MAX_AGAIN, 1.0f / 32.0f}, \
    .d_gain = {1.0f, SC132GS_MAX_DGAIN, 1.0f / 128.0f}, \
    .cur_fps = (rate) * 1000U, .max_fps = (rate) * 1000U, \
}

#include "sensor_reg_table.c"
#ifdef CONFIG_MPP_ENABLE_CSI_DEV_0
#include "sensor_csi0_mode_list.c"
#endif
#if defined(CONFIG_MPP_ENABLE_CSI_DEV_1) && !defined(CONFIG_MPP_SC132GS_CSI0_4LANE)
#include "sensor_csi1_mode_list.c"
#endif
#ifdef CONFIG_MPP_ENABLE_CSI_DEV_2
#include "sensor_csi2_mode_list.c"
#endif

static struct {
    k_bool active;
    k_u16 blc, blc_mode, dgain;
} sc132gs_tpg_state[3];

/* The Dart adapter's control bus crosses a stacking connector and a TXS0102.
 * One lost ACK must not abort a 100-entry mode table, so each single-register
 * transfer is retried a bounded number of times. Every register written here
 * is idempotent for a repeated identical value. A persistently open bus still
 * fails within a few milliseconds, and both recovery and failure are logged. */
#define SC132GS_I2C_ATTEMPTS 3U
#define SC132GS_I2C_RETRY_DELAY_MS 1

static k_s32 sc132gs_i2c_result(k_s32 ret, k_u32 attempts, const char *op, k_u16 addr)
{
    if (ret)
        rt_kprintf("SC132GS: I2C %s 0x%04x failed after %u attempts (%d)\n",
                   op, addr, attempts, ret);
    else if (attempts > 1)
        rt_kprintf("SC132GS: I2C %s 0x%04x recovered on attempt %u\n", op, addr, attempts);
    return ret;
}

static k_s32 sc132gs_reg_read(struct sensor_driver_dev *dev, k_u16 addr, k_u16 *value)
{
    k_u32 attempts = 1;
    k_s32 ret = sensor_reg_read(&dev->i2c_info, addr, value);
    while (ret && attempts < SC132GS_I2C_ATTEMPTS) {
        rt_thread_mdelay(SC132GS_I2C_RETRY_DELAY_MS);
        ++attempts;
        ret = sensor_reg_read(&dev->i2c_info, addr, value);
    }
    return sc132gs_i2c_result(ret, attempts, "read", addr);
}

static k_s32 sc132gs_reg_write(struct sensor_driver_dev *dev, k_u16 addr, k_u16 value)
{
    k_u32 attempts = 1;
    k_s32 ret = sensor_reg_write(&dev->i2c_info, addr, value);
    while (ret && attempts < SC132GS_I2C_ATTEMPTS) {
        rt_thread_mdelay(SC132GS_I2C_RETRY_DELAY_MS);
        ++attempts;
        ret = sensor_reg_write(&dev->i2c_info, addr, value);
    }
    return sc132gs_i2c_result(ret, attempts, "write", addr);
}

static k_s32 sc132gs_write_table(struct sensor_driver_dev *dev, const k_sensor_reg *regs)
{
    for (; regs->addr != REG_NULL; ++regs) {
        k_s32 ret = sc132gs_reg_write(dev, regs->addr, regs->val);
        if (ret) {
            rt_kprintf("SC132GS: mode table write failed at [0x%04x] = [0x%02x]\n",
                       regs->addr, regs->val);
            return ret;
        }
    }
    return 0;
}

static k_s32 sc132gs_update_bits(struct sensor_driver_dev *dev, k_u16 addr,
                               k_u16 mask, k_u16 value)
{
    k_u16 previous;
    k_s32 ret = sc132gs_reg_read(dev, addr, &previous);
    if (ret)
        return ret;
    return sc132gs_reg_write(dev, addr, (previous & ~mask) | (value & mask));
}

static k_s32 sc132gs_read16(struct sensor_driver_dev *dev, k_u16 addr, k_u32 *value)
{
    k_u16 high, low;
    k_s32 ret = sc132gs_reg_read(dev, addr, &high);
    if (!ret)
        ret = sc132gs_reg_read(dev, addr + 1, &low);
    if (!ret)
        *value = (high << 8) | low;
    return ret;
}

static k_s32 sc132gs_group_write(struct sensor_driver_dev *dev,
                               const k_sensor_reg *regs, k_u32 count)
{
    k_s32 ret, end_ret;
    if (count > 16)
        return -EINVAL;
    ret = sc132gs_reg_write(dev, 0x3800, 0x00);
    if (!ret) {
        for (k_u32 index = 0; index < count; ++index) {
            ret = sc132gs_reg_write(dev, regs[index].addr, regs[index].val);
            if (ret)
                break;
        }
        end_ret = sc132gs_reg_write(dev, 0x3800, 0x10);
        if (!ret)
            ret = end_ret;
        if (!ret)
            ret = sc132gs_reg_write(dev, 0x3800, 0x60);
    }
    if (ret) {
        dev->init_flag = K_FALSE;
        dev->mode_init_flag = K_FALSE;
    }
    return ret;
}

static k_s32 sc132gs_gpio(k_s32 pin, k_s32 value)
{
    k_s32 ret;
    if (pin < 0)
        return 0;
    ret = kd_pin_mode(pin, GPIO_DM_OUTPUT);
    return ret ? ret : kd_pin_write(pin, value);
}

static k_s32 sc132gs_clock(const k_sensor_mode *mode)
{
    extern k_s32 vicap_set_mclk(k_vicap_mclk *mclk);
    for (k_u32 index = 0; index < SENSOR_MCLK_MAX - 1; ++index) {
        const k_sensor_mclk_setting *setting = &mode->mclk_setting[index];
        if (setting->mclk_setting_en) {
            k_vicap_mclk mclk = {
                .id = (k_vicap_mclk_id)setting->setting.id,
                .mclk_en = 1,
                .mclk_sel = (k_vicap_mclk_sel)setting->setting.mclk_sel,
                .mclk_div = setting->setting.mclk_div,
            };
            k_s32 ret = vicap_set_mclk(&mclk);
            if (ret)
                return ret;
        }
    }
    return 0;
}

/* GPIO clients can change the control pads without updating sensor flags. */
static k_bool sc132gs_control_asserted(k_s32 pin, k_s32 value)
{
    fpioa_func_t function;
    if (pin < 0)
        return K_TRUE;
    return !drv_fpioa_get_pin_func(pin, &function) &&
        function == (fpioa_func_t)(GPIO0 + pin) &&
        kd_pin_get_ddr(pin) && kd_pin_read(pin) == value;
}

static k_bool sc132gs_power_controls_valid(struct sensor_driver_dev *dev)
{
#ifdef CONFIG_MPP_SC132GS_POWER_ACTIVE_LOW
    const k_s32 power_on = GPIO_PV_LOW;
#else
    const k_s32 power_on = GPIO_PV_HIGH;
#endif
    return dev->power_flag &&
        sc132gs_control_asserted(dev->pwd_gpio, power_on) &&
        sc132gs_control_asserted(dev->reset_gpio, GPIO_PV_HIGH);
}

static void sc132gs_invalidate_mode(struct sensor_driver_dev *dev)
{
    dev->init_flag = K_FALSE;
    dev->mode_init_flag = K_FALSE;
    sc132gs_tpg_state[dev->current_sensor_mode.mipi_info.csi_id].active = K_FALSE;
}

static k_s32 sc132gs_power(void *ctx, k_s32 on)
{
    struct sensor_driver_dev *dev = ctx;
    k_s32 ret = 0, gpio_ret;
#ifdef CONFIG_MPP_SC132GS_POWER_ACTIVE_LOW
    const k_s32 power_on = GPIO_PV_LOW, power_off = GPIO_PV_HIGH;
#else
    const k_s32 power_on = GPIO_PV_HIGH, power_off = GPIO_PV_LOW;
#endif
    if (on && sc132gs_power_controls_valid(dev))
        return 0;

    if (!on && dev->power_flag && dev->init_flag)
        ret = sc132gs_update_bits(dev, 0x0100, 1, 0);
    sc132gs_invalidate_mode(dev);

    if (on) {
        gpio_ret = sc132gs_clock(&dev->current_sensor_mode);
        if (gpio_ret)
            return gpio_ret;
    }
    gpio_ret = sc132gs_gpio(dev->reset_gpio, GPIO_PV_LOW);
    k_s32 power_ret = sc132gs_gpio(dev->pwd_gpio, power_off);
    if (!gpio_ret)
        gpio_ret = power_ret;
    if (!gpio_ret &&
        (!sc132gs_control_asserted(dev->reset_gpio, GPIO_PV_LOW) ||
         !sc132gs_control_asserted(dev->pwd_gpio, power_off)))
        gpio_ret = -EIO;
    if (gpio_ret)
        return gpio_ret; /* Physical state is uncertain: retain power_flag. */
    if (!on && dev->pwd_gpio < 0 && dev->reset_gpio < 0 && ret)
        return ret; /* With no physical controls, a failed stop is not off. */
    dev->power_flag = K_FALSE;
    if (!on) {
        if (ret)
            rt_kprintf("SC132GS: I2C stop failed (%d); physical power-off confirmed\n", ret);
        return 0;
    }

    rt_thread_mdelay(CANMV_SENSOR_POWER_RESET_DELAY_MS);
    gpio_ret = sc132gs_gpio(dev->pwd_gpio, power_on);
    if (!gpio_ret)
        gpio_ret = sc132gs_gpio(dev->reset_gpio, GPIO_PV_HIGH);
    if (gpio_ret) {
        /* Do not cache an uncertain failed rollback as powered off. */
        dev->power_flag = K_TRUE;
        if (!sc132gs_gpio(dev->pwd_gpio, power_off) &&
            sc132gs_control_asserted(dev->pwd_gpio, power_off))
            dev->power_flag = K_FALSE;
        return gpio_ret;
    }
    dev->power_flag = K_TRUE;
    if (!sc132gs_power_controls_valid(dev))
        return -EIO;
    rt_thread_mdelay(1000);
    return 0;
}

/* No new callbacks or structure fields: keep existing sensor-driver ABI.
 * Other sensors retain their existing cache behavior. There are deliberately
 * no I2C health reads here; the original initialization table remains first. */
k_bool sensor_sc132gs_power_cache_valid(struct sensor_driver_dev *dev)
{
    return dev->sensor_func.sensor_power != sc132gs_power ||
        sc132gs_power_controls_valid(dev);
}

static k_s32 sc132gs_get_id(void *ctx, k_u32 *id)
{
    struct sensor_driver_dev *dev = ctx;
    k_s32 ret;
    if (!id)
        return -EINVAL;
    *id = 0;
    if (!dev->power_flag)
        return -EIO;
    ret = sc132gs_read16(dev, 0x3107, id);
#ifndef CONFIG_MPP_SC132GS_SKIP_CHIP_ID_CHECK
    if (!ret && *id != SC132GS_CHIP_ID)
        ret = -ENODEV;
#endif
    /* ISP initialization requests the revision through this callback too.
     * Fixed binding skips matching there as well, but still reports the real
     * register value and propagates I2C errors. Never fabricate a chip ID. */
    return ret;
}

static const k_sensor_mode *sc132gs_find_mode(struct sensor_driver_dev *dev,
                                            k_vicap_sensor_type type)
{
    for (k_u32 index = 0; index < dev->mode_count; ++index)
        if (dev->sensor_mode_list[index].sensor_type == type)
            return &dev->sensor_mode_list[index];
    return NULL;
}

static void sc132gs_timing(k_sensor_mode *mode, k_u32 vts)
{
    k_sensor_ae_info *ae = &mode->ae_info;
    ae->cur_frame_length = vts;
    ae->max_integraion_line = vts - SC132GS_EXP_MARGIN;
    ae->max_integraion_time = ae->max_integraion_line * ae->one_line_exp_time;
    mode->fps = (k_u32)(1000.0f / (ae->one_line_exp_time * vts) + 0.5f);
    ae->cur_fps = mode->fps;
}

static void sc132gs_copy_mode(const k_sensor_mode *source, k_sensor_mode *mode)
{
    k_u32 clock_hz = source->mclk_setting[0].mclk_setting_en ?
        23760000U : CONFIG_MPP_SC132GS_EXTCLK_HZ;
    *mode = *source;
    mode->ae_info = *source->sensor_ae_info;
    mode->fps = (k_u64)mode->fps * clock_hz / 24000000U;
    mode->ae_info.one_line_exp_time = 1000.0f /
        ((float)mode->fps * mode->ae_info.frame_length);
    mode->ae_info.integration_time_increment = mode->ae_info.one_line_exp_time / 16.0f;
    mode->ae_info.max_fps = mode->fps;
    mode->ae_info.min_fps = (k_u32)(1000.0f /
        (mode->ae_info.one_line_exp_time * SC132GS_MAX_VTS)) + 1U;
    sc132gs_timing(mode, mode->ae_info.frame_length);
}

static k_s32 sc132gs_get_mode(void *ctx, k_sensor_mode *mode)
{
    struct sensor_driver_dev *dev = ctx;
    const k_sensor_mode *source;
    if (!mode)
        return -EINVAL;
    source = sc132gs_find_mode(dev, mode->sensor_type);
    if (!source)
        return -EINVAL;
    if (mode->sensor_type == dev->current_sensor_mode.sensor_type)
        *mode = dev->current_sensor_mode;
    else
        sc132gs_copy_mode(source, mode);
    return 0;
}

static k_s32 sc132gs_mirror(void *ctx, k_vicap_mirror_mode mirror)
{
    struct sensor_driver_dev *dev = ctx;
    static const k_u8 bits[] = {0, 0x06, 0x60, 0x66};
    static const k_sensor_bayer_pattern patterns[] = {
        BAYER_PAT_BGGR, BAYER_PAT_GBRG, BAYER_PAT_GRBG, BAYER_PAT_RGGB,
    };
    k_s32 ret = 0;
    if (!sc132gs_find_mode(dev, mirror.sensor_type) ||
        (k_u32)mirror.mirror > VICAP_MIRROR_BOTH)
        return -EINVAL;
    if (dev->init_flag && mirror.sensor_type == dev->current_sensor_mode.sensor_type)
        ret = sc132gs_update_bits(dev, 0x3221, 0x66, bits[mirror.mirror]);
    if (!ret) {
        dev->mirror_setting = mirror;
        if (mirror.sensor_type == dev->current_sensor_mode.sensor_type)
            dev->current_sensor_mode.bayer_pattern = SC132GS_COLOR_TYPE == SENSOR_MONO ?
                BAYER_PAT_BGGR : patterns[mirror.mirror];
    }
    return ret;
}

static const struct { k_u8 code; float base; k_u8 max_fine; } sc132gs_again[] = {
    {0x03, 1.0f, 0x39}, {0x23, 1.8125f, 0x3f}, {0x27, 3.625f, 0x3f},
    {0x2f, 7.25f, 0x3f}, {0x3f, 14.5f, 0x3f},
};

static k_s32 sc132gs_get_gain(void *ctx, k_sensor_gain *gain, k_bool digital)
{
    struct sensor_driver_dev *dev = ctx;
    k_sensor_ae_info *ae = &dev->current_sensor_mode.ae_info;
    k_u32 raw;
    float value = 0;
    k_s32 ret;
    if (!gain)
        return -EINVAL;
    if (!dev->init_flag)
        return -EIO;
    ret = sc132gs_read16(dev, digital ? 0x3e06 : 0x3e08, &raw);
    if (ret)
        return ret;
    k_u32 coarse = raw >> 8, fine = raw & 0xff;
    if (digital) {
        coarse &= 0x0f;
        if (fine < 0x80 || (coarse != 0 && coarse != 1 && coarse != 3 &&
                            coarse != 7 && coarse != 15))
            return -EIO;
        value = (coarse + 1U) * fine / 128.0f;
        ae->cur_dgain = value;
    } else {
        for (k_u32 index = 0; index < sizeof(sc132gs_again) / sizeof(sc132gs_again[0]); ++index)
            if (coarse == sc132gs_again[index].code && fine >= 0x20 &&
                fine <= sc132gs_again[index].max_fine)
                value = sc132gs_again[index].base * fine / 32.0f;
        if (!value)
            return -EIO;
        ae->cur_again = value;
    }
    ae->cur_gain = ae->cur_again * ae->cur_dgain;
    memset(gain, 0, sizeof(*gain));
    gain->gain[0] = value;
    return 0;
}

static k_s32 sc132gs_quantize_gain(float requested, k_bool digital,
                                 k_sensor_reg *regs, float *actual)
{
    k_u32 coarse = 0, fine;
    float base = 1.0f;
    if (!(requested > 0) || requested > FLT_MAX)
        return -EINVAL;
    requested = MAX(1.0f, MIN(requested, digital ? SC132GS_MAX_DGAIN : SC132GS_MAX_AGAIN));
    if (digital) {
        while (coarse < 4 && requested >= base * 2.0f) {
            base *= 2.0f;
            ++coarse;
        }
        fine = MIN((k_u32)(requested * 128.0f / base), 255U);
        regs[0] = (k_sensor_reg){0x3e06, (1U << coarse) - 1U};
        regs[1] = (k_sensor_reg){0x3e07, fine};
        *actual = base * fine / 128.0f;
    } else {
        for (k_u32 index = 1; index < sizeof(sc132gs_again) / sizeof(sc132gs_again[0]); ++index)
            if (requested >= sc132gs_again[index].base)
                coarse = index;
        base = sc132gs_again[coarse].base;
        fine = MIN((k_u32)(requested * 32.0f / base), sc132gs_again[coarse].max_fine);
        regs[0] = (k_sensor_reg){0x3e08, sc132gs_again[coarse].code};
        regs[1] = (k_sensor_reg){0x3e09, fine};
        *actual = base * fine / 32.0f;
    }
    return 0;
}

static void sc132gs_gain_tuning(k_sensor_reg *regs, float gain)
{
    regs[0] = (k_sensor_reg){0x33fa, gain < 2.0f ? 0x01 : 0x02};
    regs[1] = (k_sensor_reg){0x3317, gain < 2.0f ? 0xf0 : 0x14};
}

static k_s32 sc132gs_set_gain(void *ctx, k_sensor_gain gain, k_bool digital)
{
    struct sensor_driver_dev *dev = ctx;
    k_sensor_ae_info *ae = &dev->current_sensor_mode.ae_info;
    k_sensor_reg regs[4];
    k_u16 high;
    float actual;
    k_s32 ret;
    if (!dev->init_flag)
        return -EIO;
    if (digital && sc132gs_tpg_state[dev->current_sensor_mode.mipi_info.csi_id].active)
        return -EBUSY;
    ret = sc132gs_quantize_gain(gain.gain[0], digital, regs, &actual);
    if (!ret && digital) {
        ret = sc132gs_reg_read(dev, 0x3e06, &high);
        if (!ret)
            regs[0].val |= high & 0xf0;
    }
    if (ret)
        return ret;
    if (!digital)
        sc132gs_gain_tuning(regs + 2, actual);
    ret = sc132gs_group_write(dev, regs, digital ? 2 : 4);
    if (!ret) {
        if (digital)
            ae->cur_dgain = actual;
        else
            ae->cur_again = actual;
        ae->cur_gain = ae->cur_again * ae->cur_dgain;
    }
    return ret;
}

static k_s32 sc132gs_get_again(void *ctx, k_sensor_gain *gain)
{ return sc132gs_get_gain(ctx, gain, K_FALSE); }
static k_s32 sc132gs_set_again(void *ctx, k_sensor_gain gain)
{ return sc132gs_set_gain(ctx, gain, K_FALSE); }
static k_s32 sc132gs_get_dgain(void *ctx, k_sensor_gain *gain)
{ return sc132gs_get_gain(ctx, gain, K_TRUE); }
static k_s32 sc132gs_set_dgain(void *ctx, k_sensor_gain gain)
{ return sc132gs_set_gain(ctx, gain, K_TRUE); }

static k_s32 sc132gs_read_exposure(struct sensor_driver_dev *dev, k_u32 *raw)
{
    k_u16 high;
    k_s32 ret = sc132gs_reg_read(dev, 0x3e00, &high);
    if (!ret)
        ret = sc132gs_read16(dev, 0x3e01, raw);
    if (!ret)
        *raw |= (high & 0x0f) << 16;
    return ret;
}

static void sc132gs_cache_exposure(struct sensor_driver_dev *dev, k_u32 raw)
{
    dev->current_sensor_mode.et_line = raw / 16U;
    dev->current_sensor_mode.ae_info.cur_integration_time = raw / 16.0f *
        dev->current_sensor_mode.ae_info.one_line_exp_time;
}

static k_s32 sc132gs_get_exposure(void *ctx, k_sensor_intg_time *time)
{
    struct sensor_driver_dev *dev = ctx;
    k_u32 raw;
    k_s32 ret;
    if (!time)
        return -EINVAL;
    if (!dev->init_flag)
        return -EIO;
    ret = sc132gs_read_exposure(dev, &raw);
    if (!ret) {
        sc132gs_cache_exposure(dev, raw);
        memset(time, 0, sizeof(*time));
        time->intg_time[0] = dev->current_sensor_mode.ae_info.cur_integration_time;
    }
    return ret;
}

static k_s32 sc132gs_exposure_regs(struct sensor_driver_dev *dev, float seconds,
                                 k_sensor_reg *regs, k_u32 *raw)
{
    k_sensor_ae_info *ae = &dev->current_sensor_mode.ae_info;
    k_u16 high;
    k_s32 ret;
    if (!(seconds >= 0) || seconds > FLT_MAX)
        return -EINVAL;
    seconds = MIN(seconds, ae->max_integraion_time);
    *raw = (k_u32)(seconds / ae->one_line_exp_time * 16.0f + 0.5f);
    *raw = MIN(*raw, ae->max_integraion_line * 16U);
    ret = sc132gs_reg_read(dev, 0x3e00, &high);
    if (ret)
        return ret;
    regs[0] = (k_sensor_reg){0x3e00, (high & 0xf0) | ((*raw >> 16) & 0x0f)};
    regs[1] = (k_sensor_reg){0x3e01, (*raw >> 8) & 0xff};
    regs[2] = (k_sensor_reg){0x3e02, *raw & 0xff};
    return 0;
}

static k_s32 sc132gs_set_exposure(void *ctx, k_sensor_intg_time time)
{
    struct sensor_driver_dev *dev = ctx;
    k_sensor_reg regs[3];
    k_u32 raw;
    k_s32 ret;
    if (!dev->init_flag)
        return -EIO;
    ret = sc132gs_exposure_regs(dev, time.intg_time[0], regs, &raw);
    if (!ret)
        ret = sc132gs_group_write(dev, regs, 3);
    if (!ret)
        sc132gs_cache_exposure(dev, raw);
    return ret;
}

static k_s32 sc132gs_get_exp_parm(void *ctx, k_sensor_exposure_param *param)
{
    k_sensor_gain again, dgain;
    k_sensor_intg_time time;
    k_s32 ret;
    if (!param)
        return -EINVAL;
    ret = sc132gs_get_again(ctx, &again);
    if (!ret)
        ret = sc132gs_get_dgain(ctx, &dgain);
    if (!ret)
        ret = sc132gs_get_exposure(ctx, &time);
    if (!ret) {
        memset(param, 0, sizeof(*param));
        param->gain[0] = again.gain[0] * dgain.gain[0];
        param->exp_time[0] = time.intg_time[0];
    }
    return ret;
}

static k_s32 sc132gs_set_exp_parm(void *ctx, k_sensor_exposure_param param)
{
    struct sensor_driver_dev *dev = ctx;
    k_sensor_ae_info *ae = &dev->current_sensor_mode.ae_info;
    k_sensor_reg regs[9];
    k_u16 high;
    k_u32 raw;
    float again, dgain;
    k_s32 ret;
    if (!dev->init_flag)
        return -EIO;
    if (sc132gs_tpg_state[dev->current_sensor_mode.mipi_info.csi_id].active)
        return -EBUSY;
    ret = sc132gs_quantize_gain(param.gain[0], K_FALSE, regs + 3, &again);
    if (!ret)
        ret = sc132gs_quantize_gain(param.gain[0] / again, K_TRUE, regs + 5, &dgain);
    if (!ret)
        ret = sc132gs_exposure_regs(dev, param.exp_time[0], regs, &raw);
    if (!ret) {
        ret = sc132gs_reg_read(dev, 0x3e06, &high);
        if (!ret)
            regs[5].val |= high & 0xf0;
    }
    if (ret)
        return ret;
    sc132gs_gain_tuning(regs + 7, again);
    ret = sc132gs_group_write(dev, regs, 9);
    if (!ret) {
        sc132gs_cache_exposure(dev, raw);
        ae->cur_again = again;
        ae->cur_dgain = dgain;
        ae->cur_gain = again * dgain;
    }
    return ret;
}

static k_s32 sc132gs_get_fps(void *ctx, k_u32 *fps)
{
    struct sensor_driver_dev *dev = ctx;
    k_u32 vts;
    k_s32 ret;
    if (!fps)
        return -EINVAL;
    if (!dev->init_flag)
        return -EIO;
    ret = sc132gs_read16(dev, 0x320e, &vts);
    if (ret)
        return ret;
    if (vts < dev->current_sensor_mode.ae_info.frame_length)
        return -EIO;
    sc132gs_timing(&dev->current_sensor_mode, vts);
    *fps = dev->current_sensor_mode.fps;
    return 0;
}

static k_s32 sc132gs_set_fps(void *ctx, k_u32 fps)
{
    struct sensor_driver_dev *dev = ctx;
    k_sensor_ae_info *ae = &dev->current_sensor_mode.ae_info;
    k_sensor_reg regs[5];
    k_u32 vts, raw;
    k_u16 high;
    k_s32 ret;
    if (!dev->init_flag)
        return -EIO;
    if (!fps || fps > ae->max_fps)
        return -EINVAL;
    k_u64 lines = ((k_u64)ae->max_fps * ae->frame_length + fps - 1U) / fps;
    if (lines > SC132GS_MAX_VTS)
        return -EINVAL;
    vts = lines;
    ret = sc132gs_read_exposure(dev, &raw);
    if (!ret)
        ret = sc132gs_reg_read(dev, 0x3e00, &high);
    if (ret)
        return ret;
    raw = MIN(raw, (vts - SC132GS_EXP_MARGIN) * 16U);
    regs[0] = (k_sensor_reg){0x320e, vts >> 8};
    regs[1] = (k_sensor_reg){0x320f, vts & 0xff};
    regs[2] = (k_sensor_reg){0x3e00, (high & 0xf0) | ((raw >> 16) & 0x0f)};
    regs[3] = (k_sensor_reg){0x3e01, (raw >> 8) & 0xff};
    regs[4] = (k_sensor_reg){0x3e02, raw & 0xff};
    ret = sc132gs_group_write(dev, regs, 5);
    if (!ret) {
        sc132gs_timing(&dev->current_sensor_mode, vts);
        sc132gs_cache_exposure(dev, raw);
    }
    return ret;
}

static k_s32 sc132gs_init(void *ctx, k_sensor_mode requested)
{
    struct sensor_driver_dev *dev = ctx;
    const k_sensor_mode *source = sc132gs_find_mode(dev, requested.sensor_type);
    k_u32 vts, width, height, raw;
#ifndef CONFIG_MPP_SC132GS_SKIP_CHIP_ID_CHECK
    k_u32 id;
#endif
    k_u16 value;
    k_sensor_gain gain;
    k_s32 ret;
    if (!source)
        return -EINVAL;
    const k_u16 clock_invert = source->mipi_info.csi_id == 0 ?
        SC132GS_CSI0_CLOCK_INVERT : 0;
    ret = sc132gs_power(dev, 1);
    if (ret)
        return ret;
    dev->init_flag = K_FALSE;
    dev->mode_init_flag = K_FALSE;
    sc132gs_tpg_state[dev->current_sensor_mode.mipi_info.csi_id].active = K_FALSE;
    ret = sc132gs_write_table(dev, source->reg_list);
    if (ret)
        goto failed;
    rt_thread_mdelay(2);
#ifndef CONFIG_MPP_SC132GS_SKIP_CHIP_ID_CHECK
    ret = sc132gs_get_id(dev, &id);
#endif
    if (!ret && source->mipi_info.mipi_lanes == 2)
        ret = sc132gs_reg_write(dev, 0x3019, 0x0c);
    if (!ret)
        ret = sc132gs_update_bits(dev, 0x3018, 0xe0,
                                 (source->mipi_info.mipi_lanes - 1U) << 5);
    if (!ret)
        ret = sc132gs_update_bits(dev, 0x3031, 0x0f, 0x0a);
    /* Keep the vendor MIPI serializer interface in its default byte mode.
     * 0x3031 selects the CSI-2 RAW10 packet format. Forcing 0x3037[6:5]
     * to the documented 10-bit PHY mode prevented reception on the Dart
     * module; byte mode was verified with continuous full/VGA frames. */
    if (!ret)
        ret = sc132gs_update_bits(dev, 0x3037, 0x60, 0);
    /* V3.0 p. 15 / V2.6 p. 14: preserve clock delay and reserved bits. */
    if (!ret)
        ret = sc132gs_update_bits(dev, 0x3654, 0x08, clock_invert);
    if (!ret)
        ret = sc132gs_update_bits(dev, 0x3222, 0x02, 0);
    if (!ret)
        ret = sc132gs_update_bits(dev, 0x3e03, 0x0f, 0x0b);
    if (!ret)
        ret = sc132gs_update_bits(dev, 0x3e06, 0x0f, 0);
    if (!ret)
        ret = sc132gs_reg_write(dev, 0x3e07, 0x80);
    if (!ret)
        ret = sc132gs_reg_write(dev, 0x3e08, 0x03);
    if (!ret)
        ret = sc132gs_read16(dev, 0x320e, &vts);
    if (!ret)
        ret = sc132gs_read16(dev, 0x3208, &width);
    if (!ret)
        ret = sc132gs_read16(dev, 0x320a, &height);
    if (!ret && (vts != source->sensor_ae_info->frame_length ||
                  width != source->size.width || height != source->size.height))
        ret = -EIO;
    const struct { k_u16 addr, mask, expected; } fields[] = {
        {0x0100, 1, 0}, {0x3018, 0xe0, (source->mipi_info.mipi_lanes - 1U) << 5},
        {0x3031, 0x0f, 0x0a},
        {0x3654, 0x08, clock_invert},
        {0x3037, 0x60, 0}, {0x3222, 2, 0}, {0x3e03, 0x0f, 0x0b},
    };
    for (k_u32 index = 0; !ret && index < sizeof(fields) / sizeof(fields[0]); ++index) {
        ret = sc132gs_reg_read(dev, fields[index].addr, &value);
        if (!ret && (value & fields[index].mask) != fields[index].expected)
            ret = -EIO;
    }
    if (!ret)
        ret = sc132gs_read_exposure(dev, &raw);
    if (ret)
        goto failed;
    sc132gs_copy_mode(source, &dev->current_sensor_mode);
    sc132gs_cache_exposure(dev, raw);
    dev->init_flag = K_TRUE;
    ret = sc132gs_get_again(dev, &gain);
    if (!ret)
        ret = sc132gs_get_dgain(dev, &gain);
    if (!ret) {
        k_vicap_mirror_mode mirror = dev->mirror_setting;
        mirror.sensor_type = source->sensor_type;
        ret = sc132gs_mirror(dev, mirror);
    }
    if (ret)
        goto failed;
    dev->mode_init_flag = K_TRUE;
    return 0;
failed:
    sc132gs_invalidate_mode(dev);
    /* Do not overwrite the first failed transaction with a second I2C write.
     * The next attempt must execute the full, original power-on sequence. */
    k_s32 cleanup_ret = sc132gs_power(dev, 0);
    rt_kprintf("SC132GS: init failed (%d); power-off cleanup=%d, power_flag=%d\n",
               ret, cleanup_ret, dev->power_flag);
    return ret;
}

static k_s32 sc132gs_enum_mode(void *ctx, k_sensor_enum_mode *entry)
{
    struct sensor_driver_dev *dev = ctx;
    if (!entry || entry->index >= dev->mode_count)
        return -EINVAL;
    entry->mode.sensor_type = dev->sensor_mode_list[entry->index].sensor_type;
    return sc132gs_get_mode(dev, &entry->mode);
}

static k_s32 sc132gs_caps(void *ctx, k_sensor_caps *caps)
{
    struct sensor_driver_dev *dev = ctx;
    if (!caps)
        return -EINVAL;
    memset(caps, 0, sizeof(*caps));
    caps->bit_width = 10;
    caps->mode = SENSOR_MODE_LINEAR;
    caps->bayer_pattern = dev->current_sensor_mode.bayer_pattern;
    caps->resolution.width = dev->current_sensor_mode.size.width;
    caps->resolution.height = dev->current_sensor_mode.size.height;
    caps->mipi_lanes = dev->current_sensor_mode.mipi_info.mipi_lanes;
    return 0;
}

static k_s32 sc132gs_connected(void *ctx, k_s32 *connected)
{
    k_s32 ret;
    if (!connected)
        return -EINVAL;
#ifdef CONFIG_MPP_SC132GS_SKIP_CHIP_ID_CHECK
    struct sensor_driver_dev *dev = ctx;
    k_u16 value;
    ret = dev->power_flag ? sc132gs_reg_read(dev, 0x0100, &value) : -EIO;
#else
    k_u32 id;
    ret = sc132gs_get_id(ctx, &id);
#endif
    *connected = ret ? 0 : 1;
    return ret;
}

static k_s32 sc132gs_isp_status(void *ctx, k_sensor_isp_status *status)
{
    (void)ctx;
    if (!status)
        return -EINVAL;
    status->use_sensor_ae = K_FALSE;
    status->use_sensor_blc = K_FALSE;
    status->use_sensor_awb = K_FALSE;
    return 0;
}

static k_s32 sc132gs_stream(void *ctx, k_s32 enable)
{
    struct sensor_driver_dev *dev = ctx;
    k_s32 ret;
    if (!enable && !dev->power_flag)
        return 0;
    if (!dev->power_flag || (enable && !dev->init_flag))
        return -EIO;
    ret = sc132gs_update_bits(dev, 0x0100, 1, enable ? 1 : 0);
    if (!ret || enable || dev->pwd_gpio < 0)
        return ret;

    /* A lost I2C ACK must not leave the transmitter running while ISP tears
     * down its video-input callbacks. With a real power control, stop the
     * sensor physically and report success only after EN is driven off.
     * The next initialization must perform a full power/mode sequence. */
#ifdef CONFIG_MPP_SC132GS_POWER_ACTIVE_LOW
    const k_s32 power_off = GPIO_PV_HIGH;
#else
    const k_s32 power_off = GPIO_PV_LOW;
#endif
    k_s32 gpio_ret = sc132gs_gpio(dev->pwd_gpio, power_off);
    if (gpio_ret)
        return gpio_ret;
    if (kd_pin_read(dev->pwd_gpio) != power_off)
        return -EIO;
    rt_thread_mdelay(CANMV_SENSOR_POWER_RESET_DELAY_MS);
    dev->power_flag = K_FALSE;
    dev->init_flag = K_FALSE;
    dev->mode_init_flag = K_FALSE;
    sc132gs_tpg_state[dev->current_sensor_mode.mipi_info.csi_id].active = K_FALSE;
    rt_kprintf("SC132GS: stream stop I2C failed (%d); EN power-off confirmed\n", ret);
    return 0;
}

static k_s32 sc132gs_get_tpg(void *ctx, k_sensor_test_pattern *tpg)
{
    struct sensor_driver_dev *dev = ctx;
    k_u16 value;
    k_s32 ret;
    if (!tpg)
        return -EINVAL;
    if (!dev->init_flag)
        return -EIO;
    ret = sc132gs_reg_read(dev, 0x4501, &value);
    if (!ret) {
        tpg->enable = !!(value & 8);
        tpg->pattern = 0;
    }
    return ret;
}

static k_s32 sc132gs_set_tpg(void *ctx, k_sensor_test_pattern tpg)
{
    struct sensor_driver_dev *dev = ctx;
    k_u32 csi = dev->current_sensor_mode.mipi_info.csi_id;
    k_u16 pattern, blc, blc_mode, dgain;
    k_sensor_gain gain;
    k_s32 ret;
    if (tpg.pattern || tpg.enable > 1)
        return -EINVAL;
    if (!dev->init_flag)
        return -EIO;
    if (tpg.enable == sc132gs_tpg_state[csi].active)
        return 0;
    ret = sc132gs_reg_read(dev, 0x4501, &pattern);
    if (!ret)
        ret = sc132gs_reg_read(dev, 0x3902, &blc);
    if (!ret)
        ret = sc132gs_reg_read(dev, 0x391d, &blc_mode);
    if (!ret)
        ret = sc132gs_reg_read(dev, 0x3e06, &dgain);
    if (ret)
        return ret;
    if (tpg.enable) {
        sc132gs_tpg_state[csi].blc = blc & 0x40;
        sc132gs_tpg_state[csi].blc_mode = blc_mode & 7;
        sc132gs_tpg_state[csi].dgain = dgain & 0x0f;
    }
    k_sensor_reg regs[] = {
        {0x3902, (blc & ~0x40U) | (tpg.enable ? 0 : sc132gs_tpg_state[csi].blc)},
        {0x391d, (blc_mode & ~7U) | (tpg.enable ? 0 : sc132gs_tpg_state[csi].blc_mode)},
        {0x3e06, (dgain & 0xf0) | (tpg.enable ? 0x0f : sc132gs_tpg_state[csi].dgain)},
        {0x4501, (pattern & ~8U) | (tpg.enable ? 8 : 0)},
    };
    ret = sc132gs_group_write(dev, regs, sizeof(regs) / sizeof(regs[0]));
    if (!ret) {
        sc132gs_tpg_state[csi].active = tpg.enable;
        ret = sc132gs_get_dgain(dev, &gain);
    }
    return ret;
}

static k_s32 sc132gs_exposure_range(void *ctx, k_sensor_exposure_time_range *range)
{
    struct sensor_driver_dev *dev = ctx;
    if (!range)
        return -EINVAL;
    if (!dev->init_flag)
        return -EIO;
    range->min_intg_time_us = 0;
    range->max_intg_time_us = dev->current_sensor_mode.ae_info.max_integraion_time * 1000000.0f;
    return 0;
}

static const k_sensor_function sc132gs_functions = {
    .sensor_power = sc132gs_power,
    .sensor_init = sc132gs_init,
    .sensor_get_chip_id = sc132gs_get_id,
    .sensor_get_mode = sc132gs_get_mode,
    .sensor_set_mode = sc132gs_init,
    .sensor_enum_mode = sc132gs_enum_mode,
    .sensor_get_caps = sc132gs_caps,
    .sensor_conn_check = sc132gs_connected,
    .sensor_set_stream = sc132gs_stream,
    .sensor_get_again = sc132gs_get_again,
    .sensor_set_again = sc132gs_set_again,
    .sensor_get_dgain = sc132gs_get_dgain,
    .sensor_set_dgain = sc132gs_set_dgain,
    .sensor_get_intg_time = sc132gs_get_exposure,
    .sensor_set_intg_time = sc132gs_set_exposure,
    .sensor_get_exp_parm = sc132gs_get_exp_parm,
    .sensor_set_exp_parm = sc132gs_set_exp_parm,
    .sensor_get_fps = sc132gs_get_fps,
    .sensor_set_fps = sc132gs_set_fps,
    .sensor_get_isp_status = sc132gs_isp_status,
    .sensor_get_tpg = sc132gs_get_tpg,
    .sensor_set_tpg = sc132gs_set_tpg,
    .sensor_mirror_set = sc132gs_mirror,
    .sensor_get_exposure_time_range = sc132gs_exposure_range,
    .sensor_set_focus_pos = sensor_autofocus_dev_set_position,
    .sensor_get_focus_pos = sensor_autofocus_dev_get_position,
    .sensor_get_foucs_cap = sensor_autofocus_dev_get_capability,
    .sensor_set_focus_power = sensor_autofocus_dev_power,
};

k_s32 sensor_sc132gs_probe(struct k_sensor_probe_cfg *cfg, struct sensor_driver_dev *dev)
{
#ifndef CONFIG_MPP_SC132GS_SKIP_CHIP_ID_CHECK
    k_u32 id;
#endif
    k_s32 ret;
    if (!cfg || !dev || !cfg->i2c_name)
        return -EINVAL;
    memset(dev, 0, sizeof(*dev));
    switch (cfg->csi_num) {
#ifdef CONFIG_MPP_ENABLE_CSI_DEV_0
    case 0:
        dev->sensor_mode_list = sensor_csi0_mode_list;
        dev->mode_count = sizeof(sensor_csi0_mode_list) / sizeof(sensor_csi0_mode_list[0]);
        break;
#endif
#if defined(CONFIG_MPP_ENABLE_CSI_DEV_1) && !defined(CONFIG_MPP_SC132GS_CSI0_4LANE)
    case 1:
        dev->sensor_mode_list = sensor_csi1_mode_list;
        dev->mode_count = sizeof(sensor_csi1_mode_list) / sizeof(sensor_csi1_mode_list[0]);
        break;
#endif
#ifdef CONFIG_MPP_ENABLE_CSI_DEV_2
    case 2:
        dev->sensor_mode_list = sensor_csi2_mode_list;
        dev->mode_count = sizeof(sensor_csi2_mode_list) / sizeof(sensor_csi2_mode_list[0]);
        break;
#endif
    default:
        return -EINVAL;
    }
    dev->pwd_gpio = cfg->pwd_gpio;
    dev->reset_gpio = cfg->reset_gpio;
    dev->i2c_info.i2c_bus = rt_i2c_bus_device_find(cfg->i2c_name);
    if (!dev->i2c_info.i2c_bus) {
        memset(dev, 0, sizeof(*dev));
        return -ENODEV;
    }
    snprintf(dev->i2c_info.i2c_name, sizeof(dev->i2c_info.i2c_name), "%s", cfg->i2c_name);
    dev->i2c_info.reg_addr_size = SENSOR_REG_VALUE_16BIT;
    dev->i2c_info.reg_val_size = SENSOR_REG_VALUE_8BIT;
    sc132gs_copy_mode(&dev->sensor_mode_list[0], &dev->current_sensor_mode);
    ret = sc132gs_power(dev, 1);
    if (ret)
        goto failed;
#ifdef CONFIG_MPP_SC132GS_SKIP_CHIP_ID_CHECK
    dev->i2c_info.slave_addr = CONFIG_MPP_SC132GS_I2C_ADDRESS;
#else
    for (k_u16 address = 0x30; address <= 0x33; ++address) {
        dev->i2c_info.slave_addr = address;
        ret = sc132gs_get_id(dev, &id);
        if (!ret)
            break;
    }
    if (ret)
        goto failed;
#endif
    dev->mirror_setting.sensor_type = dev->current_sensor_mode.sensor_type;
    dev->mirror_setting.mirror = VICAP_MIRROR_NONE;
    dev->sensor_func = sc132gs_functions;
    snprintf(dev->sensor_name, sizeof(dev->sensor_name), "sc132gs_csi%d", cfg->csi_num);
    sensor_autofocus_dev_probe(dev);
#ifdef CONFIG_MPP_SC132GS_SKIP_CHIP_ID_CHECK
    rt_kprintf("SC132GS: fixed binding, ID check skipped, %s addr=0x%02x, %u-lane RAW10\n",
               cfg->i2c_name, dev->i2c_info.slave_addr,
               dev->current_sensor_mode.mipi_info.mipi_lanes);
#else
    rt_kprintf("SC132GS: id=0x%04x, %s addr=0x%02x, %u-lane RAW10\n",
               id, cfg->i2c_name, dev->i2c_info.slave_addr,
               dev->current_sensor_mode.mipi_info.mipi_lanes);
#endif
    return 0;
failed:
    (void)sc132gs_power(dev, 0);
    memset(dev, 0, sizeof(*dev));
    return ret;
}
