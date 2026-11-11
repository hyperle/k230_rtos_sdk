# SC132GS FIXED_BIND 固件初始化复核

## 审查对象与权限

用户报告 VICAP 初始化失败，当前板上文件名为
`CanMV_K230D_Dart_V1_SC132GS_FIXED_BIND.img`。
本地同名镜像为 671088640 字节，SHA-256：
`351c91545839025642b2a53b6cbee57e95fc18bbad0a9d0dc21a64ef93c8c201`。
已从镜像核对 A/B 内核、解释器 ELF 和八个 SC132GS ISP 文件；与本次审查的构建产物匹配。
不能仅凭文件名认定板上字节与本地完全相同。

用户只授权修改固件。本次没有连接、烧录或运行板卡，验证限于主机上的静态审查、
寄存器模拟、交叉编译和镜像内容核对。既有运行日志不能当作该镜像的本次运行证据。

硬件条件采用用户确认值：SC132GS 单 22 针相机板、直接叠插、物理 CSI0/CSI1
合并四条数据线、外部 24 MHz。配置保持逻辑 CSI0 四线接收、I2C1、固定地址
`0x30`、GPIO2 高有效 EN、无独立 reset 和无 SoC MCLK 输出。

## 已确认缺陷：ISP 路径重新强制匹配 ID

原镜像启用了 `MPP_SC132GS_SKIP_CHIP_ID_CHECK`，但只在 probe、模式初始化
和连接检查中绕过 ID 匹配。驱动函数表仍将 `sensor_get_chip_id` 指向
`sc132gs_get_id()`，该回调无条件要求读回值为 `0x0132`。

实际链接的静态库调用链如下，每一步都会检查并传递非零返回值：

```text
Sensor.run()
  kd_mpi_vicap_init()
    kd_mpi_isp_init()
      kd_mpi_isp_connect()
        VsiCamDeviceConnectCamera()
          CamDevicePipeConnectInput()
            VsiCamDeviceSensorGetInfo()
              CamDeviceSensorIsiGetInfo()
                IsiGetRevisionIss()
                  IsiAdpter_IsiGetRevisionIss()
                    kd_mpi_sensor_id_get()
                      KD_IOC_SENSOR_G_ID
                        sc132gs_get_id()
```

因此，在 I2C 正常、模式初始化通过但 ID 读回值不匹配时，固定绑定仍会在 ISP
初始化阶段失败。此前只验证 probe/init 不读 ID 的模拟用例没有覆盖此回调。
这是可由代码和主机模拟重现的缺陷；用户未提供当前镜像的完整错误路径，
不能据此认定这就是本次报错的唯一原因。

## 修正范围

`sc132gs_get_id()` 在固定绑定配置下仍读取真实的 `0x3107/0x3108`，但不再
将其与 `0x0132` 强制匹配。未开启固定绑定时保留严格匹配。
空指针、未上电和 I2C 读取失败仍然报错；没有把期望 ID 当作硬件读回值。
其他寄存器写入、尺寸、位宽和接口配置读回检查保持生效。

新增回归用例从已注册驱动的函数表调用该回调，模拟 ISP 的调用方式：
固定绑定遇到 `0x0100` 时返回真实值且成功，普通模式仍报 `-ENODEV`，
固定绑定遇到 ID 寄存器读取失败时仍失败。该用例在修改前断言失败。

## 其他核对结果与未解决事项

- 两个分辨率的 ISP 文件均实际包含在镜像中，没有发现缺失文件。
- 四线配置调用 PHY0/PHY1 主从组合；关闭独立 CSI1 设备不等于关闭第二个 PHY。
- 24 MHz 为外部参考时钟；800M/1200M 为接收 PHY 档位，两者不是同一参数。
- VICAP 预编译库的四线 STOPSTATE 超时漏报仍存在，见
  [此前 VICAP 库审查](VICAP_REVIEW.md)。它会导致超时未作为错误返回，
  不能单独解释当前非零初始化返回值；本次没有修改或宣称修复该闭源库。
- 传感器四线参数仍由现有两线模式表衍生，缺少该模组的完整厂家四线表。
  本次没有猜测新的 PLL、输出使能或 MIPI 时序寄存器。
- 原输出目录的 `BUILD_INFO.json`、`SHA256SUMS` 属于更早的 CLKINV 版本，
  不能作为 FIXED_BIND 的发布记录。本次审查另存输入校验记录，并为新镜像生成独立记录。

原镜像证据保存在
`output/k230d_canmv_dart_v1_defconfig/fixed-bind-init-review/`，
包含 `REVIEW_INPUT.json`、原始内核 ELF/配置、调用链反汇编和修正前的回归失败日志。
新镜像只交付未压缩 `.img`，不得将主机验证结果描述为已实测取帧。

## 修正后构建与核对

交付候选为 `CanMV_K230D_Dart_V1_SC132GS_FIXED_BIND_V2.img`，
未压缩大小 671088640 字节，SHA-256：
`88e71a67a03db610d42bd491f9023144b8439104310f2666dd8ebeaac0941567`。

- 112 组主机驱动与模式模拟通过，包含上述 ISP ID 回调回归。
- 本机容器以 `--network none` 完整构建，退出码为 0。
- 最终内核反汇编确认固定绑定回调仍读取 ID，但不再比较 `0x0132`。
- 镜像内 A/B 内核、解释器、启动脚本、示例和八个 ISP 文件逐项匹配；
  直接读取解释器 ELF 的两个模式表，确认 CSI0、四线、RAW10 及对应 PHY 档位。
- 原 `FIXED_BIND.img` 的 SHA-256 未改变。构建与验证记录位于
  `output/k230d_canmv_dart_v1_defconfig/fixed-bind-v2-build-records/`，
  完整构建日志为父目录的 `build-fixed-bind-v2.log`。

这些结果确认修改已进入镜像，不证明板上的 VICAP 初始化或取帧已正常。
