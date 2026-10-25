#ifndef POSITION_CONTROL_H
#define POSITION_CONTROL_H

#include <stdbool.h>
#include <stdint.h>

#define POSITION_REFERENCE_RELATIVE 0U
#define POSITION_REFERENCE_ABSOLUTE 1U

/* 本模块不读I2C、不写PWM；只累计机械角度，并把位置误差变成速度目标。
 * Init在启动前调用；Start在加载标定/使能前调用；Sample只消费新的编码器样本。
 * Update由现有TIM3速度调度每1ms调用，不能在main中重复执行。 */
void Position_Control_Init(void);
void Position_Control_Start(uint32_t raw, int32_t direction);
void Position_Control_OnSample(uint32_t raw);
bool Position_Control_Update(float *target_rpm);
void Position_Control_Stop(void);

extern volatile uint32_t position_enabled;        /* 1=位置->速度->电流；0=原速度->电流。需current_mode=1。 */
extern volatile uint32_t position_ready;          /* 1=有效绝对读数及本次相对位置零点均已建立。 */
extern volatile uint32_t position_reference_mode; /* 1=AS5600单圈绝对位置；0=相对启动位置（多圈）。 */
extern volatile float position_absolute_target_deg;   /* 绝对目标0~360度，360等同0。 */
extern volatile float position_absolute_measured_deg; /* raw*360/4096，不随方向标定取反。 */
extern volatile float position_relative_target_deg;   /* 相对启动位置目标，可正负、多圈。 */
extern volatile float position_relative_measured_deg; /* 累计机械角，沿原速度环正方向。 */
extern volatile float position_error_deg;         /* 当前模式误差：绝对为[-180,180)，相对不回绕。 */
extern volatile float position_kp;                /* rpm/deg：位置误差转换为目标速度。 */
extern volatile float position_speed_limit_rpm;   /* 位置运动的速度指令幅值上限。 */
extern volatile float position_tolerance_deg;     /* 误差在此范围内时给零速度，驱动仍保持使能。 */
extern volatile float position_speed_target_rpm;  /* 位置环输出，未经过独立加速斜坡。 */

#endif
