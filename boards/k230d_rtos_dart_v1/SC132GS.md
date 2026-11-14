# SC132GS camera support

## Local sources read

The four PDFs in `/home/hyperlee/下载` were opened with the host PDF reader.
Text was extracted with Poppler and the module drawing and power/application
diagrams were rendered for visual inspection.

| File | Findings used |
| --- | --- |
| `3763184980SC132GS_Datasheet_V3.0.pdf` | Primary register reference: power/reset pp. 7-8, I2C/ID pp. 9-11, MIPI p. 15, trigger p. 20, exposure/gain pp. 23-28, Group Hold p. 29, mirror/window p. 32, VTS/test pattern p. 33, electrical limits pp. 34-35. |
| `3763188902SC132GS_数据手册_V2.6.pdf` | Cross-check of exposure, gain, Group Hold and pin definitions. Its reset VTS is `0x061a`, whereas V3.0 lists `0x0546`; initialization explicitly sets VTS instead of relying on either reset default. |
| `3763228682SC132GS_Application_Note_V1.1_for-PLCC.pdf` | PLCC pinout and circuit, power sequence p. 5, active-low XSHUTDN p. 6, 2.5 V / 1.2 V / 1.8 V rails and I2C pull-ups p. 9. |
| `3763268327SC132摄像头120度无畸变 镜头 仁恒2025年12月23日.pdf` | Image-only mechanical drawing: module 3.3 V input, 0.5 mm / 22-pin connector, MCLK and four MIPI data pairs. The sensor label contains a spelling inconsistency; probe uses silicon ID, not that label. |

The board-level wiring was rechecked against all nine distinct schematic
images in the download directory, including the main-board connectors in
`20261108-164346.jpg` and the other camera board in `20261108-164352.jpg`.
`old.jpg` is identical to `20261108-164010.jpg`. The connector comparison
exposes a CLK0 polarity discrepancy omitted from the earlier net-name check.
See [the schematic review](SCHEMATIC_REVIEW.md) for exact pins, the distinction
between the two camera boards, and the limits of the inferred mating map.

## Actual adapter wiring

This schematic shows **one** SC132GS connector with one clock pair and four
data pairs. The data pairs occupy both physical K230 CSI0 and CSI1 groups:

| Adapter net | K230 pad group | Default four-lane mode |
| --- | --- | --- |
| `CSI_CLK0+/-` | RX0 clock | Used |
| `CSI_D0+/-` | RX0 data 0 | Used |
| `CSI_D1+/-` | RX0 data 1 | Used |
| `CSI_D2+/-` | RX1 data 2 | Used by CSI0 through PHY1 |
| `CSI_D3+/-` | RX1 data 3 | Used by CSI0 through PHY1 |
| `IIC1_SCL/SDA` | GPIO34 / GPIO35, `i2c1` | Through TXS0102, 3.3 V to 1.8 V |
| `EN` | GPIO2, active high, 10 kOhm pull-down | Enables both adapter LDOs |
| `MCLK` | Y1 output, 1.8 V, through 22 Ohm R9 | External oscillator; frequency not shown |

The user confirmed direct stacking without rewiring. The earlier connector-label
comparison led to enabling `0x3654[3]`; actual board capture disproved that
configuration. The Dart profile now disables `MPP_SC132GS_CSI0_CLOCK_INVERT`.
With `0x3037[6:5]=0` and clock inversion off, full offline, full online, and
VGA online each delivered 100/100 frames. Enabling clock inversion stopped
continuous reception. See [the capture review](CAPTURE_REVIEW.md); connector
labels alone do not determine this sensor clock phase setting.

Both Dart defconfigs (RT-Smart `k230d_rtos_dart_v1_defconfig` and MicroPython
`k230d_canmv_dart_v1_defconfig`) set `MPP_SC132GS_CSI0_4LANE=y`: **both physical CSI0
and CSI1 data groups are used simultaneously, as one logical CSI0 camera**.
`MPP_ENABLE_CSI_DEV_0=y` enables its sensor device. Leave
`MPP_ENABLE_CSI_DEV_1` unset: that flag means a second independent sensor,
not the second PHY of a four-lane input. The receiver is selected through
`VICAP_CSI0` plus `VICAP_MIPI_4LANE`; the SDK VICAP library contains the
PHY0/PHY1 master/slave configuration, so no second I2C probe is needed.
The SC132GS driver rejects a CSI1 probe before touching power, clocks or
I2C while this option is enabled. CSI2 remains independently two-lane.
Disabling this option restores the legacy two-lane modes for other boards.

The adapter's 3.3 V and 1.8 V supplies are produced by
TLV75733 and TLV75718 from the incoming 5 V supply. GPIO4 is not a reset or
power-control net on this adapter. GPIO2 was the old SC038HGS trigger input;
do not combine the two incompatible board profiles on the same connector.

The chip itself is not 3.3 V tolerant: its I/O supply is 1.8 V, with input
absolute maximum DOVDD + 0.3 V. Keep the TXS0102 translation and the 1.8 V
oscillator supply. Do not populate the adapter's optional 5 V EN pull-up R1
without checking its compatibility with the K230 GPIO circuit. Check actual
assembly and pull-ups; components marked NC are not assumed populated.

### Module connector

The module drawing numbers its connector in the opposite direction to the
adapter FPC symbol: module pin 1 corresponds to adapter FPC pin 22.

| Module pin | Signal | Adapter FPC pin |
| --- | --- | --- |
| 1 | 3.3 V | 22 |
| 2 / 3 | SDA / SCL | 21 / 20 |
| 4 / 5 | GND / NC | 19 / 18 |
| 6 / 7 | MCLK / GND | 17 / 16 |
| 8 / 9 | Data 3 P / N | 15 / 14 |
| 10 | GND | 13 |
| 11 / 12 | Data 2 P / N | 12 / 11 |
| 13 | GND | 10 |
| 14 / 15 | Clock P / N | 9 / 8 |
| 16 | GND | 7 |
| 17 / 18 | Data 1 P / N | 6 / 5 |
| 19 | GND | 4 |
| 20 / 21 | Data 0 P / N | 3 / 2 |
| 22 | GND | 1 |

The module exposes no XSHUTDN, TRIG or SID pins at this connector. Do not
connect the old SC038HGS PWDN/TRIG wiring by matching signal positions.

## Driver behavior

The existing SC132GS skeleton has been completed rather than registering a
second driver for the same chip. It is selected by `MPP_ENABLE_SENSOR_SC132GS`.

- Normal probe scans SID-selected 7-bit addresses `0x30`-`0x33` and requires
  ID `0x0132` from `0x3107/0x3108`. The Dart profile explicitly enables
  `MPP_SC132GS_SKIP_CHIP_ID_CHECK`, binding the known module at `0x30`
  without scanning or matching the ID during registration/mode initialization.
  ISP revision queries return the real ID without matching it; failed I2C
  reads still fail. See [the FIXED_BIND review](FIXED_BIND_REVIEW.md).
- Release power/reset and wait a fixed 1000 ms before I2C to allow the
  complete camera module to stabilize, rather than using the chip-only 2 ms minimum.
  Default GPIO2 controls the adapter enable, high = on. Optional inverted
  power circuits use `MPP_SC132GS_POWER_ACTIVE_LOW`.
- Preserve the repository's vendor PLL/analogue tables. The PDFs explicitly
  delegate PLL ratios and special frame-rate programming to the FAE; they do
  not provide a replacement high-rate table or a verified four-lane table.
- Select four-lane RAW10 for CSI0 by programming `0x3018[7:5]=3` (V3.0
  table 1-8), preserving the lower bits. Do not replay the vendor two-lane
  override `0x3019=0x0c` in this mode: the supplied PDFs do not describe
  that register, so leave it at the sensor's soft-reset value rather than
  inventing a four-lane value. Two-lane modes retain the vendor override.
  The four-lane configuration is a datasheet-derived bring-up profile,
  not a separate FAE-validated register table; output enables and packet
  timing still need hardware/FAE confirmation.
- Keep the existing PLL and MIPI bit-clock settings for each resolution;
  four lanes do not by themselves mean halving the PHY frequency or
  doubling the frame rate. Kernel mode/caps and user-space VICAP info all
  report four lanes for CSI0, with one sensor device and one CSI clock.
- Disable external trigger, explicitly set dimensions and VTS, and read
  back mode fields. Initialization leaves `0x0100[0]` clear;
  streaming is a separate operation.
- Keep `0x3037[6:5]=0` for the vendor MIPI byte serializer interface.
  `0x3031` still selects CSI-2 RAW10 packets. The earlier attempt to select
  the datasheet's 10-bit PHY mode (`0x3037[6:5]=1`) prevented real reception;
  restoring byte mode enabled continuous capture on both tested sensor modes.
  This is a measured module requirement, not a change to RAW10 packet depth.
- Apply the configured CSI0 clock inversion through `0x3654[3]` after each
  mode table and verify readback. Preserve delay/reserved bits and keep
  CSI1/CSI2 clock polarity normal. V3.0 p. 15 and V2.6 p. 14 document this bit.
- Full resolution: 1080 x 1280, nominal 120 fps, VTS 1350. VGA crop: 640 x 480,
  nominal 240 fps, VTS 675. These are vendor-table operating points, not
  measured capture results. At a verified 24 MHz input the nominal line time
  is `1 / (120 * 1350)` = approximately 6.173 us in both modes.
- `MPP_SC132GS_EXTCLK_HZ` defaults to 24000000, matching the external clock
  confirmed by the user for this adapter. Changing this option scales timing
  metadata, not PLL programming. Nonstandard clocks require FAE confirmation
  of PLL lock and MIPI timing. Optional K230 clock output uses PLL1/4/25,
  nominally 23.76 MHz, consistently for both modes (118.8 / 237.6 fps).
- Analogue gain follows the documented five stages (`03/23/27/2f/3f`) and
  fine codes, up to 28.546875x. Digital gain programs `0x3e06/0x3e07`, up to
  31.875x using the documented 1/128 fine precision. Quantization rounds down;
  getters report actual registers, not the unquantized request.
- Exposure uses all 20 bits of `0x3e00[3:0]/0x3e01/0x3e02`, in 1/16-line
  units, bounded by `(VTS - 8) * 16`. Reserved upper bits survive updates.
- Gain, exposure, combined total-gain/exposure and VTS/exposure changes use
  Group Hold (`0x3800`: `00`, `10`, `60`). An incomplete transaction is never
  launched, invalidates initialization, and requires a new initialization.
- FPS arguments/results and AE FPS metadata are in milli-fps: `30000` = 30 fps.
  VTS adjustment cannot exceed the nominal mode rate and clamps exposure when
  reducing VTS. Timing changes still need capture validation on the module.
- Test pattern uses the full p. 33 configuration: ramp enabled, manual BLC,
  BLC mode cleared and digital coarse gain 16x. Disabling it restores saved
  settings. Digital/combined gain changes return busy while the ramp is on.
- Mirror/flip preserves unrelated register bits and updates the Bayer pattern.
  The legacy BGGR assumption remains the color-mode baseline; verify it using
  a color target. Set `MPP_SC132GS_MONO` only for a confirmed mono part; the
  mechanical drawing does not identify the CFA variant. Unsupported HDR/OTP/
  white-balance/BLC callbacks are not fake-success stubs.

The device is `/dev/sensor_sc132gs_csi0`. Public modes retain their existing
names `SC132GS_MIPI_CSI0_1080X1280_120FPS_10BIT_LINEAR` and
`SC132GS_MIPI_CSI0_640X480_240FPS_10BIT_LINEAR`. The existing `sc132gs-1080x1280`
and `sc132gs-640x480` ISP databases are installed when the driver is enabled;
they are not a new calibration of this 120-degree lens.

## Build and validation

```sh
make k230d_rtos_dart_v1_defconfig
make
python3 src/rtsmart/mpp/unit_test/run_sc132gs.py
```

If the SDK toolchain lives outside the default directory, pass
`SDK_TOOLCHAIN_DIR` to make and export it for the unit-test runner.
The host test compiles the actual driver against real SDK ABI headers and
runs 96 two-/four-lane, mono/color, CSI-port, clock-source,
reference-frequency and power-polarity combinations
with undefined-behavior and float-cast-overflow checks. It covers all gain
entries, fractional/wide exposure, VTS, mode changes, ramp restoration,
address scanning and I2C/GPIO/Group Hold failures. Sixteen combinations
verify that the reserved CSI1 probe fails without hardware side effects.
Two additional tests compile the actual user-space sensor info table and
check both resolutions, CSI0 lane count, CSI1 suppression and CSI2 isolation.
Six further polarity cases cover inverted CSI0 on two/four lanes, isolation
of CSI1/CSI2, preserved clock-delay bits, mode changes, I2C failures and
ignored polarity writes. The current runner contains 104 simulations in total.

Before claiming hardware success, check GPIO2-powered supplies, Y1 frequency,
I2C ID, all four MIPI data pairs, the single CSI0 clock pair, raw frame
dimensions and measured frame rate. Confirm PHY1 receives D2/D3 rather
than starting an independent CSI1 receiver. Check the sensor's four-lane
output enables with the FAE if either of those pairs stays idle.
Then validate long exposure/FPS changes, gain transitions, the grey ramp,
Bayer order and ISP output. Compilation and register simulation do not prove
that this physical module captures correctly.

The driver, MPP and HAL sources on this branch are synchronized with the
`micropython` branch, whose MicroPython firmware was used for the board
measurements described in the linked reviews (MicroPython constructors, ISP
profile use and preview scripts are documented there in `MICROPYTHON.md`).
The CanMV-specific frame-buffer and pitch fixes do not apply to RT-Smart
applications, which manage VICAP buffers themselves; note that the ISP
writes RGB888/YUV420 rows at a 16-pixel aligned pitch (1088 for 1080 width).

2026-11-13 RT-Smart build: `k230d_rtos_dart_v1_defconfig` with this driver
builds in the offline K230 container; the linked kernel contains the SC132GS
driver and no SC038HGS driver, both system slots match the build, and the
BIN partition contains the SC132GS ISP databases. Build records are in
`output/k230d_rtos_dart_v1_defconfig/` (`build-sc132gs-rtsmart.log`,
`BUILD_INFO_SC132GS_RTSMART.json`). This RT-Smart image has not been flashed
or tested on the board.
