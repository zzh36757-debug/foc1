# AS5600 手转测角度

当前入口用于验证编码器。GPIO 初始化将 PA3/DRV_EN 拉低，随后调用
`MotorControl_Stop(MOTOR_FAULT_NONE)` 保持驱动关闭；不调用 `MotorControl_Init()`，
因此原先上电等待 3 秒后自动试转的流程不会启动。
TIM2 PWM、ADC DMA 和 TIM3 控制中断也没有启动。此时用手转轴读取角度。

## 代码位置

- `Core/Src/as5600.c`：I2C 读取 STATUS、RAW ANGLE，换算角度并更新观察变量。
- `Core/Inc/as5600.h`：声明读取函数和 Live Watch 变量。
- `Core/Src/main.c`：初始化外设、关闭驱动，在主循环中约每 10ms 调用一次 `AS5600_Update()`。
- `CMakeLists.txt`：加入新的 AS5600 源文件。

沿用 I2C1 的 100kHz 配置：PB6=SCL、PB7=SDA。传感器 7 位地址是 0x36，
STM32 HAL 的地址参数要左移一位，即 0x6C。
STATUS 寄存器地址为 0x0B，RAW ANGLE 高/低字节为 0x0C/0x0D。
本模块只读寄存器，不更改传感器零点、角度范围或 OTP 设置。

## 下载后观察

由用户按 F5 下载，停在 main() 后继续运行，在 Cortex Live Watch 添加：

| 变量 | 含义 |
| --- | --- |
| `live_as5600_raw` | 机械角原始值 0~4095，正常时随手转轴变化 |
| `live_as5600_angle_deg` | 单圈机械角度，0~359.912 度 |
| `live_as5600_valid` | 1 表示完整读取成功，并检测到强度正常的磁场 |
| `live_as5600_status` | STATUS 原始字节；关注的 MD/ML/MH 位正常组合为 0x20（十进制 32） |
| `live_as5600_hal_status` | 最近一次 I2C 调用结果：0 成功，1 错误，2 忙，3 超时 |
| `live_as5600_i2c_error` | HAL I2C 错误位，正常为 0；例如 4 表示应答失败 |
| `live_as5600_read_count` | 完整读取成功次数，持续增加；磁场异常时也可增加 |

先确认 `valid=1`、`hal_status=0`、`i2c_error=0`，再判断角度读数。
通信失败时保留最后一次成功的角度和状态，但会将 `valid` 清零，不能将旧角度当作新数据。
通信恢复后自动继续读取，无需复位。磁场异常时仍显示读数，同时 `valid=0`。

STATUS 的 bit5（MD）为检测到磁铁，bit4（ML）为磁场过弱，bit3（MH）为磁场过强。
只有 MD=1 且 ML=MH=0 时，本模块才将角度标记为有效。

## 手转验收

1. 驱动关闭后手转轴，约转 90 度，角度读数也应变化约 90 度，原始值相差约 1024 格。
2. 转过一整圈，单圈读数回绕一次；例如 359 度到 0 度附近属于正常现象。
3. 停住时角度基本稳定；反向转动时读数变化方向相反。

换算公式是 `机械角度 = raw * 360 / 4096`。读数的起点取决于磁铁安装方向，
不要求刚上电时为 0 度。本阶段没有计累计圈数、计算转速或标定电角度零点。
AS5600 的角度原始值通过 I2C 读取，与 PA4/PA5 的电流 ADC 采样是不同的数据。

旧的 `pwm_test_running` 预期为 0，`motor_test_state` 为 3（已停止），
`motor_test_fault` 为 0。它们不用于判断编码器是否正常；电流变量本阶段不更新。

## 软件验证

ARM Debug 编译通过，RAM 使用 2272 字节，Flash 使用 25132 字节。
主机模拟使用实际 `as5600.c`、电机模块及从当前源码提取的 `main()`，通过了：

- 全部 4096 个角度码值的拼接、换算和正反向回绕。
- 无磁铁、磁场过强/过弱、状态保留位和通信失败后的无效标记及恢复。
- 模拟运行 15 秒，并注入传感器断连和磁场异常；DRV_EN 始终保持低，
  未启动 PWM、ADC DMA 或 TIM3 控制节拍，心跳和角度读取持续工作。

这是软件模拟，不验证实物磁铁安装、I2C 接线、PA3 电压或 STM32 执行时序。
本次未替用户烧录。修改前源码、ELF 和主机验证脚本/日志保存在
`D:/PCB/as5600-angle-test-20261020/`。

参考：[ams 官方读取示例及寄存器地址](https://look.ams-osram.com/m/dea9085545cea895/original/AS5600_AB-POT_UG000378_1-00.pdf)、
[ams AS5600 数据手册（SparkFun 托管副本）](https://cdn.sparkfun.com/assets/f/a/b/b/a/AS5600_Datasheet.pdf)。
