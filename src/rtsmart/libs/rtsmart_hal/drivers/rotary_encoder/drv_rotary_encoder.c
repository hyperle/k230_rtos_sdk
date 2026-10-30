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
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "drv_rotary_encoder.h"

/* IOCTL commands */
#define ENCODER_CMD_GET_DATA  _IOR('E', 1, struct encoder_data*)
#define ENCODER_CMD_RESET     _IO('E', 2)
#define ENCODER_CMD_SET_COUNT _IOW('E', 3, int64_t*)
#define ENCODER_CMD_CONFIG    _IOW('E', 4, struct encoder_dev_cfg_t*)
#define ENCODER_CMD_WAIT_DATA _IOW('E', 5, int32_t*)

struct encoder_dev_inst_t {
    void* base;
    int   fd;

    struct encoder_dev_cfg_t cfg;
};

static uint32_t encoder_dev_type;

#define CHECK_ENCODER_INST(inst)                                                                                               \
    do {                                                                                                                       \
        if ((!(inst)) || (0 > (inst)->fd) || (&encoder_dev_type != (inst)->base)) {                                            \
            printf("[hal_encoder]: Invalid fd\n");                                                                             \
            return -1;                                                                                                         \
        }                                                                                                                      \
    } while (0)

int rotary_encoder_inst_create(struct encoder_dev_inst_t** inst, int index, struct encoder_pin_cfg_t* pin)
{
    int  dev_fd;
    char dev_name[32];

    struct encoder_dev_cfg_t cfg;

    if (!inst || !pin) {
        return -1;
    }

    if ((0 > pin->clk_pin) || (0 > pin->dt_pin)) {
        return -1;
    }

    if (*inst) {
        if (0x00 != rotary_encoder_inst_destroy(inst)) {
            return -1;
        }
    }

    printf("[hal_encoder]: crtest /dev/encoder%d with clk_pin %d, dt_pin %d, sw_pin %d\n", index, pin->clk_pin, pin->dt_pin,
           pin->sw_pin);

    cfg.index = index;
    memcpy(&cfg.cfg, pin, sizeof(struct encoder_pin_cfg_t));

    if (0x00 != canmv_misc_create_encoder_dev(&cfg)) {
        printf("[hal_encoder] Failed to create encoder device\n");
        return -1;
    }

    snprintf(dev_name, sizeof(dev_name), "/dev/encoder%d", index);

    if (0 > (dev_fd = open(dev_name, O_RDWR | O_NONBLOCK))) {
        printf("[hal_encoder]: open %s failed\n", dev_name);
        return -1;
    }

    /* Allocate and initialize instance */
    *inst = (struct encoder_dev_inst_t*)malloc(sizeof(struct encoder_dev_inst_t));
    if (*inst == NULL) {
        printf("[hal_encoder]: malloc instance failed");
        close(dev_fd);
        return -3;
    }
    memset(*inst, 0, sizeof(struct encoder_dev_inst_t));

    /* Initialize instance fields */
    (*inst)->base = (void*)&encoder_dev_type;
    (*inst)->fd   = dev_fd;
    memcpy(&((*inst)->cfg), &cfg, sizeof(struct encoder_dev_cfg_t));

    return 0;
}

int rotary_encoder_inst_destroy(struct encoder_dev_inst_t** inst)
{
    int id, fd;

    struct encoder_dev_cfg_t* cfg;

    CHECK_ENCODER_INST(*inst);

    cfg = &((*inst)->cfg);
    id  = cfg->index;
    fd  = (*inst)->fd;

    if (0 <= fd) {
        close(fd);
    }

    printf("[hal_encoder]: delete /dev/encoder%d with clk_pin %d, dt_pin %d, sw_pin %d\n", id, cfg->cfg.clk_pin,
           cfg->cfg.dt_pin, cfg->cfg.sw_pin);

    free(*inst);
    *inst = NULL;

    if (0x00 != canmv_misc_delete_encode_dev(id)) {
        printf("[hal_encoder] Failed to delete encoder device\n");
        return -1;
    }

    return 0;
}

int rotary_encoder_config(struct encoder_dev_inst_t* inst, struct encoder_pin_cfg_t* pin)
{
    struct encoder_dev_cfg_t cfg;

    CHECK_ENCODER_INST(inst);

    cfg.index = inst->cfg.index;
    memcpy(&cfg.cfg, pin, sizeof(struct encoder_pin_cfg_t));

    if (ioctl(inst->fd, ENCODER_CMD_CONFIG, cfg) < 0) {
        printf("[hal_encoder] Failed to configure encoder: %d\n", errno);
        return -1;
    }

    return 0;
}

int rotary_encoder_read(struct encoder_dev_inst_t* inst, struct encoder_data* data)
{
    CHECK_ENCODER_INST(inst);

    if (!data) {
        return -1;
    }

    if (ioctl(inst->fd, ENCODER_CMD_GET_DATA, data) < 0) {
        printf("[hal_encoder] Failed to read encoder data: %d\n", errno);
        return -1;
    }

    return 0;
}

int rotary_encoder_wait_event(struct encoder_dev_inst_t* inst, struct encoder_data* data, int timeout_ms)
{
    CHECK_ENCODER_INST(inst);

    if (!data) {
        return -1;
    }

    /* Wait for data available */
    if (ioctl(inst->fd, ENCODER_CMD_WAIT_DATA, &timeout_ms) < 0) {
        // printf("[hal_encoder] Failed to wait for data: %d\n", errno);
        return -1;
    }

    /* Read the data */
    return rotary_encoder_read(inst, data);
}

int rotary_encoder_reset(struct encoder_dev_inst_t* inst)
{
    CHECK_ENCODER_INST(inst);

    if (ioctl(inst->fd, ENCODER_CMD_RESET, NULL) < 0) {
        printf("[hal_encoder] Failed to reset encoder: %d\n", errno);
        return -1;
    }

    return 0;
}

int rotary_encoder_set_count(struct encoder_dev_inst_t* inst, int64_t count)
{
    CHECK_ENCODER_INST(inst);

    if (ioctl(inst->fd, ENCODER_CMD_SET_COUNT, &count) < 0) {
        printf("[hal_encoder] Failed to set encoder count: %d\n", errno);
        return -1;
    }

    return 0;
}

/* 便捷函数：获取当前计数值 */
int64_t rotary_encoder_get_count(struct encoder_dev_inst_t* inst)
{
    struct encoder_data data;

    if (rotary_encoder_read(inst, &data) < 0) {
        return 0;
    }

    return data.total_count;
}

/* 便捷函数：获取并清除增量 */
int32_t rotary_encoder_get_delta(struct encoder_dev_inst_t* inst)
{
    struct encoder_data data;

    if (rotary_encoder_read(inst, &data) < 0) {
        return 0;
    }

    return data.delta;
}

int rotary_encoder_get_index(struct encoder_dev_inst_t* inst, int* index)
{
    CHECK_ENCODER_INST(inst);

    *index = inst->cfg.index;

    return 0;
}

int rotary_encoder_get_pin_cfg(struct encoder_dev_inst_t* inst, struct encoder_pin_cfg_t* pin)
{
    CHECK_ENCODER_INST(inst);

    memcpy(pin, &inst->cfg.cfg, sizeof(struct encoder_pin_cfg_t));

    return 0;
}
