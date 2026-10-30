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

#include <stdint.h>

#define KD_TIMER_MAX_NUM (6)

#ifdef __cplusplus
extern "C" {
#endif

/** hard timer ***************************************************************/

/* Timer Feature Information */
typedef struct _rt_hwtimer_info {
    int32_t  maxfreq; /* the maximum count frequency timer support */
    int32_t  minfreq; /* the minimum count frequency timer support */
    uint32_t maxcnt; /* counter maximum value */

#define HWTIMER_CNTMODE_UP 0x01 /* increment count mode */
#define HWTIMER_CNTMODE_DW 0x02 /* decreasing count mode */
    uint8_t cntmode; /* count mode (inc/dec) */
} rt_hwtimer_info_t;

/* Timing Mode */
typedef enum {
    HWTIMER_MODE_ONESHOT = 0x01,
    HWTIMER_MODE_PERIOD  = 0x02,
} rt_hwtimer_mode_t;

typedef void (*timer_irq_callback)(void* args);

typedef struct _drv_hard_timer_inst drv_hard_timer_inst_t;

int  drv_hard_timer_inst_create(int id, drv_hard_timer_inst_t** inst);
void drv_hard_timer_inst_destroy(drv_hard_timer_inst_t** inst);

int drv_hard_timer_get_info(drv_hard_timer_inst_t* inst, rt_hwtimer_info_t* info);

int drv_hard_timer_set_mode(drv_hard_timer_inst_t* inst, rt_hwtimer_mode_t mode);
int drv_hard_timer_set_freq(drv_hard_timer_inst_t* inst, uint32_t freq);
int drv_hard_timer_set_period(drv_hard_timer_inst_t* inst, uint32_t period_ms);

int drv_hard_timer_get_freq(drv_hard_timer_inst_t* inst, uint32_t* freq);

int drv_hard_timer_start(drv_hard_timer_inst_t* inst);
int drv_hard_timer_stop(drv_hard_timer_inst_t* inst);

int drv_hard_timer_register_irq(drv_hard_timer_inst_t* inst, timer_irq_callback callback, void* userargs);
int drv_hard_timer_unregister_irq(drv_hard_timer_inst_t* inst);

int drv_hard_timer_get_id(drv_hard_timer_inst_t* inst);
int drv_hard_timer_is_started(drv_hard_timer_inst_t* inst);

/** soft timer ***************************************************************/
typedef struct _drv_soft_timer_inst drv_soft_timer_inst_t;

int  drv_soft_timer_create(drv_soft_timer_inst_t** inst);
void drv_soft_timer_destroy(drv_soft_timer_inst_t** inst);

int drv_soft_timer_set_mode(drv_soft_timer_inst_t* inst, rt_hwtimer_mode_t mode);
int drv_soft_timer_set_period(drv_soft_timer_inst_t* inst, int period_ms);

int drv_soft_timer_start(drv_soft_timer_inst_t* inst);
int drv_soft_timer_stop(drv_soft_timer_inst_t* inst);

int drv_soft_timer_register_irq(drv_soft_timer_inst_t* inst, timer_irq_callback callback, void* userargs);
int drv_soft_timer_unregister_irq(drv_soft_timer_inst_t* inst);

int drv_soft_timer_is_started(drv_soft_timer_inst_t* inst);

#ifdef __cplusplus
}
#endif
