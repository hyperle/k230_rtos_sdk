# SC132GS I2C 错误译码修正固件

本次把已确认的 I2C 中止状态位错误修正编入完整固件，生成可直接烧录的未压缩
`.img`。当前相机地址无应答及稳定取帧问题尚未证实恢复；本镜像尚未刷机验收。

## 交付文件

- 文件：`CanMV_K230D_Dart_V1_SC132GS_I2C_ABORT_FIXED.img`
- 大小：671088640 字节（640 MiB），有效 MBR，外层不是 gzip。
- 镜像 SHA-256：`f30747eeffd64ef826dd993a897118b3b1563b47a1f21350ca3e378f799f510b`
- 打包解释器 SHA-256：`acc503ddd90be01ee5ce579eb776f91a0b50cff877aeff84ee182cb5965f71bd`
- 链接内核 ELF SHA-256：`8e7eb5abc4a2fe225d24eacd07470003990f21a48d70d264cff0e7ea5beae174`
- 内核封装 SHA-256：`3470b150ea8c68980ba3019188064b11bcea3db8020433c786b7dc26a4290f33`

文件与 `.sha256`、`BUILD_INFO_SC132GS_I2C_ABORT_FIXED.json` 位于
`output/k230d_canmv_dart_v1_defconfig/`。

## 与 STOP_SAFE 的差异

1. `drv_i2c_reg.h` 补齐 bit8 `ABRT_HS_NORSTRT`，校正后续状态位。
   bit12 仲裁丢失现在进入正确的内部 `-RT_EBUSY` 分支；bit11 master disabled
   不再误进该分支。地址 NACK 的 bit0 一直正确，该修改不增加重试或恢复缺失的应答。
2. 随镜像安装的 `examples/dart/camera_color_preview.py` 去掉第 60 帧自动保存 JPEG，
   默认持续 RGB565 预览、不存图，仍为手动 5000 µs/4 倍增益。

保留此前 VICAP/WBC 缓冲区、devfs 和失败停流 EN 关断保护。16 项 STOP_SAFE
原记录源码哈希全部匹配；相机电源、寄存器表、CSI0 四通道、外部 24 MHz、
GPIO34/35 I2C1、GPIO2 EN、ISP 和默认总线时序均未另改。

## 构建与校验

本机固定版本 Docker 镜像和缓存工具链，禁用网络，完整 `make` 退出码 0。
日志确认 I2C 控制器驱动重新编译并最终链接。

新 ELF 的 `k230_i2c_bus_xfer` 中，`0x1d8bf8` 原来的
`srli a4,a5,0xb` 变为 `srli a4,a5,0xc`；该函数其余字节与 STOP_SAFE 一致。
独立确认七个相机/devfs/I2C 时序函数的链接代码与 STOP_SAFE 相同。
该改动已从 ELF 核对到 raw kernel，再从实际镜像 A/B 内核封装解压后核对到
相同的完整 raw kernel，校验了头 CRC、压缩内容 CRC 与长度，避免只核对暂存文件。

同时校验了 MBR 分区、FAT12 bin / FAT16 app 文件系统、打包/暂存/链接解释器
一致、ISP 二进制数据库和预览示例与本次暂存内容一致。
27 项输入文件在构建前后哈希一致。

- I2C：16 个状态位双向编码、7 种实际生产错误映射通过。
- 相机驱动/MPI：112 组配置通过。
- Sensor 帧释放：3 项回归通过。
- devfs：真实生产函数的字节/扇区边界回归通过。
- 预览示例：语法检查通过，持续循环且无自动存图调用。

构建保留 SDK 原有的 U-Boot 声明/指针转换、内核 RWX LOAD 和 ffmpeg 配置时间戳
警告；ffmpeg 子步骤含被 make 忽略的状态 2，最终构建及打包退出码为 0。
这些日志已保留，不表述为无警告构建。

## 实板状态与后续验证

本次只构建和做主机校验，没有刷机、操作摄像头或保存照片。
上一版未解决的地址无应答、IDE 停止后重跑失败、异常重启、紫色条纹和
600 秒应用窗口均不能视作因本次修正而通过；新版本实际结果需独立记录。

详细原始故障及对照见 [STABILITY_REVIEW.md](STABILITY_REVIEW.md)。
新构建审计目录 `sc132gs-i2c-abort-review/` 保存输入快照、内核 ELF、
原始内核/封装、反汇编、回归日志、构建/校验脚本和哈希清单。
未来上板识别本版时需比较生成的六函数指纹，其中包含已改变的
`k230_i2c_bus_xfer`；旧五函数指纹只覆盖未变化代码，不能单独区分这两个版本。
