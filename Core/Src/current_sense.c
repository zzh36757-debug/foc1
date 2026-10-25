#include "current_sense.h"
#include "adc.h"
#include "tim.h"
#include "motor_config.h"
#include "motor_control.h"

/* 采样链：V/W 相线 -> 0.01欧 -> INA240A2(50倍) -> PA4/PA5。
 * TIM2 CH4 只作内部触发，不把 PA3 改成复用输出，PA3 仍然是 DRV_EN。
 * TIM4 数 10 个 PWM 周期触发一次 ADC，两相完成后由 DMA 回调启动电流计算。 */
volatile uint32_t live_adc_v_raw, live_adc_w_raw;
volatile uint32_t live_adc_v_mv, live_adc_w_mv, live_adc_error;
volatile uint32_t live_adc_fault_reason, live_adc_fault_v_raw, live_adc_fault_w_raw;
volatile uint32_t live_adc_fault_hal_error;
volatile int32_t live_i_u_ma, live_i_v_ma, live_i_w_ma;
volatile uint32_t live_adc_sample_count, live_adc_sample_cycles;

static uint32_t adc_zero_v = 2048U, adc_zero_w = 2048U;
static volatile uint32_t zero_sum_v, zero_sum_w, zero_count;
static uint32_t checked_sequence;
/* 两个银行，每个银行是一对 V,W。半满读 [0,1]，全满读 [2,3]。
 * 回调只读已完成的一半；不会一边遍历平均，一边被 DMA 覆盖。 */
static volatile uint16_t adc_dma_samples[4] __attribute__((aligned(4)));

static bool LatchFault(uint32_t reason)
{
  uint32_t mask = __get_PRIMASK();
  __disable_irq();
  if (live_adc_fault_reason == 0U)
  {
    live_adc_fault_v_raw = live_adc_v_raw;
    live_adc_fault_w_raw = live_adc_w_raw;
    live_adc_fault_hal_error = live_adc_error;
    live_adc_fault_reason = reason;
  }
  __set_PRIMASK(mask);
  return false;
}

bool CurrentSense_Init(void)
{
  zero_sum_v = zero_sum_w = zero_count = 0;
  checked_sequence = live_adc_sample_count = live_adc_fault_reason = 0;
  if (HAL_ADCEx_Calibration_Start(&hadc1) != HAL_OK) { return false; }

  /* 定时器链路参考 RM0008：TIM4 的 ITR1 = TIM2_TRGO；
   * ADC1 常规组支持 TIM4_CC4。这里的 TIM4 由本模块管理，不输出 GPIO、不进 TIM4 中断。 */
  TIM_OC_InitTypeDef sample_oc = {0};
  sample_oc.OCMode = TIM_OCMODE_PWM2;
  sample_oc.Pulse = MOTOR_ADC_TRIGGER_CCR;
  sample_oc.OCPolarity = TIM_OCPOLARITY_HIGH;
  sample_oc.OCFastMode = TIM_OCFAST_DISABLE;
  TIM_MasterConfigTypeDef master = {0};
  master.MasterOutputTrigger = TIM_TRGO_OC4REF;
  master.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIM_PWM_ConfigChannel(&htim2, &sample_oc, TIM_CHANNEL_4) != HAL_OK ||
      HAL_TIMEx_MasterConfigSynchronization(&htim2, &master) != HAL_OK ||
      HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_4) != HAL_OK) { return false; }

  __HAL_RCC_TIM4_CLK_ENABLE();
  TIM4->CR1 = 0;
  TIM4->CR2 = 0;
  TIM4->DIER = 0;
  TIM4->PSC = 0;
  TIM4->ARR = MOTOR_CURRENT_PWM_DIVIDER - 1U;
  TIM4->CCR4 = MOTOR_CURRENT_PWM_DIVIDER - 1U;
  TIM4->CCMR1 = 0;
  /* F103 的 ADC TIM4_CC4 触发需要内部通道波形的有效边沿。
   * 冻结模式虽会置 CC4IF，却不会触发本板 ADC；PWM2 在 CNT=CCR4 时上升。
   * 这里只生成内部波形，不配置 PB9 或任何 GPIO 复用输出。 */
  TIM4->CCMR2 = TIM_OCMODE_PWM2 << 8U; /* OC4M=111，PWM2。 */
  TIM4->CCER = TIM_CCER_CC4E;
  TIM4->SMCR = TIM_TS_ITR1 | TIM_SLAVEMODE_EXTERNAL1;
  TIM4->EGR = TIM_EGR_UG;
  TIM4->SR = 0;
  if (HAL_ADC_Start_DMA(&hadc1, (uint32_t *)adc_dma_samples, 4U) != HAL_OK) { return false; }
  TIM4->CR1 = TIM_CR1_CEN;
  HAL_Delay(40); /* 驱动关闭时，累计 64 组零点，2kHz 下约需 32ms。 */
  if (live_adc_sample_count < 64U) { return LatchFault(1U); }
  return live_adc_fault_reason == 0U;
}

bool CurrentSense_CalibrateZero(void)
{
  if (zero_count != 64U || live_adc_fault_reason != 0U) { return false; }
  adc_zero_v = (zero_sum_v + 32U) / 64U;
  adc_zero_w = (zero_sum_w + 32U) / 64U;
  return adc_zero_v >= 1800U && adc_zero_v <= 2300U &&
         adc_zero_w >= 1800U && adc_zero_w <= 2300U &&
         live_adc_error == HAL_ADC_ERROR_NONE;
}

static void ConsumePair(uint32_t offset)
{
  uint32_t sample_cycles = DWT->CYCCNT;
  /* HT 对应另一半正在写，TC 同理；若通知积压到当前半区已重新开始写，拒绝旧数据。 */
  uint32_t remaining = __HAL_DMA_GET_COUNTER(hadc1.DMA_Handle);
  if ((offset == 0U && remaining > 2U) ||
      (offset == 2U && remaining <= 2U))
  {
    (void)LatchFault(5U);
  }
  uint32_t v = adc_dma_samples[offset];
  uint32_t w = adc_dma_samples[offset + 1U];
  live_adc_v_raw = v;
  live_adc_w_raw = w;
  live_adc_v_mv = (v * 3300U + 2047U) / 4095U;
  live_adc_w_mv = (w * 3300U + 2047U) / 4095U;
  live_adc_error = HAL_ADC_GetError(&hadc1);
  /* IN+ 在电机侧，所以“流入电机为正”时用零点减读数；1A 对应 0.5V。 */
  live_i_v_ma = ((int32_t)adc_zero_v - (int32_t)v) * 6600 / 4095;
  live_i_w_ma = ((int32_t)adc_zero_w - (int32_t)w) * 6600 / 4095;
  live_i_u_ma = -live_i_v_ma - live_i_w_ma;
  live_adc_sample_cycles = sample_cycles;
  live_adc_sample_count++;
  if (zero_count < 64U)
  {
    zero_sum_v += v;
    zero_sum_w += w;
    zero_count++;
  }
  MotorControl_OnCurrentSample();
}

void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef *hadc)
{
  if (hadc == &hadc1) { ConsumePair(0U); }
}

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
  if (hadc == &hadc1) { ConsumePair(2U); }
}

void HAL_ADC_ErrorCallback(ADC_HandleTypeDef *hadc)
{
  if (hadc == &hadc1)
  {
    live_adc_error = HAL_ADC_GetError(hadc);
    (void)LatchFault(2U);
    MotorControl_OnCurrentSample();
  }
}

bool CurrentSense_CheckSample(void)
{
  if (live_adc_fault_reason != 0U) { return false; }
  if (live_adc_error != HAL_ADC_ERROR_NONE) { return LatchFault(2U); }
  if (live_adc_v_raw < 100U || live_adc_v_raw > 4090U) { return LatchFault(3U); }
  if (live_adc_w_raw < 100U || live_adc_w_raw > 4090U) { return LatchFault(4U); }
  return true;
}

bool CurrentSense_CheckData(void)
{
  /* TIM3 只检查刷新；硬件/DMA 标志交给 HAL 中断处理，不能在此抢先清 TC/HT。 */
  if (live_adc_fault_reason != 0U) { return false; }
  uint32_t sequence = live_adc_sample_count;
  if (sequence == checked_sequence) { return LatchFault(1U); }
  checked_sequence = sequence;
  return true;
}

uint32_t CurrentSense_GetMaxAbsMa(void)
{
  uint32_t peak = (uint32_t)((live_i_u_ma < 0) ? -live_i_u_ma : live_i_u_ma);
  uint32_t abs_v = (uint32_t)((live_i_v_ma < 0) ? -live_i_v_ma : live_i_v_ma);
  uint32_t abs_w = (uint32_t)((live_i_w_ma < 0) ? -live_i_w_ma : live_i_w_ma);
  if (abs_v > peak) { peak = abs_v; }
  if (abs_w > peak) { peak = abs_w; }
  return peak;
}
