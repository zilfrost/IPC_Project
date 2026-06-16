/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    can.c
  * @brief   CAN1 initialization for Node 3.
  *          500 kbps, Normal mode, ABOM enabled, AutoRetransmission enabled.
  ******************************************************************************
  */
/* USER CODE END Header */

#include "can.h"

CAN_HandleTypeDef hcan;

/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

void MX_CAN_Init(void)
{
  /* USER CODE BEGIN CAN_Init 0 */

  /* USER CODE END CAN_Init 0 */

  /* USER CODE BEGIN CAN_Init 1 */

  /* USER CODE END CAN_Init 1 */

  hcan.Instance                  = CAN1;
  hcan.Init.Prescaler            = 4;              /* TQ = 4 / 36 MHz = 111 ns   */
  hcan.Init.Mode                 = CAN_MODE_NORMAL;
  hcan.Init.SyncJumpWidth        = CAN_SJW_1TQ;
  hcan.Init.TimeSeg1             = CAN_BS1_12TQ;   /* 12 TQ                      */
  hcan.Init.TimeSeg2             = CAN_BS2_5TQ;    /* 5 TQ; 500 kbps total       */
  hcan.Init.TimeTriggeredMode    = DISABLE;
  hcan.Init.AutoBusOff           = ENABLE;         /* ABOM: auto bus-off recovery */
  hcan.Init.AutoWakeUp           = DISABLE;
  hcan.Init.AutoRetransmission   = ENABLE;
  hcan.Init.ReceiveFifoLocked    = DISABLE;
  hcan.Init.TransmitFifoPriority = DISABLE;
  if (HAL_CAN_Init(&hcan) != HAL_OK)
  {
    Error_Handler();
  }

  /* USER CODE BEGIN CAN_Init 2 */

  /* USER CODE END CAN_Init 2 */
}

/* USER CODE BEGIN 1 */

/* USER CODE END 1 */
