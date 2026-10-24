# UART0 启动异常与 USB 文件访问

## 2026-10-21/22 实机恢复结果

已恢复通过 USB/MTP 读写 eMMC 文件，下面的 2026-10-18 内容是早期诊断记录。

UART0 实测确认，板上旧内核在组件初始化期间出现
`<ai_2d> Unhandled Exception 5:Load Access Fault`，尚未进入应用 `main()`
中的 USB 初始化。下载模式可以完整读取 eMMC，不能把这个故障解释为
eMMC 本身无法识别。源码中的 AI2D 日志参数转发修复和 USB 的
`<ioremap.h>` 声明修复已经包含在用于本次恢复的内核中。

本次使用已有的 DART 修复镜像：

```text
output/k230d_rtos_dart_v1_defconfig/sc038hgs-20261018/images/opensbi/opensbi_rtt_system.bin
SHA256: 6e9add71c539508cf204657deb6f5a1f3ba320ecb6633300f24572f91797ec36
```

更新前已通过下载模式备份 eMMC 前 640 MiB，包含启动区、两个系统槽位、
BIN 和应用文件系统。实际分区表中 BIN 从 110 MiB 开始，应用分区从
140 MiB 开始，大小为 500 MiB；当时没有第 3 分区。备份中的应用目录
包含 examples/libs/res 等旧镜像文件，没有 `app/launcher` 或摄像头诊断探针。
其他工作目录中的部署记录不能代替当前接入板子的实机检查。

仅替换了 A 系统槽位（偏移 10 MiB）中的内核镜像，保留 B 槽及文件分区。
写入后完整回读前 110 MiB，与预期修复镜像逐字节一致。
第一次正常启动时，固件原有的自动分区逻辑在 1 GiB 位置新建了 `/data`
分区并重启；这与备份时已有的 BIN、应用分区分开。

恢复后的实测结果：

- UART0 输出 `ai_2d_device_init OK` 和 SDK 版本，随后进入 `msh />`。
- USB0 正常枚举为 `1209:abd1`，主机出现 `/dev/ttyACM0`。
- 文件管理器已挂载 CanMV 的 MTP 存储 `/sdcard`、`/data`。
- 两个存储分别完成 1,048,749 字节测试文件的上传、完整回读、SHA-256
  比对和删除；测试文件均已清理。
- 2026-10-22 重新连接后仍能读取 MTP；已有 `revision.txt` 与修复前
  文件系统备份中的内容完全一致。

验证、UART 日志及回退备份均保存在：

```text
output/k230d_rtos_dart_v1_defconfig/emmc-recovery-20261021/
  uart0.log
  emmc-system-before.bin          # 原前 110 MiB
  emmc-filesystems-before.bin     # 原 110–640 MiB
  emmc-system-repaired.bin        # 保留原系统区其余字节，仅修复 A 内核
  emmc-system-after.bin           # 完整回读，与 repaired 文件一致
  layout.json
  system-verified.json
  mtp-rw-verified.json
  existing-file-preserved.json
```

### 仓库烧录 CLI 的偏移参数问题

本次实测 `tools/k230_flash/bin/k230_flash_cli`（kburn v0.0.8，
2025-11-14 构建）接受 `-a 10485760`，但实际日志和 UART 显示
`Write ... to 0x00000000`。不要用此二进制的 `-a` 选项做分区定点写入。
读操作的 `--read-address` 在本次备份和回读中正确生效。

第一次定点写入因此覆盖了前约 1.5 MiB；已立即用写入前备份恢复前
2 MiB，并逐字节回读验证。最终使用从地址 0 写入的连续 110 MiB
恢复镜像：除 A 内核外，其余字节均来自实机原备份，并已完整回读验证。
这不是可用于其他板子的通用镜像；其中包含本板原始启动数据。

日常复制文件使用 MTP，无需主机出现 `/dev/sdX`。固件仍配置自动执行
`/sdcard/app/launcher &`；当前板上没有该文件，应用自动启动需另行安装
兼容的 RT-Smart ELF。USB 产品名称 CanMV 不表示内核运行 MicroPython。

## 2026-10-18 早期诊断

2026-10-18，实机通过 CH340 连接 UART0，115200、8N1、无流控。

## 实测结果

UART0 启动日志已保存在
`output/k230d_rtos_dart_v1_defconfig/uart0-fix-20261018/uart0-crash.log`。
关键输出：

```text
MMC0: selected timing: HS200 (200MHz), 8-bit, clock=200000000 Hz
Thread Smart Operating System 5.0.0 build Oct 17 2026
Can't find sensor on csi2
[E/pm_core] Can't find regulator_cpu device
<gnne> gnne_device_init done.
<ai_2d> Unhandled Exception 5:Load Access Fault
stval = 0x0000000000000020
sepc  = 0x000000000018df18
current thread: main
```

发送 `help` 后没有得到命令行响应。主机 `lsusb` 只看到 CH340
（`1a86:7523`），未看到固件配置的 USB 设备（`1209:abd1`）；
主机没有新增 USB 块设备。用户确认 CH340 和板上 USB 数据口均已连接。

这证明启动介质至少已经被 SPL 识别，且能读取并运行 RT-Smart，
不能据此判定 eMMC 损坏；也尚未验证 eMMC 文件系统及完整读写能力。

## 本次修复

`drivers/interdrv/gnne/ai2d_dev.c` 的 `ai_2d_log(fmt, ...)`
原来只调用 `rt_kprintf(fmt)`，丢弃所有可变参数。启动时
`ai_2d_info("%s OK\n", __func__)` 会令 `%s` 读取未传入的指针，
与 `<ai_2d>` 后发生非法地址访问的现象吻合。
现改为直接转发全部格式参数。

另外，CherryUSB 的 `port/dwc2/usb_glue_kendryte.c` 调用了
`rt_ioremap` / `rt_iounmap`，却未包含声明头文件。
编译器明确警告隐式声明和整数/指针大小不一致；已补充
`<ioremap.h>`，避免 64 位地址被按 `int` 返回值处理。

注意：板上运行的固件与修复前工作区的 ELF 并非完全一致，
崩溃地址附近的机器码不匹配，因此不能把当前 ELF 的
`addr2line` 结果当成板上固件的精确源码行号。
上述格式参数错误可直接从源码确认，修复效果仍需烧录后实测。

RT-Thread 的组件初始化发生在应用 `main()` 之前，AI 模块在此期间初始化。
本固件的 USB 枚举在应用 `main()` 中启动；这次崩溃会阻断该流程。

## 为什么 eMMC 没有成为 U 盘

当前实际配置是 `CHERRY_USB_DEVICE_FUNC_CDC_MTP`：

| 通道 | 正常用途 |
| --- | --- |
| CH340 / `/dev/ttyUSB0` | UART0 启动日志、RT-Smart 命令行 |
| 板上 USB0 的 CDC | USB 虚拟串口，通常出现 `/dev/ttyACM*` |
| 同一个 USB0 的 MTP | 通过主机文件管理器访问 `/sdcard` 和 `/data` |

这里的 `/sdcard` 是 eMMC 文件系统挂载点，不表示一定插了外置 SD 卡。
MTP 提供文件传输，不导出原始 eMMC 块设备，所以正常工作时也不会出现
供 `mount /dev/sdX` 使用的新硬盘。Kconfig 中 MTP 会选择 MSC 代码依赖；
这并不表示当前 USB 描述符提供了 U 盘接口。
当前 U-Boot 也未启用 USB gadget / UMS，不能直接套用 `ums 0 mmc 0`。

若确实需要主机把 eMMC 当普通磁盘访问，需要另行实现 USB MSC/UMS
维护模式，明确导出的分区和读写权限，并在导出前处理板端文件系统的
卸载及同步。仅给主机只读权限，也不能保证板端同时写入时的文件系统
视图一致。日常复制应用文件可以使用现有 MTP 功能。

## 修复镜像与使用步骤

修复镜像：

```text
output/k230d_rtos_dart_v1_defconfig/uart0-fix-20261018/RTOS_K230D_Dart_V1_rtsmart_uart0_fix_20261018.img
```

完整编译日志：

```text
output/k230d_rtos_dart_v1_defconfig/firmware-uart0-fix-build.log
```

原 RT-Smart 镜像和 ELF 的备份在：

```text
output/k230d_rtos_dart_v1_defconfig/uart0-fix-20261018/before/
```

验证结果：完整离线 `make` 返回 0；日志宏的字符串、整数、函数名和行号
参数转发检查通过；原函数通过 `-Werror=format-security` 可复现编译诊断；
生成的 `.img` 与 `.img.gz` 校验和均通过；USB 的隐式声明和指针截断警告
已消除。SDK 其他位置仍有原有编译警告，不能等同于无警告构建。
独立命名的修复 `.img` 的 SHA-256 为：

```text
58e2fcc028bdceeed30bc65831bcd6f4027c20c9b0eba3c09416a436d42981cd
```

本次只生成了修复镜像，没有执行实机烧录。完整 `.img` 从 eMMC 偏移 0
写入会覆盖分区及其内容，应先确认数据可以覆盖或已有备份。
烧录方式参考[官方烧录文档](https://www.kendryte.com/k230_rtos/zh/main/userguide/how_to_flash.html)。
当前串口命令行已经崩溃，无法依靠 `reboot_to_upgrade` 进入烧录模式；
需要按本板的 BOOT/复位电路切换到 USB 下载模式。确认烧录工具识别到
K230 USB Boot 设备后，选择 **EMMC**，使用上面的 RT-Smart 镜像。

烧录后先观察 UART0：

```sh
minicom -D /dev/ttyUSB0 -b 115200 -8 -C uart0-after-fix.log
```

应确认不再出现 AI2D 异常、出现 SDK 版本输出及 `msh />` 提示符，
然后在板端检查：

```text
help
list_device
ls /sdcard
ls /sdcard/app
ls /data
```

在主机检查 `lsusb` 是否出现 `1209:abd1`、是否出现 `/dev/ttyACM*`，
再打开文件管理器中的 CanMV/MTP 设备。固件的 USB 产品字符串仍为
CanMV，但本板配置已禁用 MicroPython；应用是 RT-Smart 的 RISC-V ELF。
仅在固件正常启动后仍不枚举时，继续排查数据线、接口是否接 USB0、
USB 供电及 D+/D− 线路。

通过 MTP 的 `sdcard` 存储，把兼容的 RT-Smart ELF 放到 `app/`。
例如 `app/hello.elf` 在 UART0 中运行：

```text
/sdcard/app/hello.elf
```

配置中的自动启动路径为 `/sdcard/app/launcher &`；仓库未包含这个应用，
应自行编译并放入对应位置。缺少 launcher 和传感器驱动是后续功能缺项，
不能通过本次日志修复自动补齐。日志中的 `regulator_cpu` 缺失还需按
本板实际电源电路核对，不能据此随意选择调压芯片驱动。
