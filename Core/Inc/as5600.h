#ifndef AS5600_H
#define AS5600_H

#include <stdint.h>

/* MX_I2C1_Init() 之后调用；通过 PB6/SCL、PB7/SDA 读取传感器。
 * 当前在主循环中约每 10ms 调用一次，不放进电机的定时中断。
 */
void AS5600_Update(void);

/* 下面的变量可以直接添加到 Cortex Live Watch。
 * 通信失败时保留上次读数，并将 valid 清零；valid=0 时不要把角度用于控制。
 */
extern volatile uint32_t live_as5600_raw;       /* 机械角度原始值：0~4095。 */
extern volatile float live_as5600_angle_deg;   /* 机械角度：0~359.912 度，转一圈回绕。 */
extern volatile uint32_t live_as5600_valid;     /* 1=读取成功且磁场正常；0=当前读数不可用于控制。 */
extern volatile uint32_t live_as5600_status;    /* STATUS 寄存器；正常关注位为 0x20，十进制 32。 */
extern volatile uint32_t live_as5600_hal_status;/* 0成功、1错误、2忙、3超时。 */
extern volatile uint32_t live_as5600_i2c_error; /* HAL I2C 错误位；正常为 0。 */
extern volatile uint32_t live_as5600_read_count;/* 完整读取成功的次数，磁场异常时也会增加。 */

#endif
