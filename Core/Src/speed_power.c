#include "speed_power.h"
#include "as5600.h"
#include "motor_config.h"
#include "motor_control.h"
#include "motor_driver.h"
#include "motor_pwm.h"
#include "main.h"
#include <math.h>

/* 速度闭环的主要阅读顺序：
 * 1. PollSensor() 读取角度；Update1ms() 用角度变化量算实际转速。
 * 2. 启动准备约 1 秒：短暂定位、单向小角度识别，随后直接进入闭环，不再反扫。
 * 3. 进入 SPEED 状态：速度误差 -> PI -> Uq -> SVPWM -> TIM2 三路 PWM。
 *
 * 负载变重 -> 转速下降 -> 误差增大 -> PI 提高 Uq，尝试把转速拉回去。
 * 这是“速度闭环 + 电压模式换相”；电流采样仍用于原来的停机检查，
 * 这里没有 Id/Iq 电流闭环，Uq 也不等于精确的转矩或恒定电流。
 * 结构参考 SimpleFOC：docs.simplefoc.com/velocity_loop 和 voltage_torque_mode。
 */

/* ===== 初次调试主要看这里；旧开环的 100Hz 不再决定闭环速度 ===== */
#define SPEED_POLE_PAIRS          7U      /* 已知电机为 7 对极。 */
#define SPEED_DEFAULT_RPM        300.0f    /* 默认机械目标：300rpm，即轴每秒转 5 圈。 */
#define SPEED_DEFAULT_KP          0.017f   /* 误差 10rpm，比例部分请求 0.17V。 */
#define SPEED_DEFAULT_KI          0.005f   /* 误差持续存在时，积分部分逐渐补足驱动力。 */
#define SPEED_UQ_LIMIT_VOLTS      MOTOR_TEST_VECTOR_VOLTS /* 沿用原 3.50V 上限。 */
#define SPEED_ALIGN_VOLTS         0.50f   /* 定位/方向识别电压，不用 3.50V 硬拉。 */
/* 合计 100+300+100+300+200=1000ms；这 1 秒用于必要的启动准备，不是额外等 1 秒。 */
#define SPEED_SENSOR_SETTLE_MS    100U    /* 驱动关闭，先取得有效角度。 */
#define SPEED_ALIGN_RAMP_MS       300U    /* 固定电角度 0，电压从 0 升至 0.50V。 */
#define SPEED_ALIGN_HOLD_MS       100U    /* 给转子短暂的定位时间。 */
#define SPEED_SWEEP_MS            300U    /* 只正扫 90 电角度，7 对极时轴约转 12.9 度。 */
#define SPEED_SWEEP_HOLD_MS       200U    /* 保持在 90 电角度处记录零点，随后直接闭环。 */
#define SPEED_SWEEP_ANGLE_RAD     (MOTOR_TEST_TWO_PI * 0.25f)
#define SPEED_SENSOR_TIMEOUT_MS   5500U    /* 不能拿很久以前的角度继续换相。 */
#define SPEED_PI_PERIOD_MS         1U    /* 速度 PI 为 1kHz，与 TIM3 的 1ms 换相更新一致。 */
#define SPEED_FILTER_SECONDS      0.02f  /* 测速滤波时间常数 20ms；更平滑，也会增加反馈滞后。 */
#define SPEED_ACCEL_RPM_PER_S    120.0f  /* 指令变化速率，非最高转速：0->300rpm 约 2.5 秒。 */
#define SPEED_HALF_PI             (MOTOR_TEST_TWO_PI * 0.25f)
#define SPEED_RAD_PER_COUNT       (MOTOR_TEST_TWO_PI / 4096.0f)

volatile float speed_target_rpm = SPEED_DEFAULT_RPM;
volatile float speed_kp = SPEED_DEFAULT_KP;
volatile float speed_ki = SPEED_DEFAULT_KI;
volatile uint32_t speed_run = 1;
volatile float speed_command_rpm = 0.0f;
volatile float speed_measured_rpm = 0.0f;
volatile float speed_error_rpm = 0.0f;
volatile float speed_requested_uq_volts = 0.0f;
volatile float speed_uq_volts = 0.0f;
volatile float speed_electrical_angle_rad = 0.0f;
volatile float speed_zero_electrical_rad = 0.0f;
volatile int32_t speed_sensor_direction = 0;
volatile uint32_t speed_calibrated = 0;
volatile uint32_t speed_sensor_age_ms = 0;

/* 主循环写，TIM3 读。主循环发布时只短暂屏蔽中断，保证角度和时间属于同一组。
 * I2C 传输本身始终在主循环执行，不在关中断区，也不在 TIM3 中断内等待。 */
typedef struct
{
  uint32_t raw;
  uint32_t tick_ms;
  uint32_t sequence;
  uint32_t valid;
} SpeedAngleSample;
static volatile SpeedAngleSample sensor_sample;
static uint32_t seen_sequence, previous_raw, velocity_start_ms;
static uint32_t step_ms, pi_ms;
static int32_t velocity_counts, calibration_counts;
static float integral_volts;
static bool have_sample;

static float Clamp(float value, float low, float high)
{
  if (value < low) { return low; }
  if (value > high) { return high; }
  return value;
}

static float WrapAngle(float angle)
{
  while (angle < 0.0f) { angle += MOTOR_TEST_TWO_PI; }
  while (angle >= MOTOR_TEST_TWO_PI) { angle -= MOTOR_TEST_TWO_PI; }
  return angle;
}

/* 复用已有 SVPWM 和驱动模块，不在这里再写一套 PWM 算法。 */
static bool SetVoltageVector(float angle, float volts)
{
  uint16_t compare[3];
  angle = WrapAngle(angle);
  if (!MotorPwmCalculate(angle, volts, MOTOR_TEST_BUS_VOLTS, MOTOR_PWM_PERIOD, compare))
  {
    MotorControl_Stop(MOTOR_FAULT_CONFIG);
    return false;
  }
  MotorDriver_SetPwm(compare[0], compare[1], compare[2]);
  motor_command_angle_mdeg = (uint32_t)(angle * (360000.0f / MOTOR_TEST_TWO_PI));
  return true;
}

void SpeedPower_Init(void)
{
  /* 此时尚未启动 TIM3，只清理本次运行状态，PWM/ADC 初始化仍由原模块负责。 */
  sensor_sample = (SpeedAngleSample){0};
  seen_sequence = previous_raw = velocity_start_ms = step_ms = pi_ms = 0;
  velocity_counts = calibration_counts = 0;
  integral_volts = 0.0f;
  have_sample = false;
  speed_command_rpm = speed_measured_rpm = speed_error_rpm = speed_uq_volts = 0.0f;
  speed_requested_uq_volts = 0.0f;
  speed_electrical_angle_rad = speed_zero_electrical_rad = 0.0f;
  speed_sensor_direction = 0;
  speed_calibrated = speed_sensor_age_ms = 0;
}

void SpeedPower_PollSensor(void)
{
  AS5600_Update();
  uint32_t now = HAL_GetTick();
  uint32_t irq_mask = __get_PRIMASK();
  __disable_irq();
  if (live_as5600_valid && live_as5600_hal_status == HAL_OK)
  {
    sensor_sample.raw = live_as5600_raw;
    sensor_sample.tick_ms = now;
    sensor_sample.sequence = live_as5600_read_count;
    sensor_sample.valid = 1;
  }
  else
  {
    sensor_sample.valid = 0; /* 失败时不更新旧角度的时间，防止旧数据冒充新数据。 */
  }
  __set_PRIMASK(irq_mask);
}

void SpeedPower_Update1ms(void)
{
  /* 原 ADC/电流/使能检查已经在 MotorControl_Update1ms() 中执行过。 */
  SpeedAngleSample sample = sensor_sample;
  uint32_t now = HAL_GetTick();
  speed_sensor_age_ms = now - sample.tick_ms;
  if (!speed_run)
  {
    MotorControl_Stop(MOTOR_FAULT_NONE);
    return;
  }
  if (motor_test_state != MOTOR_STATE_WAIT &&
      (!sample.valid || speed_sensor_age_ms > SPEED_SENSOR_TIMEOUT_MS))
  {
    MotorControl_Stop(MOTOR_FAULT_SENSOR);
    return;
  }

  /* ===== A. 测速：单圈角度差先处理 0/4095 回绕，再按实际间隔换成 rpm ===== */
  if (sample.valid && sample.sequence != seen_sequence)
  {
    if (have_sample)
    {
      int32_t delta = (int32_t)sample.raw - (int32_t)previous_raw;
      if (delta > 2048) { delta -= 4096; }
      if (delta < -2048) { delta += 4096; }
      velocity_counts += delta;
      if (motor_test_state == MOTOR_STATE_SENSOR_FORWARD)
      {
        calibration_counts += delta;
      }
      uint32_t elapsed_ms = sample.tick_ms - velocity_start_ms;
      /* 只有新的角度样本才重算速度；I2C 读取周期不等于 PI 的 1ms 调度周期。
       * 使用真实样本间隔 dt，避免将几毫秒的位移错误地当成 1ms 的位移。 */
      if (elapsed_ms >= SPEED_PI_PERIOD_MS)
      {
        float dt = (float)elapsed_ms * 0.001f;
        float direction = (speed_sensor_direction == -1) ? -1.0f : 1.0f;
        /* rpm = 码值变化/4096 * 60000/毫秒间隔；正方向由校准结果决定。 */
        float rpm = direction * (float)velocity_counts * (60000.0f / 4096.0f) / (float)elapsed_ms;
        speed_measured_rpm += dt / (SPEED_FILTER_SECONDS + dt) * (rpm - speed_measured_rpm);
        velocity_start_ms = sample.tick_ms;
        velocity_counts = 0;
      }
    }
    else
    {
      have_sample = true;
      velocity_start_ms = sample.tick_ms;
    }
    previous_raw = sample.raw;
    seen_sequence = sample.sequence;
  }

  /* ===== B. 约 1 秒内完成必要的准备：定位 -> 单向小角度识别 -> 直接速度闭环 ===== */
  step_ms++;
  if (motor_test_state == MOTOR_STATE_WAIT)
  {
    if (step_ms < SPEED_SENSOR_SETTLE_MS) { return; }
    if (!sample.valid || !have_sample || speed_sensor_age_ms > SPEED_SENSOR_TIMEOUT_MS)
    {
      MotorControl_Stop(MOTOR_FAULT_SENSOR);
      return;
    }
    if (SPEED_POLE_PAIRS == 0U || SPEED_UQ_LIMIT_VOLTS <= 0.0f ||
        SPEED_UQ_LIMIT_VOLTS > MOTOR_TEST_BUS_VOLTS * 0.57735026919f ||
        SPEED_ALIGN_VOLTS > SPEED_UQ_LIMIT_VOLTS)
    {
      MotorControl_Stop(MOTOR_FAULT_CONFIG);
      return;
    }
    step_ms = 0;
    motor_test_state = MOTOR_STATE_ALIGN_RAMP;
    MotorDriver_Enable();
  }
  else if (motor_test_state == MOTOR_STATE_ALIGN_RAMP)
  {
    if (!SetVoltageVector(0.0f, SPEED_ALIGN_VOLTS * (float)step_ms / (float)SPEED_ALIGN_RAMP_MS)) { return; }
    if (step_ms >= SPEED_ALIGN_RAMP_MS)
    {
      step_ms = 0;
      motor_test_state = MOTOR_STATE_ALIGN_HOLD;
    }
  }
  else if (motor_test_state == MOTOR_STATE_ALIGN_HOLD)
  {
    if (step_ms >= SPEED_ALIGN_HOLD_MS)
    {
      step_ms = 0;
      calibration_counts = 0;
      motor_test_state = MOTOR_STATE_SENSOR_FORWARD;
    }
  }
  else if (motor_test_state == MOTOR_STATE_SENSOR_FORWARD)
  {
    float fraction = Clamp((float)step_ms / (float)SPEED_SWEEP_MS, 0.0f, 1.0f);
    float angle = SPEED_SWEEP_ANGLE_RAD * fraction;
    if (!SetVoltageVector(angle, SPEED_ALIGN_VOLTS)) { return; }
    if (step_ms < SPEED_SWEEP_MS + SPEED_SWEEP_HOLD_MS) { return; }
    /* 90 电角度 = 1/4 电周期，7 对极时约 146 个编码器码值（12.9 机械度）。
     * 只用这一次单向位移辨认编码器符号，不再反扫，也不把当前任意位置当作电角度零点。 */
    float expected = 4096.0f / (float)SPEED_POLE_PAIRS *
        (SPEED_SWEEP_ANGLE_RAD / MOTOR_TEST_TWO_PI);
    if (fabsf((float)calibration_counts) < expected * 0.75f ||
        fabsf((float)calibration_counts) > expected * 1.25f)
    {
      MotorControl_Stop(MOTOR_FAULT_ALIGNMENT);
      return;
    }
    speed_sensor_direction = (calibration_counts > 0) ? 1 : -1;
    /* 停在的是 90 电角度，所以偏移 = 方向*机械角度*7 - 90度。
     * 这样进入闭环后，当前位置会被正确还原为 90 电角度，而不是错误地当作 0。 */
    speed_zero_electrical_rad = WrapAngle((float)speed_sensor_direction *
        (float)sample.raw * SPEED_RAD_PER_COUNT * (float)SPEED_POLE_PAIRS - SPEED_SWEEP_ANGLE_RAD);
    speed_calibrated = 1;
    integral_volts = speed_uq_volts = speed_measured_rpm = speed_command_rpm = 0.0f;
    speed_requested_uq_volts = 0.0f;
    velocity_counts = 0;
    velocity_start_ms = sample.tick_ms;
    pi_ms = step_ms = 0;
    motor_test_state = MOTOR_STATE_SPEED;
    (void)SetVoltageVector(SPEED_SWEEP_ANGLE_RAD, 0.0f);
  }
  else if (motor_test_state == MOTOR_STATE_SPEED)
  {
    /* ===== C. 速度 PI：每 1ms 调整一次 Uq（这里没有 D 项）===== */
    if (++pi_ms >= SPEED_PI_PERIOD_MS)
    {
      pi_ms = 0;
      /* dt=0.001 秒：积分和目标加速斜坡按这个时间步长换算。 */
      float dt = (float)SPEED_PI_PERIOD_MS * 0.001f;
      float target = speed_target_rpm;
      float kp = speed_kp;
      float ki = speed_ki;
      if (!isfinite(target) || !isfinite(kp) || !isfinite(ki) || kp < 0.0f || ki < 0.0f)
      {
        MotorControl_Stop(MOTOR_FAULT_CONFIG);
        return;
      }
      /* 直接采用设定转速，不再额外截成 +/-120rpm；下方斜坡只平滑目标变化。 */
      float ramp = SPEED_ACCEL_RPM_PER_S * dt;
      speed_command_rpm += Clamp(target - speed_command_rpm, -ramp, ramp);
      speed_error_rpm = speed_command_rpm - speed_measured_rpm;
      float proposed_i = integral_volts + ki * speed_error_rpm * dt;
      float proposed_uq = kp * speed_error_rpm + proposed_i;
      /* 电压已经顶到上限且误差仍要求加大时，暂停积分，避免积累后冲过头。 */
      if (!((proposed_uq > SPEED_UQ_LIMIT_VOLTS && speed_error_rpm > 0.0f) ||
            (proposed_uq < -SPEED_UQ_LIMIT_VOLTS && speed_error_rpm < 0.0f)))
      {
        integral_volts = Clamp(proposed_i, -SPEED_UQ_LIMIT_VOLTS, SPEED_UQ_LIMIT_VOLTS);
      }
      /* PI 请求仍受电压幅值上限约束，随后直接用于 PWM，不再经过电压变化速率限制。
       * 这里的 Uq 是软件电压指令，不是 ADC 测得的绕组电压或精确转矩。 */
      speed_requested_uq_volts = Clamp(kp * speed_error_rpm + integral_volts,
                                      -SPEED_UQ_LIMIT_VOLTS, SPEED_UQ_LIMIT_VOLTS);
      /* 直接使用已限制在 ±3.50V 内的 PI 请求电压。 */
      speed_uq_volts = speed_requested_uq_volts;
    }

    /* ===== D. 每 1ms 用实测位置换相：Ud=0，电压矢量在转子 d 轴前/后 90 度 =====
     * Uq>0：theta_e+90度，产生正向驱动力；Uq<0：theta_e-90度，反向施力。
     * 负电压不用负电源，实现为“矢量方向反过来，幅值取绝对值”。 */
    speed_electrical_angle_rad = WrapAngle((float)speed_sensor_direction *
        (float)sample.raw * SPEED_RAD_PER_COUNT * (float)SPEED_POLE_PAIRS - speed_zero_electrical_rad);
    float vector_angle = speed_electrical_angle_rad + ((speed_uq_volts >= 0.0f) ? SPEED_HALF_PI : -SPEED_HALF_PI);
    if (!SetVoltageVector(vector_angle, fabsf(speed_uq_volts))) { return; }
    motor_test_rotate_ticks++;
  }
}
