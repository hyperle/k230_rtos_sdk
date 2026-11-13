"""Production Sensor sizing and packing for widths the ISP pads to a 16-pixel pitch."""

import ast
from pathlib import Path
from types import SimpleNamespace
import unittest
from unittest.mock import Mock


SOURCE = Path(__file__).resolve().parents[2] / "port/builtin_py/media/sensor.py"
RGB888, RGB888P, YUV420, BAYER10 = 3, 5, 31, 20


def align_up(value, alignment):
    return (value + alignment - 1) // alignment * alignment


class SensorPitchTest(unittest.TestCase):
    def setUp(self):
        tree = ast.parse(SOURCE.read_text())
        sensor = next(node for node in tree.body
                      if isinstance(node, ast.ClassDef) and node.name == "Sensor")
        methods = {"set_framesize", "_calculate_buffer_size", "snapshot"}
        sensor.body = [node for node in sensor.body
                       if isinstance(node, ast.FunctionDef) and node.name in methods]
        self.pack = Mock(return_value=0)
        self.images = []
        self.env = {
            "CAM_CHN_ID_0": 0, "CAM_CHN_ID_MAX": 3, "FRAME_SIZE_INVAILD": 31,
            "CAM_OUT_WIDTH_MIN": 64, "CAM_OUT_HEIGHT_MIN": 64,
            "ALIGN_UP": align_up, "VICAP_ALIGN_4K": 4096, "VB_INVALID_POOLID": -1,
            "VICAP_DUMP_YUV": 0,
            "PIXEL_FORMAT_YUV_SEMIPLANAR_420": YUV420, "PIXEL_FORMAT_RGB_888": RGB888,
            "PIXEL_FORMAT_RGB_888_PLANAR": RGB888P, "PIXEL_FORMAT_BGR_888_PLANAR": 6,
            "PIXEL_FORMAT_RGB_BAYER_10BPP": BAYER10, "PIXEL_FORMAT_RGB_BAYER_12BPP": 21,
            "PIXEL_FORMAT_RGB_BAYER_14BPP": 22, "PIXEL_FORMAT_RGB_BAYER_16BPP": 23,
            "is_vb_mgmt_vicap_image": lambda img: False,
            "vb_mgmt_dump_vicap_config": SimpleNamespace,
            "vb_mgmt_vicap_image": self.native_image,
            "vb_mgmt_dump_vicap_frame": Mock(return_value=0),
            "vb_mgmt_pack_vicap_frame": self.pack,
            "py_video_frame_info": lambda info: ("raw", info),
            "image": SimpleNamespace(RGB888=8, YUV420=9, RGBP888=10, GRAYSCALE=1,
                                     ALLOC_VB=2, Image=self.wrap_image),
        }
        exec(compile(ast.Module(body=[sensor], type_ignores=[]), str(SOURCE), "exec"), self.env)
        self.sensor = self.env["Sensor"]()
        self.sensor._dev_id = 0
        self.sensor._dev_attr = SimpleNamespace(
            dev_enable=True, acq_win=SimpleNamespace(width=1080, height=1280))
        self.sensor._chn_attr = [self.channel() for _ in range(3)]
        self.sensor._framesize = [None] * 3
        self.sensor._imgs = [None] * 3
        self.sensor._is_rgb565 = [False] * 3
        self.sensor._is_grayscale = [False] * 3

    @staticmethod
    def channel():
        window = lambda: SimpleNamespace(h_start=0, v_start=0, width=0, height=0)
        return SimpleNamespace(chn_enable=False, crop_enable=False, scale_enable=False,
                               alignment=0, out_win=window(), crop_win=window(),
                               scale_win=window(), pix_format=0, buffer_size=0,
                               buffer_pool_id=0)

    def native_image(self):
        frame = SimpleNamespace(phys_addr=[0x1000, 0, 0], virt_addr=[0x2000, 0, 0],
                                width=1080, height=1280, pixel_format=RGB888)
        return SimpleNamespace(magic=0, vf_info=SimpleNamespace(v_frame=frame, pool_id=7))

    def wrap_image(self, width, height, fmt, **kwargs):
        self.images.append((width, height, fmt, kwargs))
        return "image"

    def buffer_size(self, width, height, fmt):
        self.sensor.set_framesize(width=width, height=height)
        attr = self.sensor._chn_attr[0]
        attr.pix_format = fmt
        self.sensor._calculate_buffer_size(0)
        return attr.out_win.width, attr.buffer_size

    def test_native_width_is_kept_instead_of_exceeding_the_sensor(self):
        self.assertEqual(self.buffer_size(1080, 1280, RGB888),
                         (1080, align_up(1088 * 1280 * 3, 4096)))

    def test_native_width_yuv_buffer_holds_the_padded_planes(self):
        self.assertEqual(self.buffer_size(1080, 1280, YUV420),
                         (1080, align_up(1088 * 1280 * 3 // 2, 4096)))

    def test_widths_that_fit_after_rounding_keep_the_old_alignment(self):
        self.assertEqual(self.buffer_size(1000, 640, RGB888),
                         (1008, align_up(1008 * 640 * 3, 4096)))

    def test_aligned_widths_keep_their_buffer_size(self):
        for width, height in ((544, 640), (480, 640), (640, 480)):
            with self.subTest(width=width):
                self.assertEqual(self.buffer_size(width, height, RGB888),
                                 (width, align_up(width * height * 3, 4096)))

    def test_bayer_buffer_still_uses_the_acquisition_window(self):
        self.assertEqual(self.buffer_size(1080, 1280, BAYER10),
                         (1080, align_up(1080 * 1280 * 2, 4096)))

    def test_snapshot_packs_rows_before_wrapping_pixels(self):
        self.pack.side_effect = lambda img: self.images.append("pack") or 0
        self.assertEqual(self.sensor.snapshot(), "image")
        self.assertEqual(self.images[0], "pack")
        self.assertEqual(self.images[1][:3], (1080, 1280, 8))

    def test_raw_dump_keeps_the_hardware_pitch(self):
        kind, _ = self.sensor.snapshot(dump_frame=True)
        self.assertEqual(kind, "raw")
        self.pack.assert_not_called()

    def test_pack_failure_keeps_the_frame_owned_for_release(self):
        self.pack.return_value = 5
        with self.assertRaisesRegex(RuntimeError, r"pack frame failed\(5\)"):
            self.sensor.snapshot()
        self.assertIsNotNone(self.sensor._imgs[0])
        self.assertEqual(self.images, [])


if __name__ == "__main__":
    unittest.main()
