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
#include <stddef.h>
#include <stdbool.h>

#define SPI_HAL_MAX_DEVICES 3

/**
 * At CPOL=0 the base value of the clock is zero
 *  - For CPHA=0, data are captured on the clock's rising edge (low->high transition)
 *    and data are propagated on a falling edge (high->low clock transition).
 *  - For CPHA=1, data are captured on the clock's falling edge and data are
 *    propagated on a rising edge.
 * At CPOL=1 the base value of the clock is one (inversion of CPOL=0)
 *  - For CPHA=0, data are captured on clock's falling edge and data are propagated
 *    on a rising edge.
 *  - For CPHA=1, data are captured on clock's rising edge and data are propagated
 *    on a falling edge.
 */

/* SPI mode definitions */
#define SPI_HAL_MODE_0 (0 | 0)          /* CPOL = 0, CPHA = 0 */
#define SPI_HAL_MODE_1 (0 | 1)          /* CPOL = 0, CPHA = 1 */
#define SPI_HAL_MODE_2 (2 | 0)          /* CPOL = 1, CPHA = 0 */
#define SPI_HAL_MODE_3 (2 | 1)          /* CPOL = 1, CPHA = 1 */

/* Data line options */
#define SPI_HAL_DATA_LINE_1    1
#define SPI_HAL_DATA_LINE_2    2
#define SPI_HAL_DATA_LINE_4    4
#define SPI_HAL_DATA_LINE_8    8

#ifdef __cplusplus
extern "C" {
#endif

typedef struct drv_spi_inst *drv_spi_inst_t;

struct rt_spi_message {
    const void *send_buf;
    void *recv_buf;
    size_t length;
    struct rt_spi_message *next;

    unsigned cs_take    : 1;
    unsigned cs_release : 1;
};

struct rt_qspi_message {
    struct rt_spi_message parent;

    /* instruction stage */
    struct {
        uint32_t content;
        uint8_t size;
        uint8_t qspi_lines;
    } instruction;

    /* address and alternate_bytes stage */
    struct {
        uint32_t content;
        uint8_t size;
        uint8_t qspi_lines;
    } address, alternate_bytes;

    /* dummy_cycles stage */
    uint32_t dummy_cycles;

    /* number of lines in qspi data stage, the other configuration items are in parent */
    uint8_t qspi_data_lines;
};

/**
 * @brief Create SPI HAL instance
 *
 * @param spi_id: SPI controller ID (0-2)
 * @param active_low: true mean cs active low, once cs pin is -1, active_low takes no effect
 * @param mode: SPI mode (SPI_HAL_MODE_0 to SPI_HAL_MODE_3)
 * @param baudrate: Clock frequency in Hz
 * @param data_bits: Data bit width (4-32)
 * @param cs_pin: Chip select pin (0-63), -1 mean the control of cs pin is outside the HAL,it doesn't effect by cs_change
 * @param data_line: Number of data lines (1,2,4,8)
 *
 * @return 0 on success, negative on error, inst point to the spi instance
 */
int drv_spi_inst_create(int spi_id, bool active_low, int mode, uint32_t baudrate,
                        uint8_t data_bits, int cs_pin, uint8_t data_line, drv_spi_inst_t *inst);

/**
 * @brief Destroy SPI HAL instance
 *
 * @param inst SPI inst to destroy
 */
void drv_spi_inst_destroy(drv_spi_inst_t *inst);

/**
 * @brief Full-duplex SPI transfer
 *
 * @param inst: SPI inst
 * @param tx_data: Data to transmit
 * @param rx_data: Buffer for received data
 * @param len: Length of data in bytes
 * @param cs_change: true for deselect cs pin,false for keep cs pin select
 *
 * @return int Number of bytes transferred, negative on error
 */
int drv_spi_transfer(drv_spi_inst_t inst, const void *tx_data,
                     void *rx_data, size_t len, bool cs_change);

/**
 * @brief SPI read operation
 *
 * @param inst: SPI inst
 * @param rx_data: Buffer for received data
 * @param len: Length of data to read in bytes
 * @param cs_change: true for deselect cs pin, false for keep cs pin select
 *
 * @return int Number of bytes read, negative on error
 */
int drv_spi_read(drv_spi_inst_t inst, void *rx_data, size_t len, bool cs_change);

/**
 * @brief SPI write operation
 *
 * @param inst: SPI inst
 * @param tx_data: Data to transmit
 * @param len: Length of data to write in bytes
 * @param cs_change: true for deselect cs pin, false for keep cs pin select
 *
 * @return int Number of bytes written, negative on error
 */
int drv_spi_write(drv_spi_inst_t inst, const void *tx_data, size_t len, bool cs_change);

/**
 * @brief advance operation for transfer customized qspi message
 *
 * @param inst: SPI inst
 * @param msg: qmsg to transfer
 *
 * @return int Number of bytes written, negative on error
 */
int drv_spi_transfer_message(drv_spi_inst_t inst, struct rt_qspi_message *msg);

/**
 * @brief set spi baudrate(frequency)
 *
 * @param inst: SPI inst
 * @param baudrate: baudrate to set
 *
 * @return 0 on success, negative on error
 */
int drv_spi_set_baudrate(drv_spi_inst_t inst, uint32_t baudrate);

/**
 * @brief set spi data mode
 *
 * @param inst: SPI inst
 * @param mode: datamode to set
 *
 * @return 0 on success, negative on error
 */
int drv_spi_set_datamode(drv_spi_inst_t inst, uint8_t mode);

/**
 * @brief set spi chip select polarity
 *
 * @param inst: SPI inst
 * @param is_active_hi: true means cs is acvite hi
 *
 * @return 0 on success, negative on error
 */
int drv_spi_set_cs_polarity(drv_spi_inst_t inst, bool is_active_hi);

/**
 * @brief set cs mode
 *
 * @param inst: SPI inst
 * @param use_hw_cs: true means use hardware cs
 *
 * @return 0 on success, negative on error
 */
int drv_spi_set_cs_mode(drv_spi_inst_t inst, bool use_hw_cs);

/**
 * @brief set spi data bits
 *
 * @param inst: SPI inst
 * @param data_bits: data bits to set
 *
 * @return 0 on success, negative on error
 */
int drv_spi_set_data_bits(drv_spi_inst_t inst, uint8_t data_bits);

#ifdef __cplusplus
}
#endif
