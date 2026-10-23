#ifndef MOTOR_CONFIG_H
#define MOTOR_CONFIG_H

/* 1=AS5600 速度闭环（参数在 speed_power.c）；0=原来的短时开环试转。 */
#define MOTOR_USE_SPEED_LOOP       1U

/* 这里保留公共硬件参数和原开环参数；速度闭环的目标、PI、校准参数在 speed_power.c。
 * 公共电压上限沿用 3.50V、电流停机阈值沿用 3500mA，尚未验证持续带载能力及采样精度。
 * 修改 PWM 计数周期时，需要同步修改 CubeMX 的 TIM2 配置。
 */
#define MOTOR_PWM_PERIOD         1800U          /* TIM2 中心对齐计数的 ARR。 */
#define MOTOR_TEST_NEUTRAL_CCR   900U           /* 900/1800=50%，三路相同的初始值。 */
#define MOTOR_TEST_BUS_VOLTS     12.0f          /* 计算所用母线电压，单位 V。 */
#define MOTOR_TEST_VECTOR_VOLTS  3.50f          /* 电压矢量幅值，V；影响电流与驱动力，不是占空比。 */
#define MOTOR_TEST_ELECTRICAL_HZ 100.00f          /* 电角度每秒转多少圈，与 20kHz 载波不同。 */
#define MOTOR_TEST_WAIT_MS       1000U          /* 旧开环的等待时间；闭环约 1 秒的准备在 speed_power.c。 */
#define MOTOR_TEST_RAMP_MS       600U           /* 定位时的电压升幅时间，ms。 */
#define MOTOR_TEST_HOLD_MS       300U           /* 固定方向定位保持时间，ms。 */
#define MOTOR_TEST_RUN_MS        10000U          /* 旋转时间，ms，已包含下面的加速阶段。 */
#define MOTOR_TEST_ACCEL_MS      500U           /* 电频率从 0 升到设定值的时间，ms。 */
#define MOTOR_TEST_DOWN_MS       300U           /* 最后减速、降幅的时间，ms。 */
#define MOTOR_TEST_LIMIT_MA      3500           /* 3500mA=3.5A：软件停机阈值，不是恒流设定或硬件额定电流。 */
#define MOTOR_TEST_TWO_PI        6.28318530718f /* 2*pi 弧度 = 360 电角度。 */

/* 原开环跟随指令时：机械转速=60*MOTOR_TEST_ELECTRICAL_HZ/7，时间到后关闭。
 * 速度闭环则以 speed_target_rpm 为目标持续运行，不使用上面的开环电频率/试转时长。
 * U 后缀表示无符号整数，f 后缀表示 float 小数，均不是物理单位。
 */

#endif
