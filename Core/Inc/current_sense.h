#ifndef CURRENT_SENSE_H
#define CURRENT_SENSE_H

#include <stdbool.h>
#include <stdint.h>

/* TIM2 -> TIM4 -> ADC/DMA；每 500us 发布一组相电流，在 DMA 回调里运行电流内环。 */
bool CurrentSense_Init(void);
bool CurrentSense_CalibrateZero(void); /* 驱动关闭时用 64 组数据标定零点。 */
bool CurrentSense_CheckSample(void);   /* DMA 回调检查当前样本的错误/量程。 */
bool CurrentSense_CheckData(void);     /* TIM3 每 1ms 检查有无新样本。 */
uint32_t CurrentSense_GetMaxAbsMa(void);

/* raw 是这一组 ADC 值，ma 是这一组相电流，不再是原先的 32 点平均。 */
extern volatile uint32_t live_adc_v_raw, live_adc_w_raw;
extern volatile uint32_t live_adc_v_mv, live_adc_w_mv, live_adc_error;
extern volatile uint32_t live_adc_sample_count;  /* 预期每秒增加约 2000。 */
extern volatile uint32_t live_adc_sample_cycles; /* 本组 DMA 回调入口的 DWT 时刻。 */
/* 0正常、1未刷新、2HAL错误、3V越量程、4W越量程、5DMA通知积压/半区被覆盖。 */
extern volatile uint32_t live_adc_fault_reason;
extern volatile uint32_t live_adc_fault_v_raw, live_adc_fault_w_raw;
extern volatile uint32_t live_adc_fault_hal_error;
extern volatile int32_t live_i_u_ma, live_i_v_ma, live_i_w_ma;

#endif
