/* Copyright (c) 2025, Canaan Bright Sight Co., Ltd
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
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <pthread.h>

#include "drv_adc.h"

typedef enum {
    RT_ADC_CMD_ENABLE,
    RT_ADC_CMD_DISABLE,
} rt_adc_cmd_t;

#define DRV_ADC_DEV "/dev/adc"

static int _drv_adc_fd      = -1;
static int _drv_adc_ref_cnt = 0;

static int drv_adc_chn_state[DRV_ADC_MAX_CHANNEL] = {
    0, 0, 0, 0, 0, 0,
};

static pthread_spinlock_t _drv_adc_lock = (pthread_spinlock_t)0;

int drv_adc_init()
{
    if (0x00 > _drv_adc_fd) {
        if (0x00 > (_drv_adc_fd = open(DRV_ADC_DEV, O_RDWR))) {
            printf("[hal_adc]: open device failed\n");
            return -1;
        }
        pthread_spin_init(&_drv_adc_lock, 0);
    }
    _drv_adc_ref_cnt++;

    return 0;
}

int drv_adc_deinit()
{
    if (0x00 > _drv_adc_fd) {
        _drv_adc_ref_cnt--;

        if (0x00 == _drv_adc_ref_cnt) {
            close(_drv_adc_fd);
            _drv_adc_fd = -1;

            memset(&drv_adc_chn_state[0], 0, sizeof(drv_adc_chn_state));

            pthread_spin_destroy(&_drv_adc_lock);
        }
    }

    return 0;
}

uint32_t drv_adc_read(int channel)
{
    uint32_t value;

    if (0x00 > _drv_adc_fd) {
        printf("[hal_adc]: not inited\n");
        return __UINT32_MAX__;
    }

    if (DRV_ADC_MAX_CHANNEL <= channel) {
        printf("[hal_adc]: invalid channel %d\n", channel);
        return __UINT32_MAX__;
    }

    pthread_spin_lock(&_drv_adc_lock);

    if (0x00 == drv_adc_chn_state[channel]) {
        if (0x00 != ioctl(_drv_adc_fd, RT_ADC_CMD_ENABLE, channel)) {
            printf("[hal_adc]: enable channel failed\n");

            pthread_spin_unlock(&_drv_adc_lock);
            return __UINT32_MAX__;
        }
        drv_adc_chn_state[channel] = 1;
    }

    lseek(_drv_adc_fd, channel, SEEK_SET);
    if (0x00 == read(_drv_adc_fd, &value, sizeof(value))) {
        printf("[hal_adc]: read channel failed\n");

        pthread_spin_unlock(&_drv_adc_lock);
        return __UINT32_MAX__;
    }

    pthread_spin_unlock(&_drv_adc_lock);

    return value;
}

uint32_t drv_adc_read_uv(int channel, uint32_t ref_uv)
{
    uint32_t read, result;
    uint64_t read_u64, ref_u64;

    read = drv_adc_read(channel);

    if (read == __UINT32_MAX__) {
        return __UINT32_MAX__; // Error reading ADC
    }

    read_u64 = (uint64_t)read;
    ref_u64  = (uint64_t)ref_uv;

    // Convert to microvolts:
    // 1. Multiply by reference voltage in μV
    // 2. Divide by full scale (4095 for 12-bit)
    // 3. Add 0.5 before division for proper rounding
    return (uint32_t)((read_u64 * ref_u64 + 2047) / DRV_ADC_RESOLUTION);
}
