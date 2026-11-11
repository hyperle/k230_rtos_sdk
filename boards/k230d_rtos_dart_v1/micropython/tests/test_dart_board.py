"""Host checks for the frozen board module with compiled camera options mocked."""
from pathlib import Path
import runpy
import sys
import types
import unittest
from unittest.mock import patch

SOURCE = Path(__file__).resolve().parents[1] / "dart_board.py"


class DartBoardTests(unittest.TestCase):
    def load_board(self, constants, sensor=None, i2c=None):
        compiled = types.ModuleType("_dart_board")
        compiled.__dict__.update(constants)
        micropython = types.ModuleType("micropython")
        micropython.const = lambda value: value
        machine = types.ModuleType("machine")
        machine.I2C = i2c or self.unexpected_io
        media = types.ModuleType("media")
        media.__path__ = []
        media_sensor = types.ModuleType("media.sensor")
        media_sensor.Sensor = sensor or self.unexpected_io
        with patch.dict(sys.modules, {"_dart_board": compiled,
                                      "micropython": micropython,
                                      "machine": machine, "media": media,
                                      "media.sensor": media_sensor}):
            board = runpy.run_path(str(SOURCE))
            yield board

    def unexpected_io(self, *args, **kwargs):
        self.fail("Importing board definitions must not access hardware")

    def test_sc132gs_import_and_compiled_modes(self):
        calls = []
        def sensor(**kwargs):
            calls.append(kwargs)
            return kwargs

        # Arbitrary enum values verify that no mode ID is hard-coded in Python.
        for board in self.load_board({"SC132GS_MODE": 87, "SC132GS_VGA_MODE": 93},
                                     sensor=sensor):
            self.assertEqual(calls, [])
            self.assertEqual((board["CAMERA_NAME"], board["CAMERA_CSI"],
                              board["CAMERA_POWER"], board["CAMERA_CHIP_ID"]),
                             ("SC132GS", 0, 2, 0x0132))
            self.assertNotIn("CAMERA_TRIG", board)
            self.assertNotIn("CAMERA_PWDN", board)
            self.assertEqual(board["camera_sensor"](), {"id": 0, "type": 87})
            self.assertEqual(board["camera_sensor"](vga=True), {"id": 0, "type": 93})

    def test_probe_skips_bus_errors_and_wrong_silicon(self):
        reads = []
        def i2c(bus, freq):
            self.assertEqual((bus, freq), (1, 400000))
            class Bus:
                def readfrom_mem(self, address, register, count, *, addrsize):
                    reads.append((address, register, count, addrsize))
                    if address == 0x30:
                        raise OSError("NACK")
                    chip = 0x1A79 if address == 0x31 else 0x0132
                    return bytes([chip >> 8 if register == 0x3107 else chip & 0xff])
            return Bus()

        for board in self.load_board({"SC132GS_MODE": 9, "SC132GS_VGA_MODE": 10},
                                     i2c=i2c):
            self.assertEqual(reads, [])
            self.assertEqual(board["camera_probe"](), (0x32, 0x0132))
            self.assertEqual(reads, [(0x30, 0x3107, 1, 16),
                                     (0x31, 0x3107, 1, 16), (0x31, 0x3108, 1, 16),
                                     (0x32, 0x3107, 1, 16), (0x32, 0x3108, 1, 16)])

    def test_probe_exhausts_all_addresses(self):
        addresses = []
        class Bus:
            def readfrom_mem(self, address, *args, **kwargs):
                addresses.append(address)
                raise OSError("NACK")

        for board in self.load_board({"SC132GS_MODE": 9, "SC132GS_VGA_MODE": 10},
                                     i2c=lambda *args, **kwargs: Bus()):
            with self.assertRaisesRegex(OSError, "SC132GS chip ID 0x0132"):
                board["camera_probe"]()
            self.assertEqual(addresses, [0x30, 0x31, 0x32, 0x33])

    def test_legacy_profile(self):
        for board in self.load_board({"SC038HGS_MODE": 31}, sensor=lambda **kw: kw):
            self.assertEqual((board["CAMERA_NAME"], board["CAMERA_CSI"],
                              board["CAMERA_PWDN"], board["CAMERA_TRIG"],
                              board["CAMERA_CHIP_ID"]), ("SC038HGS", 2, 4, 2, 0x1A79))
            self.assertEqual(board["camera_sensor"](), {"id": 2, "type": 31})

    def test_no_camera_still_exposes_peripheral_pins(self):
        for board in self.load_board({}):
            self.assertEqual(board["UART_PINS"][0], (38, 39))
            self.assertIsNone(board["CAMERA_NAME"])
            for name in ("camera_sensor", "camera_probe"):
                with self.assertRaisesRegex(RuntimeError, "No Dart camera driver"):
                    board[name]()


if __name__ == "__main__":
    unittest.main()
