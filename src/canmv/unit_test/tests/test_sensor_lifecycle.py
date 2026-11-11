"""Exercise production Sensor start/stop methods with ordered native failures."""
import ast
import contextlib
import io
from pathlib import Path
from types import SimpleNamespace
import unittest
from unittest.mock import Mock
import os

SOURCE = Path(os.environ.get('SENSOR_TEST_SOURCE', str(Path(__file__).resolve().parents[2] / 'port/builtin_py/media/sensor.py')))

class SensorLifecycleTest(unittest.TestCase):
    def setUp(self):
        tree = ast.parse(SOURCE.read_text())
        sensor = next(n for n in tree.body if isinstance(n, ast.ClassDef) and n.name == 'Sensor')
        names = {'run', 'stop', '_stop_vicap', '_rollback_failed_run', '_release_csi_for_reinit', '__del__', 'deinit'}
        sensor.body = [n for n in sensor.body if isinstance(n, ast.FunctionDef) and n.name in names]
        for method in sensor.body:
            if method.name == '__del__':
                method.name = '_invoke_finalizer'  # Call the real body explicitly, without host GC callbacks.
        self.events = []
        self.native = {}
        def native(name, default=0):
            mock = Mock(return_value=default)
            self.native[name] = mock
            def call(*args):
                self.events.append(name)
                return mock(*args)
            return call
        env = {
            'kd_mpi_vicap_set_dev_attr': native('set_dev'),
            'kd_mpi_vicap_set_chn_attr': native('set_chn'),
            'kd_mpi_vicap_init': native('init'),
            'kd_mpi_vicap_get_sensor_fd': native('get_fd'),
            'kd_mpi_vicap_start_stream': native('start'),
            'kd_mpi_vicap_stop_stream': native('stop'),
            'kd_mpi_vicap_deinit': native('deinit'),
            'vb_mgmt_vicap_dev_inited': native('register'),
            'vb_mgmt_vicap_dev_deinited': native('unregister'),
            'k_vicap_sensor_attr': SimpleNamespace,
            'VICAP_CHN_ID_MAX': 3, 'CAM_DEV_ID_MAX': 3,
            'time': SimpleNamespace(sleep_ms=native('settle')),
        }
        exec(compile(ast.Module(body=[sensor], type_ignores=[]), str(SOURCE), 'exec'), env)
        self.Sensor = env['Sensor']
        self.Sensor._check_mcm_sw_tile_at_run = staticmethod(lambda: None)
        self.Sensor._is_mcm_device = staticmethod(lambda: False)
        self.Sensor.VICAP_STOP_STREAM_DELAY_MS = 100
        self.owner = self.Sensor()
        s = self.owner
        s._dev_id = s._csi_bus = 0
        s._dev_attr = SimpleNamespace(dev_enable=True, mode=1, buffer_num=4, buffer_size=100)
        s._chn_attr = [SimpleNamespace(chn_enable=False) for _ in range(3)]
        s._apply_database_parse_mode = Mock()
        s._release_all_chn_image = native('release')
        s._vicap_init_started = s._vicap_stream_start_started = s._is_started = False
        s._vb_mgmt_registered = False
        s.fd = -1
        self.Sensor._devs = [s, None, None]
        self.Sensor._csis = [True, False, False]
        def fd(attr):
            attr.sensor_fd = 47
            return 0
        self.native['get_fd'].side_effect = fd
        self.output = io.StringIO()
        self.redirect = contextlib.redirect_stdout(self.output)
        self.redirect.__enter__()
        self.addCleanup(self.redirect.__exit__, None, None, None)

    def started(self):
        self.owner._vicap_init_started = True
        self.owner._vicap_stream_start_started = True
        self.owner._is_started = True
        self.owner._vb_mgmt_registered = True
        self.owner.fd = 47

    def assert_owned(self):
        self.assertIs(self.Sensor._devs[0], self.owner)
        self.assertTrue(self.Sensor._csis[0])
        self.assertTrue(self.owner._dev_attr.dev_enable)

    def test_failed_init_rolls_back_before_returning_original_error(self):
        self.native['init'].return_value = -1609203696
        with self.assertRaisesRegex(RuntimeError, r'vicap init failed\(-1609203696\)'):
            self.owner.run()
        self.assertEqual(self.events, ['set_dev', 'init', 'release', 'deinit'])
        self.assertFalse(self.owner._vicap_init_started)
        self.assert_owned()

    def test_rollback_failure_keeps_state_and_first_error(self):
        self.native['init'].return_value = -1609203696
        self.native['deinit'].return_value = -41
        with self.assertRaisesRegex(RuntimeError, r'vicap init failed\(-1609203696\)'):
            self.owner.run()
        self.assertTrue(self.owner._vicap_init_started)
        self.assertIn('rollback incomplete', self.output.getvalue())
        self.assert_owned()

    def test_retry_same_object_after_successful_rollback(self):
        self.native['init'].return_value = -1609203696
        with self.assertRaises(RuntimeError):
            self.owner.run()
        self.native['init'].return_value = 0
        self.owner.run()
        self.assertTrue(self.owner._is_started)
        self.assertEqual(self.owner.fd, 47)
        self.assertEqual(self.native['init'].call_count, 2)
        self.assertEqual(self.native['deinit'].call_count, 1)

    def test_pending_cleanup_failure_prevents_a_second_init(self):
        self.owner._vicap_init_started = True
        self.native['deinit'].return_value = -41
        with self.assertRaisesRegex(RuntimeError, r'deinit failed\(-41\)'):
            self.owner.run()
        self.native['init'].assert_not_called()
        self.native['set_dev'].assert_not_called()
        self.assert_owned()

    def test_fd_failure_is_original_error_and_init_is_cleaned(self):
        self.native['get_fd'].side_effect = None
        self.native['get_fd'].return_value = -42
        with self.assertRaisesRegex(RuntimeError, r'get sensor fd failed\(-42\)'):
            self.owner.run()
        self.assertEqual(self.native['deinit'].call_count, 1)
        self.native['start'].assert_not_called()
        self.assertFalse(self.owner._vicap_init_started)

    def test_start_failure_stops_then_deinitializes(self):
        self.native['start'].return_value = -43
        with self.assertRaisesRegex(RuntimeError, r'start stream failed\(-43\)'):
            self.owner.run()
        self.assertEqual(self.events[-4:], ['release', 'stop', 'settle', 'deinit'])
        self.assertFalse(self.owner._vicap_init_started)
        self.assertFalse(self.owner._vicap_stream_start_started)

    def test_stop_failure_does_not_deinit_or_free_slot(self):
        self.started()
        self.native['stop'].return_value = -44
        with self.assertRaisesRegex(RuntimeError, r'stop stream failed\(-44\)'):
            self.owner.stop()
        self.native['deinit'].assert_not_called()
        self.native['unregister'].assert_not_called()
        self.assertTrue(self.owner._is_started)
        self.assert_owned()

    def test_deinit_failure_retains_slot_and_does_not_repeat_successful_stop(self):
        self.started()
        self.native['deinit'].return_value = -45
        with self.assertRaisesRegex(RuntimeError, r'deinit failed\(-45\)'):
            self.owner.stop()
        self.assertTrue(self.owner._vicap_init_started)
        self.assertFalse(self.owner._is_started)
        self.assert_owned()
        self.native['deinit'].return_value = 0
        self.owner.stop()
        self.assertEqual(self.native['stop'].call_count, 1)
        self.assertEqual(self.native['deinit'].call_count, 2)
        self.assertIsNone(self.Sensor._devs[0])
        self.assertFalse(self.Sensor._csis[0])
        self.assertEqual(self.owner.fd, -1)

    def test_finalizer_failure_keeps_ownership_without_raising(self):
        self.started()
        self.native['stop'].return_value = -46
        self.owner.stop(is_del=True)
        self.native['deinit'].assert_not_called()
        self.assert_owned()
        self.assertIn('cleanup incomplete', self.output.getvalue())

    def test_release_failure_prevents_stop_and_teardown(self):
        self.started()
        self.native['release'].side_effect = RuntimeError('release failed')
        with self.assertRaisesRegex(RuntimeError, 'release failed'):
            self.owner.stop()
        self.native['stop'].assert_not_called()
        self.native['deinit'].assert_not_called()
        self.assert_owned()

    def test_normal_stop_releases_native_before_slot(self):
        self.started()
        self.owner.stop()
        self.assertEqual(self.events, ['release', 'stop', 'settle', 'deinit', 'unregister'])
        self.assertIsNone(self.Sensor._devs[0])
        self.assertFalse(self.Sensor._csis[0])
        self.assertFalse(self.owner._dev_attr.dev_enable)
        self.assertEqual(self.owner.fd, -1)

    def test_force_reinit_cannot_steal_a_failed_stop(self):
        self.started()
        self.native['stop'].return_value = -48
        with self.assertRaisesRegex(RuntimeError, r'stop stream failed\(-48\)'):
            self.Sensor._release_csi_for_reinit(0, True)
        self.assert_owned()
        self.native['deinit'].assert_not_called()

    def test_force_reinit_stops_previous_owner_before_releasing_csi(self):
        self.started()
        self.Sensor._release_csi_for_reinit(0, True)
        self.assertEqual(self.events, ['release', 'stop', 'settle', 'deinit', 'unregister'])
        self.assertIsNone(self.Sensor._devs[0])
        self.assertFalse(self.Sensor._csis[0])

    def test_late_finalizer_does_not_release_new_owner(self):
        retired = self.owner
        self.started()
        retired.stop()
        newer = self.Sensor()
        self.Sensor._devs[0] = newer
        self.Sensor._csis[0] = True
        self.events.clear()
        retired._invoke_finalizer()
        self.assertIs(self.Sensor._devs[0], newer)
        self.assertTrue(self.Sensor._csis[0])
        self.assertEqual(self.events, [])

    def test_unclaimed_constructor_finalizer_never_touches_native_state(self):
        partial = self.Sensor()
        partial._invoke_finalizer()
        self.assert_owned()
        self.assertEqual(self.events, [])

    def test_retired_manual_stop_does_not_free_reused_slot(self):
        retired = self.owner
        retired.stop()
        newer = self.Sensor()
        self.Sensor._devs[0] = newer
        self.Sensor._csis[0] = True
        self.events.clear()
        retired.stop(is_del=True)
        self.assertIs(self.Sensor._devs[0], newer)
        self.assertTrue(self.Sensor._csis[0])
        self.assertEqual(self.events, [])

    def test_class_cleanup_raises_while_a_native_owner_remains(self):
        self.started()
        self.native['deinit'].return_value = -49
        with self.assertRaisesRegex(RuntimeError, 'cleanup incomplete; resources retained'):
            self.Sensor.deinit()
        self.assert_owned()

    def test_class_cleanup_can_retry_without_repeating_successful_stop(self):
        self.started()
        self.native['deinit'].return_value = -49
        with self.assertRaises(RuntimeError):
            self.Sensor.deinit()
        self.native['deinit'].return_value = 0
        self.Sensor.deinit()
        self.assertEqual(self.native['stop'].call_count, 1)
        self.assertIsNone(self.Sensor._devs[0])

    def test_class_cleanup_accepts_a_constructed_unstarted_sensor(self):
        self.owner._dev_attr.dev_enable = False
        self.Sensor.deinit()
        self.assertIsNone(self.Sensor._devs[0])
        self.native['stop'].assert_not_called()
        self.native['deinit'].assert_not_called()

if __name__ == '__main__':
    unittest.main()
