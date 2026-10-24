"""K230D Dart V1 pin assignments; importing this module does not drive outputs."""

from micropython import const
from _dart_board import SC038HGS_MODE

NAME = "K230D Dart V1"
UART_PINS = {0: (38, 39), 1: (40, 41), 2: (44, 45), 3: (32, 33), 4: (48, 49)}
I2C_PINS = {1: (34, 35), 2: (11, 12), 3: (36, 37)}
# CS, SCK, MOSI, MISO. SPI1 is QSPI0; SPI2 is QSPI1.
SPI_PINS = {1: (14, 15, 16, 17), 2: (20, 21, 22, 23)}
PWM_PINS = {0: 60, 1: 61, 2: 7, 3: 8, 5: 59}
SERVO_CHANNELS = (1, 2, 3, 5)
CAMERA_CSI = const(2)
CAMERA_I2C = const(1)
CAMERA_PWDN = const(4)
CAMERA_TRIG = const(2)
CAMERA_CHIP_ID = const(0x1A79)


def camera_probe():
    """Read SC038HGS chip ID on I2C1, returning its address and ID.

    Camera power and TRIG are initialized by RT-Smart. Do not call while
    another application is changing the sensor registers.
    """
    from machine import I2C

    bus = I2C(CAMERA_I2C, freq=400000)
    for address in range(0x30, 0x34):
        try:
            high = bus.readfrom_mem(address, 0x3107, 1, addrsize=16)[0]
            low = bus.readfrom_mem(address, 0x3108, 1, addrsize=16)[0]
        except OSError:
            continue
        chip_id = (high << 8) | low
        if chip_id == CAMERA_CHIP_ID:
            return address, chip_id
    raise OSError("SC038HGS chip ID 0x1A79 not found on I2C1 (0x30..0x33)")


def camera_sensor():
    """Construct the CSI2 Sensor with the mode from the compiled MPP headers.

    The ISP capture path needs a matching sc038hgs-640x480 calibration
    profile. This helper does not reset or start the camera.
    """
    from media.sensor import Sensor

    return Sensor(id=CAMERA_CSI, type=SC038HGS_MODE)
