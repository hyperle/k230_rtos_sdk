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

#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "ws2812.h"

#define WS2812_STREAM_OVER_GPIO 0x00

#define WS2812_IOCTL_STREAM _IOW('W', 0x00, void*)

struct ws2812_stream {
    uint32_t struct_sz; // Size of this structure

    uint32_t pin; // GPIO pin number
    uint32_t stream_type; // Type of stream, e.g., WS2812_STREAM_OVER_GPIO
    uint32_t timing_ns[4]; // (high_time_0, low_time_0, high_time_1, low_time_1)

    size_t  len; // Length of the data buffer
    uint8_t data[0]; // Pointer to the data buffer
};

static int ws2812_dev_fd = -1;

static int ws2812_stream(struct ws2812_stream* stream)
{
    if (stream == NULL || stream->struct_sz != sizeof(struct ws2812_stream) + stream->len) {
        printf("[hal_ws2812] Invalid stream structure\n");
        return -1; // Invalid stream structure
    }

    if (0 > ws2812_dev_fd) {
        ws2812_dev_fd = open("/dev/ws2812", O_RDWR);
        if (0 > ws2812_dev_fd) {
            printf("[hal_ws2812] Failed to open device: %d\n", errno);
            return -1; // Failed to open device
        }
    }

    return ioctl(ws2812_dev_fd, WS2812_IOCTL_STREAM, stream);
}

int ws2812_stream_over_gpio(int pin, uint32_t* timing_ns, const uint8_t* buf, size_t len)
{
    int                   ret           = 0;
    void*                 stream_buffer = NULL;
    struct ws2812_stream* stream        = NULL;

    stream_buffer = malloc(sizeof(struct ws2812_stream) + len);
    if (!stream_buffer) {
        printf("[hal_ws2812] Failed to allocate memory for stream buffer\n");
        return -1; // Memory allocation failed
    }

    stream              = (struct ws2812_stream*)stream_buffer;
    stream->struct_sz   = sizeof(struct ws2812_stream) + len;
    stream->pin         = pin;
    stream->stream_type = WS2812_STREAM_OVER_GPIO;
    stream->len         = len;
    for (int i = 0; i < 4; i++) {
        stream->timing_ns[i] = timing_ns[i];
    }

    memcpy(stream->data, buf, len);

    ret = ws2812_stream(stream);
    if (ret < 0) {
        printf("[hal_ws2812] Failed to stream over GPIO: %d\n", errno);
    }
    free(stream_buffer);

    return ret;
}

int ws2812_device_init(void)
{
    if (ws2812_dev_fd < 0) {
        ws2812_dev_fd = open("/dev/ws2812", O_RDWR);
        if (ws2812_dev_fd < 0) {
            printf("[hal_ws2812] Failed to open device: %d\n", errno);
            return -1; // Failed to open device
        }
    }
    return 0; // Device initialized successfully
}
