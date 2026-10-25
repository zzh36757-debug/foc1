#include "speed_power.h"
#include "as5600.h"
#include "motor_config.h"
#include "motor_control.h"
#include "motor_driver.h"
#include "current_control.h"
#include "position_control.h"
#include "main.h"
#include <math.h>

/* 速度闭环的主要阅读顺序：
 * 1. PollSensor() 读取角度；Update1ms() 用角度变化量算实际转速。
 * 2. 等待100ms取得新鲜角度，加载本电机已保存的方向/零点，直接进入闭环。
 * 3. 进入SPEED状态：mode=0固定Iq；mode=1由速度PI给Iq，电流环负责PWM。
 *    mode=1且position_enabled=1时，由position_control.c计算速度目标，构成三环。
 *
 * 负载变重 -> 转速下降 -> PI 提高目标 Iq -> 电流环建立电流 -> 增加扭矩。
 * 电流算法集中在 current_control.c；电流环执行与 PWM 写入由 motor_control.c 调度。
 * 结构参考 SimpleFOC：docs.simplefoc.com/velocity_loop 和 foc_current_torque_mode。
 */

/* ===== 初次调试主要看这里；旧开环的 100Hz 不再决定闭环速度 ===== */
#define SPEED_POLE_PAIRS          7U      /* 已知电机为 7 对极。 */
#define SPEED_DEFAULT_RPM        300.0f    /* 临时低速诊断目标，非额外最高转速限制。 */
#define SPEED_DEFAULT_KP          0.003f   /* A/rpm：保留用户已调的速度比例增益。 */
#define SPEED_DEFAULT_KI          0.0005f  /* A/(rpm*s)：保留用户已调的速度积分增益。 */
#define SPEED_UQ_LIMIT_VOLTS      MOTOR_TEST_VECTOR_VOLTS /* 3.80V 总电压预算。 */
#define SPEED_SENSOR_SETTLE_MS    100U    /* 驱动关闭，先取得有效角度。 */
#define SPEED_SENSOR_TIMEOUT_MS   5500U    /* 不能拿很久以前的角度继续换相。 */
#define SPEED_PI_PERIOD_MS         1U    /* 速度 PI 为 1kHz；电流/PWM 指令为 2kHz。 */
#define SPEED_FILTER_SECONDS      0.02f  /* 测速滤波时间常数 20ms；更平滑，也会增加反馈滞后。 */
#define SPEED_ACCEL_RPM_PER_S    120.0f  /* 指令变化速率，非最高转速：0->100rpm 约 0.83 秒。 */
#define SPEED_CURRENT_ANGLE_MAX_MS 15U   /* 电流模式新要求：拒绝超过 15ms 的角度，不外推陈旧数据。 */
#define SPEED_RAD_PER_COUNT       (MOTOR_TEST_TWO_PI / 4096.0f)

volatile float speed_target_rpm = SPEED_DEFAULT_RPM;
volatile float speed_kp = SPEED_DEFAULT_KP;
volatile float speed_ki = SPEED_DEFAULT_KI;
volatile uint32_t speed_run = 1;
volatile float speed_command_rpm = 0.0f;
volatile float speed_measured_rpm = 0.0f;
volatile float speed_error_rpm = 0.0f;
volatile float speed_iq_target_a = 0.0f;
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
  uint32_t cycles;
  uint32_t sequence;
  uint32_t valid;
} SpeedAngleSample;
static volatile SpeedAngleSample sensor_sample;
static uint32_t seen_sequence, previous_raw, velocity_start_ms;
static uint32_t step_ms, pi_ms;
static int32_t velocity_counts;
static float integral_amps;
static bool have_sample;
static uint32_t previous_position_mode;

/* TIM3 发布、DMA 快速环读取。发布时短暂关中断，避免目标/角度来自不同轮。 */
typedef struct
{
  float iq_a, angle_rad, velocity_rad_s;
  uint32_t angle_cycles, valid;
} SpeedCurrentCommand;
static volatile SpeedCurrentCommand current_command;

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

void SpeedPower_Init(void)
{
  /* 此时尚未启动 TIM3，只清理本次运行状态，PWM/ADC 初始化仍由原模块负责。 */
  sensor_sample = (SpeedAngleSample){0};
  seen_sequence = previous_raw = velocity_start_ms = step_ms = pi_ms = 0;
  velocity_counts = 0;
  integral_amps = 0.0f;
  have_sample = false;
  previous_position_mode = 0;
  Position_Control_Init();
  speed_command_rpm = speed_measured_rpm = speed_error_rpm = speed_uq_volts = 0.0f;
  speed_requested_uq_volts = 0.0f;
  speed_iq_target_a = 0.0f;
  current_command = (SpeedCurrentCommand){0};
  current_test_elapsed_ms = current_test_done = 0;
  current_test_iq_error_max_a = current_test_id_abs_max_a = 0.0f;
  CurrentControl_Reset();
  current_alignment_stage = 0;
  speed_electrical_angle_rad = speed_zero_electrical_rad = 0.0f;
  speed_sensor_direction = MOTOR_SENSOR_DIRECTION;
  speed_calibrated = speed_sensor_age_ms = 0;
}

void SpeedPower_PollSensor(void)
{
  AS5600_Update();
  uint32_t irq_mask = __get_PRIMASK();
  __disable_irq();
  if (live_as5600_valid && live_as5600_hal_status == HAL_OK)
  {
    sensor_sample.raw = live_as5600_raw;
    sensor_sample.tick_ms = live_as5600_angle_tick_ms;
    sensor_sample.cycles = live_as5600_angle_cycles;
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
    Position_Control_OnSample(sample.raw);
    if (have_sample)
    {
      int32_t delta = (int32_t)sample.raw - (int32_t)previous_raw;
      if (delta > 2048) { delta -= 4096; }
      if (delta < -2048) { delta += 4096; }
      velocity_counts += delta;
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

  /* B. 使用实测标定直接启动，不施加定位电压，不扫动转子。 */
  step_ms++;
  if (motor_test_state == MOTOR_STATE_WAIT)
  {
    if (step_ms < SPEED_SENSOR_SETTLE_MS) { return; }
    uint32_t age_cycles = DWT->CYCCNT - sample.cycles;
    if (!sample.valid || !have_sample ||
        age_cycles > (SystemCoreClock / 1000U) * SPEED_CURRENT_ANGLE_MAX_MS)
    {
      MotorControl_Stop(MOTOR_FAULT_SENSOR);
      return;
    }
    float zero = MOTOR_ZERO_ELECTRICAL_RAD;
    int32_t direction = MOTOR_SENSOR_DIRECTION;
    if (SPEED_POLE_PAIRS == 0U || !isfinite(zero) || zero < 0.0f ||
        zero >= MOTOR_TEST_TWO_PI || (direction != 1 && direction != -1) ||
        !isfinite(SPEED_UQ_LIMIT_VOLTS) || SPEED_UQ_LIMIT_VOLTS <= 0.0f ||
        SPEED_UQ_LIMIT_VOLTS > MOTOR_TEST_BUS_VOLTS * 0.57735026919f)
    {
      MotorControl_Stop(MOTOR_FAULT_CONFIG);
      return;
    }
    float angle = WrapAngle((float)direction * (float)sample.raw *
        SPEED_RAD_PER_COUNT * (float)SPEED_POLE_PAIRS - zero);
    /* 使能前发布有效的零Iq指令；短暂互斥，防止DMA看到半组状态，
     * 也不能在快速保护已停机后重新开启驱动。 */
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    if (motor_test_state == MOTOR_STATE_WAIT)
    {
      speed_sensor_direction = direction;
      speed_zero_electrical_rad = zero;
      speed_electrical_angle_rad = angle;
      speed_calibrated = 1U; /* 已加载历史标定，不表示本次重新测量。 */
      integral_amps = 0.0f;
      speed_iq_target_a = speed_requested_uq_volts = speed_uq_volts = 0.0f;
      speed_command_rpm = speed_error_rpm = 0.0f;
      velocity_counts = 0;
      velocity_start_ms = sample.tick_ms;
      pi_ms = step_ms = 0;
      CurrentControl_Reset();
      Position_Control_Start(sample.raw, direction); /* 发布绝对角，同时记录相对模式的启动0度。 */
      MotorDriver_SetPwm(MOTOR_TEST_NEUTRAL_CCR, MOTOR_TEST_NEUTRAL_CCR, MOTOR_TEST_NEUTRAL_CCR);
      current_command = (SpeedCurrentCommand){0.0f, angle,
          speed_measured_rpm * (MOTOR_TEST_TWO_PI / 60.0f) * SPEED_POLE_PAIRS,
          sample.cycles, 1U};
      MotorDriver_Enable();
      motor_test_state = MOTOR_STATE_SPEED;
    }
    __set_PRIMASK(mask);
  }
  else if (motor_test_state == MOTOR_STATE_SPEED)
  {
    /* ===== C. 每1ms：可选位置P -> 速度PI -> 目标Iq；mode=0仍为固定电流测试 ===== */
    if (++pi_ms >= SPEED_PI_PERIOD_MS)
    {
      pi_ms = 0;
      /* dt=0.001 秒：积分和目标加速斜坡按这个时间步长换算。 */
      float dt = (float)SPEED_PI_PERIOD_MS * 0.001f;
      float limit = current_iq_limit_a;
      uint32_t mode = current_mode;
      if (!isfinite(limit) || limit <= 0.0f || limit > MOTOR_TEST_LIMIT_MA * 0.001f || mode > 1U)
      {
        MotorControl_Stop(MOTOR_FAULT_CONFIG);
        return;
      }
      float next_integral = integral_amps;
      float iq;
      if (mode == 0U)
      {
        /* 固定电流测试：跳过速度PI，目标速度和速度增益不参与Iq计算。
         * 编码器/测速继续用于换相；时长0持续运行，非0到时关闭驱动。 */
        iq = current_test_iq_a;
        if (!isfinite(iq))
        {
          MotorControl_Stop(MOTOR_FAULT_CONFIG);
          return;
        }
        if (current_test_duration_ms != 0U && current_test_elapsed_ms >= current_test_duration_ms)
        {
          uint32_t stop_mask = __get_PRIMASK();
          __disable_irq();
          if (motor_test_state == MOTOR_STATE_SPEED)
          {
            current_test_done = 1U;
            MotorControl_Stop(MOTOR_FAULT_NONE);
          }
          __set_PRIMASK(stop_mask);
          return;
        }
        next_integral = 0.0f;
        speed_command_rpm = speed_error_rpm = 0.0f;
      }
      else
      {
        float target = speed_target_rpm;
        uint32_t position_mode = position_enabled;
        if (position_mode > 1U ||
            (position_mode && !Position_Control_Update(&target)))
        {
          MotorControl_Stop(MOTOR_FAULT_CONFIG);
          return;
        }
        /* 0=原速度，1=相对位置，2=绝对位置；换坐标目标时同样清旧速度积分。 */
        uint32_t position_source = position_mode ? position_reference_mode + 1U : 0U;
        if (position_source != previous_position_mode)
        {
          next_integral = 0.0f; /* 切换控制目标来源时不沿用旧速度积分。 */
          previous_position_mode = position_source;
        }
        float kp = speed_kp;
        float ki = speed_ki;
        if (!isfinite(target) || !isfinite(kp) || !isfinite(ki) || kp < 0.0f || ki < 0.0f)
        {
          MotorControl_Stop(MOTOR_FAULT_CONFIG);
          return;
        }
        if (position_mode)
        {
          /* 位置P输出本身随剩余距离减小；直接交速度环，避免原加速斜坡延迟制动。
           * position_speed_limit_rpm只限制位置模式；原速度模式仍使用原斜坡。 */
          speed_command_rpm = target;
        }
        else
        {
          float ramp = SPEED_ACCEL_RPM_PER_S * dt;
          speed_command_rpm += Clamp(target - speed_command_rpm, -ramp, ramp);
        }
        speed_error_rpm = speed_command_rpm - speed_measured_rpm;
        float proposed_i = next_integral + ki * speed_error_rpm * dt;
        float proposed_iq = kp * speed_error_rpm + proposed_i;
        float voltage_residual = current_q_saturation_volts;
        if (!((proposed_iq > limit && speed_error_rpm > 0.0f) ||
              (proposed_iq < -limit && speed_error_rpm < 0.0f) ||
              voltage_residual * speed_error_rpm > 0.0f))
        {
          next_integral = Clamp(proposed_i, -limit, limit);
        }
        iq = kp * speed_error_rpm + next_integral;
      }
      if (!isfinite(iq))
      {
        MotorControl_Stop(MOTOR_FAULT_CONFIG);
        return;
      }
      iq = Clamp(iq, -limit, limit);
      speed_electrical_angle_rad = WrapAngle((float)speed_sensor_direction *
          (float)sample.raw * SPEED_RAD_PER_COUNT * (float)SPEED_POLE_PAIRS - speed_zero_electrical_rad);
      float velocity = speed_measured_rpm * (MOTOR_TEST_TWO_PI / 60.0f) * SPEED_POLE_PAIRS;
      uint32_t mask = __get_PRIMASK();
      __disable_irq();
      if (motor_test_state == MOTOR_STATE_SPEED)
      {
        integral_amps = next_integral;
        speed_iq_target_a = iq;
        current_command = (SpeedCurrentCommand){iq, speed_electrical_angle_rad, velocity, sample.cycles, 1U};
        if (mode == 0U) { current_test_elapsed_ms += SPEED_PI_PERIOD_MS; }
      }
      __set_PRIMASK(mask);
    }

    /* PWM 现在由 DMA 的电流回调唯一写入；此处不再重复输出电压模式 PWM。 */
  }
}

void SpeedPower_ResetOutput(void)
{
  Position_Control_Stop();
  integral_amps = 0.0f;
  current_command.valid = 0;
  speed_iq_target_a = speed_requested_uq_volts = speed_uq_volts = 0.0f;
}

bool SpeedPower_GetCurrentCommand(uint32_t now_cycles, float *angle_rad, float *iq_a)
{
  /* 只在 DMA 高优先级中断调用，TIM3 无法打断这里，发布时也已保证结构一致。 */
  SpeedCurrentCommand command = current_command;
  uint32_t age_cycles = now_cycles - command.angle_cycles;
  if (!command.valid || age_cycles > (SystemCoreClock / 1000U) * SPEED_CURRENT_ANGLE_MAX_MS)
  {
    return false;
  }
  /* 采用最近实测速度外推到采样处理时刻；它不是新的编码器测量，仍受角度超时约束。 */
  float age = (float)age_cycles / (float)SystemCoreClock;
  float angle = command.angle_rad + command.velocity_rad_s * age;
  angle -= floorf(angle / MOTOR_TEST_TWO_PI) * MOTOR_TEST_TWO_PI;
  if (angle >= MOTOR_TEST_TWO_PI) { angle = 0.0f; }
  if (!isfinite(angle) || angle < 0.0f) { return false; }
  *angle_rad = angle;
  *iq_a = command.iq_a;
  return true;
}
