/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    i2c.c
  * @brief   I2C1 initialization for Node 3.
  *          Standard Mode, 100 kHz. Pins: PB6=SCL, PB7=SDA.
  *          Requires external 4.7 kOhm pull-up resistors on both lines.
  ******************************************************************************
  */
/* USER CODE END Header */

#include "i2c.h"

I2C_HandleTypeDef hi2c1;

/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

void MX_I2C1_Init(void)
{
  /* USER CODE BEGIN I2C1_Init 0 */

  /* USER CODE END I2C1_Init 0 */

  /* USER CODE BEGIN I2C1_Init 1 */

  /* USER CODE END I2C1_Init 1 */

  hi2c1.Instance             = I2C1;
  hi2c1.Init.ClockSpeed      = 100000;                  /* 100 kHz standard mode  */
  hi2c1.Init.DutyCycle       = I2C_DUTYCYCLE_2;
  hi2c1.Init.OwnAddress1     = 0;
  hi2c1.Init.AddressingMode  = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2     = 0;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode   = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }

  /* USER CODE BEGIN I2C1_Init 2 */

  /* USER CODE END I2C1_Init 2 */
}

/* USER CODE BEGIN 1 */

/* USER CODE END 1 */
