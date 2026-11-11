"""K230D Dart V1 pin assignments; importing this module does not drive outputs."""

from micropython import const
import _dart_board as _camera

NAME = "K230D Dart V1"
UART_PINS = {0: (38, 39), 1: (40, 41), 2: (44, 45), 3: (32, 33), 4: (48, 49)}
I2C_PINS = {1: (34, 35), 2: (11, 12), 3: (36, 37)}
# CS, SCK, MOSI, MISO. SPI1 is QSPI0; SPI2 is QSPI1.
SPI_PINS = {1: (14, 15, 16, 17), 2: (20, 21, 22, 23)}
PWM_PINS = {0: 60, 1: 61, 2: 7, 3: 8, 5: 59}
SERVO_CHANNELS = (1, 2, 3, 5)
CAMERA_I2C = const(1)

if hasattr(_camera, "SC132GS_MODE"):
    CAMERA_NAME = "SC132GS"
    CAMERA_CSI = 0
    CAMERA_POWER = 2  # Adapter EN, high = enabled.
    CAMERA_CHIP_ID = 0x0132
    SC132GS_MODE = _camera.SC132GS_MODE
    SC132GS_VGA_MODE = _camera.SC132GS_VGA_MODE
    CAMERA_MODE = SC132GS_MODE
    CAMERA_VGA_MODE = SC132GS_VGA_MODE
elif hasattr(_camera, "SC038HGS_MODE"):
    CAMERA_NAME = "SC038HGS"
    CAMERA_CSI = 2
    CAMERA_POWER = 4
    CAMERA_PWDN = 4
    CAMERA_TRIG = 2
    CAMERA_CHIP_ID = 0x1A79
    SC038HGS_MODE = _camera.SC038HGS_MODE
    CAMERA_MODE = SC038HGS_MODE
    CAMERA_VGA_MODE = SC038HGS_MODE
else:
    CAMERA_NAME = None
    CAMERA_CSI = None
    CAMERA_MODE = None
    CAMERA_VGA_MODE = None


def camera_probe():
    """Read the configured camera chip ID on I2C1, returning address and ID.

    Camera power is initialized by RT-Smart. Do not call while
    another application is changing the sensor registers.
    """
    if CAMERA_NAME is None:
        raise RuntimeError("No Dart camera driver is enabled in this firmware")
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
    raise OSError("%s chip ID 0x%04x not found on I2C1 (0x30..0x33)" %
                  (CAMERA_NAME, CAMERA_CHIP_ID))


def camera_sensor(*, vga=False):
    """Construct a Sensor with the mode from the compiled MPP headers.

    SC132GS defaults to 1080x1280; vga=True selects the 640x480 crop.
    This helper does not reset or start the camera.
    """
    if CAMERA_MODE is None:
        raise RuntimeError("No Dart camera driver is enabled in this firmware")
    from media.sensor import Sensor

    return Sensor(id=CAMERA_CSI, type=CAMERA_VGA_MODE if vga else CAMERA_MODE)
