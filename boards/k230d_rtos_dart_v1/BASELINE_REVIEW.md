# BUFFER_FIXED 相机基线与独立 devfs 防护

用户要求回到实际取帧的版本做优化。本轮以实际连续取到 5700 帧的
`CanMV_K230D_Dart_V1_SC132GS_BUFFER_FIXED.img` 的源码快照为基线，
没有用 vendor Git HEAD 代替成功会话版本。

## 基线一致性

按 `BUILD_INFO_SC132GS_BUFFER_FIXED.json` 记录的 SHA-256 核对，11 项全部一致：
生成配置、defconfig、VICAP 缓冲区 C/头文件、WBC、Sensor Python、SC132GS 驱动、
寄存器表、CSI0 模式表、MicroPython FPIOA/I2C。`sensor_comm.c` 从本次恢复修改前
的独立快照恢复，SHA-256 为
`7e3da36d3a64457c57d2834b151f2bd302e1a6b4f3530c4eb8fc9f4400966d21`。

相机仍为 CSI0 四通道、外部 24 MHz、I2C1 GPIO34/35、GPIO2 高有效 EN，
时钟反向关闭，保留已有 VICAP/WBC 缓存与帧所有权修复。

## 只增加的运行时变更

保留内核 devfs 原盘字节接口防护：原始块设备非零 read/write 返回 ENOSYS，
零长度返回 0，失败不移动位置；字符设备仍按字节处理。挂载 FAT 不走这个接口。
这项源代码已确认的越界风险单列处理，没有重跑实板原盘诊断。

RECOVERY_FIXED 新增的相机电源/ACK/缓存核验及 Python stop 所有权修改已撤下。
它们的完整生产源码、回归测试和构建记录保存到
`output/k230d_canmv_dart_v1_defconfig/sc132gs-recovery-review/recovery-source/`，
实际失败证据见 [RECOVERY 记录](RECOVERY_REVIEW.md)。撤下不代表已经定位 NACK 根因。

## 验证与交付状态

112 组实际驱动/MPI 源码主机测试、3 项实际 Sensor 帧释放方法测试、生产 devfs
read/write 边界测试通过。这些测试不等同于新候选已经取到帧。

候选名称为 `CanMV_K230D_Dart_V1_SC132GS_BASELINE_DEVFS.img`，交付真正未压缩的
`.img`。构建/镜像核验结果记录在输出目录
`BUILD_INFO_SC132GS_BASELINE_DEVFS.json`、`build-sc132gs-baseline-devfs.log`。
完整离线构建退出码 0，实际重新编译两份还原的相机 C 源码、重新冻结 Sensor，
完成内核和解释器链接；devfs 复用未改变的已编译防护对象，实际链接内核反汇编
确认 read/write 的块设备分支返回 0/ENOSYS、只有正返回才推进位置。
镜像 MBR/FAT 结构有效，解释器与新链接产物一致，A/B 内核均匹配本次暂存内核。

- 大小：671088640 字节（640 MiB），真正未压缩。
- 镜像 SHA-256：`c3296c335d8df5497855b925adef6f920e5a7cea76a88ad419a1377006b4ee4e`
- 解释器 SHA-256：`48ffbafbf929ffde1617bea842ca49638e03a9c7cfdb9f75cbf3fe5260d8a80e`
- 用户随后烧录并重连，板端解释器哈希匹配。只读比对运行内核的 devfs read/write、
  sc132gs_power、sensor_reg_list_write 四个函数代码全部与本候选匹配。

本轮初始 cycle 0 在模式 0（全幅 1080×1280/120fps）表的第 22 项
`0x3254=0x05` 失败，前 21 项写接口返回成功。单独设为 100 kHz 后一次重跑
仍在首个 `0x0103` 失败。清理后的底层状态保存了地址 NACK；它不是首次
0x3254 失败的中止原因证据。

后续取证时总线已为 400 kHz、EN 输出高，两个 RAM 映射均见驱动初始化状态；
这一状态变化的来源没有完整捕获，不把它归功于只读诊断。原生工具直接包含生产
模式 0 表，无关/上电和 cleanup，101 次真实 ioctl 写均成功，总计 11065 us、
abort=0；逐次 ioctl 间隔与生产内核表循环不同，不能据此断言时序根因。

随后原相机脚本连续通过三轮 `run → 30 帧 → stop`，并继续取得 3400 帧，
共至少 3490 张真实 640×480 RGB565。MCP 新 JPEG 预览已查看，画面较暗、偏紫，
没有保存照片。实际日志为 `three-cycles-and-steady-python.log`。

IDE 停止后的重跑验收失败：先有传感器 stream stop I2C 失败，随后 ISP disconnect
失败、release reserved VB；`ide-stop-restart-native.log` 捕获用户态 Store Page Fault：
`sepc=0x20020cb46, ra=0x2002390d4, stval=0x10`，随后完整 U-Boot/OpenSBI 重启。
精确链接 map/反汇编将 PC 定位到 libisp 的 `osTimeStampUs`，caller 为
`CamEngineCtrlVideoInEventCb`。回调从事件元数据指针取到 NULL，加 16 后作为
时间戳输出指针，导致无效写入。缺少元数据的确切生命周期仍需定位；没有仅凭
该调用栈就声称已经证明某个指定缓冲区被释放。整机重启后相机又在 0x0103 失败。
报告原始用户态异常时又触发 `rt_hw_backtrace` 的二次 Load Access Fault
（`sepc=0x1dc664, stval=0x8`），recursive crash/halt 后出现 SPL；最终复位源
未确认，未将其认定为 IDE STOP 本身直接请求硬复位。

因此，真实帧与同解释器 stop/run 已有证据，IDE 停止恢复未通过，整体仍未验收。
当前没有持续预览，也尚未交还应用侧 ROI 颜色采样。所有日志/取证源码及 map
均保存到 `output/k230d_canmv_dart_v1_defconfig/sc132gs-baseline-review/`。

基线虽然有成功帧证据，也出现过间歇 I2C NACK。该缺陷仍未定位，停止恢复、
IDE 停止/软重启、偏紫/局部条带和颜色采样均需要独立验收。
