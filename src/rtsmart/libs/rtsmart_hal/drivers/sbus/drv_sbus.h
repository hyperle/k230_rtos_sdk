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
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif /* End of #ifdef __cplusplus */

typedef struct
{
    union {
        struct {
            uint8_t ch17:1;
            uint8_t ch18:1;
            uint8_t frame_lost:1;
            uint8_t failsafe:1;
        } bit;
        uint8_t val;
    };
} sbus_flag_t;

typedef struct sbus_device *sbus_dev_t;

sbus_dev_t sbus_create(int uart_id);
void sbus_destroy(sbus_dev_t dev);
int sbus_set_all_channels(sbus_dev_t dev, uint16_t *channels);
int sbus_set_channel(sbus_dev_t dev, uint8_t channel_index, uint16_t value);
void sbus_set_flags(sbus_dev_t dev, const sbus_flag_t *flags);
void sbus_get_flags(sbus_dev_t dev, sbus_flag_t *flags_out);
int sbus_send_frame(sbus_dev_t dev);
void sbus_set_debug(sbus_dev_t dev, bool val);

#ifdef __cplusplus
}
#endif /* __cplusplus */
