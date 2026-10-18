#include "motor_driver.h"
#include "motor_config.h"
#include "motor_pwm.h"
#include "main.h"
#include "tim.h"

/* 板级输出：TIM2 CH1/2/3 -> PA0/1/2 -> DRV8313 IN1/2/3 -> OUT1/2/3 -> U/V/W。
 * PA3 同时控制 EN1/2/3；它是总使能，不设置电机方向或转速。
 * DRV8313 内部完成上下 MOS 管切换及死区，MCU 发送三路逻辑 PWM 即可。
 */
volatile uint32_t pwm_test_running = 0;
volatile uint32_t live_pwm_u_ccr = MOTOR_TEST_NEUTRAL_CCR;
volatile uint32_t live_pwm_v_ccr = MOTOR_TEST_NEUTRAL_CCR;
volatile uint32_t live_pwm_w_ccr = MOTOR_TEST_NEUTRAL_CCR;

/* 只有这个函数实际写 TIM2 的三路 CCR，并同步 Live Watch 显示值。
 * 当前占空比约为 CCR/1800。CCR 预装载的新值在定时器更新事件装入生效寄存器。 */
static void WriteCompare(uint16_t u, uint16_t v, uint16_t w)
{
  __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, u);
  __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_2, v);
  __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_3, w);
  live_pwm_u_ccr = u;
  live_pwm_v_ccr = v;
  live_pwm_w_ccr = w;
}

void MotorDriver_Disable(void)
{
  /* Error_Handler 也会调用，所以兼顾 GPIOA 时钟尚未开启的启动阶段。
   * EN=0 后输出高阻，不等于 OUT 被主动拉到 0V，也不主动保持转子位置。 */
  if ((RCC->APB2ENR & RCC_APB2ENR_IOPAEN) != 0U)
  {
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_3, GPIO_PIN_RESET);
  }
}

void MotorDriver_Enable(void)
{
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_3, GPIO_PIN_SET);
}

bool MotorDriver_IsEnabled(void)
{
  /* 只读 MCU 的输出位，不能代替实际引脚测量或驱动器故障脚反馈。 */
  return (GPIOA->ODR & GPIO_PIN_3) != 0U;
}

void MotorDriver_SetNeutral(void)
{
  WriteCompare(MOTOR_TEST_NEUTRAL_CCR, MOTOR_TEST_NEUTRAL_CCR, MOTOR_TEST_NEUTRAL_CCR);
}

bool MotorDriver_Init(void)
{
  MotorDriver_Disable();
  /* CubeMX 已设好初始 CCR=900、ARR=1800，Start 后 TIM2 硬件连续发波。
   * PA3 此时仍为低，三路 PWM 已有波形也不会主动给电机通电。 */
  if (HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1) != HAL_OK ||
      HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_2) != HAL_OK ||
      HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_3) != HAL_OK)
  {
    return false;
  }
  pwm_test_running = 1;
  return true;
}

bool MotorDriver_CheckConfiguration(void)
{
  return htim2.Init.Period == MOTOR_PWM_PERIOD && htim2.Init.Prescaler == 0U &&
         htim2.Init.CounterMode == TIM_COUNTERMODE_CENTERALIGNED1;
}

bool MotorDriver_SetVector(float angle_rad, float volts)
{
  uint16_t compare[3];
  /* 纯数学部分在 motor_pwm.c；本文件只负责把结果交给硬件。 */
  if (!MotorPwmCalculate(angle_rad, volts, MOTOR_TEST_BUS_VOLTS, MOTOR_PWM_PERIOD, compare))
  {
    return false;
  }
  WriteCompare(compare[0], compare[1], compare[2]);
  return true;
}
