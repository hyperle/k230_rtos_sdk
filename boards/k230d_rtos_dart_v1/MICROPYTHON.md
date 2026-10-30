# Dart V1 MicroPython 自定义固件

本配置在 `micropython` 分支中复用 `dart_control_board` 的 K230D 板级适配，
通过官方 CanMV K230 运行 MicroPython。板级名称仍为 `k230d_rtos_dart_v1`，
因此 U-Boot DTS、eMMC、引脚定义和 RT-Smart defconfig 使用同一套硬件配置。
`configs/k230d_canmv_dart_v1_defconfig` 使用独立输出目录。

## 官方源码与适配

已从官方仓库拉取 `canmv_k230` 分支。构建固定使用当前 SDK 配套的官方版本：

| 组件 | 官方版本 |
| --- | --- |
| CanMV (`kendryte/canmv_k230`) | `791c1ceee8acb4699d59d8dbf8f043ea3940a766` |
| MicroPython (`canmv-k230/micropython`) | `0295137e60c0de862c8ee72d81c88d9e57e3973b` |
| RT-Smart | `56852a6` |
| MPP | `2b7e6a9` |
| U-Boot | `56a131d1` |
| nncase 模型/运行时 | `v2.11.0` |

本次拉取到的 CanMV 分支头为 `0ec86ce`，但未将它与旧版 SDK/MPP 混用。
后续升级应一起更新官方 manifest 中相互配套的组件，并重新验证驱动和 API。
各组件的上游来源和导入基线见 [sources.lock.json](micropython/sources.lock.json)。

本分支已改为单一 Git 仓库：CanMV、MicroPython、RT-Smart、MPP、U-Boot 和所需
第三方源码均作为普通文件直接提交，包含当前板级、Wi-Fi、USB 和 SC038HGS 适配。
在根目录执行 `git add`、`git commit` 和 `git push origin micropython` 即可保存全部改动。
不再需要 `repo sync`、子模块初始化或额外应用板级补丁。

## 构建

已有当前多仓库工作区时：

```sh
make k230d_canmv_dart_v1_defconfig
make NCPUS=8
```

此 defconfig 直接使用本分支内的源码和板级配置，启用 MicroPython 自动启动。

推荐使用[官方构建容器](https://www.kendryte.com/k230_canmv/zh/main/userguide/how_to_build.html)。
当前机器已经有镜像和工具链卷，以下命令可离线构建。
同时挂载原始路径可兼容现有 `.repo` 中记录的绝对路径：

```sh
docker run --rm --network none \
  -v k230_toolchains:/opt/toolchains \
  -v "$PWD:/workspace" -v "$PWD:$PWD" -w /workspace \
  ghcr.io/huangzhenming/k230-builder:latest \
  make k230d_canmv_dart_v1_defconfig

docker run --rm --network none \
  -v k230_toolchains:/opt/toolchains \
  -v "$PWD:/workspace" -v "$PWD:$PWD" -w /workspace \
  ghcr.io/huangzhenming/k230-builder:latest make NCPUS=8
```

新工作区直接克隆自己的根仓库并选择本分支：

```sh
git clone --branch micropython https://github.com/hyperle/k230_rtos_sdk.git
cd k230_rtos_sdk
```

输出位于 `output/k230d_canmv_dart_v1_defconfig/`：

- `CanMV_K230D_Dart_V1_micropython_*_nncase_v2.11.0.img`：完整 eMMC 用户区镜像。
- 同名 `.img.gz`：压缩镜像，烧录前解压。
- `images/sdcard/micropython`：MicroPython 用户态 ELF。
- `build.log`：本次完整构建日志。
- `SHA256SUMS`、`BUILD_INFO.json`：本次交付镜像的校验值和构建验证记录。

虽然分区生成器仍使用 `genimage-sdcard.cfg`，此开发板从专用 eMMC0 启动。
镜像保留 20 MiB RT-Smart A/B 槽、30 MiB RTApp A/B 槽和 30 MiB BIN 分区。
Fast Boot 关闭，RTApp 槽是占位数据；MicroPython 从 500 MiB 应用分区加载。
该分区挂载在 `/sdcard`，与外部 SD 卡无关。

`*_ota.kdimg` 仅包含 RT-Smart/RTApp 槽，不含应用 FAT 分区。
它不能单独把 RTOS 固件升级成此 MicroPython 固件；首次安装应使用完整 `.img`。

## 启动与使用

RT-Smart 挂载 eMMC 后执行 `/sdcard/micropython &`，MicroPython 再运行
`/sdcard/boot.py` 和 `/sdcard/main.py`。默认 `main.py` 输出就绪信息后进入 REPL。
替换这两个脚本即可定制上电行为。

- UART0，IO38/IO39，115200：系统控制台。
- USB0 CDC (`/dev/ttyGS0`)：MicroPython REPL / CanMV IDE。
- USB 保留 CDC + MTP，以及原分支的启动和传输修复；未启用双 CDC。
- Wi-Fi 已启用 RTL8189FTV / SDIO1 和 `network.WLAN`（2026-10-31 配置修复）；
  Wi-Fi 机械开关由用户手动打开，`BSP_WIFI_SDIO_REG_ON_PIN=-1` 不控制 GPIO。
  USB 网络和 WebRTC 仍默认关闭。镜像编译与内容校验通过，尚未烧录验证无线连接。
- DDR 使用 K230D 固定 128 MiB：RT-Smart 58 MiB（含 8 MiB 内核堆）、MMZ 70 MiB；
  MicroPython 默认 GC 堆为 4 MiB，位于 RT-Smart 用户空间。

`dart_board` 模块已冻结到解释器中，启动时不需要从文件系统加载它：

```python
import dart_board
print(dart_board.UART_PINS)    # TX, RX
print(dart_board.I2C_PINS)     # SCL, SDA
print(dart_board.SPI_PINS)     # CS, SCK, MOSI, MISO
print(dart_board.PWM_PINS)     # PWM channel -> GPIO
print(dart_board.camera_probe())  # (I2C address, 0x1a79)
```

导入模块只提供定义，不切换引脚、不打开总线、不输出 PWM。
运行 `camera_probe()` 才会访问 I2C1；应在摄像头采集停止时使用。
板级示例安装在 `/sdcard/examples/dart/`。
陀螺仪总线是 SPI1（QSPI0，GPIO14/15/16/17）；具体芯片的寄存器驱动需按实际型号编写。
舵机通道为 PWM1/2/3/5（GPIO61/7/8/59）。PWM0/1/2 共用频率，PWM3/4/5 共用频率，
请按[官方 PWM 文档](https://www.kendryte.com/k230_canmv/zh/main/example/peripheral/pwm.html)
设置同组周期，并使用舵机实际规格中的脉宽。

## SC038HGS 使用范围

沿用原分支驱动：CSI2、I2C1、GPIO4 低有效 PWDN、GPIO2/TRIG 低电平、外部 24 MHz 时钟。
`dart_board.SC038HGS_MODE` 由 C 模块读取同一套 MPP 头文件中的枚举，避免启用不同驱动后
Python 中写死的模式号与内核不一致。

```python
import dart_board
sensor = dart_board.camera_sensor()  # 只构造，尚未 reset/run
```

普通 `media.sensor.Sensor` 采图路径需要 `sc038hgs-640x480` ISP 标定文件；
仓库没有该文件。当前内部 Pixel CLK 未知，驱动不提供未经验证的 fps 或曝光微秒换算。
`camera_probe()` 只确认 I2C 芯片 ID，不代表 MIPI 图像采集成功。
RAW 直采可按 [SC038HGS.md](SC038HGS.md) 构建 `sample_sc038hgs`，绕过 ISP 数据库。

## 验证与文档依据

2026-10-24：官方容器完整构建返回 0；直接从最终镜像 FAT 分区提取并核对了
RISC-V 64 位 MicroPython ELF、启动脚本和板级示例，MBR 偏移及 gzip 完整性均通过。
SC038HGS 真实驱动的主机模拟矩阵（时钟 × 黑白/彩色 × CSI0/1/2）12 组全部通过；
当时的 5 个子仓库适配补丁和准备脚本检查均通过；2026-10-31 起这些适配已直接纳入根仓库源码。
尚未将本次镜像烧录到开发板，USB REPL、外设运行和摄像头采图需要上板验证。

实现参考：

- [用户指定的 RTOS 应用开发指南](https://www.kendryte.com/k230_rtos/zh/main/app_develop_guide/index.html)
- [添加新开发板](https://www.kendryte.com/k230_rtos/zh/main/advanced_development_guide/how_to_add_newconfig_file.html)
- [内存布局与配置](https://www.kendryte.com/k230_rtos/zh/main/advanced_development_guide/memory.html)
- [CanMV 自定义固件](https://www.kendryte.com/k230_canmv/zh/main/userguide/how_to_build.html)
- [Sensor 示例与初始化顺序](https://www.kendryte.com/k230_canmv/zh/main/example/media/sensor.html)
