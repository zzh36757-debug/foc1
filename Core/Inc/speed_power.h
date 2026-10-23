#ifndef SPEED_POWER_H
#define SPEED_POWER_H

#include <stdint.h>

/* 算法、约 1 秒的启动准备和可调参数都在 speed_power.c，本头文件只声明接口。 */
void SpeedPower_Init(void);        /* 在 MotorControl_Init() 前调用一次。 */
void SpeedPower_PollSensor(void);  /* 主循环调用，I2C 读取不能放进 TIM3 中断。 */
void SpeedPower_Update1ms(void);   /* 由 MotorControl_Update1ms() 转发，不能再单独调用。 */

/* 可在 Live Watch 中观察；目标、P、I 和 run 也可在运行时修改。 */
extern volatile float speed_target_rpm;          /* 目标机械转速，默认 300rpm；不另设目标转速上限。 */
extern volatile float speed_kp;                  /* 比例系数，V/rpm。 */
extern volatile float speed_ki;                  /* 积分系数，V/(rpm*s)。 */
extern volatile uint32_t speed_run;              /* 1=运行；写 0 停机，重新启动需复位。 */
extern volatile float speed_command_rpm;         /* 经过加减速限制的实际速度指令。 */
extern volatile float speed_measured_rpm;        /* 编码器测得并滤波后的机械转速。 */
extern volatile float speed_error_rpm;           /* 指令转速 - 实测转速。 */
extern volatile float speed_requested_uq_volts;  /* PI 请求值：已受电压幅值上限约束。 */
extern volatile float speed_uq_volts;            /* 直接使用 PI 请求、送去计算 PWM 的 Uq 指令，并非实测电压。 */
extern volatile float speed_electrical_angle_rad;/* 实测转子电角度，rad。 */
extern volatile float speed_zero_electrical_rad; /* 本次上电校准得到的电角度偏移。 */
extern volatile int32_t speed_sensor_direction;  /* +1/-1：编码器相对 U/V/W 正方向；0=尚未校准。 */
extern volatile uint32_t speed_calibrated;       /* 1=短暂定位和单向识别通过，已建立电角度零点。 */
extern volatile uint32_t speed_sensor_age_ms;    /* 最近一组有效角度距现在的时间。 */

#endif
