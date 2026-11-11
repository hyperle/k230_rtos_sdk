"""Exercise production Sensor ownership handling with failing native releases."""

import ast
from pathlib import Path
from types import SimpleNamespace
import unittest
from unittest.mock import Mock


SOURCE = Path(__file__).resolve().parents[2] / "port/builtin_py/media/sensor.py"
IN_USE = 0x1234DEAD  # VB_MGMT_VICAP_IMAGE_MAGIC_IN_USE in core/mpp_vb_mgmt.c


class SensorBufferTest(unittest.TestCase):
    def setUp(self):
        tree = ast.parse(SOURCE.read_text())
        sensor = next(node for node in tree.body
                      if isinstance(node, ast.ClassDef) and node.name == "Sensor")
        methods = {"_holds_frame", "_release_image", "_release_all_chn_image", "snapshot"}
        constants = {"VICAP_IMAGE_IN_USE"}
        sensor.body = [node for node in sensor.body
                       if (isinstance(node, ast.FunctionDef) and node.name in methods) or
                       (isinstance(node, ast.Assign) and
                        any(getattr(t, "id", None) in constants for t in node.targets))]
        # Like video_struct.is_vb_mgmt_vicap_image: a struct of the right type/size.
        self.is_frame = Mock(side_effect=lambda img: isinstance(img, SimpleNamespace))
        self.release = Mock(side_effect=self._native_release)
        self.release_ret = 0
        self.dump = Mock(side_effect=AssertionError("new dump must not start"))
        self.env = {"CAM_CHN_ID_0": 0, "CAM_CHN_ID_MAX": 3,
                    "VICAP_CHN_ID_MAX": 3, "VICAP_DUMP_YUV": 0,
                    "const": lambda value: value,
                    "is_vb_mgmt_vicap_image": self.is_frame,
                    "vb_mgmt_release_vicap_frame": self.release,
                    "vb_mgmt_dump_vicap_frame": self.dump,
                    "vb_mgmt_dump_vicap_config": SimpleNamespace,
                    "vb_mgmt_vicap_image": lambda: SimpleNamespace(magic=0)}
        # Compile the real methods, excluding hardware imports and unrelated methods.
        exec(compile(ast.Module(body=[sensor], type_ignores=[]), str(SOURCE), "exec"),
             self.env)
        self.owner = self.env["Sensor"]()
        self.old_frame = SimpleNamespace(magic=IN_USE)
        self.owner._dev_attr = SimpleNamespace(dev_enable=True)
        self.owner._dev_id = 0
        self.owner._imgs = [self.old_frame, None, None]

    def _native_release(self, image):
        # mpp_vb_mgmt.c: 1 for a struct it does not own; magic cleared on success.
        if image.magic != IN_USE:
            return 1
        if self.release_ret == 0:
            image.magic = 0
        return self.release_ret

    def test_failed_release_keeps_old_frame_and_does_not_start_another_dump(self):
        self.release_ret = -21
        with self.assertRaisesRegex(RuntimeError, "release vicap image failed "):
            self.owner.snapshot()
        self.assertIs(self.owner._imgs[0], self.old_frame)
        self.release.assert_called_once_with(self.old_frame)
        self.dump.assert_not_called()

    def test_successful_release_drops_old_frame_before_next_dump(self):
        with self.assertRaisesRegex(AssertionError, "new dump must not start"):
            self.owner.snapshot()
        self.release.assert_called_once_with(self.old_frame)
        self.assertIsNot(self.owner._imgs[0], self.old_frame)
        self.dump.assert_called_once()

    def test_failed_cleanup_preserves_frame_for_native_retry(self):
        self.release_ret = -22
        with self.assertRaises(RuntimeError):
            self.owner._release_all_chn_image()
        self.assertIs(self.owner._imgs[0], self.old_frame)

    def test_interrupt_after_release_does_not_release_twice(self):
        # Native release succeeded, then an IDE interrupt fired before the slot
        # was cleared. Cleanup must not report the released struct as a failure.
        self.assertEqual(self.release(self.old_frame), 0)
        self.release.reset_mock()
        self.owner._release_all_chn_image()
        self.release.assert_not_called()
        self.assertEqual(self.owner._imgs, [None, None, None])

    def test_frame_dumped_just_before_interrupt_is_released_by_cleanup(self):
        self.owner._imgs[0] = None

        def dump_then_interrupt(cfg, image):
            image.magic = IN_USE  # native dump filled the struct and returned 0
            raise KeyboardInterrupt  # pending exception raised on return

        self.dump.side_effect = dump_then_interrupt
        with self.assertRaises(KeyboardInterrupt):
            self.owner.snapshot()
        dumped = self.dump.call_args[0][1]
        self.assertIs(self.owner._imgs[0], dumped)
        self.owner._release_all_chn_image()
        self.release.assert_called_once_with(dumped)
        self.assertEqual(dumped.magic, 0)
        self.assertEqual(self.owner._imgs, [None, None, None])

    def test_failed_dump_leaves_no_frame_owner(self):
        self.owner._imgs[0] = None
        self.dump.side_effect = None
        self.dump.return_value = 3
        with self.assertRaisesRegex(RuntimeError, r"snapshot chn\(0\) failed\(3\)"):
            self.owner.snapshot()
        self.assertIsNone(self.owner._imgs[0])
        self.owner._release_all_chn_image()
        self.release.assert_not_called()


if __name__ == "__main__":
    unittest.main()
