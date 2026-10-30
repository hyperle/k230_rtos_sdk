/* SPDX-License-Identifier: Apache-2.0 */
/*
 * SC038HGS V1.1 documented reset-default mode, external 24 MHz, MIPI 1C1D.
 * Page references refer to 3707708896SC038HGS_数据手册_V1.1.pdf.
 * PLL/analogue settings are intentionally left at silicon reset values:
 * their register addresses and divisors are not specified in this document.
 * This is a bring-up sequence, not a vendor-qualified 240 fps mode table.
 */
static const k_sensor_reg sc038hgs_default_regs[] = {
    /* Output window: 640 x 480, four border pixels/rows (p. 32). */
    {0x3208, 0x02}, {0x3209, 0x80},
    {0x320a, 0x01}, {0x320b, 0xe0},
    {0x3210, 0x00}, {0x3211, 0x04},
    {0x3212, 0x00}, {0x3213, 0x04},
    /* Reset-default HTS=750 and VTS=600 (p. 33). */
    {0x320d, 0xee}, {0x320e, 0x02}, {0x320f, 0x58},
    /* Initial exposure: 128 lines in units of 1/16 line (p. 23). */
    {0x3e01, 0x08}, {0x3e02, 0x00},
    /* Unity analogue and digital gain (pp. 24, 27). */
    {0x3e08, 0x00}, {0x3e09, 0x20},
    {0x3e06, 0x00}, {0x3e07, 0x80},
    {REG_NULL, 0x00},
};

static const struct {
    k_u16 addr;
    k_u8 mask;
    k_u8 value;
} sc038hgs_default_fields[] = {
    {0x2100, 0x01, 0x00}, /* Sleep until explicit stream-on (p. 10). */
    {0x3031, 0x1f, 0x0a}, /* MIPI RAW10 (p. 15). */
    {0x3037, 0x60, 0x20}, /* PHY 10-bit (p. 15). */
    {0x4b01, 0x08, 0x00}, /* Select MIPI rather than LVDS (p. 17). */
    {0x4603, 0x01, 0x00}, /* Enable MIPI reader (p. 15). */
    {0x3222, 0x01, 0x00}, /* Disable continuous external trigger (p. 20). */
    {0x3225, 0x04, 0x00}, /* Disable single-frame trigger (p. 21). */
    {0x3223, 0x88, 0x88}, /* Manual VTS; disable trigger exposure (p. 21). */
    {0x3282, 0x08, 0x00}, /* Disable slave mode (p. 22). */
    {0x3814, 0x40, 0x00}, /* Disable embedded lines (p. 30). */
    {0x5989, 0x10, 0x00}, /* Disable embedded image overlay (p. 30). */
    {0x320c, 0x0f, 0x02}, /* HTS high nibble (p. 33). */
    {0x3e00, 0x0f, 0x00}, /* Exposure high nibble (p. 23). */
    {0x4501, 0x08, 0x00}, /* Normal image, no test gradient (p. 33). */
};
