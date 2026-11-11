# SC132GS INIT_STATE V2（2026-11-10）

本候选修正 INIT_STATE 实板暴露的主线程死锁。旧 INIT_STATE 已撤回，不能继续使用。
V2 完成源码、主机回归和镜像内容校验。随后实板确认 IDE 死锁消失，但首帧和
清理后重跑均失败，真实传感器帧数为 0；稳定取帧仍未解决，本候选未通过验收。

交付为未压缩的 640 MiB MBR 镜像：
`output/k230d_canmv_dart_v1_defconfig/CanMV_K230D_Dart_V1_SC132GS_INIT_STATE_V2.img`。

- 镜像 SHA-256：`891c400d16849e3467a8f4a29c91ff100da3acc19945629e7b7b36298a867054`
- 解释器 SHA-256：`058a179c585a3d541e8eb53be9adfbd05661bb8a0230a971c4116c25f460db88`

## 实板发现与修正

用户烧录 INIT_STATE 并冷上电后，只读原生探针确认八项内核函数指纹与交付一致。
随后 IDE 首次执行身份核对脚本即卡住，尚未访问相机。原生只读报告中的 I2C
消息计数为 0；因此这次卡住不能解释为传感器首帧或 I2C 初始化失败。

`mp_thread_shutdown_workers()` 的实际实现会在返回前获取 GIL；INIT_STATE 新增的
VM 清理保护又获取了一次非递归 GIL，造成死锁。V2 删除保护前的重复获取和保护
后的提前释放，保留线程退出函数到 `mp_deinit()` 的原有锁交接。

相对于 INIT_STATE，运行时代码只改 `src/canmv/port/core/main.c`。相机电源缓存
校验、初始化失败关电回滚、严格停止/重试、CSI 槽位所有权和 VM 清理保护继续保留，
详见已标注撤回的 `INIT_STATE_REVIEW.md`。SC132GS 寄存器表、四线逻辑 CSI0、
外部 24 MHz、GPIO2 EN、I2C1 GPIO34/35 和地址 0x30 沿用原配置。
VICAP/WBC/Display 缓冲区防护和连续彩色示例也保持原样；不自动保存照片。

## 回归与构建

原 VM 单元测试没有执行实际线程退出函数，错误假定其返回时不持 GIL。扩展测试
同时提取并编译生产 `mp_thread_shutdown_workers()` 和 main 清理片段，模拟非递归
GIL，覆盖有/无工作线程和清理成功/失败四种交接。撤回版本复现重复获取断言，
V2 四种均通过；原五种模块/清理异常场景也通过。

- 23 个 CTest 回归目标全部通过，包含原缓冲区防护与新增生命周期、软重启测试。
- 113 组驱动/MPI/分发测试和 19 项 Sensor 生命周期测试继续通过；这些生产源码
  与 V1 哈希一致，复用对应日志，未将复用结果描述成 V2 实板证据。
- 40 项构建输入在构建前后哈希一致。离线 Docker 构建退出码 0，日志确认
  `CC core/main.c`；未改的冻结 Sensor mpy 与 frozen C 均与 V1 归档逐字节一致。
- 交付镜像 MBR/分区、FAT 内解释器与链接/暂存文件一致性、ISP 文件、A/B 内核
  以及实际解包后的内核 CRC/长度、ELF 节到 raw kernel 一致性检查通过。
- SDK 仍有既存 U-Boot 声明/指针转换、内核 RWX 警告；元数据 `repo info` 不可用
  时回退为 local，禁用 fastboot 时生成占位 RT-App。完整构建及打包退出码为 0。

本轮日志、前后源码、构建和校验脚本、内核及冻结模块位于
`output/k230d_canmv_dart_v1_defconfig/sc132gs-init-state-v2-review/`；清单位于
`BUILD_INFO_SC132GS_INIT_STATE_V2.json`。

## 随后的实板结果

用户烧录 V2 并完整断电上电，未自行运行相机脚本。主机确认无脚本运行，身份
脚本正常执行结束，板端解释器 SHA-256 与本镜像一致，八项只读内核指纹全部匹配。
首个相机请求前 `msgs=0`，EN 为输出且读回高，I2C1 GPIO34/35 复用有效。

首轮 `validate_cycles.py` 在 cycle 0 的 `sensor.run()` 失败。连续 UART 记录中最早
错误为 `sensor_reg_list_write err, [0x0103] = [0x01]`，随后为
`SC132GS: init failed (-1); power-off cleanup=0, power_flag=0`，最后才是 VICAP
NOTREADY。Python 保留原错误，严格清理返回成功，未进入 `snapshot()`。

随后仅运行只读探针，未插入 I2C 查询或复位事务：

- `msgs=1`，`last_abort=0x01000001`，`last_err=1`：首笔事务为 7 位地址无应答。
- `I2C_TAR=0x30`，`IC_CON=0x65`，TX/RX FIFO 均空。
- `power=0 / init=0 / mode_init=0`；GPIO2 仍为输出，输出和读回均低，与失败关电
  日志一致。本轮没有用停止后的 ID 查询倒推首次启动原因。

再通过 IDE 正常执行同一脚本一次，软重启和脚本执行正常，但仍在首个
`0x0103=0x01` 失败，清理成功，未取得帧。没有另读这一次的 abort，不能把首轮
数值冒充第二轮现场。UART 中 WBC 的 `24 pictures encoded` 是虚拟显示编码器
的统计，不能计为传感器取帧。当前板卡脚本已结束，未请求再次断电或重新烧录。

原始证据为本目录对应的输出归档中的 `board-identity.log`、
`installed-kernel-readonly.log`、`board-first-start.log`、
`after-first-failure-readonly.log`、`board-retry.log`、`board-first-native.log` 和
`board-retry-native.log`。日志与清单保留了脚本、内核、镜像的对应关系。

当前最早障碍位于控制总线地址阶段，尚未进入 MIPI/ISP 图像数据接收，不能据此
修改曝光、增益或认定紫条修复通过。现有信息不能把原因唯一归于传感器、TXS0102、
相机侧电源、外部时钟或尚未发现的固件影响；历史参数矩阵未再重复。

## 仍未完成的验收

安装身份与 IDE 执行已验证，但首帧失败，无法继续至少 600 秒连续真实帧、
正常停止立即重跑、IDE 停止立即重跑的验收。
启动失败时保留第一条 UART 原生错误，不能把停止后读 ID 的结果倒推为启动原因。
紫色条带需单独观察，不因主机缓冲区测试通过而认定已消失。

此修正不证明物理 I2C 无 ACK 已恢复；闭源 MPI/ISP 内部回调生命周期仍有验证边界。
VM 保护覆盖已加载 `media.sensor` 管理的对象，直接使用原始 MPI 的资源仍走原有
全局清理；多相机启动辅助方法未重构。清理失败返回 REPL 时，部分外围服务已经
停止，主要用途是保留所有者并允许重试清理。
