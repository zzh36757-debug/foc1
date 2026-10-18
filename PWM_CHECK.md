# 首次三路 PWM 信号检查

**当前固件已切换为[短时低速开环试转](MOTOR_OPENLOOP_TEST.md)，会在继续运行约3秒后使能电机并短时推进电角度。下载前先阅读该操作说明。**

本文记录此前的PWM信号检查版本：保留Live Watch计数程序，启动TIM2的三个PWM通道；该检查版本的PA3 / DRV_EN保持低电平。

当前源码同时加入了 [电流采样零点检查](ADC_ZERO_CHECK.md)，可以在 Live Watch 观察两路采样输入电压。

## 示波器测量

示波器探头使用 10×，地夹接板子的 GND，探头尖端依次测量：

| 测点 | 连接功能 | 预期波形 |
| --- | --- | --- |
| PA0 | TIM2_CH1 → DRV8313 IN1 | 约 0～3.3V、20kHz、50% 占空比 |
| PA1 | TIM2_CH2 → DRV8313 IN2 | 约 0～3.3V、20kHz、50% 占空比 |
| PA2 | TIM2_CH3 → DRV8313 IN3 | 约 0～3.3V、20kHz、50% 占空比 |
| PA3 | DRV_EN | 约 0V |

先用直流耦合、10μs/div、1V/div 观察，再按实际示波器设置调整。周期应约为 50μs，高电平持续约 25μs。若通道不足，可以依次测量。

本步骤测 STM32 的 PA0～PA2 控制信号；普通接地探头的地夹保持接 GND。后续查看电机相间电压应使用合适的差分测量方式。

## Live Watch

- `live_counter`：继续递增。
- `live_error`：应为 0。
- `pwm_test_running`：成功启动三路 PWM 后为 1。

计数器和寄存器读数只能说明软件配置及运行状态；引脚实际波形由示波器确认。

## 下一步让电机转动

确认三路波形及 PA3 电平后，再加入低幅值、缓慢变换角度的开环 SVPWM 程序，让三相占空比随着电角度变化，并控制 PA3 的使能时序。电机按用户提供的参数为 7 极对。

首次空载试转使用 12V 可调限流电源，输出电压幅值和转速从低值开始。电源输入限流与电机相电流并不相同，后续还需验证电流采样及保护。

参考：[TI DRV8313 数据手册，7.3.2 Bridge Control](https://www.ti.com/lit/ds/symlink/drv8313.pdf)、[SimpleFOC 开环速度控制](https://docs.simplefoc.com/velocity_openloop)。
