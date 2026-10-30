/* SPDX-License-Identifier: Apache-2.0 */
#include "sensor_dev.h"
#include <assert.h>
#include <math.h>
#include <stdarg.h>

/* 108 MHz is a TEST FIXTURE, not a claim about the sensor's reset PLL. */
#undef CONFIG_MPP_SC038HGS_PIXEL_CLOCK_HZ
#define CONFIG_MPP_SC038HGS_PIXEL_CLOCK_HZ TEST_PIXEL_CLOCK
#undef CONFIG_MPP_ENABLE_CSI_DEV_0
#undef CONFIG_MPP_ENABLE_CSI_DEV_1
#undef CONFIG_MPP_ENABLE_CSI_DEV_2
#define CONFIG_MPP_ENABLE_CSI_DEV_0 1
#define CONFIG_MPP_ENABLE_CSI_DEV_1 1
#define CONFIG_MPP_ENABLE_CSI_DEV_2 1
#if !TEST_MONO
#undef CONFIG_MPP_SC038HGS_MONO
#endif
#include "../../kernel/sensor/src/sc038hgs/sc038hgs.c"

static k_u16 registers[65536];
static k_sensor_reg pending[8];
static unsigned pending_count;
static int holding, pwdn, delay_ms, fail_addr = -1, fail_gpio = -1;
static int ignore_addr = -1, fail_group_value = -1;
static k_u16 address = 0x32;
static struct rt_i2c_bus_device bus;

rt_err_t kd_pin_mode(rt_base_t pin, rt_base_t mode)
{ (void)mode; return pin == fail_gpio ? -EIO : 0; }
rt_err_t kd_pin_write(rt_base_t pin, rt_base_t value)
{
    if (pin == fail_gpio)
        return -EIO;
    if (pin == 4) { pwdn = value; delay_ms = 0; }
    return 0;
}
rt_err_t rt_thread_mdelay(rt_int32_t ms) { delay_ms += ms; return 0; }
void rt_kprintf(const char *fmt, ...) { (void)fmt; }
struct rt_i2c_bus_device *rt_i2c_bus_device_find(const char *name)
{ return strcmp(name, "missing") ? &bus : NULL; }

k_s32 sensor_reg_read(k_sensor_i2c_info *info, k_u16 reg, k_u16 *value)
{
    assert(pwdn && delay_ms >= 4);
    if (info->slave_addr != address || reg == fail_addr)
        return -EIO;
    *value = registers[reg];
    return 0;
}
k_s32 sensor_reg_write(k_sensor_i2c_info *info, k_u16 reg, k_u16 value)
{
    assert(pwdn && delay_ms >= 4);
    assert(reg != 0x0100 && reg != 0x0103);
    if (info->slave_addr != address || reg == fail_addr ||
        (reg == 0x3800 && value == fail_group_value))
        return -EIO;
    if (reg == ignore_addr)
        return 0;
    if (reg == 0x2103) {
        holding = 0;
        pending_count = 0;
    }
    if (reg == 0x3800) {
        if (value == 0) { holding = 1; pending_count = 0; }
        if (value == 0x10) holding = 0;
        if (value == 0x60)
            for (unsigned i = 0; i < pending_count; ++i)
                registers[pending[i].addr] = pending[i].val;
    } else if (holding) {
        assert(pending_count < 8);
        pending[pending_count++] = (k_sensor_reg){reg, value};
    } else {
        registers[reg] = value;
    }
    return 0;
}
k_s32 sensor_reg_list_write(k_sensor_i2c_info *info, const k_sensor_reg *regs)
{
    for (; regs->addr != REG_NULL; ++regs) {
        int ret = sensor_reg_write(info, regs->addr, regs->val);
        if (ret) return ret;
    }
    return 0;
}

static void prepare(void)
{
    memset(registers, 0, sizeof(registers));
    registers[0x3107] = 0x1a;
    registers[0x3108] = 0x79;
    registers[0x3031] = 0xe0;
    registers[0x3037] = 0x9f;
    registers[0x4b01] = 0x19; /* LVDS selected, other bits must survive. */
    registers[0x3222] = 0xff;
    registers[0x3225] = 0xff;
    registers[0x3282] = 0xff;
    registers[0x3e00] = 0xa0;
    registers[0x4501] = 0xc5;
    fail_addr = fail_gpio = -1;
    ignore_addr = fail_group_value = -1;
    holding = pending_count = pwdn = delay_ms = 0;
}

int main(void)
{
    struct sensor_driver_dev dev, other;
    struct k_sensor_probe_cfg cfg = {TEST_CSI, "i2c1", 4, -1};
    k_sensor_mode mode;
    k_sensor_gain gain = {0}, got;
    k_sensor_intg_time exposure = {0};
    k_sensor_exposure_time_range range;
    k_sensor_exposure_param param = {0}, got_param;
    k_u32 fps, raw;
    prepare();
    assert(sensor_sc038hgs_probe(&cfg, &dev) == 0);
    assert(dev.i2c_info.slave_addr == address && dev.mode_count == 1);
    assert(!dev.init_flag);
    assert(sc038hgs_stream(&dev, 1) != 0);
    mode = dev.current_sensor_mode;
    assert(sc038hgs_init(&dev, mode) == 0);
    assert(registers[0x2100] == 0);
    assert(registers[0x3031] == 0xea && registers[0x3037] == 0xbf);
    assert(registers[0x4b01] == 0x11);
    assert(dev.current_sensor_mode.mipi_info.csi_id == TEST_CSI);
    assert(!(registers[0x3222] & 1) && !(registers[0x3225] & 4));
    assert(!(registers[0x3282] & 8));
    assert(registers[0x3e00] == 0xa0);
    assert(sc038hgs_stream(&dev, 1) == 0 && registers[0x2100] == 1);
    assert(sc038hgs_stream(&dev, 0) == 0 && registers[0x2100] == 0);

    /* All documented analogue gain entries, including the 2x/2.5x boundary. */
    for (unsigned i = 0; i < 6; ++i)
        for (unsigned fine = 32; fine <= sc038hgs_again[i].max_fine; ++fine) {
            gain.gain[0] = sc038hgs_again[i].base * fine / 32.0f;
            assert(sc038hgs_set_again(&dev, gain) == 0);
            assert(registers[0x3e08] == sc038hgs_again[i].code);
            assert(registers[0x3e09] == fine);
            assert(sc038hgs_get_again(&dev, &got) == 0);
            assert(got.gain[0] == gain.gain[0]);
        }
    gain.gain[0] = 2.49f;
    assert(sc038hgs_set_again(&dev, gain) == 0);
    assert(registers[0x3e08] == 1 && registers[0x3e09] == 0x27);
    for (unsigned i = 0; i < 3; ++i)
        for (unsigned fine = 32; fine <= 63; ++fine) {
            gain.gain[0] = (1U << i) * fine / 32.0f;
            assert(sc038hgs_set_dgain(&dev, gain) == 0);
            assert(registers[0x3e06] == (1U << i) - 1U);
            assert(registers[0x3e07] == fine * 4);
            assert(sc038hgs_get_dgain(&dev, &got) == 0 && got.gain[0] == gain.gain[0]);
        }
    gain.gain[0] = NAN;
    assert(sc038hgs_set_again(&dev, gain) == -EINVAL);
    gain.gain[0] = INFINITY;
    assert(sc038hgs_set_dgain(&dev, gain) == -EINVAL);
    gain.gain[0] = 1000;
    assert(sc038hgs_set_again(&dev, gain) == 0);
    assert(dev.current_sensor_mode.ae_info.cur_again == 39.375f);
    assert(sc038hgs_set_tpg(&dev, (k_sensor_test_pattern){1, 0}) == 0);
    assert(registers[0x4501] == 0xcd);
    assert(sc038hgs_set_tpg(&dev, (k_sensor_test_pattern){0, 0}) == 0);
    assert(registers[0x4501] == 0xc5);
    k_sensor_enum_mode entry = {.index = 0};
    assert(sc038hgs_enum_mode(&dev, &entry) == 0 && entry.mode.mipi_info.mipi_lanes == 1);
    entry.index = 1;
    assert(sc038hgs_enum_mode(&dev, &entry) == -EINVAL);
    mode.sensor_type = SENSOR_TYPE_MAX;
    assert(sc038hgs_init(&dev, mode) == -EINVAL && dev.init_flag);
    mode = dev.current_sensor_mode;
    assert(sc038hgs_mirror(&dev, (k_vicap_mirror_mode){mode.sensor_type, VICAP_MIRROR_BOTH}) == 0);
    assert((registers[0x3221] & 0x66) == 0x66);
    assert(dev.current_sensor_mode.bayer_pattern ==
           (TEST_MONO ? BAYER_PAT_BGGR : BAYER_PAT_RGGB));

#if TEST_PIXEL_CLOCK
    assert(sc038hgs_exposure_range(&dev, &range) == 0 && range.min_intg_time_us > 0);
    assert(sc038hgs_get_fps(&dev, &fps) == 0 && fps == 240000);
    exposure.intg_time[0] = 128.5f * dev.current_sensor_mode.ae_info.one_line_exp_time;
    assert(sc038hgs_set_exposure(&dev, exposure) == 0);
    assert(sc038hgs_read_exposure(&dev, &raw) == 0 && raw == 2056);
    assert(registers[0x3e00] == 0xa0);
    exposure.intg_time[0] = 1.0f;
    assert(sc038hgs_set_exposure(&dev, exposure) == 0);
    assert(sc038hgs_read_exposure(&dev, &raw) == 0 && raw == 589U * 16U);
    assert(sc038hgs_set_fps(&dev, 30000) == 0);
    assert(dev.current_sensor_mode.ae_info.frame_length == 4800);
    assert(sc038hgs_set_exposure(&dev, exposure) == 0);
    assert(registers[0x3e00] == 0xa1); /* Exposure exceeds 4095 lines. */
    assert(sc038hgs_set_fps(&dev, 240000) == 0);
    assert(sc038hgs_read_exposure(&dev, &raw) == 0 && raw == 589U * 16U);
    assert(sc038hgs_set_fps(&dev, 0) == -EINVAL);
    assert(sc038hgs_set_fps(&dev, 1) == -EINVAL);
    assert(sc038hgs_set_fps(&dev, 240001) == -EINVAL);
    param.gain[0] = 39.375f * 7.875f;
    param.exp_time[0] = 128.5f * dev.current_sensor_mode.ae_info.one_line_exp_time;
    assert(sc038hgs_set_exp_parm(&dev, param) == 0 && pending_count == 7);
    assert(sc038hgs_get_exp_parm(&dev, &got_param) == 0);
    assert(got_param.gain[0] == param.gain[0]);
    assert(fabsf(got_param.exp_time[0] - param.exp_time[0]) < 1e-7f);
    param.gain[0] = 2.49f;
    assert(sc038hgs_set_exp_parm(&dev, param) == 0);
    assert(dev.current_sensor_mode.ae_info.cur_again == 2.4375f);
    assert(dev.current_sensor_mode.ae_info.cur_dgain == 1.0f);
    param.gain[0] = NAN;
    assert(sc038hgs_set_exp_parm(&dev, param) == -EINVAL);
    param.gain[0] = 1.0f;
    param.exp_time[0] = INFINITY;
    assert(sc038hgs_set_exp_parm(&dev, param) == -EINVAL);
    /* The last gain register fails: neither exposure nor gain is launched. */
    k_u32 old_exp;
    assert(sc038hgs_read_exposure(&dev, &old_exp) == 0);
    fail_addr = 0x3e07;
    param.exp_time[0] = dev.current_sensor_mode.ae_info.min_integraion_time;
    assert(sc038hgs_set_exp_parm(&dev, param) == -EIO && !dev.init_flag);
    assert(sc038hgs_read_exposure(&dev, &raw) == 0 && raw == old_exp);
    assert(registers[0x3e09] == 0x27);
    fail_addr = -1;
    assert(sc038hgs_init(&dev, mode) == 0);
#else
    assert(sc038hgs_exposure_range(&dev, &range) == -ENOSYS);
    assert(dev.current_sensor_mode.fps == 0);
    assert(sc038hgs_get_fps(&dev, &fps) == -ENOSYS);
    assert(sc038hgs_set_fps(&dev, 30000) == -ENOSYS);
    assert(sc038hgs_set_exposure(&dev, exposure) == -ENOSYS);
    assert(sc038hgs_get_exposure(&dev, &exposure) == -ENOSYS);
    param.gain[0] = 1.0f;
    assert(sc038hgs_set_exp_parm(&dev, param) == -ENOSYS);
    assert(sc038hgs_get_exp_parm(&dev, &got_param) == -ENOSYS);
#endif
    /* Failed group payload must not change hardware gain or cached gain. */
    float before = dev.current_sensor_mode.ae_info.cur_again;
    k_u16 before_coarse = registers[0x3e08], before_fine = registers[0x3e09];
    fail_addr = 0x3e09;
    gain.gain[0] = 1.0f;
    assert(sc038hgs_set_again(&dev, gain) != 0);
    assert(registers[0x3e08] == before_coarse && registers[0x3e09] == before_fine);
    assert(dev.current_sensor_mode.ae_info.cur_again == before && !dev.init_flag);
    assert(sc038hgs_stream(&dev, 1) != 0);
    fail_addr = -1;
    assert(sc038hgs_init(&dev, mode) == 0);
    other = dev;
    gain.gain[0] = 10.0f;
    assert(sc038hgs_set_again(&dev, gain) == 0);
    assert(other.current_sensor_mode.ae_info.cur_again == 1.0f &&
           dev.current_sensor_mode.ae_info.cur_again == 10.0f);
    assert(sc038hgs_power(&dev, 0) == 0 && !pwdn && !dev.init_flag);
    assert(sc038hgs_get_id(&dev, &raw) != 0);
    assert(sc038hgs_exposure_range(&dev, &range) == -EIO);

    /* A sensor ACKs the write but retains LVDS: initialization must fail. */
    prepare();
    assert(sensor_sc038hgs_probe(&cfg, &dev) == 0);
    ignore_addr = 0x4b01;
    assert(sc038hgs_init(&dev, dev.current_sensor_mode) == -EIO);
    assert(!dev.init_flag && !dev.mode_init_flag && !(registers[0x2100] & 1));
    ignore_addr = -1;
    for (unsigned i = 0; i < 3; ++i) {
        assert(sc038hgs_init(&dev, dev.current_sensor_mode) == 0);
        fail_group_value = (int[]){0x00, 0x10, 0x60}[i];
        assert(sc038hgs_set_again(&dev, gain) == -EIO && !dev.init_flag);
        assert(sc038hgs_stream(&dev, 1) == -EIO);
        fail_group_value = -1;
    }
    assert(sc038hgs_power(&dev, 0) == 0);

    for (address = 0x30; address <= 0x33; ++address) {
        prepare();
        assert(sensor_sc038hgs_probe(&cfg, &dev) == 0 && dev.i2c_info.slave_addr == address);
        assert(sc038hgs_power(&dev, 0) == 0);
    }
    prepare();
    registers[0x3108] = 0;
    address = 0x30;
    assert(sensor_sc038hgs_probe(&cfg, &dev) != 0 && !dev.sensor_func.sensor_init && !pwdn);
    prepare();
    fail_gpio = 4;
    assert(sensor_sc038hgs_probe(&cfg, &dev) != 0);
    printf("SC038HGS clock=%u mono=%d csi=%d: PASS\n", TEST_PIXEL_CLOCK, TEST_MONO, TEST_CSI);
    return 0;
}
