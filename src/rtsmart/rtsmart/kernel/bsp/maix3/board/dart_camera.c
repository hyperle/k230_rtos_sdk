/* SPDX-License-Identifier: Apache-2.0 */

#include "rtconfig.h"

#ifdef CONFIG_BOARD_K230D_RTOS_DART_V1

#include <rtthread.h>

#include "drv_gpio.h"

#define DART_CAMERA_CONTROL_GPIO 2
#ifdef CONFIG_MPP_ENABLE_SENSOR_SC038HGS
#define DART_CAMERA_PWDN_GPIO  4
#endif

static int dart_camera_pin_init(void)
{
    rt_err_t ret;

    ret = kd_pin_mode(DART_CAMERA_CONTROL_GPIO, GPIO_DM_OUTPUT);
    if (ret != RT_EOK)
        return ret;
    ret = kd_pin_write(DART_CAMERA_CONTROL_GPIO, GPIO_PV_LOW);
    if (ret != RT_EOK)
        return ret;

#ifdef CONFIG_MPP_ENABLE_SENSOR_SC038HGS
    ret = kd_pin_mode(DART_CAMERA_PWDN_GPIO, GPIO_DM_OUTPUT);
    if (ret != RT_EOK)
        return ret;
    return kd_pin_write(DART_CAMERA_PWDN_GPIO, GPIO_PV_HIGH);
#else
    return RT_EOK;
#endif
}

/* After kd_pin_init (device sequence 010), before MPP sensor probing. */
INIT_DEVICE_EXPORT_SEQ(dart_camera_pin_init, 1);

#endif
