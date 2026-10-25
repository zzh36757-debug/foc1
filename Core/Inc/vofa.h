#ifndef VOFA_H
#define VOFA_H

#include <stdint.h>

/* 电流 DMA 中断只入队；主循环负责启动 USART1 DMA。 */
void Vofa_PushCurrents(float iq_a, float id_a);
void Vofa_Poll(void);

extern volatile uint32_t vofa_sent_frames;
extern volatile uint32_t vofa_dropped_frames;
extern volatile uint32_t vofa_tx_errors;

#endif
