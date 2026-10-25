#ifndef CURRENT_CONTROL_H
#define CURRENT_CONTROL_H

#include <stdbool.h>
#include <stdint.h>

/* 核心计算集中在 current_control.c；不读 I2C、不操作 ADC/定时器寄存器。
 * 输入一组相电流和与之对应的电角度，输出 U/V/W 三路 CCR。
 * mA 只在采样观察量中使用，本接口内的电流统一用 A。 */
void CurrentControl_Reset(void);
bool CurrentControl_Calculate(float angle_rad, float iv_a, float iw_a,
                              float iq_target_a, float dt, uint16_t compare[3]);
bool CurrentControl_CheckAlignment(float voltage_angle_rad, float iv_a, float iw_a);

extern volatile float current_kp_d;           /* V/A。首次带电需验证，非电机自动辨识值。 */
extern volatile float current_ki_d;           /* V/(A*s)。 */
extern volatile float current_kp_q;
extern volatile float current_ki_q;
extern volatile float current_iq_limit_a;     /* 目标 Iq 的限幅，默认 1A；不是硬件额定值。 */
extern volatile uint32_t current_mode;        /* 默认1=速度+电流双环；0=固定Iq测试。 */
extern volatile float current_test_iq_a;      /* mode=0 时使用，默认0.10A，仍受Iq上限约束。 */
extern const uint32_t current_test_duration_ms; /* 默认0=持续运行；非0=指定测试毫秒数，不含启动准备。 */
extern volatile uint32_t current_test_elapsed_ms; /* 已发布固定Iq指令的累计毫秒数。 */
extern volatile uint32_t current_test_done;   /* 1=到时正常关闭驱动；故障/提前停止不置1。 */
extern volatile float current_test_iq_error_max_a; /* 测试100ms之后，|目标Iq-实测Iq|最大值，停机保留。 */
extern volatile float current_test_id_abs_max_a;   /* 同一统计窗口的|Id|最大值，停机保留。 */
extern volatile float current_iq_target_a;    /* 实际使用的限幅后目标。 */
extern volatile float current_id_a;           /* Park 变换得到的反馈。 */
extern volatile float current_iq_a;
extern volatile float current_ud_volts;       /* 限幅后电压指令，并非实测电压。 */
extern volatile float current_uq_volts;
extern volatile float current_q_saturation_volts; /* 限幅前 Uq - 限幅后 Uq，供速度环防积分饱和。 */
extern volatile uint32_t current_voltage_limited;
extern volatile uint32_t current_alignment_stage; /* 历史定位检查：0未检查、1/2通过；跳过定位时保持0。 */
extern volatile float current_alignment_along_a;  /* 最后一次定位检查沿电压方向的电流。 */
extern volatile float current_alignment_across_a; /* 垂直方向电流；停机后保留。 */

#endif
