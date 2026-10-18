#include "motor_control.h"
#include "motor_config.h"
#include "motor_driver.h"
#include "motor_pwm.h"
#include "current_sense.h"
#include "main.h"
#include "tim.h"

/* 本文件安排“电机现在做什么”。
 * 采样/换算交给 current_sense.c；PWM 寄存器/使能交给 motor_driver.c；
 * SVPWM 公式在 motor_pwm.c；参数在 motor_config.h。
 * 本版是短时开环试转：自行推进指令电角度，尚未使用编码器反馈或 PID。
 *
 * 建议先读 RunMotorState() 看动作流程，再读末尾 Init()/Update1ms() 看调度。
 * 原有诊断集中在 CheckProtection()/CheckUpdateDuration()，与动作计算分开。
 */
volatile uint32_t motor_test_state = MOTOR_STATE_WAIT;
volatile uint32_t motor_test_fault = MOTOR_FAULT_NONE;
volatile uint32_t motor_test_peak_ma = 0;
volatile uint32_t motor_test_rotate_ticks = 0;
volatile uint32_t motor_command_angle_mdeg = 0;
volatile uint32_t motor_control_max_us = 0;

static uint32_t motor_test_step_ms = 0; /* 当前阶段的毫秒计时，换阶段时清零。 */
static float motor_field_angle = 0.0f; /* 指令电角度，单位 rad，不是实测轴角度。 */

void MotorControl_Stop(MotorFault fault)
{
  /* 先关闭半桥，再恢复三路 50%；关闭后不再主动保持轴的位置。 */
  MotorDriver_Disable();
  MotorDriver_SetPwm(MOTOR_TEST_NEUTRAL_CCR, MOTOR_TEST_NEUTRAL_CCR, MOTOR_TEST_NEUTRAL_CCR);
  motor_test_fault = fault;
  motor_test_state = (fault == MOTOR_FAULT_NONE) ? MOTOR_STATE_COMPLETE : MOTOR_STATE_FAULT;
}

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
    /* 0 等待：前 3 秒不通电，到时使能三个半桥，进入定位。 */
    if (motor_test_step_ms >= MOTOR_TEST_WAIT_MS)
    {
      motor_test_step_ms = 0;
      motor_test_state = MOTOR_STATE_ALIGN_RAMP;
      MotorDriver_Enable();
    }
  }
  else if (motor_test_state == MOTOR_STATE_ALIGN_RAMP)
  {
    /* 1 定位升幅：方向固定为 0rad，0.6 秒内从 0 升到 0.3V。
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
      /* 5 旋转：前 500ms，频率比例 factor 逐渐从 0 升到 1；电压幅值保持 0.3V。 */
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
    /* 转动的核心：delta_angle = 2*pi * 电频率 * 0.001秒 * 加减速比例。
     * 匀速时每 1ms 推进 0.00314rad=0.18 电角度，2 秒走完一个电角度周期。
     * 7 极对电机跟随时，机械角度=电角度/7。本版累计指令机械角度约 100 度。
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

/* 原有运行检查集中在这里。返回 true 才继续本轮动作，没有新增保护项。 */
static bool CheckProtection(void)
{
  /* 完成或故障后仍观察电流，但不再自动启动。 */
  if (motor_test_state == MOTOR_STATE_COMPLETE || motor_test_state == MOTOR_STATE_FAULT)
  {
    return false;
  }
  if (motor_test_state != MOTOR_STATE_WAIT && !MotorDriver_IsEnabled())
  {
    MotorControl_Stop(MOTOR_FAULT_DISABLED);
    return false;
  }
  if (!CurrentSense_CheckData())
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
  if (!MotorDriver_Init() || !CurrentSense_Init())
  {
    Error_Handler();
    return;
  }
  if (!CurrentSense_CalibrateZero())
  {
    MotorControl_Stop(MOTOR_FAULT_ZERO);
  }
  /* 原有启动检查：避免 CubeMX 定时参数与算法内的 1800、1ms 等假设不一致。 */
  if (!MotorDriver_CheckConfiguration() || htim3.Init.Period != 999U ||
      htim3.Init.Prescaler != 71U || SystemCoreClock != 72000000U)
  {
    MotorControl_Stop(MOTOR_FAULT_CONFIG);
  }
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
  if (HAL_TIM_Base_Start_IT(&htim3) != HAL_OK)
  {
    Error_Handler();
  }
}

void MotorControl_Update1ms(void)
{
  uint32_t start_cycles = DWT->CYCCNT;
  CurrentSense_Update();          /* 取样和电流换算。 */
  if (CheckProtection())
  {
    RunMotorState();              /* 按阶段推进角度，交给驱动层输出。 */
  }
  CheckUpdateDuration(start_cycles);
}
