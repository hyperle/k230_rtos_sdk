#!/usr/bin/env python3
"""Run the actual driver with simulated I2C/GPIO and the SDK's real ABI headers."""
import os
from itertools import product
from pathlib import Path
import shlex
import subprocess
import tempfile

mpp = Path(__file__).resolve().parents[1]
sensor = mpp / 'kernel/sensor'
command = ['make', '-nB', 'all', 'V=1']
if os.environ.get('SDK_TOOLCHAIN_DIR'):
    command.append('SDK_TOOLCHAIN_DIR=' + os.environ['SDK_TOOLCHAIN_DIR'])
dry_run = subprocess.check_output(command, cwd=sensor, text=True)
compile_line = next(line for line in dry_run.splitlines()
                    if ' -c src/sc038hgs/sc038hgs.c ' in line)
args = shlex.split(compile_line)[1:]
flags = []
it = iter(args)
for arg in it:
    if arg in ('-o', '-MF', '-c'):
        next(it)
    elif not (arg.startswith(('-march=', '-mabi=', '-mcmodel=')) or
              arg in ('-MD', '-MP', '-fopenmp')):
        flags.append(arg)

with tempfile.TemporaryDirectory(prefix='sc038hgs-test-') as directory:
    for clock, mono, csi in product((0, 108000000), (0, 1), (0, 1, 2)):
        binary = str(Path(directory) / f'test-{clock}-{mono}-{csi}')
        subprocess.run([os.environ.get('CC', 'cc'),
                        '-I' + str(mpp / 'unit_test/mocks/sensor'), *flags,
                        '-Werror', '-Wno-unused-variable',
                        f'-DTEST_PIXEL_CLOCK={clock}',
                        f'-DTEST_MONO={mono}', f'-DTEST_CSI={csi}',
                        str(mpp / 'unit_test/tests/test_sc038hgs.c'),
                        '-o', binary], cwd=sensor, check=True)
        subprocess.run([binary], check=True)
