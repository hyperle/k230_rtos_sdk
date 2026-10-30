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
#include <stdlib.h>
#include <string.h>

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "drv_timer.h"

static int soft_timer_type = 0;

static int soft_timer_in_used = 0;

struct _drv_soft_timer_inst {
    void*              base;
    timer_t            timerid;
    int                started;
    rt_hwtimer_mode_t  mode;
    uint32_t           period_ms;
    void*              irq_args;
    timer_irq_callback irq_callback;
};

static void soft_timer_sig_handler(int sig, siginfo_t* si, void* uc)
{
    drv_soft_timer_inst_t* inst = si->si_ptr;

    if (SIGALRM != sig) {
        return;
    }

    if (SI_TIMER != si->si_code) {
        return;
    }

    if (!inst || (&soft_timer_type != inst->base)) {
        return;
    }

    if (inst->irq_callback) {
        inst->irq_callback(inst->irq_args);
    }
}

int drv_soft_timer_create(drv_soft_timer_inst_t** inst)
{
    if (soft_timer_in_used) {
        printf("[hal_sfttimer]: soft timer in use\n");
        return -1;
    }

    if (*inst) {
        drv_soft_timer_destroy(inst);
        *inst = NULL;
    }

    *inst = malloc(sizeof(drv_soft_timer_inst_t));
    if (NULL == (*inst)) {
        printf("[hal_sfttimer]: malloc failed");
        return -1;
    }
    memset(*inst, 0x00, sizeof(drv_soft_timer_inst_t));

    (*inst)->base         = &soft_timer_type;
    (*inst)->started      = 0;
    (*inst)->mode         = HWTIMER_MODE_ONESHOT;
    (*inst)->period_ms    = 1000;
    (*inst)->irq_args     = NULL;
    (*inst)->irq_callback = NULL;

    struct sigaction sa;
    sa.sa_flags     = SA_SIGINFO;
    sa.sa_sigaction = soft_timer_sig_handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGALRM, &sa, NULL);

    struct sigevent sev;
    sev.sigev_notify          = SIGEV_SIGNAL;
    sev.sigev_signo           = SIGALRM;
    sev.sigev_value.sival_ptr = (*inst);
    if (timer_create(CLOCK_REALTIME, &sev, &(*inst)->timerid)) {
        printf("[hal_sfttimer]: create soft timer failed.\n");
        return -1;
    }
    soft_timer_in_used = 1;

    return 0;
}

void drv_soft_timer_destroy(drv_soft_timer_inst_t** inst)
{
    struct sigaction sa;

    if(NULL == *inst) {
        return;
    }

    if((void*)&soft_timer_type != (*inst)->base) {
        printf("[hal_sfttimer]: inst not soft timer\n");
        return;
    }

    drv_soft_timer_stop(*inst);
    drv_soft_timer_unregister_irq(*inst);

    if ((*inst)->timerid) {
        timer_delete((*inst)->timerid);
        (*inst)->timerid = NULL;
    }

    sa.sa_handler   = SIG_IGN;
    sa.sa_sigaction = NULL;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGALRM, &sa, NULL);

    free(*inst);
    *inst = NULL;

    soft_timer_in_used = 0;
}

int drv_soft_timer_set_mode(drv_soft_timer_inst_t* inst, rt_hwtimer_mode_t mode)
{
    if ((NULL == inst) || (inst->started)) {
        printf("[hal_sfttimer]: timer not created or started\n");
        return -1;
    }

    inst->mode = mode;

    return 0;
}

int drv_soft_timer_set_period(drv_soft_timer_inst_t* inst, int period_ms)
{
    if ((NULL == inst) || (inst->started)) {
        printf("[hal_sfttimer]: timer not created or started\n");
        return -1;
    }

    inst->period_ms = period_ms;

    return 0;
}

int drv_soft_timer_start(drv_soft_timer_inst_t* inst)
{
    struct itimerspec its      = {};
    const uint64_t    ms_to_ns = 1000000ULL; // 1ms = 1,000,000ns

    if ((NULL == inst) || (inst->started)) {
        printf("[hal_sfttimer]: timer not created or started\n");
        return -1;
    }
    uint64_t ns = (uint64_t)inst->period_ms * ms_to_ns;

    memset(&its, 0x00, sizeof(its));

    its.it_value.tv_sec  = ns / 1000000000ULL;
    its.it_value.tv_nsec = ns % 1000000000ULL;

    if (inst->mode == HWTIMER_MODE_PERIOD) {
        its.it_interval = its.it_value;
    }

    if (timer_settime(inst->timerid, 0, &its, NULL)) {
        printf("[hal_sfttimer]: start soft timer failed\n");
        return -1;
    }
    inst->started = 1;

    return 0;
}

int drv_soft_timer_stop(drv_soft_timer_inst_t* inst)
{
    struct itimerspec its = {};

    if ((NULL == inst)) {
        printf("[hal_sfttimer]: timer not created or started\n");
        return -1;
    }

    if (inst->started) {
        its.it_value.tv_sec  = 0;
        its.it_value.tv_nsec = 0;
        if (timer_settime(inst->timerid, 0, &its, NULL)) {
            printf("[hal_sfttimer]: stop soft timer failed\n");
            return -1;
        }
    }
    inst->started = 0;

    return 0;
}

int drv_soft_timer_register_irq(drv_soft_timer_inst_t* inst, timer_irq_callback callback, void* userargs)
{
    if ((NULL == inst) || (inst->started)) {
        printf("[hal_sfttimer]: timer not created or started\n");
        return -1;
    }

    inst->irq_args     = userargs;
    inst->irq_callback = callback;

    return 0;
}

int drv_soft_timer_unregister_irq(drv_soft_timer_inst_t* inst)
{
    if ((NULL == inst) || (inst->started)) {
        printf("[hal_sfttimer]: timer not created or started\n");
        return -1;
    }

    inst->irq_args     = NULL;
    inst->irq_callback = NULL;

    return 0;
}

int drv_soft_timer_is_started(drv_soft_timer_inst_t* inst)
{
    if ((void*)0 == inst) {
        return 0;
    }

    return inst->started;
}
