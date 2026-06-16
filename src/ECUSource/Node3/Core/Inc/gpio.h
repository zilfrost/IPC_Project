/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    gpio.h
  * @brief   GPIO initialization for Node 3.
  *          PC13=LED (active-LOW), PA0=LEFT_SIGNAL (pull-up), PA1=RIGHT_SIGNAL (pull-up).
  ******************************************************************************
  */
/* USER CODE END Header */

#ifndef __GPIO_H__
#define __GPIO_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

void MX_GPIO_Init(void);

#ifdef __cplusplus
}
#endif

#endif /* __GPIO_H__ */
