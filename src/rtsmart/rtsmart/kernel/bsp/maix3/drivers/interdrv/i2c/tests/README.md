# DesignWare I2C register regressions

Run from the SDK root:

```sh
python3 src/rtsmart/rtsmart/kernel/bsp/maix3/drivers/interdrv/i2c/tests/test_abort_decode.py
```

This compiles the production register declarations and the abort mapping function
extracted from `drv_i2c_master.c`. It checks hardware word size, all low 16 raw
abort bit positions in both directions, and the actual error classification for
arbitration loss, master disabled, slave FIFO flush, NACKs, and general-call read.
It exercises the observed `0x01001000` and `0x01000001` values. A host pass does
not demonstrate camera communication or frame recovery.

Primary cross-checks for the DesignWare encodings and SDA RX-hold workaround:

- [Linux DesignWare core definitions](https://github.com/torvalds/linux/blob/master/drivers/i2c/busses/i2c-designware-core.h)
- [Linux DesignWare common implementation](https://github.com/torvalds/linux/blob/master/drivers/i2c/busses/i2c-designware-common.c)

The installed STOP_SAFE kernel predates this source correction. No change to
clock, SDA hold time, input filter, retry policy, or pin voltage is implied.
