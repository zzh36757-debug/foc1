/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    tim.c
  * @brief   This file provides code for the configuration
  *          of the TIM instances.
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
#include "tim.h"

/* USER CODE BEGIN 0 */
/* 本工程两个定时器的分工：
 * TIM2：三通道 PWM 硬件发生器，PA0/PA1/PA2 接 DRV8313 的 IN1/IN2/IN3。
 *       motor_driver.c 更新它的 CCR 占空比，硬件负责持续输出 20kHz 波形。
 * TIM3：控制程序的定时闹钟，每 1ms 产生一次中断，通知软件更新电机指令。
 * TIM2 内部 CH4/TIM4 另由 current_sense.c 配置，给 ADC 提供 2kHz 同步触发。
 * CH4 不配置 PA3 引脚，PA3 保持 GPIO 驱动使能；TIM4 不输出 GPIO、不进中断。
 *
 * 时钟来源：APB1 总线为 36MHz，且 APB1 预分频不为 1，定时器时钟自动乘 2，
 * 所以 TIM2/TIM3 的计数时钟输入都是 72MHz。
 * 本文件以 CubeMX 生成为主，教学注释放在 USER CODE 区域，便于再次生成时保留。
 */
/* USER CODE END 0 */

TIM_HandleTypeDef htim2;
TIM_HandleTypeDef htim3;

/* TIM2 init function */
void MX_TIM2_Init(void)
{

  /* USER CODE BEGIN TIM2_Init 0 */

  /* USER CODE END TIM2_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};

  /* USER CODE BEGIN TIM2_Init 1 */
  /* TIM2 的 PSC=0 表示除以 (0+1)，计数速度仍是 72MHz。
   * 中心对齐：CNT 从 0 数到 ARR=1800，再数回 0，一个 PWM 周期共 2*1800 个计数。
   * PWM 频率 = 72MHz / ((PSC+1)*2*ARR) = 20kHz，周期为 50us。
   * 这是中心对齐公式；TIM3 的向上计数则使用下面的 (ARR+1) 公式。
   * 初始 Pulse=900 是 CCR 比较值，占空比约为 900/1800=50%。
   * PWM1 模式、高电平有效，由计数器与 CCR 的比较结果决定引脚高低电平。
   */
  /* USER CODE END TIM2_Init 1 */
  htim2.Instance = TIM2;
  htim2.Init.Prescaler = 0;
  htim2.Init.CounterMode = TIM_COUNTERMODE_CENTERALIGNED1;
  htim2.Init.Period = 1800;
  htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
  if (HAL_TIM_Base_Init(&htim2) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim2, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_Init(&htim2) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim2, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 900;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_PWM_ConfigChannel(&htim2, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_ConfigChannel(&htim2, &sConfigOC, TIM_CHANNEL_2) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_ConfigChannel(&htim2, &sConfigOC, TIM_CHANNEL_3) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM2_Init 2 */
  /* 上面的三次 HAL_TIM_PWM_ConfigChannel() 分别配置 CH1/2/3，初始参数相同。
   * HAL 会开启各通道 CCR 预装载；新比较值到定时器更新事件时装入生效寄存器。
   * 配置完成后，还要由 motor_driver.c 的 HAL_TIM_PWM_Start() 才真正启动输出。
   * TIM2 不需要每个 PWM 周期进中断，也不需要三组互补引脚：
   * DRV8313 内部包含三个半桥及上下管死区逻辑，MCU 发送三路 IN 逻辑 PWM 即可。
   */
  /* USER CODE END TIM2_Init 2 */
  HAL_TIM_MspPostInit(&htim2);

}
/* TIM3 init function */
void MX_TIM3_Init(void)
{

  /* USER CODE BEGIN TIM3_Init 0 */

  /* USER CODE END TIM3_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM3_Init 1 */
  /* TIM3 的 PSC=71：72MHz/(71+1)=1MHz，每个计数为 1us。
   * 向上计数从 0 到 ARR=999，共 1000 个计数，更新周期为：
   * (71+1)*(999+1)/72MHz = 0.001 秒 = 1ms，即每秒 1000 次中断。
   * TIM3 负责安排“何时更新指令”，不直接输出电机三路 PWM。
   */
  /* USER CODE END TIM3_Init 1 */
  htim3.Instance = TIM3;
  htim3.Init.Prescaler = 71;
  htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim3.Init.Period = 999;
  htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim3) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim3, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim3, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM3_Init 2 */
  /* 真正启动由 motor_control.c 的 HAL_TIM_Base_Start_IT(&htim3) 完成。
   * 中断路径：TIM3_IRQHandler -> HAL_TIM_IRQHandler ->
   * stm32f1xx_it.c 的 HAL_TIM_PeriodElapsedCallback -> MotorControl_Update1ms。
   */
  /* USER CODE END TIM3_Init 2 */

}

void HAL_TIM_Base_MspInit(TIM_HandleTypeDef* tim_baseHandle)
{

  if(tim_baseHandle->Instance==TIM2)
  {
  /* USER CODE BEGIN TIM2_MspInit 0 */

  /* USER CODE END TIM2_MspInit 0 */
    /* TIM2 clock enable */
    __HAL_RCC_TIM2_CLK_ENABLE();
  /* USER CODE BEGIN TIM2_MspInit 1 */

  /* USER CODE END TIM2_MspInit 1 */
  }
  else if(tim_baseHandle->Instance==TIM3)
  {
  /* USER CODE BEGIN TIM3_MspInit 0 */
    /* 下面的 NVIC 设置允许 CPU 接收 TIM3 中断；还需要 Start_IT 才开始周期通知。 */
  /* USER CODE END TIM3_MspInit 0 */
    /* TIM3 clock enable */
    __HAL_RCC_TIM3_CLK_ENABLE();

    /* TIM3 interrupt Init */
    HAL_NVIC_SetPriority(TIM3_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(TIM3_IRQn);
  /* USER CODE BEGIN TIM3_MspInit 1 */

  /* USER CODE END TIM3_MspInit 1 */
  }
}
void HAL_TIM_MspPostInit(TIM_HandleTypeDef* timHandle)
{

  GPIO_InitTypeDef GPIO_InitStruct = {0};
  if(timHandle->Instance==TIM2)
  {
  /* USER CODE BEGIN TIM2_MspPostInit 0 */
    /* AF_PP 是“复用推挽输出”：这三个引脚由 TIM2 外设输出波形。
     * CH1 -> PA0 -> IN1 -> OUT1/U；CH2 -> PA1 -> IN2 -> OUT2/V；
     * CH3 -> PA2 -> IN3 -> OUT3/W。实际功率输出还受 PA3/DRV_EN 控制。
     * Speed 配置的是 GPIO 输出边沿驱动速度，不是 PWM 的频率或电机转速。
     */
  /* USER CODE END TIM2_MspPostInit 0 */

    __HAL_RCC_GPIOA_CLK_ENABLE();
    /**TIM2 GPIO Configuration
    PA0-WKUP     ------> TIM2_CH1
    PA1     ------> TIM2_CH2
    PA2     ------> TIM2_CH3
    */
    GPIO_InitStruct.Pin = GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_2;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* USER CODE BEGIN TIM2_MspPostInit 1 */

  /* USER CODE END TIM2_MspPostInit 1 */
  }

}

void HAL_TIM_Base_MspDeInit(TIM_HandleTypeDef* tim_baseHandle)
{

  if(tim_baseHandle->Instance==TIM2)
  {
  /* USER CODE BEGIN TIM2_MspDeInit 0 */

  /* USER CODE END TIM2_MspDeInit 0 */
    /* Peripheral clock disable */
    __HAL_RCC_TIM2_CLK_DISABLE();
  /* USER CODE BEGIN TIM2_MspDeInit 1 */

  /* USER CODE END TIM2_MspDeInit 1 */
  }
  else if(tim_baseHandle->Instance==TIM3)
  {
  /* USER CODE BEGIN TIM3_MspDeInit 0 */

  /* USER CODE END TIM3_MspDeInit 0 */
    /* Peripheral clock disable */
    __HAL_RCC_TIM3_CLK_DISABLE();

    /* TIM3 interrupt Deinit */
    HAL_NVIC_DisableIRQ(TIM3_IRQn);
  /* USER CODE BEGIN TIM3_MspDeInit 1 */

  /* USER CODE END TIM3_MspDeInit 1 */
  }
}

/* USER CODE BEGIN 1 */

/* USER CODE END 1 */

