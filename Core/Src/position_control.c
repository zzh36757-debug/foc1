#include "position_control.h"
#include <math.h>

/* ===== 位置环的默认参数：主要修改这里 =====
 * 三环关系：位置P -> 目标rpm -> 现有速度PI -> 目标Iq -> 现有d/q电流PI -> PWM。
 * 默认绝对模式：目标0度就是AS5600原始读数映射的0度，跨0/360走最短方向。
 * 相对模式：以使能前位置为0度，沿原速度正方向累计；720度表示正转两圈。
 * 绝对/相对目标分别保存，切换模式不改变目标，也不重新建立启动零点。
 * 参数为首次位置调试起点，需按实际负载整定。 */
#define POSITION_DEFAULT_ENABLED          1U
#define POSITION_DEFAULT_REFERENCE_MODE   POSITION_REFERENCE_ABSOLUTE
#define POSITION_DEFAULT_ABSOLUTE_DEG     0.0f  /* AS5600单圈坐标，0~360度，360等同0。 */
#define POSITION_DEFAULT_RELATIVE_DEG     0.0f  /* 相对启动位置，可正负、多圈。 */
#define POSITION_DEFAULT_KP               7.00f  /* rpm/deg；保留用户调好的位置增益。 */
#define POSITION_DEFAULT_SPEED_LIMIT_RPM 200.0f
#define POSITION_DEFAULT_TOLERANCE_DEG    0.5f

volatile uint32_t position_enabled = POSITION_DEFAULT_ENABLED;
volatile uint32_t position_ready = 0U;
volatile uint32_t position_reference_mode = POSITION_DEFAULT_REFERENCE_MODE;
volatile float position_absolute_target_deg = POSITION_DEFAULT_ABSOLUTE_DEG;
volatile float position_absolute_measured_deg = 0.0f;
volatile float position_relative_target_deg = POSITION_DEFAULT_RELATIVE_DEG;
volatile float position_relative_measured_deg = 0.0f;
volatile float position_error_deg = 0.0f;
volatile float position_kp = POSITION_DEFAULT_KP;
volatile float position_speed_limit_rpm = POSITION_DEFAULT_SPEED_LIMIT_RPM;
volatile float position_tolerance_deg = POSITION_DEFAULT_TOLERANCE_DEG;
volatile float position_speed_target_rpm = 0.0f;

/* AS5600只有单圈绝对角度，多圈位置由本次上电后的连续采样累计。
 * 64位计数只由TIM3维护，不在DMA读取；Watch显示float，极大圈数时精度会降低。
 * 断电不保存圈数；相邻有效样本必须相差小于半圈，否则无法判定转过几圈。 */
static int64_t accumulated_counts;
static uint32_t previous_raw;
static int32_t sensor_direction;

void Position_Control_Init(void)
{
  accumulated_counts = 0;
  previous_raw = 0;
  sensor_direction = 1;
  position_ready = 0;
  position_absolute_measured_deg = position_relative_measured_deg = 0.0f;
  position_error_deg = position_speed_target_rpm = 0.0f;
}

void Position_Control_Start(uint32_t raw, int32_t direction)
{
  Position_Control_Init();
  if (raw > 4095U || (direction != 1 && direction != -1)) { return; }
  previous_raw = raw;
  sensor_direction = direction;
  position_absolute_measured_deg = (float)raw * (360.0f / 4096.0f);
  position_ready = 1;
}

void Position_Control_OnSample(uint32_t raw)
{
  if (!position_ready) { return; }
  if (raw > 4095U)
  {
    Position_Control_Stop();
    return;
  }
  /* 1. 消除4095->0回绕，再按已标定的编码器方向累计机械角。 */
  int32_t delta = (int32_t)raw - (int32_t)previous_raw;
  if (delta > 2048) { delta -= 4096; }
  if (delta < -2048) { delta += 4096; }
  previous_raw = raw;
  accumulated_counts += (int64_t)sensor_direction * delta;
  position_absolute_measured_deg = (float)raw * (360.0f / 4096.0f);
  position_relative_measured_deg = (float)accumulated_counts * (360.0f / 4096.0f);
}

bool Position_Control_Update(float *target_rpm)
{
  if (!target_rpm) { return false; }
  *target_rpm = 0.0f;
  position_speed_target_rpm = 0.0f;
  uint32_t mode = position_reference_mode;
  float kp = position_kp;
  float limit = position_speed_limit_rpm;
  float tolerance = position_tolerance_deg;
  if (!position_ready ||
      !isfinite(kp) || kp < 0.0f || !isfinite(limit) || limit <= 0.0f ||
      !isfinite(tolerance) || tolerance < 0.0f) { return false; }

  float error, control_error;
  if (mode == POSITION_REFERENCE_ABSOLUTE)
  {
    float target = position_absolute_target_deg;
    float measured = position_absolute_measured_deg;
    if (!isfinite(target) || target < 0.0f || target > 360.0f ||
        !isfinite(measured) || measured < 0.0f || measured >= 360.0f) { return false; }
    /* 绝对刻度必须与AS5600一致，不因传感器方向而把显示角度取反。
     * 例如目标0、实测359，误差为+1度；目标359、实测0，误差为-1度。
     * 正好相差180度时统一选AS5600角度减小方向。 */
    error = target - measured;
    if (error >= 180.0f) { error -= 360.0f; }
    if (error < -180.0f) { error += 360.0f; }
    control_error = error * (float)sensor_direction; /* 转为原速度环的正方向。 */
  }
  else if (mode == POSITION_REFERENCE_RELATIVE)
  {
    float target = position_relative_target_deg;
    float measured = position_relative_measured_deg;
    if (!isfinite(target) || !isfinite(measured)) { return false; }
    error = target - measured; /* 多圈差值不回绕，720与0是不同位置。 */
    control_error = error;    /* 累计时已处理传感器方向，不能重复取反。 */
  }
  else { return false; }

  /* 2. 外环只用P，输出保持原速度坐标下的rpm。 */
  float rpm = kp * control_error;
  if (!isfinite(error) || !isfinite(rpm)) { return false; }
  position_error_deg = error;
  if (fabsf(error) <= tolerance) { rpm = 0.0f; }
  if (rpm > limit) { rpm = limit; }
  if (rpm < -limit) { rpm = -limit; }

  /* 3. 越靠近目标，要求的转速越小；超过目标时反向纠正。
   * 零速度不等于关驱动，后面的速度/电流环继续制动并抵抗负载。
   * 不写speed_target_rpm，切回速度模式时保留用户原来的速度目标。 */
  *target_rpm = position_speed_target_rpm = rpm;
  return true;
}

void Position_Control_Stop(void)
{
  position_ready = 0;
  position_speed_target_rpm = 0.0f;
  /* 保留最后的位置/误差供停机诊断，不自动重新建立位置零点。 */
}
