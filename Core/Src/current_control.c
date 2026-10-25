#include "current_control.h"
#include "motor_config.h"
#include "motor_pwm.h"
#include <math.h>

/* 一轮按顺序执行：Clarke -> Park -> d/q PI -> 总电压限幅 -> 逆 Park -> SVPWM。
 * 速度环输出目标 Iq；Id 目标固定为 0。电流环最终仍靠改变 Ud/Uq 调节实际电流。
 * 下面的 PI 是首次调试起点，尚未根据实物的电阻/电感整定。 */
volatile float current_kp_d = 0.20f;
volatile float current_ki_d = 100.0f;
volatile float current_kp_q = 3.40f;
volatile float current_ki_q = 150.0f;
volatile float current_iq_limit_a = 3.5f;
volatile uint32_t current_mode = 1U; /* 默认速度+电流双环；0为固定Iq测试。 */
volatile float current_test_iq_a = 0.10f; /* A，测试目标；不要求转速恒定。 */
const uint32_t current_test_duration_ms = 0U; /* 0=持续运行；非0=固定Iq运行指定毫秒后关闭驱动。 */
volatile uint32_t current_test_elapsed_ms;
volatile uint32_t current_test_done;
volatile float current_test_iq_error_max_a;
volatile float current_test_id_abs_max_a;
volatile float current_iq_target_a = 0.0f;
volatile float current_id_a = 0.0f;
volatile float current_iq_a = 0.0f;
volatile float current_ud_volts = 0.0f;
volatile float current_uq_volts = 0.0f;
volatile float current_q_saturation_volts = 0.0f;
volatile uint32_t current_voltage_limited = 0;
volatile uint32_t current_alignment_stage = 0;
volatile float current_alignment_along_a = 0.0f;
volatile float current_alignment_across_a = 0.0f;

static float integral_d_volts, integral_q_volts;

/* F103 没有 FPU。用 65 点四分之一正弦表线性插值，一轮只算一次 sin/cos，
 * Park 和逆 Park 共用。插值的绝对误差小于 0.000076，不在快速环重复调用 sinf/cosf。 */
static const float quarter_sine[65] = {
  0.0000000000f, 0.0245412285f, 0.0490676743f, 0.0735645636f, 0.0980171403f,
  0.1224106752f, 0.1467304745f, 0.1709618888f, 0.1950903220f, 0.2191012402f,
  0.2429801799f, 0.2667127575f, 0.2902846773f, 0.3136817404f, 0.3368898534f,
  0.3598950365f, 0.3826834324f, 0.4052413140f, 0.4275550934f, 0.4496113297f,
  0.4713967368f, 0.4928981922f, 0.5141027442f, 0.5349976199f, 0.5555702330f,
  0.5758081914f, 0.5956993045f, 0.6152315906f, 0.6343932842f, 0.6531728430f,
  0.6715589548f, 0.6895405447f, 0.7071067812f, 0.7242470830f, 0.7409511254f,
  0.7572088465f, 0.7730104534f, 0.7883464276f, 0.8032075315f, 0.8175848132f,
  0.8314696123f, 0.8448535652f, 0.8577286100f, 0.8700869911f, 0.8819212643f,
  0.8932243012f, 0.9039892931f, 0.9142097557f, 0.9238795325f, 0.9329927988f,
  0.9415440652f, 0.9495281806f, 0.9569403357f, 0.9637760658f, 0.9700312532f,
  0.9757021300f, 0.9807852804f, 0.9852776424f, 0.9891765100f, 0.9924795346f,
  0.9951847267f, 0.9972904567f, 0.9987954562f, 0.9996988187f, 1.0000000000f
};

static float Clamp(float value, float low, float high)
{
  if (value < low) { return low; }
  if (value > high) { return high; }
  return value;
}

static float SineAt(uint32_t index, float fraction)
{
  uint32_t quadrant = (index >> 6U) & 3U;
  uint32_t offset = index & 63U;
  float first, second;
  if ((quadrant & 1U) == 0U)
  {
    first = quarter_sine[offset];
    second = quarter_sine[offset + 1U];
  }
  else
  {
    first = quarter_sine[64U - offset];
    second = quarter_sine[63U - offset];
  }
  float value = first + fraction * (second - first);
  return (quadrant >= 2U) ? -value : value;
}

static void SinCos(float angle, float *sine, float *cosine)
{
  /* 调用者保证 angle 在 [0,2pi)，避免快速环做不受限的回绕循环。 */
  float position = angle * (256.0f / MOTOR_TEST_TWO_PI);
  uint32_t index = (uint32_t)position;
  float fraction = position - (float)index;
  *sine = SineAt(index & 255U, fraction);
  *cosine = SineAt((index + 64U) & 255U, fraction);
}

void CurrentControl_Reset(void)
{
  integral_d_volts = integral_q_volts = 0.0f;
  current_iq_target_a = current_ud_volts = current_uq_volts = 0.0f;
  current_q_saturation_volts = 0.0f;
  current_voltage_limited = 0;
  /* Id/Iq 和相线检查阶段保留，便于停机后观察最后一次反馈。 */
}

bool CurrentControl_CheckAlignment(float angle, float iv, float iw)
{
  /* 在原来的定位保持阶段检查相线/极性；不增加额外动作。
   * 静止并施加固定电压时，电流应主要沿该电压方向。阈值用于拒绝反接和零反馈，
   * 不是精确电阻辨识，也不自动猜测并交换引脚。 */
  if (!isfinite(angle) || angle < 0.0f || angle >= MOTOR_TEST_TWO_PI ||
      !isfinite(iv) || !isfinite(iw)) { return false; }
  float sine, cosine;
  SinCos(angle, &sine, &cosine);
  float alpha = -iv - iw;
  float beta = (iv - iw) * 0.57735026919f;
  float along = alpha * cosine + beta * sine;
  float across = -alpha * sine + beta * cosine;
  current_alignment_along_a = along;
  current_alignment_across_a = across;
  return along > 0.02f && fabsf(across) < along * 0.75f;
}

bool CurrentControl_Calculate(float angle, float iv, float iw,
                              float iq_target, float dt, uint16_t compare[3])
{
  if (!compare) { return false; }
  compare[0] = compare[1] = compare[2] = MOTOR_TEST_NEUTRAL_CCR;
  float kpd = current_kp_d, kid = current_ki_d;
  float kpq = current_kp_q, kiq = current_ki_q;
  float limit_a = current_iq_limit_a;
  const float limit_v = MOTOR_TEST_VECTOR_VOLTS;
  if (!isfinite(angle) || angle < 0.0f || angle >= MOTOR_TEST_TWO_PI ||
      !isfinite(iv) || !isfinite(iw) || !isfinite(iq_target) ||
      !isfinite(dt) || dt <= 0.0f || dt > 0.002f ||
      !isfinite(kpd) || !isfinite(kid) || !isfinite(kpq) || !isfinite(kiq) ||
      kpd < 0.0f || kid < 0.0f || kpq < 0.0f || kiq < 0.0f ||
      !isfinite(limit_a) || limit_a <= 0.0f ||
      limit_a > (float)MOTOR_TEST_LIMIT_MA * 0.001f ||
      !isfinite(limit_v) || limit_v <= 0.0f ||
      limit_v > MOTOR_TEST_BUS_VOLTS * 0.57735026919f) { return false; }

  /* 1. Clarke：三相电流 -> 静止的 alpha/beta；Iu=-Iv-Iw。 */
  float alpha = -iv - iw;
  float beta = (iv - iw) * 0.57735026919f;
  float sine, cosine;
  SinCos(angle, &sine, &cosine);

  /* 2. Park：旋转到转子坐标系。Id/Iq 保留原始反馈，不照搬 20ms 测速滤波。 */
  float id = alpha * cosine + beta * sine;
  float iq = -alpha * sine + beta * cosine;
  float target = Clamp(iq_target, -limit_a, limit_a);

  /* 3. 两个独立 PI。Ud=0 不保证 Id=0，因此需要 d 轴反馈调节。 */
  float error_d = -id;
  float error_q = target - iq;
  float next_d = integral_d_volts + kid * error_d * dt;
  float next_q = integral_q_volts + kiq * error_q * dt;
  float raw_d = kpd * error_d + next_d;
  float raw_q = kpq * error_q + next_q;
  float magnitude_sq = raw_d * raw_d + raw_q * raw_q;
  if (!isfinite(magnitude_sq) || !isfinite(next_d) || !isfinite(next_q)) { return false; }

  /* 4. 总矢量限制：sqrt(Ud^2+Uq^2)<=3.8V，两轴按同一比例缩放。
   * 回算积分：将被限幅裁掉的电压从积分中扣除，避免两轴积分继续堆积。 */
  float ud = raw_d, uq = raw_q;
  bool limited = magnitude_sq > limit_v * limit_v;
  if (limited)
  {
    float scale = limit_v / sqrtf(magnitude_sq);
    ud *= scale;
    uq *= scale;
  }
  integral_d_volts = Clamp(next_d + ud - raw_d, -limit_v, limit_v);
  integral_q_volts = Clamp(next_q + uq - raw_q, -limit_v, limit_v);

  /* 5. 逆 Park：两轴电压 -> alpha/beta，复用 sin/cos，再交给现有 SVPWM。 */
  float u_alpha = ud * cosine - uq * sine;
  float u_beta = ud * sine + uq * cosine;
  if (!MotorPwmCalculateAlphaBeta(u_alpha, u_beta, MOTOR_TEST_BUS_VOLTS,
                                 MOTOR_PWM_PERIOD, compare)) { return false; }
  current_id_a = id;
  current_iq_a = iq;
  current_iq_target_a = target;
  current_ud_volts = ud;
  current_uq_volts = uq;
  current_q_saturation_volts = raw_q - uq;
  current_voltage_limited = limited;
  /* 前100ms留给阶跃建立；之后记录每次采样的误差峰值，停机时保留。
   * 不给反馈加慢滤波，也不在快速环加入统计平均所需的除法。 */
  if (current_mode == 0U && current_test_elapsed_ms >= 100U && !current_test_done)
  {
    float error_abs = fabsf(target - iq);
    float id_abs = fabsf(id);
    if (error_abs > current_test_iq_error_max_a) { current_test_iq_error_max_a = error_abs; }
    if (id_abs > current_test_id_abs_max_a) { current_test_id_abs_max_a = id_abs; }
  }
  return true;
}
