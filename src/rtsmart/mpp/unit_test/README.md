# unit_test

This directory contains GoogleTest-based unit tests for sources under:

`mpp/mpp/middleware/src/`

Excluded 3rd party codes (per request): `ffmpeg`, `live555`, `mp4_player`, `mp4_format`, `x264`.

## Current coverage

Runnable source-level tests:
- `ogg/src/bitwise.c`
- `ogg/src/framing.c`
- `ogg/src/libogg.c`
- `rtsp_server/JpegFrameParser.cpp`
- `rtsp_server/LiveFrameSource.cpp`
- `rtsp_server/g711LiveFrameSource.cpp`
- `rtsp_server/h264LiveFrameSource.cpp`
- `rtsp_server/h265LiveFrameSource.cpp`
- `rtsp_pusher/rtsp_pusher.cpp` (with mocked `RTSPPusherImpl`)
- `rtsp_client/LiveFrameSource.cpp` (in separate binary)
- `rtsp_client/g711LiveFrameSource.cpp` (in separate binary)

API-contract tests:
- `kdmedia/include/media.h`
- `rtsp_client/include/rtsp_client.h`
- `rtsp_server/include/rtsp_server.h`

Remaining heavy integration files are tracked as TODO placeholders in `tests/test_integration_scaffold.cpp`.

## Run

```bash
cd mpp/middleware/src/unit_test
./compile_run.sh
```

`compile_run.sh` builds and runs:
- `all_tests` (core + kdmedia + rtsp_server + rtsp_pusher + API tests)
- `rtsp_client_tests` (separate binary to avoid symbol clashes with rtsp_server `LiveFrameSource`)

## Sensor register tests

From the SDK root, run `python3 src/rtsmart/mpp/unit_test/run_sc132gs.py`
or `python3 src/rtsmart/mpp/unit_test/run_sc038hgs.py`. These compile the
actual sensor drivers against SDK headers and simulate I2C/GPIO on the host.
The SC038HGS runner expects a configuration with its driver enabled.
Export `SDK_TOOLCHAIN_DIR` if the SDK toolchain is not in its default location.
SC132GS covers 96 two-/four-lane, color/mono, CSI, clock, reference-frequency
and power-polarity combinations,
including gain-table entries, fractional and wide exposure, Group Hold
failures, mode/FPS changes and test-pattern restoration. This includes
16 reserved-CSI1 rejection cases with no hardware side effects. Two
additional tests use the real user-space sensor metadata table to verify
four-lane CSI0 reception, CSI1 exclusion and independent two-lane CSI2.
Six further polarity cases check CSI0 clock inversion on two/four lanes,
CSI1/CSI2 isolation, preserved delay/reserved bits, mode changes, failed I2C
access and ignored writes to the polarity register. Eight fixed-binding
cases cover all four I2C addresses on CSI0/CSI2, skipped ID reads during
registration/mode initialization, the ISP revision callback returning a real
mismatched ID without rejecting fixed binding, and retained ID-read,
transport and mode-readback failures
(112 simulations total).
Simulated register tests do not establish successful capture on hardware.

The SC132GS runner also compiles the production sensor ioctl dispatcher. Its
power/mode cache tests check real driver dispatch and failed-power-off state.
The driver matrix covers lost EN direction/mux, failed-init physical cleanup,
first-error preservation and GPIO write failures in addition to the existing
camera configuration tests. Host models do not establish real I2C ACK recovery.
