# 2026-11-09 VICAP 静态代码审查

审查对象是已交付的 `CanMV_K230D_Dart_V1_SC132GS_CLKINV.img`。
本次仅分析源码、最终 ELF、链接映射和实际静态库，没有连接或运行开发板。
用户确认的硬件条件是 SC132GS、物理 CSI0/CSI1 共四条数据线、外部 24 MHz MCLK。

## 结论

四线接收路径确实进入 PHY0＋PHY1 主从组合，外部参考时钟配置也正确。
但现有 VICAP 库存在确定的 PHY 超时漏报：接收条件未满足时仍可能返回成功。
因此不能用初始化接口返回 0 证明 VICAP 已就绪。
传感器端也缺少完整、经确认的四线初始化参数依据。
这些代码问题和适配缺口已经成立，但不能据静态审查认定它们是本次无帧的唯一原因。

## 最终固件中的四线配置

直接从交付解释器 ELF 的 `sensor_info_list` 读取结构体字段，结果如下。
使用本 SDK 的实际枚举：`VICAP_CSI0=1`、`VICAP_MIPI_4LANE=3`、RAW10=`0x2b`。

| 传感器模式 | csi_num | mipi_lanes | source_id | phy_freq | data_type |
| --- | --- | --- | --- | --- | --- |
| 1080×1280 | 1（CSI0） | 3（四线） | 0（CSI0） | 2（1200M） | 0x2b |
| 640×480 | 1（CSI0） | 3（四线） | 0（CSI0） | 1（800M） | 0x2b |

对应源码是 `src/rtsmart/mpp/userapps/src/sensor/mpi_sensor.c:733` 起的 SC132GS 表。
独立 CSI1 设备关闭不妨碍组合接收，因为四线分支直接配置第二个 PHY。
交付内核的实际调用链为：

```text
dwc_csi_phy_init(CSI0=1, phy_freq, MIPI_4LANE=3)
  -> k230_csi0_config_4lan_phy
       -> k230_csi0_phy0_4lane_config
       -> k230_csi0_phy1_4lane_config
       -> k230_phy0_master_phy1_slave_config
```

实际编译头文件包含 `SC132GS_CSI0_4LANE=1`、`EXTCLK_HZ=24000000`，
CSI0 的 MCLK 编号为 -1，模式表中的 SoC MCLK 设置全部关闭。
24 MHz 是传感器参考输入，不是接收器的 MIPI PHY 速率档位。

## 已确认缺陷：四线 PHY 等待超时不向上传递

在实际交付内核 ELF 中：

- `k230_phy0_master_phy1_slave_config` 位于 `0x1907c2`。
  第一段等待检查 `STOPSTATE & 0x10003`，第二段检查 `STOPSTATE & 0x0c`。
  循环计数超过 1000000 后调用错误日志，然后跳出循环继续执行。
  第一段和第二段共用计数器，进入第二段时没有重新清零。
- `k230_csi0_config_4lan_phy` 位于 `0x1909e0`。
  调用两路 PHY 配置和主从配置，没有错误返回值可检查。
- `dwc_csi_phy_init` 位于 `0x190a10`。
  CSI0 四线分支调用上述函数后，最终在 `0x190b3e` 将返回值置 0。
  PHY 等待超时不会使该接口失败。

其逻辑等价于：

```c
while (phy_not_ready) {
    if (++count > limit) {
        log_error("4lan wait stop timeout");
        break;
    }
}
/* 上层没有接收此失败状态。 */
return 0;
```

`media.sensor.Sensor.run()` 会检查 `kd_mpi_vicap_init/start_stream()` 的返回值，
但 Python 层检查无法补回 PHY 层已经丢失的失败信息。
修正方向应是让 PHY 配置函数返回状态、分别限制两段等待，并把失败一直传到调用者；
仅修 Python 报错文本或增加 snapshot 超时时间不能修正这个问题。
当前 RT-Smart VICAP 实现来自 `kernel/lib/libvicap.a`，没有对应的 C 源文件，
不能修改另一套 Linux SDK 的同名文件后就宣称已修复交付固件。

## 传感器端尚缺完整四线参数依据

`sc132gs_init()` 先执行原有两线模式衍生的 PLL/模拟表，再设置：

- `0x3018[7:5]=3`：四线选择，有手册依据。
- `0x3031` / `0x3037`：RAW10 和 PHY 数据模式。
- `0x3654[3]`：按原理图对接关系推导的时钟反向补偿。

四线分支不写原两线表的 `0x3019=0x0c`，也没有该寄存器的四线值及输出使能依据。
PLL 表、MIPI 时序和接收端 PHY 档位仍沿用原模式；没有匹配这颗模组的完整四线厂家表。
不能把 lane 选择位读回正确等同于四条数据线已经输出有效包。
时钟反向补偿也不能作为“无帧已经修好”的依据。

## 审查依据与范围

交付镜像 SHA-256：
`54997a67c91d75037c3cdfc8f144950351970cc010225e113810d47c268aa98f`。
已确认被审查的内核 ELF 和解释器均与该次交付的构建记录匹配。
反汇编与最终元数据保存在：
`output/k230d_canmv_dart_v1_defconfig/vicap-code-review-20261109/`。

此前 104 组模拟验证的是传感器驱动和模式元数据，没有覆盖 VICAP 库的 PHY 状态等待。
本次没有改动固件源码或重新生成镜像；这是对当前交付版本的代码审查结论。
