#include "motor_control.h"
#include "vofa.h"
#include "motor_config.h"
#include "motor_driver.h"
#include "motor_pwm.h"
#include "current_sense.h"
#include "main.h"
#include "tim.h"
#include "speed_power.h"
#include "current_control.h"

/* 本文件安排“电机现在做什么”。
 * 采样/换算交给 current_sense.c；PWM 寄存器/使能交给 motor_driver.c；
 * SVPWM 公式在 motor_pwm.c；参数在 motor_config.h。
 * MOTOR_USE_SPEED_LOOP=1 时，动作交给 speed_power.c 的速度闭环；
 * 设为 0 时保留原来的短时开环试转。两者共用同步采样和停机检查。
 * 电流内环在 ADC/DMA 的 2kHz 回调执行，TIM3 只处理 1kHz 速度外环/启动流程。
 *
 * 当前闭环动作先读 speed_power.c 的 SpeedPower_Update1ms()；RunMotorState() 只用于开环。
 * Init()/Update1ms() 负责初始化和速度外环，OnCurrentSample() 负责电流内环。
 * 原有诊断集中在 CheckProtection()/CheckUpdateDuration()，与动作计算分开。
 */
volatile uint32_t motor_test_state = MOTOR_STATE_WAIT;
volatile uint32_t motor_test_fault = MOTOR_FAULT_NONE;
volatile uint32_t motor_test_peak_ma = 0;
volatile uint32_t motor_test_rotate_ticks = 0;
volatile uint32_t motor_command_angle_mdeg = 0;
volatile uint32_t motor_control_max_us = 0;
volatile uint32_t current_control_max_us = 0;
volatile uint32_t current_control_period_us = 0;
volatile uint32_t current_control_count = 0;
static volatile uint32_t control_ready;
static uint32_t last_sample_cycles;

#if !MOTOR_USE_SPEED_LOOP
static uint32_t motor_test_step_ms = 0; /* 当前阶段的毫秒计时，换阶段时清零。 */
static float motor_field_angle = 0.0f; /* 指令电角度，单位 rad，不是实测轴角度。 */
#endif

void MotorControl_Stop(MotorFault fault)
{
  uint32_t mask = __get_PRIMASK();
  __disable_irq();
  /* DMA 可抢占 TIM3；保留第一次故障，不让后续退出路径覆盖根因。 */
  if (motor_test_state == MOTOR_STATE_FAULT)
  {
    __set_PRIMASK(mask);
    return;
  }
  /* 先关闭半桥，再恢复三路 50%；关闭后不再主动保持轴的位置。 */
  MotorDriver_Disable();
  MotorDriver_SetPwm(MOTOR_TEST_NEUTRAL_CCR, MOTOR_TEST_NEUTRAL_CCR, MOTOR_TEST_NEUTRAL_CCR);
  motor_test_fault = fault;
  motor_test_state = (fault == MOTOR_FAULT_NONE) ? MOTOR_STATE_COMPLETE : MOTOR_STATE_FAULT;
#if MOTOR_USE_SPEED_LOOP
  SpeedPower_ResetOutput();
#endif
  CurrentControl_Reset();
  __set_PRIMASK(mask);
}

#if !MOTOR_USE_SPEED_LOOP
static void SetMotorVector(float angle, float volts)
{
  uint16_t compare[3];
  /* 第一步：用方向和幅值算出 U/V/W 三路比较值；公式在 motor_pwm.c。
   * 例如 angle=0、volts=0.3 时，得到约 [934,866,866]。 */
  if (!MotorPwmCalculate(angle, volts, MOTOR_TEST_BUS_VOLTS, MOTOR_PWM_PERIOD, compare))
  {
    MotorControl_Stop(MOTOR_FAULT_CONFIG);
    return;
  }
  /* 第二步：把三个比较值写入 TIM2 的三路 CCR，更新实际占空比。 */
  MotorDriver_SetPwm(compare[0], compare[1], compare[2]);
  /* 弧度换成千分之一度，只供 Live Watch 观察；90000 表示 90 电角度。 */
  motor_command_angle_mdeg = (uint32_t)(angle * (360000.0f / MOTOR_TEST_TWO_PI));
}

/* 电机动作的主流程：每次调用经过 1ms，按当前阶段更新指令。 */
static void RunMotorState(void)
{
  motor_test_step_ms++;
  if (motor_test_state == MOTOR_STATE_WAIT)
  {
    /* 0 等待：前 1 秒不通电，到时使能三个半桥，进入定位。 */
    if (motor_test_step_ms >= MOTOR_TEST_WAIT_MS)
    {
      motor_test_step_ms = 0;
      motor_test_state = MOTOR_STATE_ALIGN_RAMP;
      MotorDriver_Enable();
    }
  }
  else if (motor_test_state == MOTOR_STATE_ALIGN_RAMP)
  {
    /* 1 定位升幅：方向固定为 0rad，0.6 秒内从 0 升到配置的电压幅值。
     * 先让转子靠近一个固定磁场方向，再开始旋转；此时没有编码器确认到位。 */
    uint32_t ramp_ms = motor_test_step_ms;
    if (ramp_ms > MOTOR_TEST_RAMP_MS) { ramp_ms = MOTOR_TEST_RAMP_MS; }
    SetMotorVector(0.0f, MOTOR_TEST_VECTOR_VOLTS *
        (float)ramp_ms / (float)MOTOR_TEST_RAMP_MS);
    if (motor_test_state == MOTOR_STATE_FAULT) { return; }
    if (motor_test_step_ms >= MOTOR_TEST_RAMP_MS)
    {
      motor_test_step_ms = 0;
      motor_test_state = MOTOR_STATE_ALIGN_HOLD;
    }
  }
  else if (motor_test_state == MOTOR_STATE_ALIGN_HOLD)
  {
    /* 2 定位保持：沿用最后一组占空比 0.3 秒，TIM2 硬件继续输出 20kHz。 */
    if (motor_test_step_ms >= MOTOR_TEST_HOLD_MS)
    {
      motor_test_step_ms = 0;
      motor_test_state = MOTOR_STATE_ROTATE;
    }
  }
  else if (motor_test_state == MOTOR_STATE_ROTATE || motor_test_state == MOTOR_STATE_RAMP_DOWN)
  {
    float factor;
    float volts = MOTOR_TEST_VECTOR_VOLTS;
    if (motor_test_state == MOTOR_STATE_ROTATE)
    {
      /* 5 旋转：前 500ms，频率比例 factor 逐渐从 0 升到 1；电压幅值保持配置值。 */
      factor = (motor_test_step_ms < MOTOR_TEST_ACCEL_MS) ?
          (float)motor_test_step_ms / (float)MOTOR_TEST_ACCEL_MS : 1.0f;
    }
    else
    {
      /* 6 减速：300ms 内同时减小指令频率和电压幅值，结束后正常关闭驱动。 */
      if (motor_test_step_ms >= MOTOR_TEST_DOWN_MS)
      {
        MotorControl_Stop(MOTOR_FAULT_NONE);
        return;
      }
      factor = 1.0f - (float)motor_test_step_ms / (float)MOTOR_TEST_DOWN_MS;
      volts *= factor;
    }
    /* 开环转动：delta_angle = 2*pi * 电频率 * 0.001秒 * 加减速比例。
     * 具体每次推进多少由 MOTOR_TEST_ELECTRICAL_HZ 决定。
     * 7 极对电机跟随时，机械角度=电角度/7；累计角度还取决于试转时长。
     * 每次给新方向，SetMotorVector() 先计算三路 PWM，再交给驱动模块写入 TIM2。
     */
    motor_field_angle += MOTOR_TEST_TWO_PI * MOTOR_TEST_ELECTRICAL_HZ * 0.001f * factor;
    /* 2*pi 与 0 的方向相同，走完一圈后回绕，避免角度无限增大。 */
    if (motor_field_angle >= MOTOR_TEST_TWO_PI) { motor_field_angle -= MOTOR_TEST_TWO_PI; }
    motor_test_rotate_ticks++;
    SetMotorVector(motor_field_angle, volts);
    if (motor_test_state == MOTOR_STATE_FAULT) { return; }
    if (motor_test_state == MOTOR_STATE_ROTATE && motor_test_step_ms >= MOTOR_TEST_RUN_MS)
    {
      motor_test_step_ms = 0;
      motor_test_state = MOTOR_STATE_RAMP_DOWN;
    }
  }
}

#endif

/* 原有运行检查集中在这里。闭环所需的角度/校准检查放在 speed_power.c。 */
static bool CheckProtection(void)
{
  /* 完成或故障后仍观察电流，但不再自动启动。 */
  if (motor_test_state == MOTOR_STATE_COMPLETE || motor_test_state == MOTOR_STATE_FAULT)
  {
    return false;
  }
#if MOTOR_CHECK_DRV_ENABLE
  if (motor_test_state != MOTOR_STATE_WAIT && !MotorDriver_IsEnabled())
  {
    MotorControl_Stop(MOTOR_FAULT_DISABLED);
    return false;
  }
#endif
  if (!CurrentSense_CheckSample())
  {
    MotorControl_Stop(MOTOR_FAULT_ADC);
    return false;
  }
  uint32_t peak = CurrentSense_GetMaxAbsMa();
  if (peak > motor_test_peak_ma) { motor_test_peak_ma = peak; }
  if (peak >= MOTOR_TEST_LIMIT_MA)
  {
    MotorControl_Stop(MOTOR_FAULT_CURRENT);
    return false;
  }
  return true;
}

static void CheckUpdateDuration(uint32_t start_cycles)
{
  /* DWT 计数器仅测量这段更新用时，不产生 PWM，也不设置电机转速。
   * 沿用原版超过 500us 的停机检查，相关诊断不混在角度推进公式中。 */
  uint32_t elapsed_cycles = DWT->CYCCNT - start_cycles;
  uint32_t elapsed_us = elapsed_cycles / (SystemCoreClock / 1000000U);
  if (elapsed_us > motor_control_max_us) { motor_control_max_us = elapsed_us; }
  if (elapsed_cycles > SystemCoreClock / 2000U &&
      motor_test_state != MOTOR_STATE_COMPLETE && motor_test_state != MOTOR_STATE_FAULT)
  {
    MotorControl_Stop(MOTOR_FAULT_TIME);
  }
}

void MotorControl_Init(void)
{
  /* main.c 已完成 CubeMX 外设配置。本函数只在每次启动时调用一次。
   * 输出模块先保持关闭，再准备 PWM 和采样；最后才开始 1ms 控制更新。 */
  control_ready = 0;
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
  CurrentControl_Reset();
  if (!MotorDriver_Init())
  {
    MotorControl_Stop(MOTOR_FAULT_CONFIG);
    Error_Handler();
    return;
  }
  if (!CurrentSense_Init())
  {
    MotorControl_Stop(MOTOR_FAULT_ADC);
    Error_Handler();
    return;
  }
  if (!CurrentSense_CalibrateZero())
  {
    MotorControl_Stop(MOTOR_FAULT_ZERO);
  }
  /* 原有启动检查：避免 CubeMX 定时参数与算法内的 1800、1ms 等假设不一致。 */
  if (!MotorDriver_CheckConfiguration() || htim3.Init.Period != 999U ||
      htim3.Init.Prescaler != 71U || SystemCoreClock != 72000000U ||
      MOTOR_CURRENT_PWM_DIVIDER < 2U ||
      SystemCoreClock / (2U * MOTOR_PWM_PERIOD * MOTOR_CURRENT_PWM_DIVIDER) != MOTOR_CURRENT_HZ)
  {
    MotorControl_Stop(MOTOR_FAULT_CONFIG);
  }
  last_sample_cycles = live_adc_sample_cycles;
#if !MOTOR_USE_SPEED_LOOP
  /* 旧开环的状态/PWM 在 TIM3 内一起更新，与 DMA 同优先级，避免中途抢占改写状态。 */
  HAL_NVIC_SetPriority(TIM3_IRQn, 0, 0);
#endif
  control_ready = 1;
  if (HAL_TIM_Base_Start_IT(&htim3) != HAL_OK)
  {
    Error_Handler();
  }
}

void MotorControl_Update1ms(void)
{
  uint32_t start_cycles = DWT->CYCCNT;
  /* 相电流已由 DMA 发布。此处只检查流是否持续刷新，快速保护由采样回调执行。 */
  if (motor_test_state != MOTOR_STATE_COMPLETE && motor_test_state != MOTOR_STATE_FAULT &&
      !CurrentSense_CheckData())
  {
    MotorControl_Stop(MOTOR_FAULT_ADC);
  }
  if (motor_test_state != MOTOR_STATE_COMPLETE && motor_test_state != MOTOR_STATE_FAULT)
  {
#if MOTOR_USE_SPEED_LOOP
    SpeedPower_Update1ms();       /* 实测速度 -> 速度 PI -> 目标 Iq。 */
#else
    RunMotorState();              /* 开环：按时间推进指令角度。 */
#endif
  }
  CheckUpdateDuration(start_cycles);
}

void MotorControl_OnCurrentSample(void)
{
  /* 初始化零点期间 DMA 已在工作，但不运行控制或使能驱动。 */
  if (!control_ready) { return; }
  uint32_t start = live_adc_sample_cycles;
  uint32_t elapsed = start - last_sample_cycles;
  last_sample_cycles = start;
  if (!CheckProtection()) { return; }
#if MOTOR_USE_SPEED_LOOP
  if (motor_test_state != MOTOR_STATE_SPEED) { return; }
  current_control_period_us = elapsed / (SystemCoreClock / 1000000U);
  /* 丢掉一次 500us 控制已会改变 PI 时序；不拿旧积分步长继续追赶输出。 */
  if (elapsed > (SystemCoreClock / MOTOR_CURRENT_HZ) * 3U / 2U)
  {
    MotorControl_Stop(MOTOR_FAULT_TIME);
    return;
  }
  float angle, iq_target;
  if (!SpeedPower_GetCurrentCommand(start, &angle, &iq_target))
  {
    MotorControl_Stop(MOTOR_FAULT_SENSOR);
    return;
  }
  uint16_t compare[3];
  if (!CurrentControl_Calculate(angle, (float)live_i_v_ma * 0.001f,
                                (float)live_i_w_ma * 0.001f, iq_target,
                                MOTOR_CURRENT_DT_SECONDS, compare))
  {
    MotorControl_Stop(MOTOR_FAULT_CONFIG);
    return;
  }
  MotorDriver_SetPwm(compare[0], compare[1], compare[2]);
  speed_uq_volts = current_uq_volts;
  Vofa_PushCurrents(current_iq_a, current_id_a); /* 同一轮反馈；只入队，不发送等待。 */
  speed_requested_uq_volts = current_uq_volts + current_q_saturation_volts;
  current_control_count++;
  motor_test_rotate_ticks++;
  uint32_t used_cycles = DWT->CYCCNT - start;
  uint32_t used_us = used_cycles / (SystemCoreClock / 1000000U);
  if (used_us > current_control_max_us) { current_control_max_us = used_us; }
  if (used_cycles > SystemCoreClock / (MOTOR_CURRENT_HZ * 2U))
  {
    MotorControl_Stop(MOTOR_FAULT_TIME);
  }
#else
  (void)elapsed;
#endif
}
