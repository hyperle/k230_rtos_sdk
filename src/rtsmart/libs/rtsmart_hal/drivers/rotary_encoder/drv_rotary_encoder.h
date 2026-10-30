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
#ifndef __HAL_ROTARY_ENCODER_H__
#define __HAL_ROTARY_ENCODER_H__

#include <stdint.h>

#include "canmv_misc.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Encoder direction definitions */
#define ENCODER_DIR_CW   1 /* Clockwise */
#define ENCODER_DIR_CCW  2 /* Counter-clockwise */
#define ENCODER_DIR_NONE 0 /* No movement */

/* Data structures */
struct encoder_data {
    int32_t  delta; /* Change since last read */
    int64_t  total_count; /* Total count */
    uint8_t  direction; /* Last direction */
    uint8_t  button_state; /* Button pressed state */
    uint32_t timestamp; /* Tick timestamp */
};

struct encoder_dev_inst_t;

int rotary_encoder_inst_create(struct encoder_dev_inst_t** inst, int index, struct encoder_pin_cfg_t* pin);
int rotary_encoder_inst_destroy(struct encoder_dev_inst_t** inst);
int rotary_encoder_config(struct encoder_dev_inst_t* inst, struct encoder_pin_cfg_t* pin);
int rotary_encoder_read(struct encoder_dev_inst_t* inst, struct encoder_data* data);
int rotary_encoder_wait_event(struct encoder_dev_inst_t* inst, struct encoder_data* data, int timeout_ms);
int rotary_encoder_reset(struct encoder_dev_inst_t* inst);
int rotary_encoder_set_count(struct encoder_dev_inst_t* inst, int64_t count);
int64_t rotary_encoder_get_count(struct encoder_dev_inst_t* inst);
int32_t rotary_encoder_get_delta(struct encoder_dev_inst_t* inst);
int rotary_encoder_get_index(struct encoder_dev_inst_t* inst, int* index);
int rotary_encoder_get_pin_cfg(struct encoder_dev_inst_t* inst, struct encoder_pin_cfg_t* pin);

#ifdef __cplusplus
}
#endif

#endif /* __HAL_ROTARY_ENCODER_H__ */
