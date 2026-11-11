"""Continuous SC132GS color preview in CanMV IDE without saving photos."""
import os
import time

import dart_board
from media.sensor import Sensor
from media.display import Display
from media.media import MediaManager

WIDTH = 640
HEIGHT = 480
# Manual exposure avoids the current firmware's failing automatic-exposure
# callbacks. The ISP still performs its normal color processing/white balance.
AUTO_EXPOSURE = False
EXPOSURE_US = 5000
ANALOG_GAIN = 4.0

sensor = None

try:
    # Keep the full 1080x1280 sensor mode; scale/crop its color output to VGA.
    sensor = dart_board.camera_sensor()
    sensor.reset()
    sensor.set_framesize(width=WIDTH, height=HEIGHT)
    sensor.set_pixformat(Sensor.RGB565)
    sensor.auto_exposure(AUTO_EXPOSURE)

    Display.init(Display.VIRT, width=WIDTH, height=HEIGHT, fps=30, to_ide=True)
    MediaManager.init()
    sensor.run()

    if not AUTO_EXPOSURE:
        if sensor.again(ANALOG_GAIN):
            raise RuntimeError("Setting camera gain failed")
        if not sensor.exposure(EXPOSURE_US):
            raise RuntimeError("Setting camera exposure failed")

    print("SC132GS RGB565 color preview ready; stop from IDE or Ctrl-C")
    frame_count = 0
    while True:
        os.exitpoint()
        img = sensor.snapshot(timeout=1000, auto_reboot=False)
        frame_count += 1
        Display.show_image(img)
        if frame_count % 500 == 0:
            print("Color frames:", frame_count)
        time.sleep_ms(20)
except KeyboardInterrupt:
    pass
finally:
    try:
        if sensor is not None:
            sensor.stop(is_del=True)
    finally:
        try:
            Display.deinit()
        finally:
            os.exitpoint(os.EXITPOINT_ENABLE_SLEEP)
            time.sleep_ms(100)
            MediaManager.deinit()
