# 短时低速开环试转

**当前固件在继续运行约3秒后使能电机，先做低幅值定位，再让磁场缓慢旋转约4秒，随后关闭驱动。每次复位、重新下载或重新上电都会重做一次。**

## 代码在哪里看

电流采样与电机控制已分开，中文注释放在对应函数旁边，已有Live Watch变量名保持不变。

| 文件 | 负责的内容 |
| --- | --- |
| `Core/Src/main.c` | CubeMX初始化、调用`MotorControl_Init()`、主循环计数 |
| `Core/Inc/motor_config.h` | 集中设置电压幅值、指令电频率、试转时间、原有电流阈值 |
| `Core/Src/motor_control.c` | `RunMotorState()`安排等待、定位、旋转、减速、关闭驱动 |
| `Core/Src/current_sense.c` | 启动ADC/DMA、记录零点、平均取样、换算三相电流 |
| `Core/Src/motor_driver.c` | 操作PA3使能和TIM2三路CCR比较寄存器 |
| `Core/Src/motor_pwm.c` | 纯数学：指令电角度和幅值如何变成三个PWM比较值 |
| `Core/Src/stm32f1xx_it.c` | 将TIM3的1ms回调转发给`MotorControl_Update1ms()` |

每个自定义模块在`Core/Inc`下都有对应头文件，用来声明提供给其他文件调用的函数。
参数文件`motor_config.h`只存参数，不执行动作。

阅读时先打开`main.c`，再进入`MotorControl_Init()`和`RunMotorState()`。
需要理解PWM关系时，直接看`SetMotorVector()`里的两步：

```text
SetMotorVector(angle, volts)
  1. MotorPwmCalculate(...)：算出 U/V/W 三个比较值
  2. MotorDriver_SetPwm(u, v, w)：直接写入 TIM2 三路 CCR
```

停止时先关闭驱动，再调用`MotorDriver_SetPwm(900, 900, 900)`恢复三路50%。
写PWM统一使用这一个接口，原先的`SetNeutral()`和`MotorDriver_SetVector()`转发层已去掉。

一次1ms更新的顺序是：

```text
TIM3中断 → HAL定时器回调 → MotorControl_Update1ms()
                              ├─ CurrentSense_Update()：采样与电流换算
                              ├─ CheckProtection()：原有运行检查
                              ├─ RunMotorState()：推进指令并输出PWM
                              └─ CheckUpdateDuration()：原有耗时记录/检查
```

原有诊断集中在检查函数内，没有增加保护项。TIM2在两次更新之间自动输出约20个PWM周期。

## 当前已知结果

- 用户最新确认：本版试转时电机实际转动约1/4圈。按下面的指令角度估算，这与本版短时试转的预期运动量基本相符；尚未用编码器测量实际角度或转速。
- 用户已测到PA0为20kHz、50%占空比；PA1、PA2尚未分别实测。
- 此前固定电压定位测试结束时：motor_test_state=3、fault=0、peak_ma=149。用户没有观察到电机动作。
- 用户已连接U/V/W三根相线；轴端磁铁尚未装好或位置不确定，本次试转不使用编码器反馈。
- 用户随后在某一相对GND捕获到20kHz、约49%占空比的功率PWM。最新照片的Top=13.2V、Base=1.4V，两电平相差约11.8V；Max=14.2V、Min=0.8V包含极值，尚需核对探头零位和实际母线电压。该测量仅验证被测相存在开关输出。

## 操作

1. 使用12V可调限流电源，输入电流上限先设0.2A。电机三相接好、空载并固定外壳。
2. 用户按F5编译和下载；停在main()后再按F5继续运行。
3. 程序在驱动关闭时检查ADC零点，通过后等待约3秒。
4. 用0.6秒将电压矢量幅值从0增加到约0.3V，固定方向保持0.3秒。
5. 让电角度缓慢推进，电频率在0.5秒内逐渐升到0.5Hz，旋转阶段总共4秒。
6. 用0.3秒逐渐降低频率和幅值，然后关闭PA3 / DRV_EN。三路PWM恢复为相同的50%，本次启动不再自动使能。

全过程约8.2秒。需要立即停止时，关闭板子的12V电源。

## 转速与幅值

按电机的7极对数计算，0.5Hz电频率对应的目标机械转速为：

`60 × 0.5 ÷ 7 ≈ 4.29 转/分钟`

扣除0.5秒加速段的频率斜坡，并计入0.3秒减速段，旋转磁场的累计指令机械角度约为：

`360 × 0.5 × (4 - 0.5/2 + 0.3/2) ÷ 7 ≈ 100°`

因此本版只应缓慢转过约1/4圈就主动停止，并非持续转动程序。这个数值以转子跟随磁场为前提；初始定位也可能产生额外小幅动作，不能用肉眼观察代替编码器测量。

这是旋转磁场的目标速度；没有编码器反馈时，电机可能跟随、失步或停住，不能把它当成实测转速。实际方向取决于接线。

本次沿用约0.3V的电压矢量幅值，先前固定定位版本的理想幅值约0.298V。使用最小/最大值公共偏置实现中心对称SVPWM，三路占空比随电角度变化；不能用某一路对GND的约6V平均电压当作绕组相电压。母线电压参数仍按12V填写。

## Live Watch

| 变量 | 含义及正常结果 |
| --- | --- |
| motor_test_state | 0等待、1定位加力、2定位保持、5旋转、6减速、3完成、4故障 |
| motor_test_fault | 正常为0；故障含义见下表 |
| motor_test_peak_ma | 相电流峰值估算，单位mA |
| motor_test_rotate_ticks | 旋转/减速时执行的角度更新次数，正常结束时大于4000 |
| motor_control_max_us | 一次TIM3控制回调的最大耗时，单位微秒，正常应低于500 |
| live_pwm_u_ccr / live_pwm_v_ccr / live_pwm_w_ccr | 三路比较值，旋转期间变化，结束后均恢复900 |
| motor_command_angle_mdeg | 指令电角度，单位千分之一度；不是实际转子角度 |
| live_i_u_ma / live_i_v_ma / live_i_w_ma | 三相电流估算值，单位mA |

| fault | 含义 |
| --- | --- |
| 1 | ADC零点检查异常 |
| 2 | ADC/DMA错误、数据未刷新或读数接近量程端点 |
| 3 | 任意相电流估算达到约0.3A |
| 4 | 已使能时检测到PA3变低，例如调试暂停钩子关闭了驱动 |
| 5 | 一次控制回调耗时超过0.5ms |
| 6 | PWM参数、定时器参数或内核时钟与本测试要求不符 |

若没有转动，记录state、fault、peak_ma、rotate_ticks。若fault=5，再记录motor_control_max_us。先根据这些读数区分程序停机、角度未推进和实际驱动/电机问题。

## 验证范围

0.01Ω采样电阻、INA240A2的50倍增益和3.3V ADC参考值用于电流估算。采样增益、极性和软件停机阈值仍未用已知电流进行板上验证。1kHz平均采样检查仅辅助本次低速试验，不代替电源限流或快速硬件保护；正式FOC电流环需要另行设计同步采样和调度。

本工程继续加载debug/motor-test.cfg，目标暂停时尝试关闭PA3；检测到该变化后锁定fault=4。暂停钩子为本板专用，实际硬件行为仍待验证。

SVPWM数学部分此前已通过21600组矢量数值检查。本次模块拆分后，ARM固件编译通过，
并将拆分前的实际控制源码与拆分后的实际C模块放在同一套模拟HAL下进行36组主机对照。
对照覆盖正常试转、采样零点与错误、正负电流和推算U相超限、各阶段使能清零、
停止后不重启、计时阈值、配置不匹配及启动失败；每1ms的状态、电流和PWM观察值一致。
主机对照不测量STM32实机执行耗时。本次未替用户下载，实机结果仍需用户手动下载确认。

PWM接口精简后再次通过上述36组对照，每1ms的状态、电流和三路比较值与原版一致，
ARM固件编译通过。精简前源码、ELF和本次对照记录保存在
`D:/PCB/bringup-foc1-20261018-223611-pwm-simplify/`。

固定定位版本、原ELF及测试记录备份在D:/PCB/bringup-foc1-20261018-openloop/。
本次拆分前的源码、ELF及主机对照记录在D:/PCB/bringup-foc1-20261018-modules-145530/。

参考：[SimpleFOC开环角度推进方式](https://docs.simplefoc.com/velocity_openloop)、[SimpleFOC的公共偏置SVPWM](https://docs.simplefoc.com/foc_implementation)、[TI DRV8313桥控制](https://www.ti.com/lit/ds/symlink/drv8313.pdf)。
