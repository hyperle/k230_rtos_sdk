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
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "onewire.h"

#define ONEWIRE_IOCTL_RESET      _IOWR('o', 0x00, struct onewire_rdwr_t*)
#define ONEWIRE_IOCTL_WRITE_BYTE _IOWR('o', 0x01, struct onewire_rdwr_t*)
#define ONEWIRE_IOCTL_READ_BYTE  _IOWR('o', 0x02, struct onewire_rdwr_t*)
#define ONEWIRE_IOCTL_SEARCH_ROM _IOWR('o', 0x03, struct onwwire_search_rom_t*)
#define PIN_PULSE_US             _IOWR('o', 0x04, struct pin_pulse_t*)

struct onewire_rdwr_t {
    int     pin;
    uint8_t data;
};

struct onwwire_search_rom_t {
    int     pin;
    uint8_t rom[8];
    uint8_t l_rom[8];
    int     diff;

    int result;
};

struct pin_pulse_t {
    int      pin;
    int      pulse_level;
    uint64_t timeout_us;

    uint64_t result;
};

static int onewire_ioctl(int cmd, void* data)
{
    static int onewire_dev_fd = -1;

    if (onewire_dev_fd < 0) {
        onewire_dev_fd = open("/dev/onewire", O_RDWR);
        if (onewire_dev_fd < 0) {
            printf("[hal_onewire] Failed to open device: %d\n", errno);
            return -1; // Failed to open device
        }
    }

    int ret = ioctl(onewire_dev_fd, cmd, data);

    if (0x00 != ret) {
        printf("[hal_onewire]: ioctl failed with cmd 0x%08X\n", cmd);
    }

    return ret;
}

int onewire_reset(int pin)
{
    struct onewire_rdwr_t cfg;

    cfg.pin = pin;

    onewire_ioctl(ONEWIRE_IOCTL_RESET, &cfg);

    return cfg.data;
}

int onewire_write_byte(int pin, uint8_t data)
{
    struct onewire_rdwr_t cfg;

    cfg.pin  = pin;
    cfg.data = data;

    return onewire_ioctl(ONEWIRE_IOCTL_WRITE_BYTE, &cfg);
}

uint8_t onewire_read_byte(int pin)
{
    struct onewire_rdwr_t cfg;

    cfg.pin = pin;

    if (0x00 != onewire_ioctl(ONEWIRE_IOCTL_READ_BYTE, &cfg)) {
        return 0x00;
    }

    return cfg.data;
}

int onewire_search_rom(int pin, uint8_t rom[8], uint8_t l_rom[8], int* diff_in)
{
    struct onwwire_search_rom_t cfg;

    cfg.pin = pin;
    memcpy(&cfg.rom[0], rom, 8);
    memcpy(&cfg.l_rom[0], l_rom, 8);
    cfg.diff = *diff_in;

    onewire_ioctl(ONEWIRE_IOCTL_SEARCH_ROM, &cfg);

    *diff_in = cfg.diff;
    memcpy(rom, &cfg.rom[0], 8);

    return cfg.result;
}

uint64_t pin_pulse_us(int pin, int pulse_level, uint64_t timeout_us)
{
    struct pin_pulse_t cfg;

    cfg.pin         = pin;
    cfg.pulse_level = pulse_level;
    cfg.timeout_us  = timeout_us;

    onewire_ioctl(PIN_PULSE_US, &cfg);

    return cfg.result;
}
