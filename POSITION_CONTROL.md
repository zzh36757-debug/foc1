# 位置控制：绝对与相对两种坐标

算法集中在 `Core/Src/position_control.c`，声明在 `Core/Inc/position_control.h`。
函数统一使用 `Position_Control_` 前缀。

```text
绝对目标/反馈 -> 单圈最短角差 -> 按编码器方向转换 ┐
                                               ├-> 位置P -> 目标rpm -> 原速度PI -> 目标Iq -> 原电流PI -> PWM
相对目标/反馈 -> 多圈角度差（已处理方向）         ┘
```

## 默认模式与目标

`current_mode=1`、`position_enabled=1` 时位置环生效。
`position_reference_mode=1`（默认）为AS5600单圈绝对角度控制；`0`为相对启动位置的多圈控制。

| 模式 | 目标变量 | 反馈变量 | 0度含义 |
| --- | --- | --- | --- |
| 绝对，mode=1 | position_absolute_target_deg | position_absolute_measured_deg | AS5600原始读数映射到的0度 |
| 相对，mode=0 | position_relative_target_deg | position_relative_measured_deg | 本次使能驱动前记录的轴位置 |

`mode`在这里指 `position_reference_mode`，不是 `current_mode`。
旧的 `position_target_deg`、`position_measured_deg` 已删除，避免把两个坐标混为一谈；Live Watch请换成上表的新名字。
目标独立保存。切换时既不覆盖另一个目标，也不重新把当前位置设为相对零点。

默认两个目标均0度，但现在默认绝对模式：如果启动时AS5600显示23度，驱动使能后会尝试转到AS5600的0度。
旧版默认是在23度处保持（将23度作为相对0度），本次行为已改变。
仍沿用ADC校零、100ms等待和已保存的电角度标定，不增加定位/扫角流程。

## 绝对位置模式

```c
position_reference_mode = 1;
position_absolute_target_deg = 90.0f;
```

表示保持在AS5600原始读数映射的90度附近。目标允许0~360度，360等同0；负数、超过360或非有限数在启用绝对模式时报配置故障6。
绝对反馈始终为 `raw * 360 / 4096`，范围[0,360)，与AS5600刻度一致，不因编码器方向标定而取反。
它与主循环的 `live_as5600_angle_deg` 可能相差一个采样更新时刻，因为位置层在TIM3消费新样本。

选择单圈最短方向：目标0度、实际359度时误差+1度；目标359度、实际0度时误差-1度。
正好相差180度时统一选择AS5600角度减小方向。
误差乘已标定的sensor_direction后才交原速度环，保证编码器方向为-1时仍朝目标运动。
没有改变电流环使用的电角度零点 `MOTOR_ZERO_ELECTRICAL_RAD`。

0度与360度在圆周上相邻，因此实际359.9度距离0度只有0.1度，属于默认0.2度容差内，并不要求显示恰好0.000。
单圈绝对模式不指定圈数；要正向走完整圈或多圈，请使用相对模式。

## 相对位置模式

```c
position_reference_mode = 0;
position_relative_target_deg = 720.0f;
```

表示到本次启动位置正方向两圈的位置，正方向沿用原速度环。目标90、-90、360、720、0分别为相对启动位置的坐标。
先到90再设180是再向前90度，不是每写一次就额外增加180度。
相对误差不做单圈最短路径处理，720和0是不同位置。

AS5600只有单圈绝对读数，相对多圈值通过连续新样本消除4095/0回绕后累计。
切换到绝对模式期间也继续累计相对位置；断电不保存圈数。
相邻样本实际位移必须小于半圈，否则单圈读数不能恢复漏掉的圈数。
相对累计已乘sensor_direction，控制时不再重复取反。

## 源码默认参数与在线调节

修改宏后需重新编译、下载；在线调节改右侧对应变量。

| 宏 | 在线变量 | 当前默认值/单位 |
| --- | --- | --- |
| POSITION_DEFAULT_ENABLED | position_enabled | 1，启用位置环 |
| POSITION_DEFAULT_REFERENCE_MODE | position_reference_mode | POSITION_REFERENCE_ABSOLUTE，即1 |
| POSITION_DEFAULT_ABSOLUTE_DEG | position_absolute_target_deg | 0度 |
| POSITION_DEFAULT_RELATIVE_DEG | position_relative_target_deg | 0度 |
| POSITION_DEFAULT_KP | position_kp | 7 rpm/deg，保留用户设置 |
| POSITION_DEFAULT_SPEED_LIMIT_RPM | position_speed_limit_rpm | 200 rpm，保留用户设置 |
| POSITION_DEFAULT_TOLERANCE_DEG | position_tolerance_deg | 0.2度 |

绝对与相对共用位置Kp、速度指令上限、容差。容差内给零速度，驱动仍使能；它是控制容差，不是定位精度保证。
`position_error_deg`显示当前模式误差，绝对模式范围[-180,180)，相对模式不回绕；绝对误差符号采用AS5600刻度方向。
`position_speed_target_rpm`显示转换到原速度方向后的速度指令。

位置P输出直接交给原速度PI，不经过原120rpm/s斜坡，以免接近目标时减速指令延迟；目标阶跃可能使速度指令阶跃。
`position_enabled=0`恢复原速度模式，用 `speed_target_rpm` 和原速度斜坡，不限制原速度模式最高转速。
`current_mode=0`仍为固定Iq测试，此时位置环不控制输出。
速度、相对位置、绝对位置三种目标来源之间切换都会清旧速度积分，不保证运动中切换无冲击。

## 调用与停机

1. `SpeedPower_Init()`调用 `Position_Control_Init()`。
2. WAIT使能前调用 `Position_Control_Start(raw, direction)`，发布绝对角并记录相对零点。
3. TIM3消费新AS5600样本时调用 `Position_Control_OnSample(raw)`，同时更新两个反馈。
4. 每1ms位置模式调用 `Position_Control_Update(&target_rpm)`，选择坐标并输出速度。
5. 原速度PI给Iq，ADC/DMA每500us执行原电流PI并更新PWM。
6. 原停机路径调用 `Position_Control_Stop()`，清ready/速度输出，保留最后反馈供诊断。

main不重复计算控制，也不额外读I2C。`speed_run=0`及原故障保护仍关闭驱动、不保持位置、不自动重启。
无效或陈旧角度沿用故障7，非法模式/参数故障6。

## 验证

ARM Debug编译通过。主机测试直接编译实际speed_power.c和position_control.c，覆盖原相对功能、
绝对坐标启动读数、0/360回绕和最短路、360等同0、180度平局、正负编码器方向、两个目标独立保存、
切换清积分、无效模式/参数、停机/传感器保护、原速度/固定Iq模式。
理想速度跟随模型仅验证软件逻辑收敛，不代表实际定位精度或负载响应。

记录：`D:/PCB/position-absolute-20261025/`。保留用户位置Kp7、200rpm上限和现有速度/电流增益及保护参数。
未烧录、复位、启动实物或写在线参数。实物需下载新版后验证。
