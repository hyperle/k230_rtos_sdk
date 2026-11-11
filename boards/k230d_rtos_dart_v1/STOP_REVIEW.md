# SC132GS 已捕获停流故障的单项候选

## 本轮板上证据

已烧录 BASELINE_DEVFS，解释器哈希匹配，内核的四个相关函数与已链接 ELF 字节
一致。初始在表第 22 项 0x3254 失败；后续生产原表 101 项原生写成功，再运行
相机脚本通过三轮各 30 帧与停止，并连续到 3400 帧。见 [基线实测](BASELINE_REVIEW.md)。
没有把这次至少 3490 帧和此前 BUFFER_FIXED 的 5700 帧混算。

IDE 停止时 UART 捕获：sensor stream stop I2C 失败、ISP disconnect 失败、
release reserved VB，随后用户态 `Store/AMO Page Fault`：
`sepc=0x20020cb46, ra=0x2002390d4, stval=0x10`，再完整重启。
精确链接 map 和反汇编定位到 libisp 的 `osTimeStampUs`，调用方是
`CamEngineCtrlVideoInEventCb`。事件的元数据指针为 NULL，偏移 16 后被作为
时间戳输出指针。它证明真实无效写入，尚未证明是哪一次释放造成元数据无效。
崩溃报告过程中另有 `Load Access Fault`，内核 `rt_hw_backtrace` 的
`sepc=0x1dc664, stval=0x8`，打印 recursive crash/halt 后才看到 SPL。
原始用户态异常、报告自身的二次异常和最终复位分开记录；未确认最终复位源。
IDE STOP 生产路径本身请求的是软重置，不能把完整 SPL 启动归为正常软重置。

## 唯一相机修改

仅修改 `sc132gs_stream`，其余取帧基线及 devfs 防护保持不变：

- 普通上电、probe、初始化表、正常启停均沿用原流程。
- I2C 停流失败且配置了实际 EN 时，把 EN 驱动到关电电平、读回核验，并等候
  原有 5 ms 复位间隔；只有确认物理关电控制成功，才将这次停流视为成功。
- 关电后清除电源/初始化/模式/测试图状态，使下一次启动执行完整初始化。
- 没有 EN、GPIO 调用失败或电平读回不符时保留错误；已关电的停流幂等返回 0。

这避免仅因停流寄存器失联而让传感器继续输出，同时让 ISP 获得真实物理停流的
成功结果。它是针对已捕获错误路径的候选，**不等同于已经证明消除异步回调崩溃**。
没有修改闭源 ISP 库或用空指针检查掩盖生命周期问题。

## 独立清理风险及验收条件

上层任务只读审查后，本任务复读生产源码，确认仍有下列错误处理边界：

- `Sensor.stop` 保存 stop 错误后仍调用 native deinit，随后清除 Python 设备状态。
- `vb_mgmt_deinit` 忽略 stop 结果并继续 deinit；deinit 失败也清除 native 登记。
- `py_media_vbmgmt_deinit` 忽略上述结果，继续销毁池和退出 VB。

这些位置有风险，当前证据尚不能把某次指定释放认定为元数据 NULL 的来源。
仅阻止 Python 的后续调用不能覆盖 native 全局清理，也不能阻止 MPI deinit 内部
可能先做的释放。STOP_SAFE 不混入这一层面的所有权重构。

实板验收必须同时确认真实取帧、ISP stop/disconnect 成功、无 NULL 元数据回调、
无二次崩溃或整机复位，以及 IDE 停止后重跑首帧。只看到 EN 关电确认日志、
设备节点存在或接口返回成功，不足以判定恢复通过。

## 验证与交付

112 组实际驱动/MPI 配置主机测试通过，新增停流 NACK、无 EN、GPIO 失败、读回
失败、关电确认、再次 stop 与完整重启初始化的回归。现有 `fail_addr` 同时影响
寄存器读写，0x0100 停流注入首先在 update_bits 的读阶段失败；`fail_gpio` 同时
影响 mode/write，当前首先命中 mode 失败。因此未独立覆盖寄存器写阶段或 GPIO
写阶段的失败，不能把 112 组结果表述为各写阶段均经过独立注入。

3 项 Sensor 帧释放和生产 devfs 边界测试通过。构建退出码 0；源码哈希确认
只有 SC132GS 驱动不同于基线，
链接内核含新增停流日志、devfs 防护，镜像解释器和 A/B 内核与新产物匹配。

- 文件：`CanMV_K230D_Dart_V1_SC132GS_STOP_SAFE.img`
- 大小：671088640 字节，真正未压缩 `.img`。
- 镜像 SHA-256：`e1719ad01a32a53ad084da2eeed21ac7413d7a6864883f7bd287e493b963ac8a`
- 解释器 SHA-256：`ddd35a7a619e44df96785da33a1d62e465341d84aa3b29d15c87267e7532f674`
- 记录：`BUILD_INFO_SC132GS_STOP_SAFE.json`、`build-sc132gs-stop-safe.log`。

## STOP_SAFE 本轮实板结果

用户确认已烧录后，从普通 FAT 文件 `/sdcard/micropython` 读取的解释器 SHA-256
与上述新构建匹配。随后只读比较内核中的 `dfs_device_fs_read/write`、
`sc132gs_power`、新增 `sc132gs_stream`、`sensor_reg_list_write` 五个函数，均与
本构建链接 ELF 的实际字节一致。没有读取原盘，也没有用旧基线帧数代替本次证据。

首次启动直接成功，未执行原生寄存器表预写。640×480 RGB565、手动 5000 µs、
模拟增益 1×：三轮各 30 个真实帧后调用严格的 `stop()` 均返回，随后持续至少
3500 帧，IDE 停止前合计至少 **3590 帧**。新鲜 IDE JPEG 帧 id=2345，15160 字节，
只在内存查看；画面仍偏暗、偏紫，不能认定颜色质量或紫色横条问题已解决。

IDE 停止时自然发生 I2C 停流失败，UART 明确打印：
`SC132GS: stream stop I2C failed (1); EN power-off confirmed`。
之后释放 reserved VB 并回到 REPL；该窗口未见旧 `Store/AMO Page Fault`、
recursive crash 或 ISP stop/disconnect 错误日志。没有逐一插桩 ISP 的成功返回码，
不能只依据无错误日志把所有异步回调退出都认定为已验证。

**立即重跑失败**：首个原生错误为
`sensor_reg_list_write err, [0x0103] = [0x01]`，Python 报 VICAP init
`-1609203696`。失败清理后的只读状态为 init/power/mode 全 0、GPIO2 输出低，
因此未发现该时刻仍缓存上电的证据。`last_abort=0x01000001` 来自清理后的最后
事务，不能用于断言第一笔 0x0103 的 abort 类型。

稍后另一次 UART 工具请求在 shell 提示符出现前捕获 DDR/OpenSBI/RT-Thread
完整启动，工具因无提示符而提前终止，**请求的原生 poweron 诊断并未执行**。
用户明确确认期间没有断电、复位或重新烧录。此次重启起点没有连续日志，来源
未明，也不能确定与 ISP 清理或此前只读检查的关系；不能标为无整机重启。
保留证据于 `later-full-boot-exception-evidence.txt`。

后续完整启动后，同样普通相机脚本再次取得至少 **1200 帧**；两次不同会话的
STOP_SAFE 实际帧下限合计 4790，不能把第二次成功作为“IDE 停止立即重跑通过”。
已明确将板卡交还应用任务，由其继续固定 ROI 和 IDE 两点 30 帧 Lab/JSON 验收。
交接时 `/sdcard/sc132gs_after_later_boot.py` 持续预览，固件任务之后不再操作板卡。

实板总验收为 **未通过**。本轮验证了新停流分支实际生效与局部退出改善，仍需
解决 IDE 停止后的首帧恢复和后续整机重启。未保存照片，未在板上重跑原盘诊断。

## 应用窗口及本任务接回后的单次对照

应用任务独占板卡期间，固定 ROI `(288,208,64,64)` 成功采样 30 个真实帧、
122880 像素，Lab 均值 `[36.76667,6.633333,-10.7]`；8 秒共 349 帧、8029 ms，
独立 JSON 已下载并校验。此结果来自应用任务，本任务只读核对并复制其日志与
JSON 至 `sc132gs-stop-review/application-roi/`。应用脚本自然结束后，IDE 两点版
在 sensor.run 报 NOTREADY，0 帧且未创建触控。该次原生首错没有被 UART 覆盖，
不能用此前或以下新的 0x0103 错误替代它的现场证据。`is_del=True` 还会抑制部分
停止错误，应用打印 resources released 不等于 native 停止每一步都成功。

应用任务明确交回后，本任务调用现有驱动的 S_POWER(1)，返回 0；EN 的 DDR、
输出和读回均为 1，GPIO34/35 保持 I2C 复用且 PAD 输入均为高。紧接着只读一次
`0x3107` 失败，**在下一次传感器操作或 cleanup 前**保存
`master.abrt_src=0x01000001`，低位含义为 7 位地址无 ACK。这是本次直接读取
事务的证据，不是模式表 0x0103 的原始 abort。数值来自生产 IRQ 在清除硬件
TX_ABRT 前保存的 master.abrt_src；两消息读不能区分首次写地址或重复起始后的
读地址 NACK。该独立读取的 IC_CON=0x65 确认 7 位模式，但其当时的 TAR 没有
单独采集，后面的 TAR=0x30 快照不能冒充首事务。没有做 GPIO 模拟恢复或写传感器
寄存器。本次不能由 power_flag 缓存跳过或 EN 为输入解释。
见 `after-app-poweron-id-state.log`。

随后执行**一次已明确记录的受控 `msh reboot`**，连续保留 SPL、OpenSBI 和
RT-Thread 启动，区别于先前非人工重启。重连后直接运行已部署的 120 秒 IDE
采样脚本（600 秒窗口建议抵达时该调用已经发出），仍然 0 帧、触控未初始化；
这一次 UART 连续捕获首条错误 `0x0103=0x01`，见
`controlled-reboot-native.log`、`controlled-boot-ide-window-native.log` 及
`controlled-boot-ide-python.log`。不能声称整机软件重启已恢复此次故障。
启动日志还有 eMMC HS200 tuning 失败后降级至 52 MHz 的独立观察，随后解释器
普通 FAT 哈希仍匹配；尚无证据把该存储观察归为相机 NACK 根因。

最后只读核对运行态地址，防止仅看编译配置：sensor i2c_bus=0x45cad0 与内核
I2C1 实例一致，slave_addr=0x30，寄存器地址/数据宽度分别为 2/1 字节，硬件
IC_TAR=0x30、TX/RX FIFO 均空。五个内核函数字节仍与 STOP_SAFE 一致。此快照
在失败清理后取得（EN 输出低、三标志为 0），其最后 abort 不能冒充首错。
见 `after-controlled-boot-target-state.log`。

本次对照没有新增生产代码或另一份固件镜像，未重复电源矩阵、未读原盘、未保存
照片。当前固件任务持有板卡，相机未运行；固定选区已验证，IDE 两点仍待真实帧，
根因及整体恢复仍未解决，不能仅凭主机测试或接口返回值宣称修复完成。

后续重新插接、数字 PAD、引脚/时序对照和日志新鲜度更正见
[STABILITY_REVIEW.md](STABILITY_REVIEW.md)。已确认的 I2C 中止位定义修正仅在
源码中，尚未包含在本文 STOP_SAFE 镜像内；后续真实取帧验收仍为未通过。
