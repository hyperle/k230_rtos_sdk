#!/usr/bin/env python3
"""Exercise the SC132GS driver with real SDK headers and simulated I2C/GPIO."""
import os
from itertools import product
from pathlib import Path
import shlex
import subprocess
import tempfile

mpp = Path(__file__).resolve().parents[1]
sensor = mpp / 'kernel/sensor'
command = ['make', '-nB', 'all', 'V=1', 'CONFIG_MPP_ENABLE_SENSOR_SC132GS=y']
if os.environ.get('SDK_TOOLCHAIN_DIR'):
    command.append('SDK_TOOLCHAIN_DIR=' + os.environ['SDK_TOOLCHAIN_DIR'])
dry_run = subprocess.check_output(command, cwd=sensor, text=True)
compile_line = next(line for line in dry_run.splitlines()
                    if ' -c src/sc132gs/sc132gs.c ' in line)
flags = []
arguments = iter(shlex.split(compile_line)[1:])
for argument in arguments:
    if argument in ('-o', '-MF', '-c'):
        next(arguments)
    elif not (argument.startswith(('-march=', '-mabi=', '-mcmodel=')) or
              argument in ('-MD', '-MP', '-fopenmp')):
        flags.append(argument)

with tempfile.TemporaryDirectory(prefix='sc132gs-test-') as directory:
    cases = [(*case, 0, 0, 0x32) for case in product(
        (0, 1), (0, 1), (0, 1, 2), (0, 1), (0, 1), (12000000, 24000000))]
    # Exercise inverted CSI0 on both lane configurations and isolate CSI1/2.
    cases += [(four_lane, 0, csi, 0, 0, 24000000, 1, 0, 0x32)
              for four_lane, csi in product((0, 1), (0, 1, 2))]
    cases += [(1, 0, csi, 0, 0, 24000000, 0, 1, address)
              for csi, address in product((0, 2), range(0x30, 0x34))]
    for four_lane, mono, csi, clock, active_low, extclk, invert, skip_id, fixed_address in cases:
        binary = str(Path(directory) / f'test-{four_lane}-{mono}-{csi}-{clock}-{active_low}-{extclk}-{invert}-{skip_id}-{fixed_address}')
        subprocess.run([os.environ.get('CC', 'cc'),
                        '-I' + str(mpp / 'unit_test/mocks/sensor'), *flags,
                        '-Werror', '-Wno-unused-variable',
                        '-fsanitize=undefined,float-cast-overflow',
                        '-fno-sanitize-recover=all',
                        f'-DTEST_MONO={mono}', f'-DTEST_CSI={csi}',
                        f'-DTEST_CLOCK={clock}', f'-DTEST_ACTIVE_LOW={active_low}',
                        f'-DTEST_EXTCLK={extclk}',
                        f'-DTEST_FOUR_LANE={four_lane}',
                        f'-DTEST_CLOCK_INVERT={invert}',
                        f'-DTEST_SKIP_ID_CHECK={skip_id}',
                        f'-DTEST_FIXED_ADDRESS={fixed_address}',
                        str(mpp / 'unit_test/tests/test_sc132gs.c'),
                        '-o', binary], cwd=sensor, check=True)
        subprocess.run([binary], check=True)

    for four_lane in (0, 1):
        binary = str(Path(directory) / f'test-mpi-{four_lane}')
        subprocess.run([os.environ.get('CC', 'cc'), *flags,
                        '-I' + str(mpp / 'userapps/api'),
                        '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections',
                        '-Werror', '-Wno-unused-parameter',
                        '-fsanitize=undefined,float-cast-overflow',
                        '-fno-sanitize-recover=all',
                        f'-DTEST_FOUR_LANE={four_lane}',
                        str(mpp / 'unit_test/tests/test_sc132gs_mpi.c'),
                        '-o', binary], cwd=sensor, check=True)
        subprocess.run([binary], check=True)


    dispatch_binary = str(Path(directory) / 'test-sensor-power-dispatch')
    subprocess.run([os.environ.get('CC', 'cc'),
                    '-I' + str(mpp / 'unit_test/mocks/sensor'), *flags,
                    '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections',
                    '-Werror', '-Wno-unused-parameter',
                    str(mpp / 'unit_test/tests/test_sensor_power_dispatch.c'),
                    '-o', dispatch_binary], cwd=sensor, check=True)
    subprocess.run([dispatch_binary], check=True)
