# SC132GS 启动及停止恢复复核（2026-11-09）

> 后续按用户要求，生产相机源码已回到实际连续取帧的 BUFFER_FIXED 基线，
> 只独立保留 devfs 原盘防护。本文的恢复修改及回归是 RECOVERY_FIXED 的历史
> 记录；完整源码/测试已归档在输出目录 `sc132gs-recovery-review/recovery-source/`。
> 当前最小候选见 [基线构建记录](BASELINE_REVIEW.md)。

交接来源：`/tmp/dart-sc132gs-firmware-handoff-20261109.md`，应用任务
`01a1c02b-ba98-7070-ab06-0df783cf5268`。本记录区分已烧录的 BUFFER_FIXED、
主机源码修改和新候选 RECOVERY_FIXED 的验证状态。

## 已烧录 BUFFER_FIXED 的本轮实测

- 冷启动复用读回 GPIO2/34/35 为 2/94/95；`Sensor.run()` 之前芯片 ID 读取失败。
- 同步 UART0 抓到这轮首错：`sensor_reg_list_write err, [0x0103] = [0x01]`，
  随后是 `kd_mpi_sensor_init, error(-1)` 和 VICAP sensor init failed。
  因此 NOTREADY 的本轮前置错误是传感器 I2C 软件复位写入失败，尚未到首帧。
- 原生 GPIO2 输入模式模拟 Pin 析构后，S_POWER(1) 返回 0，但 DDR 仍为输入。
  明确关/开电后 DDR=输出、EN=1，原生中断及直接轮询在 100/400 kHz、0x30..0x33
  仍均得到地址 NACK。不能将全部失败仅归因于缓存。
- 一次有界的 2 秒 EN 断电和 9 个开漏恢复时钟未恢复。
  切为 GPIO 后 SCL/SDA 可回到高电平，GPIO 模拟读取真实 ID 也无 ACK。
  原生寄存器 ioctl 的 errno=1 是通用错误映射，不能解释成主机文件权限故障。
- 用户把 USB、外部供电和 CH340 全部断开、重新连接后，原 BUFFER_FIXED 实际
  启动成功，手动曝光 5000 us、增益 1 倍，640×480 RGB565 连续帧计数达到 5700。
  MCP 返回新 JPEG 帧，预览仍明显偏紫，不能据帧计数认定画质合格。
- IDE 停止后观察到正常 interrupt 与软重启输出，USB 曾短暂不可见，随后可重连。
  我请求的三轮重跑脚本因当时 No board 未执行；随后终端出现应用脚本再次
  NOTREADY 的输出。因此没有把这轮记录标为受控的三轮恢复测试通过。
- 这些出图结果发生在新源码修改尚未上板时，不能归功于 RECOVERY_FIXED。

原生首错存档：输出目录 `sc132gs-recovery-review/buffer-fixed-native-repro.log`。
原生审计工具不读取原盘，不更改启动文件，不保存照片。

## 本次源码修改

1. `sensor_comm.c`：上电请求交给驱动，取消公共 power_flag 的成功短路；
   同模式缓存命中前重新请求驱动核验上电，修复供电时清除的模式状态会触发真正初始化。
   物理关电失败不会被公共层改写成已经关电。
2. `sc132gs.c`：上电缓存核验实际 FPIOA 功能、GPIO DDR 和读回电平；
   运行时地址已绑定后，以真实 0x0100 读取核验通信。无 ACK 会清除初始化缓存并报错，
   没有伪造 ID 或添加无界重试。引脚失配时重新执行驱动电源流程。
   关电时即使停流 I2C 失败，也继续关闭 EN；实际 GPIO 关电失败仍返回错误并保留状态。
3. `media/sensor.py`：native deinit 失败时保留 CSI 所有权及清理标记，允许再次 stop；
   防止新对象复用仍由原对象持有的设备。清理成功后将旧传感器 fd 设为 -1。
4. 内核 `devfs.c`：针对原始块设备的非零 read/write 字节流返回 ENOSYS，
   零长度请求返回 0，失败不移动文件位置。字符设备继续使用字节计数及部分传输结果。
   挂载 FAT 使用原生扇区 API，不经过这一不安全的 devfs 字节流接口。

保留此前 VICAP invalidate/flush、RGB565 缓存归还、WBC 帧所有权修复。
没有把启动恢复和偏紫问题归为同一项。

## P2 原盘诊断的独立缺陷

`dfs_device_fs_read` 原样把 POSIX 字节 count 交给 `rt_device_read`；
`rt_mmcsd_read` 则把 size 当扇区数，并用 `req_size << 9` 推进目标缓冲区。
`sys_read` 只按字节数分配内核临时缓冲。这形成真实的单位不一致和越界写入风险；
write 同样有单位不一致及越界读取风险，文件位置和返回值也不能按字节正确解释。
本次先拒绝该不支持的原始字节流接口，而未增加可写原盘支持。

`MP_HAL_RETRY_SYSCALL` 已经在阻塞 syscall 前释放 GIL，持有 GIL 的猜测已撤回。
没有在板上重跑 `/dev/sd0` 原始读取脚本。

## 主机与构建验证

- 112 组 SC132GS 驱动/MPI 配置通过，增加 EN 方向丢失、缓存上电无 ACK、
  关电失败保留状态的回归。
- 生产 `sensor_priv_ioctl` 的分发测试通过，覆盖 cached-on、同模式缓存重新初始化、
  健康缓存复用、通信失败及关电失败后的重试。
- 6 项生产 Sensor 方法测试通过，包括 deinit 失败保留所有权、部分启动清理失败和
  帧释放失败时阻止 teardown / 复用。
- 生产 devfs read/write 函数边界测试通过：0/1/511/512/513/4096/SIZE_MAX 字节，
  多个对齐/非对齐位置，字符设备部分读写及错误位置保留。没有向真实块设备发出请求。
- 完整离线固件构建退出码 0；实际重新编译 sensor_comm、sc132gs、devfs，
  重新冻结 Sensor，完成内核和解释器链接。
- 从镜像 FAT 分区提取的解释器与新链接产物一致；A/B 内核槽与本次暂存内核一致。

## 候选固件及验收状态

`output/k230d_canmv_dart_v1_defconfig/CanMV_K230D_Dart_V1_SC132GS_RECOVERY_FIXED.img`
是真正未压缩的 640 MiB 镜像。

- 镜像 SHA-256：`6bbafe9007541caad031d7c6c10dc74bb4e56b764cdcbfc35526d4601bf80632`
- 解释器 SHA-256：`81fde4f52b153af160e36214b5426cab51c1ff3fc93da52bce297347f672da2e`
- 记录：`BUILD_INFO_SC132GS_RECOVERY_FIXED.json`
- 构建日志：`build-sc132gs-recovery-fixed.log`

用户已烧录候选并报告完整断电重连、未运行脚本。板端 FAT 解释器 SHA-256 与
候选一致；本轮 UART 出现新驱动的健康核验日志，也证实新内核逻辑在运行。
第 0 轮仍然失败，实际图像帧数为 0：上电核验读取 0x0100 无应答，
`kd_mpi_sensor_power_set` 报错，VICAP 提示 sensor power on failed，Python 为 NOTREADY。
日志见 `sc132gs-recovery-review/recovery-first-cycles-native.log`。

这里的 `powered sensor is not responding` 来自 cached-on 且 GPIO 核验通过的
分支，不代表本次请求已经重新关开 EN。boot probe 先上电、后设置固定地址，
所以 probe 当时不执行地址绑定后的 ACK 检查；明确关开电后失败会打印另一条
`no response after power-on`。MPI 的 `sensor power on failed` 不能区分这三条路径。

同一新固件的独立原生审计已确认两项局部修复生效：模拟 Pin 析构把 GPIO2 改为
输入后，cached-on 请求会把 DDR 恢复为输出；无 ACK 时返回 EIO 而非假成功。
明确关电返回 0，EN 实际为低，不再把传感器停流失败当作 GPIO 关电失败。
100/400 kHz、0x30..0x33 的中断和直接轮询仍均为地址 NACK。
日志为 `sc132gs-recovery-review/recovery-pin-i2c-audit.log`。

为排除只是新增健康读阻挡了原先可用的复位写，另用原生 ioctl 直接写
`0x0103=0x01`，仍失败；读 `0x0100/0x3107/0x3108` 也失败。
日志为 `sc132gs-recovery-review/recovery-startup-probe.log`。诊断退出时经原生
电源接口保留 EN 高，未运行相机脚本。用户确认电源及外部时钟正常；这项确认
与主机可读回的 GPIO 电平分开记录，不将本轮 NACK 直接归因为硬件故障。

随后对照三条路径：A 为 cached-on；B 为明确 EN 关断 5000 ms 后上电、保留
原 I2C 复用；C 为明确 EN 关断 5000 ms、只在关电期间把 GPIO34/35 拉低、
恢复原 I2C 复用后再上电。每条均无 ACK。GPIO 模拟前核验两根线可拉低为 0、
释放为 1，0x30..0x33 仍在首个写地址字节 NACK。没有用延长延时的猜测改固件。
日志为 `sc132gs-recovery-review/recovery-power-matrix.log`，诊断源码同目录存档。

一次正常软件重启捕获的完整日志见 `sc132gs-recovery-review/recovery-reboot-native.log`。
I2C1 先注册，随后 fixed binding 注册 sensor；该日志没有把 fixed binding 的
成功当成真实芯片应答。未在重启中执行原盘诊断。
重启后、未运行相机脚本时，独立原生 ioctl 的 cached-on 读取和直接 0x0103
复位写仍失败，0x3107/0x3108 仍无应答。USB 随后恢复 REPL，可继续访问普通 FAT
日志文件。见 `sc132gs-recovery-review/recovery-reboot-startup-probe.log`。

I2C 复用态 IOMUX 值 0x800009cf 的 bit31 是只读 PAD 输入高，GPIO EXT 在该
复用态读 0 不代表总线低。此时 IE/OE 开启、io_sel=1、3.3 V bank 配置；
不能把 GPIO EXT 的复用行为误判为总线被拉低或 pinmux 再次被覆盖。

另与 BUFFER_FIXED 构建时保存的源码 SHA-256 逐项对照：defconfig、生成配置、
SC132GS 寄存器表/CSI0 模式表、MicroPython FPIOA/I2C 实现及 VICAP/WBC 缓冲区
实现均一致；本次预定变化是 Sensor 清理、SC132GS 电源状态和内核分发/devfs。
对照记录为 `sc132gs-recovery-review/source-comparison-with-buffer.json`。
这只限定软件差异范围，不代替新候选的真实取帧验证。

因此候选的整体启动验收未通过，三轮停止恢复和 IDE 中断恢复也未进入验证。
不能用 BUFFER_FIXED 的 5700 帧代替新固件的首帧，也不能宣称偏紫或局部条带已解决。
应用侧 ROI 颜色取样暂未开始，板卡尚未交还应用 agent。
