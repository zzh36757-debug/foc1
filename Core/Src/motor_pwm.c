#include "motor_pwm.h"
#include <math.h>

/*
 * 这个文件只做“指令方向 -> 三路 PWM 比较值”的数学换算，不操作外设寄存器。
 * motor_control.c 取得 compare[0/1/2] 后，调用 MotorDriver_SetPwm() 写入 TIM2，才会改变实际引脚波形。
 *
 * 两个输入描述一个电压矢量：
 * - angle_rad：它在定子平面内指向哪里（电角度，rad）。
 * - vector_volts：矢量有多长（电压幅值，V），本次为 0.3V。
 * 角度连续推进时，合成磁场方向随之变化；转子能否跟随还取决于电流、负载等。
 * 本函数不读取转子位置，也不保证电机实际转过了指定角度。
 *
 * 所用 SVPWM 方法：先算三相电压，再给三相加上相同的公共偏置，居中放进母线范围。
 * 三相同加一个数，不改变任意两相之间的差，因此保留电机所需的线电压。
 */
bool MotorPwmCalculate(float angle_rad, float vector_volts, float bus_volts,
                       uint16_t period, uint16_t compare[3])
{
  /* 必须有可写的输出数组。先填中性值，后面若发现非法参数，会返回 false。
   * 调用者看到 false 后关闭驱动；仅把比较值设成 50% 不能替代关闭使能。 */
  if (!compare) { return false; }
  compare[0] = compare[1] = compare[2] = period / 2U;
  /* isfinite() 排除 NaN/无穷大，防止非法浮点数进入定时器比较值。
   * 0.57735 = 1/sqrt(3)：按这里矢量幅值的定义，这是 SVPWM 线性区上限，
   * 满足此限制时，各个电角度的三相指令都能放在 0~bus_volts 的范围内。 */
  if (period == 0U || !isfinite(angle_rad) || !isfinite(vector_volts) ||
      !isfinite(bus_volts) || bus_volts <= 0.0f || vector_volts < 0.0f ||
      vector_volts > bus_volts * 0.57735026919f)
  {
    return false;
  }

  /* 第一步：把“长度 + 角度”投影到静止的 alpha/beta 两个垂直坐标轴。
   * 就像用 x=r*cos(theta)、y=r*sin(theta) 描述平面上的一个箭头。
   * 这些是目标电压分量；它们不是从三相电流测出来的 Clarke/Park 变换结果。 */
  float alpha = vector_volts * cosf(angle_rad);
  float beta = vector_volts * sinf(angle_rad);
  /* 第二步：逆 Clarke 变换，求三个相差 120 电角度的相电压指令。
   * U = alpha；V = -alpha/2 + sqrt(3)*beta/2；W = -alpha/2 - sqrt(3)*beta/2。
   * phase[0/1/2] 分别是 U/V/W，相对电机浮动中性点的目标平均电压，三者之和为 0。
   * 某一项为负，不要求驱动器输出负电源：后面会给三项一起加公共偏置。
   */
  float phase[3] = {
    alpha,
    -0.5f * alpha + 0.86602540378f * beta,
    -0.5f * alpha - 0.86602540378f * beta
  };
  /* 第三步：寻找三相中的最大值与最小值，准备把这一组电压上下居中。 */
  float lowest = phase[0];
  float highest = phase[0];
  for (uint32_t i = 1; i < 3; ++i)
  {
    if (phase[i] < lowest) { lowest = phase[i]; }
    if (phase[i] > highest) { highest = phase[i]; }
  }
  /* 第四步：SVPWM 的公共偏置 = -(最大值+最小值)/2。
   * 加完后，三相最大值和最小值关于 0 对称；再整体移到母线中点 bus_volts/2。
   * 例如 U-V = (U+common)-(V+common)，common 抵消，所需线电压不变。
   * 这里是 min/max 公共偏置实现，不需要逐扇区编写 T1/T2/T0 分支。
   */
  float common = -0.5f * (highest + lowest);
  /* 每伏对应多少个比较计数：本板 1800/12=150 个计数/V。 */
  float scale = (float)period / bus_volts;
  for (uint32_t i = 0; i < 3; ++i)
  {
    /* 第五步：目标平均端电压 -> duty -> CCR。
     * duty = 0.5 + (phase[i]+common)/bus_volts；CCR = duty*period。
     * 本板对 GND 的平均端电压约为 12V*duty；电机受三相之间的电压差驱动。
     * MCU 引脚本身仍只输出约 0/3.3V 逻辑 PWM，DRV8313 将其变成约 0/12V 功率 PWM。
     */
    float value = 0.5f * (float)period + (phase[i] + common) * scale;
    /* 限制输出比较值范围；加 0.5f 后转整数，实现就近取整。 */
    if (value <= 0.0f) { compare[i] = 0; }
    else if (value >= (float)period) { compare[i] = period; }
    else { compare[i] = (uint16_t)(value + 0.5f); }
  }
  /* 数值例子：angle=0、vector=0.3V、bus=12V、period=1800 时：
   * phase=[0.30,-0.15,-0.15]V；common=-0.075V；
   * compare 约为 [934,866,866]，占空比约 [51.9%,48.1%,48.1%]。
   * 改变 angle 会改变三个比较值的组合，保持 angle 则保持同一个指令方向。
   * 在定位升幅、旋转和减速阶段，本函数约每 1ms 被调用；20kHz 载波由 TIM2 硬件产生。
   */
  return true;
}
