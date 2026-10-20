#ifndef CURRENT_SENSE_H
#define CURRENT_SENSE_H

#include <stdbool.h>
#include <stdint.h>

/* 本模块负责 ADC/DMA 采样、零点记录和电流换算，不输出 PWM、不安排电机动作。 */
bool CurrentSense_Init(void);          /* 校准并启动 ADC/DMA，等待首批数据；失败返回 false。 */
bool CurrentSense_CalibrateZero(void); /* 驱动关闭时记录零电流刻度；零点异常返回 false。 */
void CurrentSense_Update(void);        /* 更新平均采样值、电压和 U/V/W 估算电流。 */
bool CurrentSense_CheckData(void);     /* 检查采样刷新/量程，并清除本轮 DMA 完成标志。 */
uint32_t CurrentSense_GetMaxAbsMa(void);/* 当前三相估算电流绝对值中的最大值，mA。 */

/* 保留原 Live Watch 名称。raw 是 ADC 码值，mv 是采样放大器电压，ma 是电流。 */
extern volatile uint32_t live_adc_v_raw;
extern volatile uint32_t live_adc_w_raw;
extern volatile uint32_t live_adc_v_mv;
extern volatile uint32_t live_adc_w_mv;
extern volatile uint32_t live_adc_error;
/* 采样检查结果在 fault=2 停机后保留，可直接放进 Live Watch。 */
extern volatile uint32_t live_adc_fault_reason; /* 0正常、1未刷新、2 HAL错误、3 V相越量程、4 W相越量程。 */
extern volatile uint32_t live_adc_fault_v_raw;  /* 最后一次检查时的 V 相 ADC 平均码值。 */
extern volatile uint32_t live_adc_fault_w_raw;  /* 最后一次检查时的 W 相 ADC 平均码值。 */
extern volatile uint32_t live_adc_fault_hal_error; /* 最后一次检查时的 HAL ADC 错误位。 */
extern volatile int32_t live_i_u_ma;
extern volatile int32_t live_i_v_ma;
extern volatile int32_t live_i_w_ma;

#endif
