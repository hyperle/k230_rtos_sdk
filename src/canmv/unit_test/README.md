# CANMV unit_test

This directory contains a host-side GoogleTest scaffold for CANMV source validation.

## Current coverage
- Functional unit tests for C sources:
  - `port/omv/common/array.c`
  - `port/omv/common/ringbuf.c`
  - `port/omv/alloc/unaligned_memcpy.c`
  - `port/omv/common/mutex.c`
  - `port/omv/common/ff_wrapper.c`
- Source-wide catalog-driven tests for all `*.c`, `*.cc`, `*.cpp`, `*.h`, `*.hpp`, and `*.py` files under the CANMV root:
  - catalog completeness and metadata validation
  - file readability/non-empty checks
  - Python file catalog coverage checks
- Static source contract tests for upgraded high-risk areas:
  - MPP binding module dictionary/object coverage
  - `modmpp.c` API module references
  - MicroPython binding include-shape checks
  - board `mpconfigboard.h` / `manifest.py` pairing
- Auto-generated catalog:
  - `ALL_SOURCE_UNIT_TEST_SUGGESTIONS.md`

## Run
```bash
cd unit_test
./compile_run.sh
./build/canmv_unit_tests
```

`compile_run.sh` always performs a clean build and verifies/installs gtest when missing.
- Some environment will not auto trigger unit test, hence manually run it.

## VICAP frame buffer regression tests

`canmv_vicap_buffer_tests` compiles the production `mpp_vb_mgmt.c` with the real
SDK frame types and a noncoherent DMA/cache model. It checks fresh frame reads,
RGB565 dirty-cache handoff, 120 reuses of one physical frame block, and failure
handling for invalidation, flushing, unmapping, driver release and double release.
These are host tests; they do not establish camera image quality on hardware.

After configuring the existing CMake project, build and run the focused target:

```bash
cmake --build build --target canmv_vicap_buffer_tests canmv_wbc_buffer_tests
ctest --test-dir build --output-on-failure -R '^(VicapBufferTest\.|WbcBufferTest\.|SensorBufferTest$|DisplayBufferTest$)'
```

`canmv_wbc_buffer_tests` compiles the production IDE WBC encoder and models
exclusive frame ownership. It verifies initialization probes, input ownership
until encoding finishes, initialization failures, and channel cleanup before
releasing inputs after status failures or stream timeouts. `SensorBufferTest`
runs the real Python methods with mocked native return values, checking that
failed release keeps the old frame reference and prevents another dump.

## Unsafe block-device streams

The camera implementation currently matches the frame-producing BUFFER_FIXED
baseline. The withdrawn RECOVERY cleanup/power changes and their regressions are
archived under `output/k230d_canmv_dart_v1_defconfig/sc132gs-recovery-review/recovery-source`;
they are not requirements of the restored baseline.

`python3 -B tests/test_devfs_block_io.py` extracts the actual kernel devfs
read/write functions and compiles them with sector-counting device mocks. Raw
block-device byte streams return ENOSYS without invoking the driver; zero-length
requests and file position are checked at aligned, unaligned and large lengths.
Character-device partial reads/writes retain byte counts, and errors preserve
file position. This does not read or write the board's raw disk. Mounted FAT I/O
uses the native sector API and is unaffected by rejecting these devfs streams.

`DisplayBufferTest` compiles the two real display submission functions and their
real buffer structure against SDK frame types. A separate CPU cache/device-memory
model covers synchronized submission, copy/direct cache failures, undersized
buffers, invalid state and retry. The WBC tests also model separate cached/device
JPEG output and exercise failed/partial DMA, failed invalidation and the next
successful encode. These are host failure-injection tests; they do not demonstrate
that the reported purple bands were caused by one of the injected failures.

## Sensor initialization and restart state

`SensorLifecycleTest` executes production Sensor methods, checking failed-init
rollback, retry before another init, preservation of the first native error,
strict stop/deinit ordering, retained CSI ownership, force reinitialization and
retired-object finalizers. `SensorSoftResetTest` compiles the real C VM cleanup
guard with a simulated MicroPython exception boundary (five cases), and checks
that workers exit before the guard and that pool/GC/VM destruction follows it.
These tests exercise mocked native failures, not real camera recovery.
