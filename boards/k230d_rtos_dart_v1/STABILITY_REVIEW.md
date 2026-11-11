# SC132GS 稳定取帧后续核查（2026-11-10）

稳定取帧仍未修复。本次后续未取得真实帧，重新插接后首个模式表写入
`0x0103=0x01` 失败，Python 在 `Sensor.run()` 报 VICAP NOTREADY。
不能把此前 STOP_SAFE 不同会话中的真实帧数作为本次恢复成功。

## 安装版本与硬件约束

板上仍为 STOP_SAFE；五个内核函数与对应链接 ELF 的字节比较均匹配。
主机镜像再次核对 SHA-256 为
`e1719ad01a32a53ad084da2eeed21ac7413d7a6864883f7bd287e493b963ac8a`。
本轮没有生成或烧录另一份固件。当前源码中新增的 I2C 译码修正尚不在该镜像内。

配置仍是 SC132GS 单 22 针、直接叠插，物理 CSI0/CSI1 数据组合为逻辑 CSI0
四通道，外部 24 MHz，GPIO2 高有效 EN，GPIO34/35 的 I2C1，7 位地址 0x30。
没有改变 MIPI、ISP、缓冲区、电压选择或相机默认时序。

## 日志新鲜度更正

`reseat-cold-setup-native.log` 中设置命令被乱码前缀污染并报
`command not found`。随后下载的固定路径 setup 日志与前一轮完全相同，
SHA-256 为 `51ae234f767621a1ca7f131e1e5926eb1f508435f628c4557845ab5609fca753`。
因此撤回“重新插接后已经确认首事务前为 100 kHz、msgs=0”的表述。
该轮相机初始化首错与 0 帧记录有效，受控时钟设置的新鲜度未确认。

UART helper 原先使用的 Ctrl-U 不是本版 msh 支持的清行操作。现改为分批发送
DEL，检测 `command not found`；新探针用唯一 run ID、`O_EXCL` 新建日志，并
同时核对 UART 和文件中的 BEGIN/END。固定路径旧文件不再作为新轮成功证据。

## 实板对照结果

此前顺序采样 SoC FPIOA 数字输入的记录，解码到地址字节 `0x60`，第九个 SCL
上升沿 SDA 为 1，硬件中止低位为 bit0（7 位地址无应答）。只采集到了地址阶段；
不能说寄存器和数据已经送达传感器。该记录不测模拟边沿、相机侧信号、供电或
外部时钟，也没有采集 STOP 与恢复后的 PAD 电平。

下列三组有效探针都在相应事务前后即时读回参数，且五个内核函数仍匹配：

| run ID | 变化 | 实际结果 |
| --- | --- | --- |
| `20261110_pads_01` | 100 kHz，DS/PU 分别 7/开、7/关、2/关、2/开 | ID 高字节读均失败；中止分别 `01000001`、`01001000`、`01001000`、`01000001` |
| `20261110_timing_01` | 保持 PU 开，SDA_HOLD 为 0、0x10000、0x1001e、0x1001e，滤波为 1、1、1、5 | 四次单消息写均 bit0 地址 NACK；未进一步读 ID |
| `20261110_combined_02` | PU 关、DS7，SDA_HOLD 为 0x10000、0x1001e、0x1001e、0x4001e，滤波为 1、1、5、5 | 四次单消息写均 bit12 仲裁丢失；未进一步读 ID |

单消息对照请求的是 `0x0100=0`，只有地址应答后才可能发送寄存器与数据。
没有调用流启动、重复电源循环或 GPIO 总线恢复。单独或组合设置均未恢复通信，
没有把实验值改成生产默认值。TI 手册说明 TXS0102 内置上拉，额外上拉与负载会
影响电平和边沿；这只支持测试变量的选择，不能据此判定转换器已经损坏。
见 [TI TXS0102 数据手册](https://www.ti.com/lit/ds/symlink/txs0102.pdf)。

`combined_01` 在入口发现 power=0、EN=0，前置检查拒绝执行，没有 CASE/END，
不能计为一次参数对照。与 timing_01 结束时相比，power/EN 从 1 变 0、最后消息数
从 1 变 2；变化起点没有连续日志，来源未确认。用户确认未操作，应用任务也
明确没有上板调用。旧 IDE 缓冲区的额外失败记录没有时间和序号，不能把它们
直接认定为这一间隔中新运行的脚本。

combined_02 在同一原生进程内按需调用 **一次正常 S_POWER(1)** 后完成比较。
该次原始日志继承的 END 尾注写有 `no EN/power/reset write`，其中关于 EN/power
的表述不适用于这一次驱动上电；应以明确的 ONE_NORMAL_POWERON 和前后状态为准。
原始日志未改写。该程序没有在 CASE 之间切换 EN，也没有写软件复位寄存器。

有效探针结束时均恢复原 I2C 复用、MSC=0（3.3 V）、PU 开、DS7、400 kHz、
SDA_HOLD=0、滤波 1。最后 combined_02 结束时 EN 输出/读回为高、相机未初始化。
随后保留的 35 秒 UART 窗口没有额外脚本或重启日志，同期新 IDE 终端缓冲区为空；
这是有限观察窗口，不能保证之后状态不会变化。

## 已确认并修正的源码错误

`drv_i2c_reg.h` 漏掉 bit8 `ABRT_HS_NORSTRT`，后续字段整体少一位，把实际
bit12 仲裁丢失当成了 bit12 的 slave FIFO flush，把 bit11 master disabled
误判成仲裁丢失。已补齐 bit8，并校正 bit9–15，寄存器总宽度仍为 32 位。
生产 `k230_i2c_map_abort()` 主体未改动；修正位定义后才能正确选到它的错误分支。
bit0 地址无应答的定义一直正确，本轮原配置的 NACK 不会因此变成成功。

参考 [Linux 官方 DesignWare 定义](https://github.com/torvalds/linux/blob/master/drivers/i2c/busses/i2c-designware-core.h)
与 [RX 保持时间处理](https://github.com/torvalds/linux/blob/master/drivers/i2c/busses/i2c-designware-common.c)。
RX hold 非零的上游处理在本板只作为对照变量，不能据上游代码就声称能恢复本板。

新增回归编译真实生产寄存器头和实际 `k230_i2c_map_abort()` 函数，检查 16 个
硬件位的双向编码及 7 种错误映射。修正前测试失败，修正后通过。
同一断言集用 RISC-V 工具链交叉编译通过，所有常量断言均被折叠为成功，目标
对象只剩 `puts` 和返回 0；这不是目标板执行或完整固件构建/烧录证据。

```sh
python3 src/rtsmart/rtsmart/kernel/bsp/maix3/drivers/interdrv/i2c/tests/test_abort_decode.py
```

## 当前限制与证据位置

当前已确认的直接阻碍位于传感器控制总线，早于图像采集，单改 VICAP 缓冲区
无法补出缺失的地址应答。现有证据仍不能把根因唯一归到传感器、TXS0102、
相机侧电源或外部时钟，也不能排除尚未发现的固件影响。用户只有这一套设备且
没有示波器/逻辑分析仪；继续区分这些原因需要新的相机侧测量或替换对照证据。

原始 UART/状态日志、实际运行的探针源码/ELF、修正前后头文件、主机测试和
目标对象，以及 SHA-256 清单位于
`output/k230d_canmv_dart_v1_defconfig/sc132gs-stability-review/`。
结构化记录为 `review.json`。没有保存照片、读取原盘或把未验收镜像标作已修复。

## 随后按用户请求构建新版本

上述诊断结束后，用户要求生成最新固件。已将状态位修正编入
`CanMV_K230D_Dart_V1_SC132GS_I2C_ABORT_FIXED.img`，并去掉附带彩色示例的自动存图。
构建、主机回归和镜像内容校验通过，尚未刷机取帧验收；见
[I2C_ABORT_REVIEW.md](I2C_ABORT_REVIEW.md)。本文此前“未生成另一份镜像”描述的是
诊断阶段，不能据新版本的构建成功改变该阶段故障结论。
