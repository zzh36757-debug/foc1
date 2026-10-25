#include "vofa.h"
#include "usart.h"

/* FireWater文本: "0.100,-0.002\n"，Iq/Id单位A，四舍五入保留三位小数。
 * 格式化只在主循环做，电流中断保留原始float入队，控制计算不降精度。
 * 正常电流绝对值<10A时每行最多14字节，2kHz最多28000字节/s。
 * USART1 460800、8N1可传46080字节/s。
 * 单生产者(ADC IRQ)/单消费者(UART完成IRQ)，DMA读期间不释放槽位。
 * 满队列丢新帧并计数，绝不等待串口或覆盖DMA正在读取的数据。 */
#define VOFA_QUEUE_SIZE 64U
#define VOFA_BATCH_MAX 16U
#define VOFA_LINE_MAX 18U /* 两个带符号的999.999 + 逗号 + 换行。 */
typedef struct
{
  float iq_a;
  float id_a;
} VofaFrame;

static VofaFrame frames[VOFA_QUEUE_SIZE];
static uint8_t tx_text[VOFA_BATCH_MAX * VOFA_LINE_MAX];
static volatile uint32_t head, tail;
static volatile uint32_t tx_busy, tx_count, tx_failed;
volatile uint32_t vofa_sent_frames;
volatile uint32_t vofa_dropped_frames;
volatile uint32_t vofa_tx_errors;

void Vofa_PushCurrents(float iq_a, float id_a)
{
  uint32_t write = head;
  uint32_t next = (write + 1U) % VOFA_QUEUE_SIZE;
  if (tx_failed || next == tail)
  {
    vofa_dropped_frames++;
    return;
  }
  frames[write].iq_a = iq_a;
  frames[write].id_a = id_a;
  __DMB(); /* 两个字段写完，再发布新head。 */
  head = next;
}

/* 不用浮点printf；先取整到mA，再输出固定三位小数，负零输出0.000。
 * 无效/超显示范围的数据停止遥测并计错，不发送伪造的限幅电流。 */
static uint32_t FormatCurrent(uint8_t *out, float amps)
{
  if (!(amps >= -999.999f && amps <= 999.999f)) { return 0U; }
  float scaled = amps * 1000.0f;
  int32_t ma = (int32_t)(scaled + ((scaled < 0.0f) ? -0.5f : 0.5f));
  uint32_t length = 0U;
  if (ma < 0) { out[length++] = '-'; ma = -ma; }
  uint32_t whole = (uint32_t)ma / 1000U;
  uint32_t fraction = (uint32_t)ma % 1000U;
  if (whole >= 100U) { out[length++] = (uint8_t)('0' + whole / 100U); }
  if (whole >= 10U) { out[length++] = (uint8_t)('0' + whole / 10U % 10U); }
  out[length++] = (uint8_t)('0' + whole % 10U);
  out[length++] = '.';
  out[length++] = (uint8_t)('0' + fraction / 100U);
  out[length++] = (uint8_t)('0' + fraction / 10U % 10U);
  out[length++] = (uint8_t)('0' + fraction % 10U);
  return length;
}

void Vofa_Poll(void)
{
  if (tx_busy || tx_failed) { return; }
  uint32_t read = tail;
  uint32_t write = head;
  __DMB();
  if (read == write) { return; }
  /* 每次只发内存中连续的一段，回绕留给下一次。 */
  uint32_t count = (write > read) ? write - read : VOFA_QUEUE_SIZE - read;
  if (count > VOFA_BATCH_MAX) { count = VOFA_BATCH_MAX; }
  uint32_t length = 0U;
  for (uint32_t i = 0; i < count; ++i)
  {
    uint32_t n = FormatCurrent(&tx_text[length], frames[read + i].iq_a);
    if (n == 0U) { vofa_tx_errors++; tx_failed = 1U; return; }
    length += n;
    tx_text[length++] = ',';
    n = FormatCurrent(&tx_text[length], frames[read + i].id_a);
    if (n == 0U) { vofa_tx_errors++; tx_failed = 1U; return; }
    length += n;
    tx_text[length++] = '\n';
  }
  tx_count = count;
  tx_busy = 1U;
  __DMB();
  HAL_StatusTypeDef result = HAL_UART_Transmit_DMA(&huart1,
      tx_text, (uint16_t)length);
  if (result != HAL_OK)
  {
    /* BUSY时原队列留到下次；错误只停遥测，不改电机状态。 */
    if (result != HAL_BUSY)
    {
      vofa_tx_errors++;
      tx_failed = 1U;
    }
    tx_busy = 0U;
  }
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart != &huart1 || !tx_busy || tx_failed) { return; }
  uint32_t count = tx_count;
  tail = (tail + count) % VOFA_QUEUE_SIZE;
  vofa_sent_frames += count;
  __DMB();
  tx_busy = 0U;
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
  if (huart != &huart1) { return; }
  /* 不释放可能仍被DMA使用的内存；重新初始化固件后恢复遥测。 */
  tx_failed = 1U;
  vofa_tx_errors++;
}
