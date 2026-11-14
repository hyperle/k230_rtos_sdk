# HAL lifecycle host regressions

Run from the SDK root. These tests include the actual HAL sources with a
recording ioctl backend; they do not access hardware.

```sh
cc -Wall -Wextra -Werror -ffunction-sections -fdata-sections -Wl,--gc-sections \
  -Isrc/rtsmart/libs/rtsmart_hal/components/canmv_misc \
  -Isrc/rtsmart/libs/rtsmart_hal/drivers/fpioa \
  src/rtsmart/libs/rtsmart_hal/tests/test_i2c_master_lifecycle.c \
  -o /tmp/test_i2c_master_lifecycle
/tmp/test_i2c_master_lifecycle

cc -Wall -Wextra -Wno-sign-compare -ffunction-sections -fdata-sections \
  -Wl,--gc-sections -pthread -Iinclude \
  -Isrc/rtsmart/libs/rtsmart_hal/drivers/fpioa \
  -Isrc/rtsmart/libs/rtsmart_hal/drivers/timer \
  src/rtsmart/libs/rtsmart_hal/tests/test_gpio_lifecycle.c \
  -o /tmp/test_gpio_lifecycle
/tmp/test_gpio_lifecycle
```

The I2C regression checks first-create clock/timeout configuration, same-value
setters, preservation of the frequency cache on ioctl failure, failed-create
cleanup, and a subsequent retry. The GPIO regression recreates a Pin being
destroyed after I2C reclaimed its pad and checks that ordinary GPIO cleanup
continues to work. A failed mux query must not remap an unknown owner.
