/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    can.h
  * @brief   CAN1 initialization for Node 3.
  ******************************************************************************
  */
/* USER CODE END Header */

#ifndef __CAN_H__
#define __CAN_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

extern CAN_HandleTypeDef hcan;

void MX_CAN_Init(void);

#ifdef __cplusplus
}
#endif

#endif /* __CAN_H__ */
