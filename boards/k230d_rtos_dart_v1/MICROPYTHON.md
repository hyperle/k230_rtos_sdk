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
第三方源码均作为普通文件直接提交，包含当前板级、Wi-Fi、USB 和相机适配。
在根目录执行 `git add`、`git commit` 和 `git push origin micropython` 即可保存全部改动。
不再需要 `repo sync`、子模块初始化或额外应用板级补丁。

## 构建

已有当前多仓库工作区时：

```sh
make k230d_canmv_dart_v1_defconfig
make NCPUS=8
```

此 defconfig 直接使用本分支内的源码和板级配置，启用 MicroPython 自动启动。
默认相机为 SC132GS 四通道适配板。切换固件分支后应重新执行对应 defconfig，
因为 Git 忽略的 `.config` 会保留上一分支的配置。

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
- `images/sdcard/micropython`：MicroPython 用户态 ELF。
- `build.log`：本次完整构建日志。
- `SHA256SUMS`、`BUILD_INFO.json`：本次交付镜像的校验值和构建验证记录。

按用户约束，固件直接交付未压缩的 `.img` 文件，提供对应文件链接。

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
print(dart_board.CAMERA_NAME)     # SC132GS
print(dart_board.camera_probe())  # (I2C address, 0x0132)
```

导入模块只提供定义，不切换引脚、不打开总线、不输出 PWM。
运行 `camera_probe()` 才会访问 I2C1；应在摄像头采集停止时使用。
板级示例安装在 `/sdcard/examples/dart/`。
陀螺仪总线是 SPI1（QSPI0，GPIO14/15/16/17）；具体芯片的寄存器驱动需按实际型号编写。
舵机通道为 PWM1/2/3/5（GPIO61/7/8/59）。PWM0/1/2 共用频率，PWM3/4/5 共用频率，
请按[官方 PWM 文档](https://www.kendryte.com/k230_canmv/zh/main/example/peripheral/pwm.html)
设置同组周期，并使用舵机实际规格中的脉宽。

## SC132GS 相机

默认使用 SC132GS、I2C1 和 GPIO2 高有效适配板 EN。四条 MIPI 数据线同时占用
物理 CSI0/CSI1 引脚组，作为一个逻辑 CSI0 相机；不启用独立 CSI1 设备。
GPIO4 不连接新适配板的复位或电源，外部 Y1 提供 MCLK，用户已确认输入为 24 MHz。
四通道初始化沿用现有厂家 PLL 表并设置文档中的
lane 选择位，尚无该模组的四通道厂家验证表，需要上板确认输出使能、时序和采图。

用户确认直接叠插、未改线。逐针对比主板 U7 与 SC132GS 板 U3 的图片后，发现
CLK0 极性标注相反；此前据此开启 `MPP_SC132GS_CSI0_CLOCK_INVERT` 的推断已被
实板对照否定。本配置现关闭该选项，并将 `0x3037[6:5]` 恢复为 MIPI 字节接口模式。
这两个修正下，全幅离线、全幅在线、VGA 在线各连续取得 100/100 帧；开启时钟反向
则持续取帧失败。具体证据与图像质量限制见[实际采集复核](CAPTURE_REVIEW.md)。

`dart_board.SC132GS_MODE` 和 `SC132GS_VGA_MODE` 由 C 模块读取与 MPP 相同的枚举，
Python 不写死模式号。全幅为 1080×1280、标称 120 fps；VGA 裁剪为 640×480、
标称 240 fps。帧率是厂家模式表在 24 MHz 输入下的标称值，并非实测结果。

```python
import dart_board
sensor = dart_board.camera_sensor()          # 全幅，只构造，尚未 reset/run
# 或在新会话中选择 VGA 裁剪：
# sensor = dart_board.camera_sensor(vga=True)
```

本配置启用 `MPP_SC132GS_SKIP_CHIP_ID_CHECK`，按固定 7 位地址 `0x30` 绑定已知模组；
启动注册和模式初始化不读取或匹配芯片 ID，连接检查改读普通寄存器 `0x0100`。
ISP 初始化还会查询 revision：该回调读取并返回真实 ID，但在固定绑定模式中
不再强制匹配 `0x0132`；ID 读取本身失败仍返回错误，不伪造 ID 或吞掉 I2C 故障。
I2C 传输、模式尺寸和位宽回读失败仍会上报，不将缺失的相机认定为采集成功。
此选项需要重新构建并安装内核，单独修改 Python 脚本不会改变驱动注册行为。
`FIXED_BIND.img` 遗漏了 ISP revision 回调中的匹配限制，修正记录见
[固定绑定初始化复核](FIXED_BIND_REVIEW.md)。

内核设备为 `/dev/sensor_sc132gs_csi0`。构建会安装仓库已有的
`sc132gs-1080x1280`、`sc132gs-640x480` ISP 数据库；这些文件不是新 120 度镜头的重新标定。
曝光、模拟/数字增益、VTS 帧率调整、镜像和灰阶测试图由传感器驱动实现。
普通 `Sensor.set_framerate()` 尚未实现；传感器底层 fps 接口的单位为千分之一 fps。

运行 `/sdcard/examples/dart/camera_preview.py` 可用 VGA、RGB565、手动曝光 2000 us
和 1x 模拟增益向 CanMV IDE 输出预览，不需要 HDMI/LCD。
虚拟显示配置为 30 fps，不代表传感器采集帧率；该脚本不作为帧率测量工具。
示例在失败或中断时释放相机、显示和媒体资源，并关闭采图失败自动重启。
彩色获取和呈现示例为 `/sdcard/examples/dart/camera_color_preview.py`：保留全幅
1080×1280 传感器模式，以 640×480 RGB565 输出到 IDE，默认手动曝光 5000 us、4x 增益，
第 60 帧保存为 `/sdcard/sc132gs_color.jpg`。脚本中的 `PHOTO_PATH` 可修改保存位置；
可用 `EXPOSURE_US` 和 `ANALOG_GAIN` 手动调整亮度；本轮自动曝光曾连续报原生
曝光设置错误，因此暂不默认启用 `AUTO_EXPOSURE`。
`Sensor.GRAYSCALE` 只输出亮度；彩色采图应从 `reset()` 后直接设置
`Sensor.RGB565`，保留 ISP 默认的彩色处理和白平衡。输出格式不能让黑白型号产生
真实颜色，模组的 CFA 型号及色彩准确度仍以实拍彩色物体核对。
I2C 探测示例为 `/sdcard/examples/dart/camera_probe.py`；读到 ID 不能证明 MIPI 采图成功。
原理图接线、寄存器依据和驱动测试见 [SC132GS.md](SC132GS.md)。

### 非 16 倍数宽度的行距（PITCH_FIX）

VICAP ISP 以宽度向上对齐到 16 像素的行距写 RGB888 和 YUV420：2026-11-13 板上读回，
1080 宽的行间隔为 1088 像素，528、544 宽不填充，YUV420 的 UV 平面从
`phys_addr[1] = 行距 × 高度` 开始；而帧的 `stride` 字段报告的仍是可见宽度。
此前 `set_framesize()` 把 1080 对齐为 1088 后超过传感器宽度而拒绝，绕过它时
`_calculate_buffer_size()` 按 1080 分配，每帧约越界 28 KiB，图像也按错误行距解释。

本固件中：`set_framesize()` 只在对齐后不超过采集宽度时才对齐，否则保留请求宽度；
缓冲区按行距计算；`mpp_vb_mgmt.c` 映射、失效和刷新整帧的行距范围，
新增 `vb_mgmt_pack_vicap_frame()` 在原地压紧各行，`snapshot()` 包装图像前调用它。
`snapshot(dump_frame=True)` 仍返回硬件行距的原始帧，读取时按 `ALIGN_UP(width, 16)` 计算行距，
不要使用帧的 `stride` 字段。宽度为 16 倍数时行为不变。主机测试为
`unit_test` 的 `canmv_vicap_padding_tests` 与 `SensorPitchTest`；这些只验证布局模型。
2026-11-13 刷入本固件后：宽度为 16 倍数的 480 × 640 RGB565 正常（开虚拟显示预览 103.6 fps，
无预览 119.8 fps）；1080 × 1280 时 `set_framesize()` 接受宽度、缓冲区为 4177920 字节，
但单路 RGB565 取帧超时，ISP 停住且 `sensor.stop()` 卡死，需要复位，未能取得压紧后的图像。
旧固件上 1080 宽输出叠加第二路缩放通道或虚拟显示预览时同样停住；只取帧、不写帧的单路
1080 宽 RGB888 多次正常。原因尚未定位（推测为离线 ISP 在 120 fps 下的内存带宽）。

## SC038HGS 兼容配置

旧相机驱动仍可在 menuconfig 中选择：关闭 SC132GS/CSI0，选择默认 CSI2、
CSI2/I2C1、GPIO4 电源、无复位和外部时钟，启用 SC038HGS 后重新编译。
`dart_board` 会根据编译选项提供旧的 `SC038HGS_MODE` 和 CSI2 板级定义。
旧 `camera_sensor()` 的 ISP 路径仍需要仓库没有的 `sc038hgs-640x480` 标定文件；
内部 Pixel CLK 未知时不提供 fps 或曝光微秒换算。新的预览示例用于 SC132GS。
RAW 直采可按 [SC038HGS.md](SC038HGS.md) 构建 `sample_sc038hgs`，绕过 ISP 数据库。

## 验证与文档依据

2026-11-08 原理图复核后：SC132GS 驱动的 96 组寄存器模拟、6 组时钟极性
测试与 2 组用户态模式测试通过（共 104 组），
板级模块的 5 项主机测试通过。官方离线容器完整构建返回 0，最终镜像中的
RISC-V 64 位解释器、A/B 系统镜像、启动脚本、相机示例和两种分辨率的 ISP
配置均与本次构建或源码一致，镜像与 OTA gzip 校验通过。实际编译头文件和
链接后的内核均确认包含时钟翻转配置。
构建日志为 `output/k230d_canmv_dart_v1_defconfig/build-schematic-clock-invert.log`，
镜像验证记录为同目录的 `verify-schematic-clock-invert.log`、`BUILD_INFO.json` 和 `SHA256SUMS`。
尚未烧录新固件；时钟翻转效果、24 MHz 输入、四通道实际出图和真实帧率仍待上板验证。

主机测试命令：

```sh
python3 src/rtsmart/mpp/unit_test/run_sc132gs.py
python3 boards/k230d_rtos_dart_v1/micropython/tests/test_dart_board.py
```

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
