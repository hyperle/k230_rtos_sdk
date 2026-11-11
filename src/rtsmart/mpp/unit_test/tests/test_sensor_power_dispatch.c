#include <assert.h>
#include <rtthread.h>
#include "../../kernel/sensor/src/sensor_dev.h"
#undef RT_ASSERT
#define RT_ASSERT(expr) assert(expr)
#include "../../kernel/sensor/src/sensor_comm.c"

static int power_calls, init_calls, cache_valid, power_error;
void rt_kprintf(const char *format, ...) { (void)format; }
rt_size_t rt_i2c_transfer(struct rt_i2c_bus_device *bus, struct rt_i2c_msg msgs[], rt_uint32_t num)
{ (void)bus; (void)msgs; (void)num; return 0; }
k_s32 vicap_set_mclk(k_vicap_mclk *clock) { (void)clock; return 0; }
k_bool sensor_sc132gs_power_cache_valid(struct sensor_driver_dev *dev)
{ (void)dev; return cache_valid; }
static k_s32 power_impl(void *ctx, k_s32 on) {
    struct sensor_driver_dev *dev = ctx;
    ++power_calls;
    if (power_error) return power_error;
    if (!cache_valid || !on) {
        dev->init_flag = K_FALSE;
        dev->mode_init_flag = K_FALSE;
    }
    dev->power_flag = on;
    cache_valid = on;
    return 0;
}
static k_s32 init_impl(void *ctx, k_sensor_mode mode) {
    struct sensor_driver_dev *dev = ctx;
    ++init_calls;
    int ret = power_impl(dev, 1);
    if (ret) return ret;
    dev->current_sensor_mode = mode;
    dev->init_flag = K_TRUE;
    return 0;
}
static struct sensor_driver_dev initialized(void) {
    struct sensor_driver_dev dev = {0};
    dev.power_flag = dev.init_flag = dev.mode_init_flag = K_TRUE;
    dev.sensor_func.sensor_power = power_impl;
    dev.sensor_func.sensor_init = init_impl;
    dev.current_sensor_mode.sensor_type = 0;
    power_calls = init_calls = power_error = 0;
    cache_valid = 1;
    return dev;
}
int main(void) {
    struct sensor_driver_dev dev = initialized();
    k_s32 on = 1;
    assert(sensor_priv_ioctl(&dev,KD_IOC_SENSOR_S_POWER,&on)==0);
    assert(power_calls==0); /* Healthy cached power retains previous behavior. */
    cache_valid=0;
    assert(sensor_priv_ioctl(&dev,KD_IOC_SENSOR_S_POWER,&on)==0);
    assert(power_calls==1 && !dev.init_flag && !dev.mode_init_flag);

    dev=initialized(); k_sensor_mode mode=dev.current_sensor_mode;
    assert(sensor_priv_ioctl(&dev,KD_IOC_SENSOR_S_INIT,&mode)==0);
    assert(power_calls==0 && init_calls==0);
    cache_valid=0;
    assert(sensor_priv_ioctl(&dev,KD_IOC_SENSOR_S_INIT,&mode)==0);
    assert(power_calls==1 && init_calls==1 && dev.init_flag && dev.mode_init_flag);

    dev=initialized(); dev.init_flag=K_FALSE;
    assert(sensor_priv_ioctl(&dev,KD_IOC_SENSOR_S_INIT,&mode)==0);
    assert(init_calls==1 && dev.init_flag);
    dev=initialized(); dev.power_flag=K_FALSE;
    assert(sensor_priv_ioctl(&dev,KD_IOC_SENSOR_S_INIT,&mode)==0);
    assert(init_calls==1 && dev.power_flag);

    dev=initialized(); on=0; power_error=-EIO;
    assert(sensor_priv_ioctl(&dev,KD_IOC_SENSOR_S_POWER,&on)==-EIO);
    assert(dev.power_flag && !dev.init_flag && !dev.mode_init_flag);
    power_error=0;
    assert(sensor_priv_ioctl(&dev,KD_IOC_SENSOR_S_POWER,&on)==0);
    assert(!dev.power_flag);
    puts("Sensor power/mode cache validation and failed-power-off retry: PASS");
    return 0;
}
