#ifndef MOTOR_CONFIG_H
#define MOTOR_CONFIG_H

/* 电机试转的可调参数集中在这里，单位写在每一项旁边。
 * 保留用户设置的 5.50V / 5500mA 试验参数，尚未验证持续带载能力及采样精度。
 * 修改 PWM 计数周期时，需要同步修改 CubeMX 的 TIM2 配置。
 */
#define MOTOR_PWM_PERIOD         1800U          /* TIM2 中心对齐计数的 ARR。 */
#define MOTOR_TEST_NEUTRAL_CCR   900U           /* 900/1800=50%，三路相同的初始值。 */
#define MOTOR_TEST_BUS_VOLTS     12.0f          /* 计算所用母线电压，单位 V。 */
#define MOTOR_TEST_VECTOR_VOLTS  3.50f          /* 电压矢量幅值，V；影响电流与驱动力，不是占空比。 */
#define MOTOR_TEST_ELECTRICAL_HZ 100.00f          /* 电角度每秒转多少圈，与 20kHz 载波不同。 */
#define MOTOR_TEST_WAIT_MS       3000U          /* 启动后等待 3 秒。 */
#define MOTOR_TEST_RAMP_MS       600U           /* 定位时的电压升幅时间，ms。 */
#define MOTOR_TEST_HOLD_MS       300U           /* 固定方向定位保持时间，ms。 */
#define MOTOR_TEST_RUN_MS        10000U          /* 旋转时间，ms，已包含下面的加速阶段。 */
#define MOTOR_TEST_ACCEL_MS      500U           /* 电频率从 0 升到设定值的时间，ms。 */
#define MOTOR_TEST_DOWN_MS       300U           /* 最后减速、降幅的时间，ms。 */
#define MOTOR_TEST_LIMIT_MA      3500           /* 5500mA=5.5A：软件停机阈值，不是恒流设定或硬件额定电流。 */
#define MOTOR_TEST_TWO_PI        6.28318530718f /* 2*pi 弧度 = 360 电角度。 */

/* 7 极对电机跟随指令时：机械转速=60*0.5/7，约 4.29 转/分钟。
 * 算上加减速，本版累计指令机械角度约 100 度，时间到后关闭驱动。
 * U 后缀表示无符号整数，f 后缀表示 float 小数，均不是物理单位。
 */

#endif
