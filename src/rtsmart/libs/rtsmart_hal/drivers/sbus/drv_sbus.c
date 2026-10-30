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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include "drv_uart.h"
#include "drv_sbus.h"

#define IOC_SET_BAUDRATE            _IOW('U', 0x40, int)

#define SBUS_CHANNEL_NUM 16
#define SBUS_FRAME_SIZE 25

#define SBUS_MIN 172
#define SBUS_MAX 1811
#define SBUS_NEUTRAL 1024

struct sbus_device
{
    bool debug;
    drv_uart_inst_t *uart_inst;
    uint16_t channels[SBUS_CHANNEL_NUM];
    sbus_flag_t flags;
};

sbus_dev_t sbus_create(int uart_id)
{
    int ret;
    sbus_dev_t dev;

    if (uart_id != 1 && uart_id != 2 && uart_id != 3 && uart_id != 4) {
        printf("[hal_sbus]: pls use /dev/uart1 ~ /dev/uart4\n");
        goto err1;
    }

    dev = malloc(sizeof(struct sbus_device));
    if (!dev) {
        printf("[hal_sbus]: sbus_create fail\n");
        goto err1;
    }

    memset(dev, 0, sizeof(struct sbus_device));

    ret = drv_uart_inst_create(uart_id, &dev->uart_inst);
    if (ret != 0) {
        printf("[hal_sbus]: uart%d creation failed: %d\n", uart_id, ret);
        goto err2;
    }

    struct uart_configure cfg = {
        .baud_rate = 100000,
        .data_bits = DATA_BITS_8,
        .stop_bits = STOP_BITS_2,
        .parity    = PARITY_EVEN,
        .bit_order = BIT_ORDER_LSB,
        .invert    = NRZ_NORMAL,
        .bufsz     = 1 << 10
    };

    ret = drv_uart_set_config(dev->uart_inst, &cfg);
    if (ret != 0) {
        printf("[hal_sbus]: uart configuration failed for %d baud: %d\n", cfg.baud_rate, ret);
        goto err3;
    }

    return dev;
err3:
    drv_uart_inst_destroy(&dev->uart_inst);
err2:
    free(dev);
err1:
    return NULL;
}

void sbus_destroy(sbus_dev_t dev)
{
    if (!dev) {
        printf("[hal_sbus]: %s: pls ensure sbus_create called\n", __func__);
        return;
    } else {
        drv_uart_inst_destroy(&dev->uart_inst);
        free(dev);
    }
}

int sbus_set_channel(sbus_dev_t dev, uint8_t channel_index, uint16_t value)
{
    int ret = 0;

    if (!dev) {
        printf("[hal_sbus]: %s: pls ensure sbus_create called\n", __func__);
        ret = -1;
        goto out;
    }

    if (channel_index >= SBUS_CHANNEL_NUM) {
        printf("[hal_sbus]: channel index is over range\n");
        ret = -1;
        goto out;
    }

#if 0
    if (value > 2047) {
        value = 2047;
    }

    if (value < 172) {
        value = 172;
    }
#endif

    dev->channels[channel_index] = value;

out:
    return ret;
}

int sbus_set_all_channels(sbus_dev_t dev, uint16_t *channels)
{
    int ret = 0;

    for (int i = 0; i < SBUS_CHANNEL_NUM; i++) {
        ret = sbus_set_channel(dev, i, channels[i]);
        if (ret != 0) {
            break;
        }
    }

    return ret;
}

void sbus_set_flags(sbus_dev_t dev, const sbus_flag_t *flags)
{
    if (!dev) {
        printf("[hal_sbus]: %s: pls ensure sbus_create called\n", __func__);
        return;
    }

    if (flags) {
        dev->flags = *flags;
    }
}

void sbus_get_flags(sbus_dev_t dev, sbus_flag_t *flags_out)
{
    if (!dev) {
        printf("[hal_sbus]: %s: pls ensure sbus_create called\n", __func__);
        return;
    }

    if (flags_out) {
        *flags_out = dev->flags;
    }
}

static void sbus_decode_frame(uint8_t *sbus_frame, uint16_t *ch, sbus_flag_t *flags)
{
    if (!sbus_frame || !ch || sbus_frame[0] != 0x0F || sbus_frame[24] != 0x00) {
        return;
    }

    for (int i = 0; i < SBUS_CHANNEL_NUM; i++) {
        int byte_pos = 1 + (i * 11) / 8;
        int bit_pos = (i * 11) % 8;

        ch[i] = (sbus_frame[byte_pos] >> bit_pos) |
            (sbus_frame[byte_pos + 1] << (8 - bit_pos));

        if (bit_pos > 5) {
            ch[i] |= (sbus_frame[byte_pos + 2] << (16 - bit_pos));
        }

        ch[i] &= 0x07FF;
    }

    if (flags) {
        uint8_t flag_byte = sbus_frame[23];
        flags->bit.ch17       = ((flag_byte >> 0) & 0x01) ? true : false;
        flags->bit.ch18       = ((flag_byte >> 1) & 0x01) ? true : false;
        flags->bit.frame_lost = ((flag_byte >> 2) & 0x01) ? true : false;
        flags->bit.failsafe   = ((flag_byte >> 3) & 0x01) ? true : false;
    }
}

int sbus_send_frame(sbus_dev_t dev)
{
    uint8_t flags = 0;
    uint8_t sbus_frame[SBUS_FRAME_SIZE];
    int len;

    if (!dev) {
        printf("[hal_sbus]: pls ensure sbus_create called \n");
        return -1;
    }

    sbus_frame[0] = 0x0F;
    memset(&sbus_frame[1], 0, 22);

    for (int i = 0; i < SBUS_CHANNEL_NUM; i++) {
        uint16_t val = dev->channels[i];

        int byte_pos = 1 + (i * 11) / 8;
        int bit_pos = (i * 11) % 8;

        sbus_frame[byte_pos] |= (val << bit_pos) & 0xFF;
        sbus_frame[byte_pos + 1] |= (val >> (8 - bit_pos)) & 0xFF;
        if (bit_pos > 5) {
            sbus_frame[byte_pos + 2] |= (val >> (16 - bit_pos)) & 0xFF;
        }
    }

    if (dev->flags.bit.ch17) {
        flags |= (1 << 0);
    }
    if (dev->flags.bit.ch18) {
        flags |= (1 << 1);
    }
    if (dev->flags.bit.frame_lost) {
        flags |= (1 << 2);
    }
    if (dev->flags.bit.failsafe) {
        flags |= (1 << 3);
    }

    sbus_frame[23] = flags;
    sbus_frame[24] = 0x00;

    if (dev->debug) {
        sbus_flag_t flag;
        uint16_t ch[SBUS_CHANNEL_NUM];

        sbus_decode_frame(sbus_frame, ch, &flag);
        for (int i = 0; i < SBUS_CHANNEL_NUM; i++) {
            printf("[hal_sbus]: CH[%02d] = 0x%-3x(%4d)\n", i, ch[i], ch[i]);
        }
        printf("[hal_sbus]: ch17 = %d, ch18 = %d, frame_lost = %d, failsafe = %d\n",
               flag.bit.ch17, flag.bit.ch18, flag.bit.frame_lost, flag.bit.failsafe);
    }

    len = drv_uart_write(dev->uart_inst, sbus_frame, SBUS_FRAME_SIZE);
    if (len != SBUS_FRAME_SIZE) {
        printf("[hal_sbus]: %s short write , len = %d\n", __func__, len);
        return -1;
    }

    return 0;
}

void sbus_set_debug(sbus_dev_t dev, bool val)
{
    if (!dev) {
        printf("[hal_sbus]: %s: pls ensure sbus_create called\n", __func__);
        return;
    }

    dev->debug = val;
}
