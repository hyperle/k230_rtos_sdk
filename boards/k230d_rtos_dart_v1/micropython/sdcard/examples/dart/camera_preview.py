"""SC132GS VGA capture to the CanMV IDE framebuffer (no external display)."""
import os
import time
import dart_board
from media.sensor import Sensor
from media.display import Display
from media.media import MediaManager

sensor = None

try:
    sensor = dart_board.camera_sensor(vga=True)
    sensor.reset()
    sensor.set_framesize(Sensor.VGA)
    sensor.set_pixformat(Sensor.RGB565)
    # VGA mode disables AE in media.sensor; start with explicit manual values.
    sensor.auto_exposure(False)
    Display.init(Display.VIRT, width=640, height=480, fps=30, to_ide=True)
    MediaManager.init()
    sensor.run()
    if sensor.again(1.0):
        raise RuntimeError("Setting camera gain failed")
    if not sensor.exposure(2000):
        raise RuntimeError("Setting camera exposure failed")
    print("%s VGA preview ready; stop from IDE or Ctrl-C" % dart_board.CAMERA_NAME)

    while True:
        os.exitpoint()
        img = sensor.snapshot(timeout=1000, auto_reboot=False)
        Display.show_image(img)
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
