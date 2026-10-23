#ifndef MOTOR_CONTROL_H
#define MOTOR_CONTROL_H

#include <stdint.h>

/* 用名称替代状态判断中的数字。数值保持原样，已有 Live Watch 无需修改。
 * 开环：WAIT -> ALIGN_RAMP -> ALIGN_HOLD -> ROTATE -> RAMP_DOWN -> COMPLETE。
 * 闭环：WAIT -> ALIGN_RAMP -> ALIGN_HOLD -> SENSOR_FORWARD -> SPEED，准备合计约 1 秒。
 */
typedef enum
{
  MOTOR_STATE_WAIT = 0,
  MOTOR_STATE_ALIGN_RAMP = 1,
  MOTOR_STATE_ALIGN_HOLD = 2,
  MOTOR_STATE_COMPLETE = 3,
  MOTOR_STATE_FAULT = 4,
  MOTOR_STATE_ROTATE = 5,
  MOTOR_STATE_RAMP_DOWN = 6,
  MOTOR_STATE_SENSOR_FORWARD = 7,  /* 闭环启动：单向小角度识别，不再回扫。 */
  MOTOR_STATE_SENSOR_BACKWARD = 8, /* 保留旧编号；当前启动流程不使用。 */
  MOTOR_STATE_SPEED = 9            /* 持续速度闭环。 */
} MotorState;

/* 0~6 保留原有软件诊断码，7/8 用于闭环所需的角度与校准检查。
 * 原板 nFAULT 未接 MCU，fault=0 不能代替 DRV8313 芯片故障检测。
 */
typedef enum
{
  MOTOR_FAULT_NONE = 0,
  MOTOR_FAULT_ZERO = 1,
  MOTOR_FAULT_ADC = 2,
  MOTOR_FAULT_CURRENT = 3,
  MOTOR_FAULT_DISABLED = 4,
  MOTOR_FAULT_TIME = 5,
  MOTOR_FAULT_CONFIG = 6,
  MOTOR_FAULT_SENSOR = 7,    /* 闭环角度无效、通信失败或数据过期。 */
  MOTOR_FAULT_ALIGNMENT = 8  /* 校准时实测位移与 7 对极的预期不符。 */
} MotorFault;

void MotorControl_Init(void);            /* 启动输出/采样模块，检查初始状态，最后开启 TIM3。 */
void MotorControl_Update1ms(void);       /* 只从 TIM3 的 1ms 回调调用一次。 */
void MotorControl_Stop(MotorFault fault);/* 关闭驱动并记录完成/故障，复位前不自动重试。 */

extern volatile uint32_t motor_test_state;
extern volatile uint32_t motor_test_fault;
extern volatile uint32_t motor_test_peak_ma;      /* 平均采样估算值的历史最大绝对值，mA。 */
extern volatile uint32_t motor_test_rotate_ticks; /* 指令角度更新次数，非转数。 */
extern volatile uint32_t motor_command_angle_mdeg;/* 指令电角度，千分之一度，非编码器读数。 */
extern volatile uint32_t motor_control_max_us;    /* 一次更新所用的最长时间，us。 */

#endif
