# GPIO、EN 与 I2C1 运行状态复核

用户于本轮明确撤销了禁止上板测试的限制。已更新根目录 `AGENTS.md`；
与正在复测的另一个线程交接板卡后才执行诊断，没有烧录或硬复位。
这份记录描述本轮板上状态，不能代替先前失败时刻的波形或状态记录。

## 结论

用户提出的三类固件问题都有代码检查依据，其中引脚生命周期覆盖、EN 状态缓存失配
和首次 I2C 配置遗漏已经复现；本轮没有发现 I2C1 小报文的中断/FIFO 路径完全失效。
另外发现 `FPIOA.set_pin_cfg()` 错误调用 getter，不能据其返回 `True` 判断恢复成功。

本轮在明确上电后读到真实 ID `0x0132`，原生传感器模式初始化成功；
随后 `Sensor.run()` 成功，第一帧 `snapshot(timeout=1500, auto_reboot=False)`
失败，返回 `failed(3)`。没有取得图像，不能宣称相机取帧正常。
此前固定绑定 ID 回调的修改不能解释本轮真实 ID 正常时的无帧。

## 1. 引脚配置可能被后续代码覆盖：确认

启动配置使用 GPIO34/35 的 I2C1，GPIO2 控制高有效 EN。
本轮初始读回函数编号为 `2/94/95`，分别对应 GPIO2、IIC1_SCL、IIC1_SDA；
IOMUX 原始值为 GPIO2 `0x800001ef`、GPIO34/35 `0x800009cf`。
没有发现当前板级启动流程把 I2C1 固定改到其他脚的证据。

但存在明确的运行时覆盖路径：

```text
Pin(34) 构造
  -> machine_pin_make_new()
  -> drv_fpioa_set_pin_func(34, GPIO34)

Pin 对象垃圾回收／软重启时的 gc_sweep_all()
  -> machine_pin_destroy()
  -> drv_gpio_inst_destroy()
  -> drv_gpio_mode_set(INPUT)
  -> kd_pin_mode() 把 io_sel 改回 GPIO
```

板上复现顺序：

```text
original IIC1_SCL                        94
after Pin(34)                           34
set_function(34, IIC1_SCL)               94
delete Pin object + gc.collect()        34
```

所以在 GPIO 模拟 I2C 后先恢复 FPIOA、后回收 Pin 对象，会把刚恢复的 I2C1 再次覆盖。
软重启并非无条件改变所有引脚，触发条件是存在需要析构的相关 Pin 对象。
GPIO35 走同一实现。本轮复现后已显式 `set_function()` 恢复 GPIO34/35，
最终读回 `2/94/95`。

另外，`machine_fpioa_set_pin_cfg()` 当前调用的是
`drv_fpioa_get_pin_cfg(ctx->pin, &ctx->cfg.u.value)`。
板上保存 I2C 配置、切为 GPIO 后调用 `set_pin_cfg(saved)` 返回 `True`，
引脚却仍为 GPIO。这是 setter 实现错误，不是恢复成功的证据。

## 2. EN 的硬件状态与 power_flag 缓存失配：确认

`sensor_comm.c` 的 `KD_IOC_SENSOR_S_POWER` 在
`power_on && dev->power_flag` 时直接返回成功，未调用传感器驱动。
`sc132gs_power()` 也有相同的缓存短路。
用户侧 Pin 析构改 GPIO2 为输入时，没有同步清除传感器的 `power_flag`、
`init_flag` 或 `mode_init_flag`。

原生诊断通过 GPIO ioctl 执行与 Pin 析构相同的输入模式操作，实测：

| 阶段 | 上电接口返回 | GPIO2 DDR | IOMUX |
| --- | --- | --- | --- |
| 明确关电再上电 | 0 | 1，输出 | `0x800001ef` |
| 设置 GPIO2 为输入 | 0 | 0，输入 | `0x8000010f` |
| 再次请求 S_POWER(1) | 0 | **仍为 0** | **仍为 `0x8000010f`** |
| S_POWER(0) 后 S_POWER(1) | 0 | 恢复为 1 | 恢复为 `0x800001ef` |

该测试确认状态管理不一致；本轮输入模式下 EN 读回仍为高，
没有据此认定模组供电实际已掉落，也不能认定它解释了全部先前失败。
直接拉高 EN 也不能修复内核保存的初始化状态，显式驱动关／开电才会同步重置这些缓存。

## 3. I2C 实现与实际配置：首次配置存在遗漏；小报文路径本轮正常

MicroPython 路径经过用户 HAL 的 `drv_i2c_transfer()`，再通过 ioctl 进入
内核 `rt_i2c_transfer()`；传感器驱动也进入同一内核控制器实现。
两条上层路径同时失败不能独立排除该公共底层。

用户 HAL 的 `drv_i2c_inst_create()` 首次构造时只设置
`inst->freq`、`inst->timeout_ms`，没有下发频率和超时 ioctl。
随后调用相同参数的 setter 又会因缓存相同而提前返回。
使用真实 HAL 源码、拦截 ioctl 的主机复现结果为：

```text
create(freq=100000, timeout_ms=50): clock ioctls=0 timeout ioctls=0
reapply same values:               clock ioctls=0 timeout ioctls=0
change freq to400000:              clock ioctls=1
```

因此只记录 `I2C(freq=100000)` 不能证明首次构造时已改为 100 kHz。
MicroPython 构造函数也未检查后续频率／超时 setter 的错误返回。
此外内核超时单位是 tick，HAL 参数称为 ms；当前配置为 1000 Hz tick，
本板两者数值相同，不能把它作为本次错误原因。

本轮原生诊断显式调用 `/dev/i2c1` 的时钟 ioctl，并核对了实际寄存器：

| 请求速率 | IC_CON | 使用的 HCNT/LCNT | 0x30 内核中断传输 | 0x30 直接轮询 |
| --- | --- | --- | --- | --- |
| 100000 | `0x63`，Standard | SS `462/534` | 成功 | 收到 1 字节、STOP，无 abort |
| 400000 | `0x65`，Fast | FS `87/159` | 成功 | 收到 1 字节、STOP，无 abort |

轮询访问同一控制器，但绕开内核中断处理和事件等待。两者在 `0x30` 均成功；
`0x31..0x33` 轮询读到 `TX_ABRT_SOURCE=1`，明确为地址无 ACK。
FIFO 深度参数读回 `0x001f1fee`，对应 TX/RX 各 32；入口处控制器空闲、FIFO 均为空。
本轮未使用示波器测量 SCL 占空比、边沿或转换器两侧波形，
寄存器配置不等于实测电气时序，也没有覆盖所有大报文和并发情形。

之后通过传感器 ioctl 正常读回：

```text
0x3107=0x01, 0x3108=0x32
full mode: 1080x1280, 4 lanes
KD_IOC_SENSOR_S_INIT: ret=0
0x3018=0x72, 0x3019=0x00, 0x3654=0x08
```

因此当前证据降低了“只有硬件 I2C 中断/FIFO 路径完全出错”的可能性，
也推翻了用先前无 ACK 日志描述当前板上状态的做法。

## 范围与证据

本轮修改了操作权限约束，添加本审查记录；没有修改或重新交付相机固件源码／镜像。
上述 setter、首次配置和缓存问题尚需固件修复，不能将“已查明”写成“已修好”。
临时原生诊断上传至 `/sdcard/sc132gs_pin_i2c_audit.elf`，日志为同名 `.log`；
主机证据归档于
`output/k230d_canmv_dart_v1_defconfig/pin-power-i2c-review/`。

相机复测已执行 `sensor.stop()`；引脚最终明确恢复并读回为 GPIO2/I2C1 SCL/I2C1 SDA。
未修改启动脚本，没有保存 `main.py` 或 `boot.py`。
