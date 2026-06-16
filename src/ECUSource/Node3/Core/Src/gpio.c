/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    gpio.c
  * @brief   GPIO initialization for Node 3.
  *          PC13=LED output (active-LOW, initial HIGH = off).
  *          PA0=LEFT_SIGNAL input pull-up (active-LOW switch).
  *          PA1=RIGHT_SIGNAL input pull-up (active-LOW switch).
  *          PB6/PB7 (I2C1 SCL/SDA) are configured by HAL_I2C_MspInit() in
  *          stm32f1xx_hal_msp.c -- not here.
  ******************************************************************************
  */
/* USER CODE END Header */

#include "gpio.h"

/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();   /* PC13 LED       */
  __HAL_RCC_GPIOD_CLK_ENABLE();   /* OSC_IN/OUT     */
  __HAL_RCC_GPIOA_CLK_ENABLE();   /* PA0/PA1 signals, PA11/PA12 CAN */
  __HAL_RCC_GPIOB_CLK_ENABLE();   /* PB6/PB7 I2C1   */

  /* PC13 -- LED Output (active-LOW: GPIO_PIN_SET = LED off on startup) */
  HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_SET);
  GPIO_InitStruct.Pin   = LED_Pin;
  GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull  = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LED_GPIO_Port, &GPIO_InitStruct);

  /* PA0 -- LEFT_SIGNAL (input pull-up; active-LOW = signal ON) */
  GPIO_InitStruct.Pin  = LEFT_SIGNAL_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(LEFT_SIGNAL_GPIO_Port, &GPIO_InitStruct);

  /* PA1 -- RIGHT_SIGNAL (input pull-up; active-LOW = signal ON) */
  GPIO_InitStruct.Pin  = RIGHT_SIGNAL_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(RIGHT_SIGNAL_GPIO_Port, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 1 */

/* USER CODE END 1 */
