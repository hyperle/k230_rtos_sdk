# K230D RTOS Dart V1

## Board configuration

- SoC: K230D, 128 MiB SiP LPDDR4.
- Boot storage: eMMC on the dedicated eMMC0/SDIO0 bank, 1.8 V VCCQ,
  8 data bits (confirmed by the board designer).
- Console: UART0, IO38/IO39, 115200 baud.
- U-Boot is built from source. Its board target and device tree are both
  `k230d_rtos_dart_v1`.
- DDR uses the existing K230D SiP LPDDR4 1600 NODT implementation selected
  in the original board configuration. Validate this setting on hardware.
- RT-Smart inherits the U-Boot pinmux and uses software RTC, consistent
  with this board's `BOARD_NOT_SUPPORT_HW_RTC` declaration.
- `k230d_rtos_dart_v1_defconfig` runs RT-Smart applications with CanMV
  disabled. For the custom MicroPython firmware, select
  `k230d_canmv_dart_v1_defconfig` and follow the
  [MicroPython build and usage guide](MICROPYTHON.md).
- The RT-Smart profile starts at the UART0 shell with no application
  auto-execute command. Application preloading (Fast Boot Configuration)
  is disabled. Wi-Fi support is enabled in both Dart V1 profiles.

For the UART0 startup crash diagnosis, USB MTP behavior, repair image and
first-use steps, see [UART0 / USB diagnosis](UART0_DIAGNOSIS.md).

## Pin assignments from the supplied DTS

The pinmux below comes from the original `config.dts`, now named
`src/uboot/uboot/arch/riscv/dts/k230d_rtos_dart_v1.dts`.
Peripheral functions do not identify the external device attached to a bus
or the circuit controlled by a generic GPIO.

| Function | Pins |
| --- | --- |
| BOOT0 / BOOT1 | IO0 / IO1 |
| UART0 TX / RX | IO38 / IO39 |
| UART1 TX / RX | IO40 / IO41 |
| UART2 TX / RX | IO44 / IO45 |
| UART3 TX / RX | IO32 / IO33 |
| UART4 TX / RX | IO48 / IO49 |
| I2C1 SCL / SDA | IO34 / IO35 |
| I2C2 SCL / SDA | IO11 / IO12 |
| I2C3 SCL / SDA | IO36 / IO37 |
| QSPI0 CS0 / CLK / D0 / D1 (`spi1`) | IO14 / IO15 / IO16 / IO17 |
| QSPI1 CS0 / CLK / D0 / D1 (`spi2`) | IO20 / IO21 / IO22 / IO23 |
| SDIO1 CLK / CMD / D0 / D1 / D2 / D3 | IO26 / IO27 / IO28 / IO29 / IO30 / IO31 |
| PWM0 / PWM1 / PWM2 / PWM3 / PWM5 | IO60 / IO61 / IO7 / IO8 / IO59 |
| SC038HGS TRIG1 / PWDN | IO2 / IO4 |
| Generic GPIO | IO3, IO5, IO6, IO10, IO18, IO19, IO24, IO25 |
| NC | IO13, IO62, IO63 |

IO0/IO1 are configured for 1.8 V. The other GPIO banks in this DTS are
configured for 3.3 V; this is separate from the dedicated eMMC0 bank.
The supplied pinmux screenshot agrees with the peripheral assignments
above. It shows GPIO function selections for IO13/IO62/IO63; the DTS
keeps these unused pins disabled as NC. The screenshot does not show
the dedicated eMMC0 bank or the MIPI CSI wiring.
The RT-Smart configuration enables I2C1/2/3, SPI1/2 and SDIO0/1 to match
these assignments. In this SDK `spi0` is the OSPI controller, `spi1` is
QSPI0 and `spi2` is QSPI1. The two QSPI pin groups currently configure
only D0/D1 for standard SPI, not D2/D3.
SDIO1 is enabled because its signals are present in the supplied pinmux;
it is not forced as the boot storage.

## SC038HGS camera driver

The SC038HGS driver is enabled in this board's defconfig. It uses CSI2
(`CSI_CLK2/CSI_D4`), I2C1 on IO34/IO35, active-low PWDN on GPIO4 and an
external 24 MHz oscillator. GPIO2/TRIG stays low; the driver disables
external trigger and slave modes in the sensor. It does not program a
K230 MCLK output.

The driver probes addresses 0x30 through 0x33 for chip ID 0x1A79, observes
the shutdown recovery delay, and implements a documented reset-default
640 x 480, RAW10, one-lane bring-up sequence. It also implements streaming,
analogue/digital gain, 1/16-line exposure, VTS adjustment, mirror/flip and
the grey gradient test pattern. MIPI selection is explicit and mode
registers are read back before streaming. Combined exposure and total
gain updates use one Group Hold transaction. Device: `/dev/sensor_sc038hgs_csi2`;
mode: `SC038HGS_MIPI_CSI2_640X480_10BIT_LINEAR`.

The two supplied PDFs are the register and module-interface sources.
PLL and analogue registers retain silicon reset values because the
V1.1 datasheet does not describe their programming. The default mode
therefore has no claimed frame rate. `MPP_SC038HGS_PIXEL_CLOCK_HZ=0`
means unknown internal Pixel CLK; streaming, raw register access, gain
and test pattern are available, while fps and exposure in seconds return
an unsupported error. Set this option only after verifying Pixel CLK.
It is separate from the external 24 MHz input. Receiver PHY selection
is configurable with `MPP_SC038HGS_PHY_CLOCK`; its default is the K230
800M receiver setting, pending capture validation.

No sensor-specific ISP calibration database is supplied. The user-space
mode names `sc038hgs-640x480` as its database; an application that invokes
the ISP database loader must install a matching profile before using that
path. The sensor-control driver does not depend on that database.
The `sample_sc038hgs` example captures RAW10 through MCM without an ISP
database, saves a gradient or live image, and measures the arrival rate.
Build it directly with `make -C src/rtsmart/examples/mpp/sample_sc038hgs all`.
Compilation and simulated register tests do not establish successful
capture on the physical module.

See [SC038HGS implementation and local document findings](SC038HGS.md)
for register references, connector pinout, existing electrical findings,
test commands and hardware bring-up checks.

## Other board peripherals

- Gyroscope SPI uses QSPI0 (`spi1` / controller ID 1):
  CS/CLK/MOSI/MISO = GPIO14/15/16/17. Its device model, SPI mode, maximum
  clock and register initialization must be supplied by the device driver
  or application.
- Servo outputs use PWM1/PWM2/PWM3/PWM5 = GPIO61/7/8/59. PWM0 on GPIO60
  is available for uses with a compatible frequency: PWM0/1/2 share one
  period, PWM3/4/5 share another. Set a common period per group and adjust
  each servo pulse width independently. Choose the frequency and pulse
  range from the actual servo specification.
- Wi-Fi: RTL8189FTV on SDIO1 (IO26-31), enabled in the RT-Smart
  configuration. The CanMV configuration also exposes `network.WLAN`.
  Power is controlled by the physical Wi-Fi switch; REG_ON GPIO is -1
  (no GPIO control). Enable the switch before boot. Firmware build and
  image checks passed on 2026-10-31; radio operation awaits board testing.

## Build

All component sources are tracked by this root repository as ordinary files.
Use `git add`, `git commit`, and `git push origin HEAD` from the root. No `repo sync`
or submodule setup is needed. Upstream import revisions for the common components
are recorded in [sources.lock.json](sources.lock.json).


```sh
make k230d_rtos_dart_v1_defconfig
make menuconfig
make savedefconfig
make
```

`make uboot-menuconfig` and `make rtsmart-menuconfig` save their respective
subsystem defconfigs automatically. Run them after selecting the board.

The complete image is generated in `output/k230d_rtos_dart_v1_defconfig/`
with a name starting with `RTOS_K230D_Dart_V1_rtsmart_`.
The repository does not currently contain the user's `launcher` application.
To enable application auto-start, install a compatible RT-Smart RISC-V
ELF executable at `/sdcard/app/launcher`, set `RTT_AUTO_EXEC_CMD` to
`/sdcard/app/launcher &`, and rebuild. Here `/sdcard` is a mount point
for an eMMC partition; it does not imply an external SD card.

With the physical Wi-Fi switch on before boot, use the UART0 shell
(115200 baud) to test the RTL8189FTV interface:

```sh
wifi scan
wifi join "YOUR_SSID" "YOUR_PASSWORD"
ifconfig
```

If application preloading is enabled later, its build-time file path must
refer to a real executable available when `tools/gen_image_rtapp.py` runs,
and the auto-execute command must be `@preload &` (not `@oreload &`).
The `images/` staging directory is recreated by `make`, so a custom app
build/install step must place the executable there after that preparation,
or the preload path must point to a persistent executable outside it.

The existing `genimage-sdcard.cfg` filename also applies to the eMMC user
area image. It contains a 20 MiB RT-Smart slot and a 30 MiB application
slot for each A/B copy. The OTA application limit matches the 30 MiB slot.
Compile and inspect the image before flashing; this adaptation has not
yet been tested on the custom board.
