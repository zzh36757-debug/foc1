#ifndef MOTOR_PWM_H
#define MOTOR_PWM_H

#include <stdbool.h>
#include <stdint.h>

/* 开环试转用的三相 SVPWM 计算接口（min/max 公共偏置法）。
 * angle_rad    输入：定子电压矢量的指令电角度，弧度；不是编码器实测角度。
 * vector_volts 输入：矢量幅值，V，并非 PWM 引脚高电平电压。
 * bus_volts    输入：计算所用母线电压，V；本次 12V。
 * period       输入：PWM 计数周期参数，与本版 TIM2 ARR=1800 对应。
 * compare[3]   输出：U/V/W 三路 CCR 比较值，占空比约为 compare[i]/period。
 * 返回 true：计算成功；false：参数非法。数组有效时，失败输出恢复 period/2。
 * 调用者必须处理失败并关闭驱动，本函数本身不控制 PA3 或定时器。
 */
bool MotorPwmCalculate(float angle_rad, float vector_volts, float bus_volts,
                       uint16_t period, uint16_t compare[3]);

/* 电流环入口：逆 Park 已得到 alpha/beta 电压，直接做逆 Clarke + SVPWM。
 * 避免先算 atan2/hypot，再在上面的接口里重新算 sin/cos。 */
bool MotorPwmCalculateAlphaBeta(float alpha, float beta, float bus_volts,
                                uint16_t period, uint16_t compare[3]);

#endif
