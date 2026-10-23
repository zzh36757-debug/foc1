#ifndef AS5600_H
#define AS5600_H

#include <stdint.h>

/* MX_I2C1_Init() 之后调用；通过 PB6/SCL、PB7/SDA 读取传感器。
 * 在主循环中调用，不放进电机的定时中断。闭环读取后仅等待 1ms，
 * 实际读取间隔还包括 I2C 传输时间，不等于严格的 1ms。
 */
void AS5600_Update(void);

/* 下面的变量可以直接添加到 Cortex Live Watch。
 * 通信失败时保留上次读数，并将 valid 清零；valid=0 时不要把角度用于控制。
 */
extern volatile uint32_t live_as5600_raw;       /* 机械角度原始值：0~4095。 */
extern volatile float live_as5600_angle_deg;   /* 机械角度：0~359.912 度，转一圈回绕。 */
extern volatile uint32_t live_as5600_valid;     /* 1=完整读取成功、MD=1 且 MH=0；允许 ML=1，不保证角度精度。 */
extern volatile uint32_t live_as5600_status;    /* 原始 STATUS：bit5 MD，bit4 ML，bit3 MH；ML 仅观察。 */
extern volatile uint32_t live_as5600_agc;       /* 自动增益码值；配合磁铁状态判断强度是否合适。 */
extern volatile uint32_t live_as5600_magnitude; /* 芯片内部磁场幅度码值，0~4095；单位不是 mT。 */
extern volatile uint32_t live_as5600_hal_status;/* 0成功、1错误、2忙、3超时。 */
extern volatile uint32_t live_as5600_i2c_error; /* HAL I2C 错误位；正常为 0。 */
extern volatile uint32_t live_as5600_read_count;/* 完整读取成功的次数，磁场异常时也会增加。 */

#endif
