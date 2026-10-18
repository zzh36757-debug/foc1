#ifndef MOTOR_DRIVER_H
#define MOTOR_DRIVER_H

#include <stdbool.h>
#include <stdint.h>

/* 本模块操作板上的 TIM2 和 PA3。电机何时定位/转动/停止，由 motor_control.c 决定。 */
bool MotorDriver_Init(void);                  /* 关闭驱动并启动三路初始 50% PWM。 */
bool MotorDriver_CheckConfiguration(void);    /* 检查 TIM2 参数是否与本试转版本一致。 */
void MotorDriver_Enable(void);                /* PA3 拉高，允许三个半桥输出。 */
void MotorDriver_Disable(void);               /* PA3 拉低；启动早期 GPIO 未开时也可调用。 */
bool MotorDriver_IsEnabled(void);             /* 读取 MCU 的 PA3 输出位，非芯片 nFAULT。 */
void MotorDriver_SetNeutral(void);            /* 三路比较值都恢复 900，即 50%。 */
bool MotorDriver_SetVector(float angle_rad, float volts); /* 计算并写入三路 PWM；失败交由控制模块停机。 */

/* 保留原 Live Watch 名称。PWM 已启动不等于 DRV_EN 已使能。 */
extern volatile uint32_t pwm_test_running;
extern volatile uint32_t live_pwm_u_ccr;
extern volatile uint32_t live_pwm_v_ccr;
extern volatile uint32_t live_pwm_w_ccr;

#endif
