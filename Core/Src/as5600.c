#include "as5600.h"
#include "i2c.h"

/* AS5600 的 7 位 I2C 地址为 0x36；STM32 HAL 要求左移一位，即传入 0x6C。
 * STATUS 在 0x0B；RAW ANGLE 的高、低字节分别在 0x0C、0x0D。
 * 只读这些寄存器，不修改传感器的零点、量程或永久烧写设置。
 */
#define AS5600_I2C_ADDRESS       (0x36U << 1)
#define AS5600_STATUS_REGISTER   0x0BU
#define AS5600_RAW_REGISTER      0x0CU
#define AS5600_TIMEOUT_MS        10U

/* STATUS：bit5 MD=检测到磁铁，bit4 ML=磁场过弱，bit3 MH=磁场过强。 */
#define AS5600_MAGNET_STATUS_MASK 0x38U
#define AS5600_MAGNET_NORMAL      0x20U

volatile uint32_t live_as5600_raw = 0;
volatile float live_as5600_angle_deg = 0.0f;
volatile uint32_t live_as5600_valid = 0;
volatile uint32_t live_as5600_status = 0;
volatile uint32_t live_as5600_hal_status = HAL_ERROR;
volatile uint32_t live_as5600_i2c_error = 0;
volatile uint32_t live_as5600_read_count = 0;

void AS5600_Update(void)
{
  uint8_t status;
  uint8_t angle_bytes[2];
  HAL_StatusTypeDef result;

  /* 第一步：读取磁铁状态。两次读取都有超时，未接上传感器时不会一直等。 */
  result = HAL_I2C_Mem_Read(&hi2c1, AS5600_I2C_ADDRESS,
                          AS5600_STATUS_REGISTER, I2C_MEMADD_SIZE_8BIT,
                          &status, 1, AS5600_TIMEOUT_MS);
  live_as5600_hal_status = result;
  live_as5600_i2c_error = HAL_I2C_GetError(&hi2c1);
  if (result != HAL_OK)
  {
    live_as5600_valid = 0;
    return;
  }

  /* 第二步：同一次传输连续读出角度的两个字节，避免分两次读取时跨过角度更新。
   * 读取 RAW ANGLE，得到未经过用户零点/量程缩放的单圈机械角度。
   */
  result = HAL_I2C_Mem_Read(&hi2c1, AS5600_I2C_ADDRESS,
                          AS5600_RAW_REGISTER, I2C_MEMADD_SIZE_8BIT,
                          angle_bytes, 2, AS5600_TIMEOUT_MS);
  live_as5600_hal_status = result;
  live_as5600_i2c_error = HAL_I2C_GetError(&hi2c1);
  if (result != HAL_OK)
  {
    live_as5600_valid = 0;
    return;
  }

  /* 第三步：高字节只有低 4 位有效，和低字节拼成 12 位角度。
   * 一整圈有 4096 格，所以这里除以 4096：1024=90度，2048=180度。
   * 这测的是轴的位置；还没有乘 7 极对数，也没有做电角度零点标定。
   */
  uint32_t raw = ((uint32_t)(angle_bytes[0] & 0x0FU) << 8) | angle_bytes[1];
  live_as5600_raw = raw;
  live_as5600_angle_deg = (float)raw * (360.0f / 4096.0f);
  live_as5600_status = status;
  live_as5600_read_count++;
  /* 能读到寄存器不等于磁铁正常，MD=1 且 ML/MH=0 才认为角度可用。
   * valid 最后更新；磁场异常时仍显示本次读数，便于检查安装位置。
   */
  live_as5600_valid = ((status & AS5600_MAGNET_STATUS_MASK) == AS5600_MAGNET_NORMAL);
}
