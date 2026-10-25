/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    dma.c
  * @brief   This file provides code for the configuration
  *          of all the requested memory to memory DMA transfers.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "dma.h"

/* USER CODE BEGIN 0 */
/* DMA 是外设数据的自动搬运器：ADC 转换完成后，把结果直接写进内存数组，
 * CPU 不必逐个等待转换。此文件打开 DMA1 时钟并配置中断入口；具体通道、数据宽度、
 * 循环模式在 adc.c 中设置，缓冲区地址/长度在 current_sense.c 的 HAL_ADC_Start_DMA() 中指定。
 * 四项循环缓冲区的半满/全满各对应一组 V/W；保持 HT/TC 通知，2kHz 运行电流内环。
 * DMA 优先级 0 高于 TIM3 的 1；及时取走已完成半区，避免读取正在覆盖的数据。
 */
/* USER CODE END 0 */

/*----------------------------------------------------------------------------*/
/* Configure DMA                                                              */
/*----------------------------------------------------------------------------*/

/* USER CODE BEGIN 1 */

/* USER CODE END 1 */

/**
  * Enable DMA controller clock
  */
void MX_DMA_Init(void)
{

  /* DMA controller clock enable */
  __HAL_RCC_DMA1_CLK_ENABLE();

  /* DMA interrupt init */
  /* DMA1_Channel1_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Channel1_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Channel1_IRQn);
  HAL_NVIC_SetPriority(DMA1_Channel4_IRQn, 2, 0);
  HAL_NVIC_EnableIRQ(DMA1_Channel4_IRQn);

}

/* USER CODE BEGIN 2 */

/* USER CODE END 2 */

