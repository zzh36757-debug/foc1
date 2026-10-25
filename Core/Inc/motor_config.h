#ifndef MOTOR_CONFIG_H
#define MOTOR_CONFIG_H

/* 1=AS5600电流控制入口：current_mode=0固定Iq，=1速度+电流双环；0=旧开环。 */
#define MOTOR_USE_SPEED_LOOP       1U

/* 0=不因PA3输出使能位为低报告故障4；1=恢复原运行使能检查。
 * 不控制调试器的暂停关驱动动作，也不会自动重新使能电机。 */
#define MOTOR_CHECK_DRV_ENABLE     0U

/* 本电机已实测的编码器方向/电角度零点，2026-10-25确认安装及相线未改变。
 * 使用此标定直接启动闭环，不再上电定位/扫角。正常转动电机轴不影响该零点。
 * 更换电机、改变相线或磁铁/编码器安装位置后必须重新标定，不可沿用。
 * 来源：2026-10-23 not-turning-213647 的已校验实物运行记录。 */
#define MOTOR_SENSOR_DIRECTION       1
#define MOTOR_ZERO_ELECTRICAL_RAD    4.233786106f

/* 这里保留公共硬件参数和原开环参数；速度闭环的目标、PI、校准参数在 speed_power.c。
 * 公共电压矢量上限 3.80V、电流停机阈值 3500mA，尚未验证持续带载能力及采样精度。
 * 修改 PWM 计数周期时，需要同步修改 CubeMX 的 TIM2 配置。
 */
#define MOTOR_PWM_PERIOD         1800U          /* TIM2 中心对齐计数的 ARR。 */
#define MOTOR_TEST_NEUTRAL_CCR   900U           /* 900/1800=50%，三路相同的初始值。 */
#define MOTOR_TEST_BUS_VOLTS     12.0f          /* 计算所用母线电压，单位 V。 */
#define MOTOR_TEST_VECTOR_VOLTS  3.80f          /* 电压矢量幅值，V；影响电流与驱动力，不是占空比。 */
#define MOTOR_TEST_ELECTRICAL_HZ 100.00f          /* 电角度每秒转多少圈，与 20kHz 载波不同。 */
#define MOTOR_TEST_WAIT_MS       1000U          /* 仅旧开环等待；闭环读取已标定参数后直接启动。 */
#define MOTOR_TEST_RAMP_MS       600U           /* 定位时的电压升幅时间，ms。 */
#define MOTOR_TEST_HOLD_MS       300U           /* 固定方向定位保持时间，ms。 */
#define MOTOR_TEST_RUN_MS        10000U          /* 旋转时间，ms，已包含下面的加速阶段。 */
#define MOTOR_TEST_ACCEL_MS      500U           /* 电频率从 0 升到设定值的时间，ms。 */
#define MOTOR_TEST_DOWN_MS       300U           /* 最后减速、降幅的时间，ms。 */
#define MOTOR_TEST_LIMIT_MA      3500           /* 3500mA=3.5A：软件停机阈值，不是恒流设定或硬件额定电流。 */
#define MOTOR_TEST_TWO_PI        6.28318530718f /* 2*pi 弧度 = 360 电角度。 */

/* TIM2 内部 CH4 每个 PWM 周期提供一个采样时序基准；不连接 PA3。
 * TIM4 对 20kHz 基准 10 分频，通过 CC4 触发 ADC：2kHz，即 500us 一组 V/W。
 * CH4 在中心计数顶点前约 1.67us 上升；13.5 周期采样，两相约 4.33us 转换完成。
 * 本组参数需要与 TIM2=72MHz/1800、ADC=12MHz 一起核对。 */
#define MOTOR_CURRENT_PWM_DIVIDER  10U
#define MOTOR_CURRENT_HZ           2000U
#define MOTOR_CURRENT_DT_SECONDS   (1.0f / (float)MOTOR_CURRENT_HZ)
#define MOTOR_ADC_TRIGGER_CCR      (MOTOR_PWM_PERIOD - 120U)

/* 原开环跟随指令时：机械转速=60*MOTOR_TEST_ELECTRICAL_HZ/7，时间到后关闭。
 * 速度闭环则以 speed_target_rpm 为目标持续运行，不使用上面的开环电频率/试转时长。
 * U 后缀表示无符号整数，f 后缀表示 float 小数，均不是物理单位。
 */

#endif
