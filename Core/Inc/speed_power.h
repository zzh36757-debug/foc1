#ifndef SPEED_POWER_H
#define SPEED_POWER_H

#include <stdint.h>
#include <stdbool.h>

/* 启动加载motor_config.h中的实测方向/零点，不定位/扫角；测速/速度算法在本模块。
 * 固定Iq与测试时长在current_control.c。 */
void SpeedPower_Init(void);        /* 在 MotorControl_Init() 前调用一次。 */
void SpeedPower_PollSensor(void);  /* 主循环调用，I2C 读取不能放进 TIM3 中断。 */
void SpeedPower_Update1ms(void);   /* 由 MotorControl_Update1ms() 转发，不能再单独调用。 */
void SpeedPower_ResetOutput(void); /* 停机时清速度积分和已发布的电流指令。 */
bool SpeedPower_GetCurrentCommand(uint32_t now_cycles, float *angle_rad, float *iq_a);

/* 可在 Live Watch 中观察；目标、P、I 和 run 也可在运行时修改。 */
extern volatile float speed_target_rpm;          /* mode=1的诊断目标100rpm；mode=0不使用此目标。 */
extern volatile float speed_kp;                  /* A/rpm，本轮0.001，减弱外环以观察电流跟踪。 */
extern volatile float speed_ki;                  /* A/(rpm*s)，本轮0；重新启动清除旧积分。 */
extern volatile uint32_t speed_run;              /* 1=运行；写 0 停机，重新启动需复位。 */
extern volatile float speed_command_rpm;         /* 经过加减速限制的实际速度指令。 */
extern volatile float speed_measured_rpm;        /* 编码器测得并滤波后的机械转速。 */
extern volatile float speed_error_rpm;           /* 指令转速 - 实测转速。 */
extern volatile float speed_iq_target_a;         /* 速度 PI 或验证模式给出的目标 Iq。 */
extern volatile float speed_requested_uq_volts;  /* 兼容旧 Watch：现在是电流 PI 限幅前 Uq。 */
extern volatile float speed_uq_volts;            /* 兼容旧 Watch：现在是电流 PI 限幅后 Uq。 */
extern volatile float speed_electrical_angle_rad;/* 实测转子电角度，rad。 */
extern volatile float speed_zero_electrical_rad; /* 从motor_config.h加载的历史实测电角度偏移。 */
extern volatile int32_t speed_sensor_direction;  /* 已保存的编码器相对U/V/W方向，+1/-1。 */
extern volatile uint32_t speed_calibrated;       /* 1=角度有效且已加载标定；并非本次重新校准。 */
extern volatile uint32_t speed_sensor_age_ms;    /* 最近一组有效角度距现在的时间。 */

#endif
