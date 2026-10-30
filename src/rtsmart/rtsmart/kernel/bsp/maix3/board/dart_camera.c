/* SPDX-License-Identifier: Apache-2.0 */

#include "rtconfig.h"

#ifdef CONFIG_BOARD_K230D_RTOS_DART_V1

#include <rtthread.h>

#include "drv_gpio.h"

#define DART_CAMERA_TRIG1_GPIO 2
#define DART_CAMERA_PWDN_GPIO  4

static int dart_camera_pin_init(void)
{
    rt_err_t ret;

    /* Free-running capture: do not generate external trigger pulses. */
    ret = kd_pin_mode(DART_CAMERA_TRIG1_GPIO, GPIO_DM_OUTPUT);
    if (ret != RT_EOK)
        return ret;
    ret = kd_pin_write(DART_CAMERA_TRIG1_GPIO, GPIO_PV_LOW);
    if (ret != RT_EOK)
        return ret;

    /* This board powers SC038HGS down at low level and enables it at high. */
    ret = kd_pin_mode(DART_CAMERA_PWDN_GPIO, GPIO_DM_OUTPUT);
    if (ret != RT_EOK)
        return ret;
    return kd_pin_write(DART_CAMERA_PWDN_GPIO, GPIO_PV_HIGH);
}

/* After kd_pin_init (device sequence 010), before MPP sensor probing. */
INIT_DEVICE_EXPORT_SEQ(dart_camera_pin_init, 1);

#endif
