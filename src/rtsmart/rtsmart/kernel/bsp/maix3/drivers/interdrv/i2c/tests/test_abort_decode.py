#!/usr/bin/env python3
"""Check raw DesignWare abort encodings against the production register/API code.

The map function is compiled from drv_i2c_master.c, so this also checks that
an arbitration-loss bit is classified correctly rather than merely renamed.
No hardware, driver initialization, or camera transaction is simulated.
"""
import os
from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "drv_i2c_master.c").read_text()
start = source.index("static rt_err_t k230_i2c_map_abort(")
end = source.index("\n}", start) + 2
production_map = source[start:end]

HARNESS = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "drv_i2c_reg.h"
typedef int rt_err_t;
/* Public RT-Thread errno values used by the extracted production function. */
#define RT_EIO 8
#define RT_EBUSY 7
#define RT_EINVAL 10
struct dw_i2c_master { uint32_t abrt_src; };
'''

CASES = r'''
#define CHECK_FIELD(name, bit) do { \
    struct dw_ic_tx_abrt_source_t s = { .reg = UINT32_C(1) << (bit) }; \
    assert(s.bits.name == 1); \
    s.reg = 0; s.bits.name = 1; \
    assert(s.reg == (UINT32_C(1) << (bit))); \
} while (0)
int main(void)
{
    _Static_assert(sizeof(struct dw_ic_tx_abrt_source_t) == sizeof(uint32_t),
                   "TX_ABRT_SOURCE must be exactly one hardware word");
    CHECK_FIELD(ABRT_7B_ADDR_NOACK, 0);
    CHECK_FIELD(ABRT_10ADDR1_NOACK, 1);
    CHECK_FIELD(ABRT_10ADDR2_NOACK, 2);
    CHECK_FIELD(ABRT_TXDATA_NOACK, 3);
    CHECK_FIELD(ABRT_GCALL_NOACK, 4);
    CHECK_FIELD(ABRT_GCALL_READ, 5);
    CHECK_FIELD(ABRT_HS_ACKDET, 6);
    CHECK_FIELD(ABRT_SBYTE_ACKDET, 7);
    CHECK_FIELD(ABRT_HS_NORSTRT, 8);
    CHECK_FIELD(ABRT_SBYTE_NORSTRT, 9);
    CHECK_FIELD(ABRT_10B_RD_NORSTRT, 10);
    CHECK_FIELD(ABRT_MASTER_DIS, 11);
    CHECK_FIELD(ABRT_ARB_LOST, 12);
    CHECK_FIELD(ABRT_SLVFLUSH_TXFIFO, 13);
    CHECK_FIELD(ABRT_SLV_ARBLOST, 14);
    CHECK_FIELD(ABRT_SLV_RD_INTX, 15);
    struct dw_i2c_master master;
    master.abrt_src = UINT32_C(0x01001000); /* observed with PU disabled */
    assert(k230_i2c_map_abort(&master) == -RT_EBUSY);
    master.abrt_src = UINT32_C(1) << 11; /* master disabled, not arbitration */
    assert(k230_i2c_map_abort(&master) == -RT_EIO);
    master.abrt_src = UINT32_C(1) << 13; /* slave FIFO flush, not arbitration */
    assert(k230_i2c_map_abort(&master) == -RT_EIO);
    master.abrt_src = UINT32_C(0x01000001); /* observed address NACK */
    assert(k230_i2c_map_abort(&master) == -RT_EIO);
    master.abrt_src = UINT32_C(1) << 3; /* data NACK */
    assert(k230_i2c_map_abort(&master) == -RT_EIO);
    master.abrt_src = UINT32_C(1) << 5; /* invalid general call read */
    assert(k230_i2c_map_abort(&master) == -RT_EINVAL);
    master.abrt_src = (UINT32_C(1) << 12) | 1; /* keep NACK precedence */
    assert(k230_i2c_map_abort(&master) == -RT_EIO);
    puts("PASS: 16 hardware bit encodings and 7 production abort mappings");
    return 0;
}
'''

with tempfile.TemporaryDirectory(prefix="k230-i2c-abort-") as tmp:
    tmp = Path(tmp)
    harness = tmp / "abort.c"
    harness.write_text(HARNESS + production_map + "\n" + CASES)
    exe = tmp / "abort"
    subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-O2", "-Wall",
                    "-Wextra", "-Werror", "-I", str(ROOT), str(harness),
                    "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
