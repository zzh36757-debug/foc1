#include "current_sense.h"
#include "adc.h"

/* 电流采样链：相线电流 -> 0.01 欧电阻 -> INA240A2（50 倍）-> PA4/V、PA5/W。
 * REF 约 1.65V，因此零电流的 ADC 码值约 2048。先记零点，再用偏差换算电流。
 * 本版为异步连续采样、取平均，供低速试转观察和辅助停机检查，尚非 FOC 同步采样。
 */
volatile uint32_t live_adc_v_raw = 0;
volatile uint32_t live_adc_w_raw = 0;
volatile uint32_t live_adc_v_mv = 0;
volatile uint32_t live_adc_w_mv = 0;
volatile uint32_t live_adc_error = 0;
volatile int32_t live_i_u_ma = 0;
volatile int32_t live_i_v_ma = 0;
volatile int32_t live_i_w_ma = 0;

static uint32_t adc_zero_v = 0;
static uint32_t adc_zero_w = 0;
/* DMA 按 V,W,V,W,... 循环写入 64 个 16 位数据，即每相 32 个样本。 */
static volatile uint16_t adc_dma_samples[64] __attribute__((aligned(4)));

static void ReadAdcAverage(void)
{
  uint32_t sum_v = 0;
  uint32_t sum_w = 0;
  for (uint32_t sample = 0; sample < 64; sample += 2)
  {
    sum_v += adc_dma_samples[sample];
    sum_w += adc_dma_samples[sample + 1];
  }
  live_adc_v_raw = sum_v / 32;
  live_adc_w_raw = sum_w / 32;
  /* 12 位 ADC：0~4095。按 3.3V 参考电压换算 mV，加 2047 实现就近取整。
   * 这里是放大器输出电压，不是 U/V/W 端子上约 0/12V 的功率 PWM 电压。 */
  live_adc_v_mv = (live_adc_v_raw * 3300U + 2047U) / 4095U;
  live_adc_w_mv = (live_adc_w_raw * 3300U + 2047U) / 4095U;
  live_adc_error = HAL_ADC_GetError(&hadc1);
}

bool CurrentSense_Init(void)
{
  /* 此时驱动必须关闭。ADC 自身校准与下面的“传感器零点校准”是两件事。 */
  if (HAL_ADCEx_Calibration_Start(&hadc1) != HAL_OK)
  {
    return false;
  }
  /* HAL 形参使用 uint32_t 指针；DMA 实际宽度在 adc.c 中设为 16 位。 */
  if (HAL_ADC_Start_DMA(&hadc1, (uint32_t *)adc_dma_samples, 64) != HAL_OK)
  {
    return false;
  }
  /* 关闭频繁的半满/全满中断通知，数据搬运和状态标志仍继续工作。
   * 控制模块每 1ms 调用本模块读取数据，不在每次 ADC 转换后执行电机算法。 */
  __HAL_DMA_DISABLE_IT(hadc1.DMA_Handle, DMA_IT_HT | DMA_IT_TC);
  HAL_Delay(20);
  return true;
}

bool CurrentSense_CalibrateZero(void)
{
  ReadAdcAverage();
  adc_zero_v = live_adc_v_raw;
  adc_zero_w = live_adc_w_raw;
  return adc_zero_v >= 1800U && adc_zero_v <= 2300U &&
         adc_zero_w >= 1800U && adc_zero_w <= 2300U &&
         live_adc_error == HAL_ADC_ERROR_NONE;
}

void CurrentSense_Update(void)
{
  ReadAdcAverage();
  /* 1A -> 0.01欧*50=0.5V 的输出变化。
   * I(mA)=码值差*(3.3/4095)/(50*0.01)*1000=码值差*6600/4095。
   * 原板 IN+ 在电机侧、IN- 在驱动侧，流入电机时输出低于零点，所以用零点减读数。
   * 先转有符号 int32_t 再相减，保留电流方向。参考电压/增益偏差仍影响估算精度。 */
  live_i_v_ma = ((int32_t)adc_zero_v - (int32_t)live_adc_v_raw) * 6600 / 4095;
  live_i_w_ma = ((int32_t)adc_zero_w - (int32_t)live_adc_w_raw) * 6600 / 4095;
  /* 三线电机无中性线，iU+iV+iW=0，U 相由另外两相推算。 */
  live_i_u_ma = -live_i_v_ma - live_i_w_ma;
}

/* 采样诊断集中在这里。控制模块只接收“数据能不能用于本轮判断”的结果。 */
bool CurrentSense_CheckData(void)
{
  /* 每个转换约 (28.5+12.5)/12MHz，64 项约 0.22ms；每 1ms 应看到新的完成标志。 */
  if ((DMA1->ISR & DMA_ISR_TCIF1) == 0U || live_adc_error != HAL_ADC_ERROR_NONE)
  {
    return false;
  }
  /* 标志位写 1 清除，便于下一轮检查；不会停止 DMA。 */
  DMA1->IFCR = DMA_IFCR_CTCIF1 | DMA_IFCR_CHTIF1;
  return live_adc_v_raw >= 100U && live_adc_v_raw <= 3995U &&
         live_adc_w_raw >= 100U && live_adc_w_raw <= 3995U;
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
