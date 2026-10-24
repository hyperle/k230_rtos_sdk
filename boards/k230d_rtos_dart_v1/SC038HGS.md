# SC038HGS 驱动与本地资料核对

实现与核对日期：2026-10-18。来源为用户提供的两份文件，未将原始 PDF 复制进仓库：

- `3707708896SC038HGS_数据手册_V1.1.pdf`：45 页，版本日期 2024-03-22。
- `3707742639SC038HGS摄像头 80度 仁恒 2025年8月27日.pdf`：1 页，模组接口和结构尺寸图，无电路原理图或寄存器初始化表。

## 手册已提供的信息

| 项目 | 已确认内容 | 手册页码 |
| --- | --- | --- |
| 接口能力 | MIPI D-PHY，1 clock lane + 1 data lane；最高 640 × 480 @ 240 fps、10-bit | 2、13–16 |
| 外部时钟 | EXTCLK 允许 6–27 MHz，因此外部 24 MHz 在输入频率范围内；仍需匹配的 PLL 初始化值 | 18、35 |
| 时钟占空比 | 45%–55%，高、低电平脉宽各至少 5 ns | 35 |
| I2C 地址 | SID0/SID1 均低时，7 位地址为 `0x30`；其余组合为 `0x31`–`0x33`，模组绑位需确认 | 10 |
| 芯片 ID | `0x3107 = 0x1A`、`0x3108 = 0x79`，组合 ID 为 `0x1A79` | 13 |
| 硬件关断/复位 | 芯片信号名 XSHUTDN，低有效；拉低时不能读写 I2C。模组接口将控制脚标为 PWDN | 10、37 |
| 上电时序 | DOVDD → DVDD → AVDD → 释放 XSHUTDN；相邻电源间隔允许为零。释放后等待至少 4 ms，再进行 I2C 初始化 | 8 |
| 软件睡眠/输出控制 | `0x2100[0] = 0` 进入睡眠、停止输出并保留寄存器；置 1 退出睡眠 | 10 |
| 软件复位 | `0x2103[0] = 1` | 10 |
| 外部触发控制 | `0x3222[0]` 控制连续触发模式，`0x3225[2]` 控制单帧触发；自由运行初始化需关闭外部触发模式 | 20–21 |
| 曝光/增益 | 曝光 `0x3E00`–`0x3E02`，模拟增益 `0x3E08/09`，数字增益 `0x3E06/07`，附增益表 | 23–28 |
| 输出窗口/帧率 | 窗口 `0x3208`–`0x320B`，HTS `0x320C/D`，VTS `0x320E/F`；fps = Pixel CLK / (HTS × VTS) | 32–33 |

驱动按上述寄存器实现复位默认工作模式：640 × 480、RAW10、MIPI 单数据通道、关闭外部触发。初始化保留芯片复位后的 PLL 和模拟电路设置，并显式设置 `0x4B01[3]=0` 选择 MIPI（第 17 页），以及窗口、HTS=750、VTS=600、初始曝光 128 行和 1 倍增益。软件复位/睡眠使用 `0x2103/0x2100`，不使用其他型号的 `0x0103/0x0100`。初始化逐项读回已写入字段及窗口、曝光、增益寄存器；不一致时保持停流并返回错误。

GPIO2 固定低电平配合 `0x3222[0]=0`、`0x3225[2]=0`、`0x3282[3]=0` 关闭外部触发。驱动在 I2C 探测前拉高 GPIO4/PWDN 并等待 20 ms，扫描 `0x30`–`0x33`，核对 ID `0x1A79`。多字节曝光、增益、帧长更新使用 `0x3800` 的 Group 0，写入失败不提交该组、不更新缓存，返回错误并要求重新初始化。

`sensor_get_exp_parm` / `sensor_set_exp_parm` 已接入：`exp_time[0]` 单位为秒，`gain[0]` 表示总增益。设置时优先使用模拟增益，再补数字增益，严格向下量化到手册中的合法表项。曝光与两类增益的 7 个寄存器一次打包提交，未超过默认 Group 0 的 8 个寄存器容量（第 29 页）；提交成功后才更新缓存。开始打包、写入、结束打包或激活任一步失败均要求重新初始化。

实现位于 `src/rtsmart/mpp/kernel/sensor/src/sc038hgs/`；MPP 构建、探测注册、模式枚举和用户态模式映射均已接入。板级 defconfig 默认启用该驱动，不改变其他板的选择。

## 模组 24 针接口

下表直接转录模组图的接口编号；连接器朝向以原图的 TOP/BOTTOM VIEW 和 01/24 标记为准。

| 模组针脚 | 信号 | Dart 板已确认用途 |
| --- | --- | --- |
| 1 | LED_STROBE | 未提供连接信息 |
| 2 | TRIG | GPIO2，计划自由运行 |
| 3 | PWDN | GPIO4，低关断、高使能 |
| 4 | NC | 不使用 |
| 5 / 6 | SDA / SCL | I2C1：GPIO35 / GPIO34 |
| 7 / 8 | GND | 地 |
| 9 / 10 | MIPI_RDN0 / MIPI_RDP0 | 一组数据差分对，板端为 CSI_D4 |
| 11–16 | NC | 不使用 |
| 17 | GND | 地 |
| 18 / 19 | MIPI_RCN / MIPI_RCP | 时钟差分对，板端为 CSI_CLK2 |
| 20 | GND | 地 |
| 21 | MCLK | 由板上外部 24 MHz 有源晶振供给，不使用 K230 的 MCLK 输出 |
| 22 | DOVDD 1.8V | 模组图规定的 I/O 电源 |
| 23 | DVDD 1.2V | 需核实实际供电，见下文标注矛盾 |
| 24 | AVDD 2.8V | 模拟电源 |

## 已发现的电气问题

1. 模组图标明 DOVDD 为 **1.8 V**。芯片手册第 34 页的 I/O 输入绝对最大值是 `DOVDD + 0.3 V`，在 1.8 V 供电下为 **2.1 V**；这不是推荐工作电平。当前 Bank0、Bank2 配置为 3.3 V，必须确认 TRIG、PWDN、SCL、SDA 的电平转换和 I2C 上拉电源。外部 24 MHz 晶振的输出电平也应匹配传感器输入。
2. 模组图接口表第 23 针写 **DVDD 1.2 V**，底部参数表却写 **DVDD 1.5 V**。芯片手册要求 1.14–1.26 V，绝对最大值为 1.4 V，因此不能按底部 1.5 V 标注供电，应向模组厂核实。
3. 这张模组图没有电路原理图，无法据此确认模组/主板是否包含电平转换，也不能确认 SID0/SID1 的实际绑位。不能据此直接修改共用 Bank 的电压。

## 模式时钟和使用范围

手册第 18 页给出 PLL 结构和频率范围，未给出分频寄存器地址、分频系数或复位后的 Pixel CLK。因此这里实现的是保留 PLL 默认值的初始化路径，不能据此证明默认帧率为 240 fps，亦不能保证默认模拟设置即可采图。

`MPP_SC038HGS_PIXEL_CLOCK_HZ` 默认 `0`，表示未知内部像素时钟。此时模式报告 fps=0，读取/设置 fps、以秒为单位的曝光、曝光微秒范围返回不支持错误；寄存器访问、输出启停、增益、镜像和测试图仍可用。可通过 SDK 的原始寄存器接口写曝光行数，但不能把未知行时间换算成微秒。

确认 Pixel CLK 后填写该配置，驱动使用 `fps = Pixel CLK / (HTS × VTS)` 和 `line_time = HTS / Pixel CLK` 进行换算。若实测默认帧率为 F，且读回 HTS=750、VTS=600，则 Pixel CLK = F × 750 × 600。24 MHz 是外部输入，不能直接作为此配置的依据。测试中的 108 MHz 只是用于验证算术的夹具值。

- 曝光：`0x3E00[3:0]/01/02`，单位 1/16 行；保守最短 4 行，最长 VTS−11 行。
- 模拟增益：严格使用第 24–26 页表中的合法组合，1–39.375 倍；包含 2 倍到 2.5 倍之间的编码切换。
- 数字增益：严格使用第 27–28 页表中的组合，1–7.875 倍。
- fps API 和 `k_sensor_mode.fps` 使用 milli-fps，例如 30000 表示 30 fps；用户态 `k_vicap_sensor_info.fps` 使用 fps。
- 调低帧长时，将曝光限制到新 VTS−11，曝光与 VTS 在同一组中提交。
- 渐变测试图：`enable=1, pattern=0`；正常画面：`enable=0, pattern=0`。
- `MPP_SC038HGS_MONO` 默认开启；彩色版本关闭该选项，镜像/翻转同步调整 Bayer 格式。
- `MPP_SC038HGS_PHY_CLOCK` 选择 K230 接收器档位，默认 1（800M），并不表示已获知传感器实际 lane rate。

用户态模式的 ISP 数据库名为 `sc038hgs-640x480`。会加载 ISP 数据库的 VICAP 应用需要对应的 XML/JSON 或二进制配置。新增的 `sample_sc038hgs` 使用 `VICAP_WORK_ONLY_MCM_MODE`，通过 MIPI → MCM → RAW 内存采集，跳过 ISP 与数据库加载；这条路径与传感器 ioctl 控制均不需要 ISP 标定文件。

## 验证和上板检查

交叉编译命令（将工具链目录设为本机位置）：

```sh
make -C src/rtsmart/mpp/kernel/sensor all SDK_TOOLCHAIN_DIR=/path/to/toolchains
make -C src/rtsmart/mpp/userapps/src/sensor all SDK_TOOLCHAIN_DIR=/path/to/toolchains
SDK_TOOLCHAIN_DIR=/path/to/toolchains python3 src/rtsmart/mpp/unit_test/run_sc038hgs.py
```

主机测试直接编译真实驱动、使用 SDK 的真实结构体，模拟 I2C/GPIO；覆盖四个 SID 地址、低有效 PWDN 与 I2C 等待时序、全部增益表项、共享寄存器保留位、渐变图、模式索引、未知时钟错误、1/16 行曝光、高位曝光、VTS/曝光联动、曝光和总增益统一提交、Group Hold 各阶段失败、MIPI 选择写入未生效、停流后的接口状态和重新初始化。测试矩阵为未知/108 MHz 夹具时钟 × 黑白/彩色 × CSI0/1/2，共 12 组，本次全部通过。内核传感器库、用户态传感器库和 RAW 示例交叉编译通过；示例启用 `-Wall -Wextra -Werror`。

### RAW 直采程序

在本机直接编译（无需打开整套 examples）：

```sh
make -C src/rtsmart/examples/mpp/sample_sc038hgs all \
  SDK_TOOLCHAIN_DIR=/mnt/mydata/SelfGuidingDart/third_party/toolchain
```

生成 `src/rtsmart/examples/elf/mpp/sample_sc038hgs.elf`。也可在 menuconfig 中启用 `RTSMART_MPP_ENABLE_SAMPLE_SC038HGS` 随 examples 构建。板上需运行包含本次驱动更新的 RT-Smart 固件，再将 ELF 放到 `/sdcard/app/`，分别采测试图和真实图：

```sh
/sdcard/app/sample_sc038hgs.elf 2 1 120 /sdcard/sc038hgs-gradient.raw
/sdcard/app/sample_sc038hgs.elf 2 0 120 /sdcard/sc038hgs-image.raw
```

参数为 CSI 编号、渐变图开关、帧数、可选保存路径；默认 `2 1 120`。只保存最后一帧，640 × 480，RAW10 在 16 位字中存放，文件大小 614400 字节。程序校验返回的像素格式、尺寸和行跨度后保存，并输出 PTS、time_ref、到帧速度及像素时钟估计。时钟估计来自主机侧到帧间隔，受调度、缓冲和丢帧影响，应增加帧数重复测量并核对帧号连续性后再用于时钟配置。

SDK 的 ONLY_MCM 初始化分支会关闭传感器电源并跳过 `sensor_init`，因此示例在接收器初始化完成后显式上电、初始化传感器、设置测试图，再启动接收与传感器输出；退出时停流、恢复正常图像、关闭传感器并释放 VICAP/VB。未在物理模组上运行本次 ELF，不能将交叉编译或模拟测试视为已经采图成功。

上板后先确认启动日志 `SC038HGS: id=0x1a79` 与 `find sensor(sc038hgs_csi2) on csi2`。使用 `SC038HGS_MIPI_CSI2_640X480_10BIT_LINEAR` 初始化，先启用渐变图检查 CSI 接收，再关闭测试图检查真实画面；记录帧间隔并核对 HTS/VTS。默认 PLL 输出、模拟电路和接收器档位的可用性仍以这些实测为准。
