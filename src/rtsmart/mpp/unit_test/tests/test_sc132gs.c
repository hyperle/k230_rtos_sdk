#include <rtthread.h>
#include "k_type.h"
#include <assert.h>
#include <math.h>
#include <stdarg.h>

#ifndef TEST_SKIP_ID_CHECK
#define TEST_SKIP_ID_CHECK 0
#endif
#ifndef TEST_FIXED_ADDRESS
#define TEST_FIXED_ADDRESS 0x32
#endif
#undef CONFIG_MPP_SC132GS_SKIP_CHIP_ID_CHECK
#undef CONFIG_MPP_SC132GS_I2C_ADDRESS
#if TEST_SKIP_ID_CHECK
#define CONFIG_MPP_SC132GS_SKIP_CHIP_ID_CHECK 1
#define CONFIG_MPP_SC132GS_I2C_ADDRESS TEST_FIXED_ADDRESS
#endif

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
#define TEST_LANES ((TEST_FOUR_LANE && TEST_CSI == 0) ? 4 : 2)
#undef CONFIG_MPP_SC132GS_CSI0_CLOCK_INVERT
#if TEST_CLOCK_INVERT
#define CONFIG_MPP_SC132GS_CSI0_CLOCK_INVERT 1
#endif
#define TEST_CLOCK_INVERT_VALUE ((TEST_CLOCK_INVERT && TEST_CSI == 0) ? 8U : 0U)
#undef CONFIG_MPP_SC132GS_EXTCLK_HZ
#define CONFIG_MPP_SC132GS_EXTCLK_HZ TEST_EXTCLK
#undef CONFIG_MPP_SC132GS_MONO
#if TEST_MONO
#define CONFIG_MPP_SC132GS_MONO 1
#endif
#undef CONFIG_MPP_SC132GS_POWER_ACTIVE_LOW
#if TEST_ACTIVE_LOW
#define CONFIG_MPP_SC132GS_POWER_ACTIVE_LOW 1
#endif
#undef CONFIG_MPP_ENABLE_SENSOR_SC132GS_ON_CSI0_USE_CHIP_CLK
#undef CONFIG_MPP_ENABLE_SENSOR_SC132GS_ON_CSI1_USE_CHIP_CLK
#undef CONFIG_MPP_ENABLE_SENSOR_SC132GS_ON_CSI2_USE_CHIP_CLK
#if TEST_CLOCK
#define CONFIG_MPP_ENABLE_SENSOR_SC132GS_ON_CSI0_USE_CHIP_CLK 1
#define CONFIG_MPP_ENABLE_SENSOR_SC132GS_ON_CSI1_USE_CHIP_CLK 1
#define CONFIG_MPP_ENABLE_SENSOR_SC132GS_ON_CSI2_USE_CHIP_CLK 1
#undef CONFIG_MPP_CSI_DEV0_MCLK_NUM
#undef CONFIG_MPP_CSI_DEV1_MCLK_NUM
#undef CONFIG_MPP_CSI_DEV2_MCLK_NUM
#define CONFIG_MPP_CSI_DEV0_MCLK_NUM 1
#define CONFIG_MPP_CSI_DEV1_MCLK_NUM 2
#define CONFIG_MPP_CSI_DEV2_MCLK_NUM 3
#endif

#include "../../kernel/sensor/src/sc132gs/sc132gs.c"

static k_u16 registers[65536];
static k_sensor_reg pending[16];
static unsigned pending_count, group_launches, register_writes, clock_calls;
static unsigned id_reads;
static int holding, delay_ms, fail_addr = -1, fail_gpio = -1;
static int ignore_addr = -1, fail_group_value = -1, fail_clock;
static int fail_gpio_read = -1;
static int fail_gpio_write = -1;
/* Transient bus errors: the next access to this register fails once. */
static int fail_once_read = -1, fail_once_write = -1;
static int gpio_values[64], gpio_modes[64];
static int lost_mux = -1;
static k_u16 address = TEST_FIXED_ADDRESS;
static struct rt_i2c_bus_device bus;

static void reset_registers(void)
{
    memset(registers, 0, sizeof(registers));
    registers[0x3107] = 0x01;
    registers[0x3108] = 0x32;
    registers[0x3018] = 0x72;
    registers[0x3019] = 0xa5;
    registers[0x3208] = 0x04;
    registers[0x3209] = 0x38;
    registers[0x320a] = 0x05;
    registers[0x320b] = 0x00;
    registers[0x320e] = 0x06;
    registers[0x320f] = 0x1a;
    registers[0x3031] = 0xea;
    /* Start in the incompatible PHY word mode so a skipped write is caught. */
    registers[0x3037] = 0xbf;
    /* Opposite polarity requires an actual write; preserve delay/reserved bits. */
    registers[0x3654] = 0xa5U | (TEST_CLOCK_INVERT_VALUE ^ 8U);
    registers[0x3222] = 0xff;
    registers[0x3e03] = 0xa3;
    registers[0x3e06] = 0xb0;
    registers[0x4501] = 0xc5;
    holding = pending_count = 0;
}

uint32_t kd_pin_get_ddr(rt_base_t pin) { return gpio_modes[pin] == GPIO_DM_OUTPUT; }
int drv_fpioa_get_pin_func(int pin, fpioa_func_t *function) {
    *function = (fpioa_func_t)(GPIO0 + pin + (pin == lost_mux ? 1 : 0));
    return 0;
}

rt_err_t kd_pin_mode(rt_base_t pin, rt_base_t mode)
{ if (pin == fail_gpio) return -EIO; gpio_modes[pin] = mode; if (pin == lost_mux) lost_mux = -1; return 0; }
rt_err_t kd_pin_read(rt_base_t pin)
{ return pin == fail_gpio_read ? -EIO : gpio_values[pin]; }
rt_err_t kd_pin_write(rt_base_t pin, rt_base_t value)
{
    if (pin == fail_gpio || pin == fail_gpio_write)
        return -EIO;
    gpio_values[pin] = value;
    delay_ms = 0;
    return 0;
}
rt_err_t rt_thread_mdelay(rt_int32_t milliseconds)
{ delay_ms += milliseconds; return 0; }
void rt_kprintf(const char *format, ...) { (void)format; }
struct rt_i2c_bus_device *rt_i2c_bus_device_find(const char *name)
{ return strcmp(name, "missing") ? &bus : NULL; }
k_s32 vicap_set_mclk(k_vicap_mclk *mclk)
{
    ++clock_calls;
    assert(mclk->mclk_en == 1 && mclk->mclk_div == 25);
    assert(mclk->id == TEST_CSI + 1);
    return fail_clock ? -EIO : 0;
}
k_s32 sensor_autofocus_dev_probe(struct sensor_driver_dev *dev)
{ (void)dev; return 0; }
k_s32 sensor_autofocus_dev_set_position(void *ctx, k_sensor_focus_pos *position)
{ (void)ctx; (void)position; return -ENOSYS; }
k_s32 sensor_autofocus_dev_get_position(void *ctx, k_sensor_focus_pos *position)
{ (void)ctx; (void)position; return -ENOSYS; }
k_s32 sensor_autofocus_dev_get_capability(void *ctx, k_sensor_autofocus_caps *caps)
{ (void)ctx; (void)caps; return -ENOSYS; }
k_s32 sensor_autofocus_dev_power(void *ctx, int on)
{ (void)ctx; (void)on; return -ENOSYS; }

k_s32 sensor_reg_read(k_sensor_i2c_info *info, k_u16 reg, k_u16 *value)
{
    assert(delay_ms >= 1000);
    if (reg == 0x3107 || reg == 0x3108)
        ++id_reads;
    if (info->slave_addr != address || reg == fail_addr)
        return -EIO;
    if (reg == fail_once_read) {
        fail_once_read = -1;
        return -EIO;
    }
    *value = registers[reg];
    return 0;
}
k_s32 sensor_reg_write(k_sensor_i2c_info *info, k_u16 reg, k_u16 value)
{
    assert(delay_ms >= 1000);
    ++register_writes;
    if (info->slave_addr != address || reg == fail_addr ||
        (reg == 0x3800 && value == fail_group_value))
        return -EIO;
    if (reg == fail_once_write) {
        fail_once_write = -1;
        return -EIO;
    }
    if (reg == ignore_addr)
        return 0;
    if (reg == 0x0103)
        reset_registers();
    if (reg == 0x3800) {
        if (value == 0) { holding = 1; pending_count = 0; }
        if (value == 0x10)
            holding = 0;
        if (value == 0x60) {
            ++group_launches;
            for (unsigned index = 0; index < pending_count; ++index)
                registers[pending[index].addr] = pending[index].val;
        }
    } else if (holding) {
        assert(pending_count < 16);
        pending[pending_count++] = (k_sensor_reg){reg, value};
    } else {
        registers[reg] = value;
    }
    return 0;
}
k_s32 sensor_reg_list_write(k_sensor_i2c_info *info, const k_sensor_reg *regs)
{
    for (; regs->addr != REG_NULL; ++regs) {
        assert(regs->addr != 0x0100 || !regs->val);
        int ret = sensor_reg_write(info, regs->addr, regs->val);
        if (ret)
            return ret;
    }
    return 0;
}

static void prepare(void)
{
    reset_registers();
    memset(gpio_values, -1, sizeof(gpio_values));
    for (unsigned n = 0; n < 64; ++n) gpio_modes[n] = GPIO_DM_INPUT;
    lost_mux = -1;
    fail_addr = fail_gpio = ignore_addr = fail_group_value = -1;
    fail_gpio_read = -1;
    fail_once_read = fail_once_write = -1;
    fail_clock = delay_ms = 0;
    register_writes = group_launches = clock_calls = 0;
    id_reads = 0;
}

static void test_controls(struct sensor_driver_dev *dev)
{
    k_sensor_gain gain = {0}, got;
    k_sensor_intg_time time = {0}, got_time;
    k_sensor_exposure_param param = {0}, got_param;
    k_sensor_exposure_time_range range;
    k_sensor_isp_status status;
    k_sensor_caps caps;
    k_sensor_test_pattern tpg;
    k_u32 fps, raw;
    k_u32 max_fps = TEST_CLOCK ? 118800U : TEST_EXTCLK / 200U;
    assert(sc132gs_get_fps(dev, &fps) == 0 && fps == max_fps);
    assert(dev->current_sensor_mode.ae_info.cur_fps == fps);
    assert(dev->current_sensor_mode.ae_info.frame_length == 1350);
    assert(dev->sensor_func.sensor_get_caps(dev, &caps) == 0);
    assert(caps.mipi_lanes == TEST_LANES && caps.resolution.width == 1080);
    assert(dev->sensor_func.sensor_get_isp_status(dev, &status) == 0);
    assert(!status.use_sensor_ae && !status.use_sensor_blc && !status.use_sensor_awb);
    assert(registers[0x3031] == 0xea && registers[0x3037] == 0x9f);
    assert(registers[0x3654] == (0xa5U | TEST_CLOCK_INVERT_VALUE));
    assert(registers[0x3e03] == 0xab && registers[0x3e06] == 0xb0);
    assert(!(registers[0x3222] & 2));
    assert(sc132gs_read_exposure(dev, &raw) == 0 && raw == 1342U * 16U);
    assert(sc132gs_get_again(dev, &got) == 0 && got.gain[0] == 1.0f);
    assert(sc132gs_get_dgain(dev, &got) == 0 && got.gain[0] == 1.0f);
    for (unsigned index = 0; index < 5; ++index)
        for (unsigned fine = 32; fine <= sc132gs_again[index].max_fine; ++fine) {
            gain.gain[0] = sc132gs_again[index].base * fine / 32.0f;
            assert(sc132gs_set_again(dev, gain) == 0);
            assert(registers[0x3e08] == sc132gs_again[index].code);
            assert(registers[0x3e09] == fine);
            assert(sc132gs_get_again(dev, &got) == 0 && got.gain[0] == gain.gain[0]);
            assert(registers[0x33fa] == (gain.gain[0] < 2 ? 1 : 2));
        }
    gain.gain[0] = 1.8f;
    assert(sc132gs_set_again(dev, gain) == 0);
    assert(registers[0x3e08] == 3 && registers[0x3e09] == 0x39);
    gain.gain[0] = 3.57f;
    assert(sc132gs_set_again(dev, gain) == 0);
    assert(registers[0x3e08] == 0x23 && registers[0x3e09] == 0x3f);
    for (unsigned coarse = 0; coarse < 5; ++coarse)
        for (unsigned fine = 128; fine <= 255; ++fine) {
            gain.gain[0] = (1U << coarse) * fine / 128.0f;
            assert(sc132gs_set_dgain(dev, gain) == 0);
            assert(registers[0x3e06] == (0xb0U | ((1U << coarse) - 1U)));
            assert(registers[0x3e07] == fine);
            assert(sc132gs_get_dgain(dev, &got) == 0 && got.gain[0] == gain.gain[0]);
        }
    gain.gain[0] = NAN;
    assert(sc132gs_set_again(dev, gain) == -EINVAL);
    gain.gain[0] = INFINITY;
    assert(sc132gs_set_dgain(dev, gain) == -EINVAL);
    gain.gain[0] = -1;
    assert(sc132gs_set_again(dev, gain) == -EINVAL);
    gain.gain[0] = FLT_MAX;
    assert(sc132gs_set_again(dev, gain) == 0);
    assert(dev->current_sensor_mode.ae_info.cur_again == SC132GS_MAX_AGAIN);
    assert(sc132gs_set_dgain(dev, gain) == 0);
    assert(dev->current_sensor_mode.ae_info.cur_dgain == SC132GS_MAX_DGAIN);

    registers[0x3e00] = 0xa0;
    time.intg_time[0] = dev->current_sensor_mode.ae_info.one_line_exp_time * 10.0625f;
    assert(sc132gs_set_exposure(dev, time) == 0);
    assert(sc132gs_read_exposure(dev, &raw) == 0 && raw == 161);
    assert(registers[0x3e00] == 0xa0);
    assert(sc132gs_get_exposure(dev, &got_time) == 0);
    assert(fabsf(got_time.intg_time[0] - time.intg_time[0]) < 1e-9f);
    time.intg_time[0] = 0;
    assert(sc132gs_set_exposure(dev, time) == 0);
    assert(sc132gs_read_exposure(dev, &raw) == 0 && raw == 0);
    time.intg_time[0] = NAN;
    assert(sc132gs_set_exposure(dev, time) == -EINVAL);
    assert(sc132gs_set_fps(dev, 0) == -EINVAL);
    assert(sc132gs_set_fps(dev, max_fps + 1) == -EINVAL);
    assert(sc132gs_set_fps(dev, 1) == -EINVAL);
    assert(sc132gs_set_fps(dev, 3000) == 0);
    assert(sc132gs_get_fps(dev, &fps) == 0 && fps == 3000);
    time.intg_time[0] = FLT_MAX;
    assert(sc132gs_set_exposure(dev, time) == 0);
    assert(sc132gs_read_exposure(dev, &raw) == 0 && raw > 0xffff);
    assert((registers[0x3e00] & 0xf0) == 0xa0);
    assert(sc132gs_set_fps(dev, max_fps) == 0);
    assert(sc132gs_read_exposure(dev, &raw) == 0 && raw == 1342U * 16U);
    assert(sc132gs_exposure_range(dev, &range) == 0 && range.max_intg_time_us > 8000);

    param.gain[0] = 50;
    param.exp_time[0] = 0.001f;
    unsigned launches = group_launches;
    assert(sc132gs_set_exp_parm(dev, param) == 0 && group_launches == launches + 1);
    assert(pending_count == 9);
    assert(sc132gs_get_exp_parm(dev, &got_param) == 0);
    assert(got_param.gain[0] <= 50 && got_param.gain[0] > 49);
    assert(fabsf(got_param.exp_time[0] - param.exp_time[0]) < 1e-6f);
    assert(sc132gs_set_tpg(dev, (k_sensor_test_pattern){1, 0}) == 0);
    assert(registers[0x4501] == 0xcd);
    assert(!(registers[0x3902] & 0x40) && !(registers[0x391d] & 7));
    assert((registers[0x3e06] & 0x0f) == 0x0f);
    assert(sc132gs_set_dgain(dev, gain) == -EBUSY);
    assert(sc132gs_set_exp_parm(dev, param) == -EBUSY);
    assert(sc132gs_get_tpg(dev, &tpg) == 0 && tpg.enable);
    assert(sc132gs_set_tpg(dev, (k_sensor_test_pattern){0, 0}) == 0);
    assert(registers[0x4501] == 0xc5);
    assert(registers[0x3902] == 0xc5);
    assert(dev->current_sensor_mode.ae_info.cur_dgain < 2.0f);
    assert(sc132gs_set_tpg(dev, (k_sensor_test_pattern){1, 1}) == -EINVAL);
    for (unsigned mirror = 0; mirror < 4; ++mirror) {
        k_vicap_mirror_mode setting = {0};
        setting.sensor_type = dev->current_sensor_mode.sensor_type;
        setting.mirror = mirror;
        registers[0x3221] = 0x99;
        assert(sc132gs_mirror(dev, setting) == 0);
        assert((registers[0x3221] & ~0x66) == 0x99);
        assert((registers[0x3221] & 0x66) == (mirror & 1 ? 6 : 0) + (mirror & 2 ? 0x60 : 0));
        if (TEST_MONO)
            assert(dev->current_sensor_mode.bayer_pattern == BAYER_PAT_BGGR);
        else if (mirror == 3)
            assert(dev->current_sensor_mode.bayer_pattern == BAYER_PAT_RGGB);
    }
}

int main(void)
{
    struct sensor_driver_dev dev;
    struct k_sensor_probe_cfg cfg = {TEST_CSI, "i2c1", 4, -1};
    k_sensor_mode full, vga;
    k_sensor_enum_mode entry = {.index = 1};
    k_sensor_gain gain = {.gain = {2}};
    k_u32 id;
    k_s32 connected;
    prepare();
    if (TEST_SKIP_ID_CHECK)
        fail_addr = 0x3108;
    k_s32 probe_result = sensor_sc132gs_probe(&cfg, &dev);
    if (TEST_FOUR_LANE && TEST_CSI == 1) {
        assert(probe_result == -EINVAL);
        assert(!register_writes && !clock_calls && !delay_ms);
        assert(gpio_values[4] == -1 && !dev.power_flag && !dev.sensor_func.sensor_init);
        printf("SC132GS four_lane=%d mono=%d csi=%d clock=%d active_low=%d extclk=%u clock_invert=%d reserved: PASS\n",
               TEST_FOUR_LANE, TEST_MONO, TEST_CSI, TEST_CLOCK, TEST_ACTIVE_LOW, TEST_EXTCLK, TEST_CLOCK_INVERT);
        return 0;
    }
    assert(probe_result == 0);
    if (TEST_SKIP_ID_CHECK)
        assert(id_reads == 0);
    assert(dev.i2c_info.slave_addr == address && dev.mode_count == 2);
    assert(!dev.init_flag && dev.power_flag && !register_writes);
    assert(clock_calls == TEST_CLOCK);
    assert(gpio_values[4] == (TEST_ACTIVE_LOW ? GPIO_PV_LOW : GPIO_PV_HIGH));
    /* A Pin finalizer can change DDR while the software still says powered. */
    gpio_modes[4] = GPIO_DM_INPUT;
    assert(sc132gs_power(&dev, 1) == 0);
    assert(gpio_modes[4] == GPIO_DM_OUTPUT && dev.power_flag);
    assert(sensor_sc132gs_power_cache_valid(&dev));
    lost_mux = 4;
    assert(!sensor_sc132gs_power_cache_valid(&dev));
    assert(sc132gs_power(&dev, 1) == 0 && sensor_sc132gs_power_cache_valid(&dev));
    /* A healthy cache check performs no sensor I2C transaction. */
    unsigned cached_writes = register_writes, cached_id_reads = id_reads;
    assert(sc132gs_power(&dev, 1) == 0);
    assert(register_writes == cached_writes && id_reads == cached_id_reads);
    full = dev.current_sensor_mode;
    assert(full.mipi_info.csi_id == TEST_CSI && full.mipi_info.mipi_lanes == TEST_LANES);
    assert(sc132gs_stream(&dev, 1) == -EIO);
    assert(sc132gs_init(&dev, full) == 0 && registers[0x0100] == 0);
    if (TEST_SKIP_ID_CHECK)
        assert(id_reads == 0);
    fail_addr = -1;
    assert(registers[0x3018] == (((TEST_LANES - 1U) << 5) | 0x12));
    assert(registers[0x3019] == (TEST_LANES == 2 ? 0x0c : 0xa5));
    assert(sc132gs_stream(&dev, 1) == 0 && registers[0x0100] == 1);
    assert(sc132gs_get_id(&dev, &id) == 0 && id == SC132GS_CHIP_ID);
    assert(registers[0x0100] == 1);
    assert(sc132gs_stream(&dev, 0) == 0);
    test_controls(&dev);
    assert(sc132gs_enum_mode(&dev, &entry) == 0 && entry.mode.size.width == 640);
    vga = entry.mode;
    assert(dev.current_sensor_mode.sensor_type == full.sensor_type);
    assert(vga.ae_info.frame_length == 675 && vga.mipi_info.csi_id == TEST_CSI);
    assert(vga.mipi_info.mipi_lanes == TEST_LANES);
    assert(sc132gs_init(&dev, vga) == 0);
    assert(registers[0x3018] == (((TEST_LANES - 1U) << 5) | 0x12));
    assert(registers[0x3019] == (TEST_LANES == 2 ? 0x0c : 0xa5));
    assert(registers[0x3654] == (0xa5U | TEST_CLOCK_INVERT_VALUE));
    assert(registers[0x320e] == 2 && registers[0x320f] == 0xa3);
    assert(dev.current_sensor_mode.ae_info.cur_fps == (TEST_CLOCK ? 237600U : TEST_EXTCLK / 100U));
    assert(sc132gs_get_mode(&dev, &full) == 0);
    assert(dev.current_sensor_mode.sensor_type == vga.sensor_type);
    assert(sc132gs_init(&dev, full) == 0 && registers[0x320e] == 5);
    entry.index = 2;
    assert(sc132gs_enum_mode(&dev, &entry) == -EINVAL);
    assert(sc132gs_get_id(&dev, NULL) == -EINVAL);
    fail_addr = TEST_SKIP_ID_CHECK ? 0x0100 : 0x3108;
    assert(sc132gs_connected(&dev, &connected) != 0 && !connected);
    fail_addr = -1;
    assert(sc132gs_connected(&dev, &connected) == 0 && connected);

    /* Failed initialization must leave a confirmed power-off, preserving the
     * actual failure instead of sending another I2C cleanup transaction. */
    unsigned failed_init_writes = register_writes;
    fail_addr = 0x0103;
    assert(sc132gs_init(&dev, full) == -EIO);
    assert(!dev.init_flag && !dev.mode_init_flag && !dev.power_flag);
    /* Only the bounded retries of the failing write; no cleanup transaction. */
    assert(register_writes == failed_init_writes + SC132GS_I2C_ATTEMPTS);
    assert(gpio_values[4] == (TEST_ACTIVE_LOW ? GPIO_PV_HIGH : GPIO_PV_LOW));
    fail_addr = -1;
    assert(sc132gs_init(&dev, full) == 0 && dev.power_flag);

    /* A single lost ACK in the mode table (seen on the board at 0x36f9) or in
     * a readback is retried; initialization then completes normally. */
    fail_once_write = 0x36f9;
    unsigned transient_writes = register_writes;
    assert(sc132gs_init(&dev, full) == 0 && dev.init_flag && dev.mode_init_flag);
    assert(fail_once_write == -1 && register_writes > transient_writes);
    fail_once_write = 0x0103;
    assert(sc132gs_init(&dev, full) == 0 && dev.init_flag);
    fail_once_read = 0x320e;
    assert(sc132gs_init(&dev, full) == 0 && dev.init_flag && fail_once_read == -1);

    /* A physical power-off failure must not become an already-off cache hit. */
    fail_gpio_write = 4;
    assert(sc132gs_power(&dev, 0) == -EIO);
    assert(dev.power_flag && !dev.init_flag && !dev.mode_init_flag);
    fail_gpio_write = -1;
    assert(sc132gs_power(&dev, 0) == 0 && !dev.power_flag);
    assert(sc132gs_init(&dev, full) == 0);

    const int interface_failures[] = {0x3018, 0x3019, 0x3654};
    for (unsigned index = 0; index < sizeof(interface_failures) / sizeof(interface_failures[0]); ++index) {
        fail_addr = interface_failures[index];
        if (fail_addr == 0x3019 && TEST_LANES == 4)
            continue;
        assert(sc132gs_init(&dev, full) != 0);
        assert(!dev.init_flag && !dev.mode_init_flag && !registers[0x0100]);
        fail_addr = -1;
        assert(sc132gs_init(&dev, full) == 0);
    }
    fail_addr = -1;

    const int failures[] = {0x3e09, 0x33fa};
    for (unsigned index = 0; index < sizeof(failures) / sizeof(failures[0]); ++index) {
        unsigned launches = group_launches;
        fail_addr = failures[index];
        assert(sc132gs_set_again(&dev, gain) != 0 && group_launches == launches);
        assert(!dev.init_flag && !dev.mode_init_flag);
        assert(sc132gs_stream(&dev, 1) == -EIO);
        fail_addr = -1;
        assert(sc132gs_init(&dev, full) == 0);
    }
    const int groups[] = {0, 0x10, 0x60};
    for (unsigned index = 0; index < sizeof(groups) / sizeof(groups[0]); ++index) {
        fail_group_value = groups[index];
        assert(sc132gs_set_again(&dev, gain) != 0 && !dev.init_flag);
        fail_group_value = -1;
        assert(sc132gs_init(&dev, full) == 0);
    }
    const int ignored_interface_writes[] = {0x3037, 0x3654};
    for (unsigned index = 0; index < sizeof(ignored_interface_writes) / sizeof(ignored_interface_writes[0]); ++index) {
        ignore_addr = ignored_interface_writes[index];
        assert(sc132gs_init(&dev, full) != 0);
        assert(!dev.init_flag && !dev.mode_init_flag && !registers[0x0100]);
        ignore_addr = -1;
        assert(sc132gs_init(&dev, full) == 0);
    }
    assert(sc132gs_power(&dev, 0) == 0 && !dev.power_flag && !dev.init_flag);
    assert(gpio_values[4] == (TEST_ACTIVE_LOW ? GPIO_PV_HIGH : GPIO_PV_LOW));
    assert(sc132gs_power(&dev, 1) == 0 && !dev.init_flag);
    assert(sc132gs_stream(&dev, 1) == -EIO);
    assert(sc132gs_init(&dev, full) == 0);
    assert(sc132gs_power(&dev, 0) == 0);

    /* A failed stop may report success only after actual EN power-off. */
    assert(sc132gs_power(&dev, 1) == 0);
    assert(sc132gs_init(&dev, full) == 0);
    assert(sc132gs_stream(&dev, 1) == 0);
    fail_addr = 0x0100;
    assert(sc132gs_stream(&dev, 1) == -EIO && dev.power_flag);
    k_s32 saved_power_pin = dev.pwd_gpio;
    dev.pwd_gpio = -1;
    assert(sc132gs_stream(&dev, 0) == -EIO && dev.power_flag);
    dev.pwd_gpio = saved_power_pin;
    fail_gpio = saved_power_pin;
    assert(sc132gs_stream(&dev, 0) == -EIO && dev.power_flag);
    fail_gpio = -1;
    fail_gpio_read = saved_power_pin;
    assert(sc132gs_stream(&dev, 0) == -EIO && dev.power_flag);
    fail_gpio_read = -1;
    /* The mock resets its settle timer after a GPIO write, even when its
     * subsequent readback fails. A later stop retry must remain truthful. */
    rt_thread_mdelay(1000);
    assert(sc132gs_stream(&dev, 0) == 0);
    assert(!dev.power_flag && !dev.init_flag && !dev.mode_init_flag);
    assert(gpio_modes[saved_power_pin] == GPIO_DM_OUTPUT);
    assert(gpio_values[saved_power_pin] == (TEST_ACTIVE_LOW ? GPIO_PV_HIGH : GPIO_PV_LOW));
    unsigned stopped_register_writes = register_writes;
    assert(sc132gs_stream(&dev, 0) == 0 && register_writes == stopped_register_writes);
    assert(sc132gs_stream(&dev, 1) == -EIO);
    fail_addr = -1;
    assert(sc132gs_init(&dev, full) == 0 && dev.power_flag);
    assert(sc132gs_stream(&dev, 1) == 0);
    assert(sc132gs_stream(&dev, 0) == 0);
    assert(sc132gs_power(&dev, 0) == 0);
#if TEST_SKIP_ID_CHECK
    prepare();
    registers[0x3108] = 0;
    assert(sensor_sc132gs_probe(&cfg, &dev) == 0 && !id_reads);
    assert(sc132gs_init(&dev, dev.current_sensor_mode) == 0 && !id_reads);
    registers[0x3108] = 0; /* The simulated soft reset restores the ID. */
    /* ISP initialization calls this callback after the fixed-address bind.
     * Report the actual revision without reintroducing chip ID matching. */
    assert(dev.sensor_func.sensor_get_chip_id(&dev, &id) == 0 && id == 0x0100);
    fail_addr = 0x3108;
    assert(dev.sensor_func.sensor_get_chip_id(&dev, &id) != 0);
    fail_addr = -1;
    assert(sc132gs_power(&dev, 0) == 0);
#else
    for (address = 0x30; address <= 0x33; ++address) {
        prepare();
        assert(sensor_sc132gs_probe(&cfg, &dev) == 0 && dev.i2c_info.slave_addr == address);
        assert(sc132gs_power(&dev, 0) == 0);
    }
    address = 0x30;
    prepare();
    registers[0x3108] = 0;
    assert(sensor_sc132gs_probe(&cfg, &dev) != 0 && !dev.sensor_func.sensor_init);
    assert(!register_writes);
    prepare();
    assert(sensor_sc132gs_probe(&cfg, &dev) == 0);
    registers[0x3108] = 0;
    assert(dev.sensor_func.sensor_get_chip_id(&dev, &id) == -ENODEV && id == 0x0100);
    assert(sc132gs_power(&dev, 0) == 0);
#endif
    prepare();
    fail_gpio = 4;
    assert(sensor_sc132gs_probe(&cfg, &dev) != 0 && !dev.power_flag);
    if (TEST_CLOCK) {
        prepare();
        fail_clock = 1;
        assert(sensor_sc132gs_probe(&cfg, &dev) != 0 && !dev.power_flag);
    }
    prepare();
    cfg.pwd_gpio = -1;
    assert(sensor_sc132gs_probe(&cfg, &dev) == 0 && sc132gs_init(&dev, dev.current_sensor_mode) == 0);
    assert(sc132gs_power(&dev, 0) == 0);
    cfg.reset_gpio = 5;
    prepare();
    assert(sensor_sc132gs_probe(&cfg, &dev) == 0 && gpio_values[5] == GPIO_PV_HIGH);
    assert(sc132gs_get_id(&dev, &id) == 0 && gpio_values[5] == GPIO_PV_HIGH);
    assert(sc132gs_power(&dev, 0) == 0 && gpio_values[5] == GPIO_PV_LOW);
    cfg.i2c_name = "missing";
    assert(sensor_sc132gs_probe(&cfg, &dev) == -ENODEV);
    cfg.csi_num = 3;
    assert(sensor_sc132gs_probe(&cfg, &dev) == -EINVAL);
    printf("SC132GS four_lane=%d mono=%d csi=%d clock=%d active_low=%d extclk=%u clock_invert=%d skip_id=%d address=0x%02x: PASS\n",
           TEST_FOUR_LANE, TEST_MONO, TEST_CSI, TEST_CLOCK, TEST_ACTIVE_LOW, TEST_EXTCLK, TEST_CLOCK_INVERT,
           TEST_SKIP_ID_CHECK, TEST_FIXED_ADDRESS);
    return 0;
}
