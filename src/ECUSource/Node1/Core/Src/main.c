/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body -- Node 1 Powertrain / Odometer
  ******************************************************************************
  * Copyright (c) 2026 STMicroelectronics. All rights reserved.
  * Licensed AS-IS; see LICENSE file in root directory.
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "can.h"
#include "tim.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdlib.h>  /* abs() -- used in encoder diff calculation                */
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

/*
 * trip_distance_km -- accumulated trip odometer in kilometres.
 *
 * Integrated at 100 Hz (every TIM2 interrupt = 10 ms) using:
 *   trip += speed_kmh * (0.01 / 3600.0)        [dt = 10 ms = 0.01 s]
 *
 * Transmitted on CAN 0x106 at 10 Hz encoded as:
 *   uint16_t encoded = (uint16_t)(trip_distance_km * 10.0);
 *   -> 1 LSB = 0.1 km, range 0-6553.5 km before uint16 wrap-around.
 *
 * FIX: 'volatile' removed -- written and read exclusively from the main loop
 * after moving integration out of the ISR. Same execution context; no race.
 */
double trip_distance_km = 0.0;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/*
 * Encoder_Data_t -- all encoder state packed in one struct.
 *
 * target_rpm   : RPM setpoint derived from raw encoder count delta.
 * current_rpm  : displayed/transmitted RPM after slew-rate limiting.
 *                Ramps toward target_rpm by +/-50 RPM per 10 ms tick.
 * send_rpm     : current_rpm cast to uint16 -- the CAN frame value.
 * send_speed_kmh: derived from send_rpm using the fixed ratio RPM/33.
 *                 Clamp: 300 km/h max (matches dashboard gauge upper bound).
 * data_ready   : flag set by TIM2 ISR, cleared by main loop.
 *                1-slot producer/consumer queue; no interrupt nesting.
 * last_counter : TIM1 counter value at the last processed tick.
 *                Signed 16-bit subtraction handles counter wrap-around.
 */
typedef struct {
    int32_t  target_rpm;
    int32_t  current_rpm;
    uint16_t send_rpm;
    uint16_t send_speed_kmh;
    volatile uint8_t data_ready;
    uint16_t last_counter;
} Encoder_Data_t;

Encoder_Data_t hEncoder = {0};

/*
 * Encoder_Init -- start TIM1 in encoder mode.
 * HAL_TIM_Encoder_Start() puts TIM1 into hardware quadrature decode mode.
 */
void Encoder_Init(void) {
    HAL_TIM_Encoder_Start(&htim1, TIM_CHANNEL_ALL);
    hEncoder.last_counter = __HAL_TIM_GET_COUNTER(&htim1);
}

/* RPM_PER_ENCODER_COUNT -- one CW detent adds this many RPM.
 * With 100 counts max (0-10000 RPM), 100 detents cover full scale.
 * Increase to 200 for coarser steps; decrease to 50 for finer. */
#define RPM_PER_ENCODER_COUNT  100

/*
 * Encoder_Update_Calculations -- called from main loop when data_ready == 1.
 *
 * STEP 1 -- Count delta (position change since last tick).
 * STEP 2 -- Position-based RPM target (s_encoder_pos accumulates CW/CCW).
 *           RPM holds its value when rotation stops (no decay to zero).
 * STEP 3 -- Slew-rate limiter: +/-50 RPM per tick (+/-500 RPM/s).
 * STEP 4 -- Derive speed: send_speed_kmh = send_rpm / 33.
 */
void Encoder_Update_Calculations(void) {
    /* STEP 1 */
    uint16_t current_counter = (uint16_t)__HAL_TIM_GET_COUNTER(&htim1);
    int16_t diff = (int16_t)(current_counter - hEncoder.last_counter);

    if (diff != 0) {
        /* STEP 2 -- position-based: accumulate and clamp */
        static int32_t s_encoder_pos = 0;
        s_encoder_pos += diff;
        if (s_encoder_pos < 0)
            s_encoder_pos = 0;
        if (s_encoder_pos > (10000 / RPM_PER_ENCODER_COUNT))
            s_encoder_pos = (10000 / RPM_PER_ENCODER_COUNT);
        hEncoder.target_rpm   = s_encoder_pos * RPM_PER_ENCODER_COUNT;
        hEncoder.last_counter = current_counter;
    }

    /* STEP 3 -- slew-rate limiter: +/-50 RPM per 10 ms tick */
    int32_t step = 50;
    if (hEncoder.current_rpm < hEncoder.target_rpm) {
        hEncoder.current_rpm += step;
        if (hEncoder.current_rpm > hEncoder.target_rpm)
            hEncoder.current_rpm = hEncoder.target_rpm;
    } else if (hEncoder.current_rpm > hEncoder.target_rpm) {
        hEncoder.current_rpm -= step;
        if (hEncoder.current_rpm < hEncoder.target_rpm)
            hEncoder.current_rpm = hEncoder.target_rpm;
    }

    /* STEP 4 */
    hEncoder.send_rpm       = (uint16_t)hEncoder.current_rpm;
    hEncoder.send_speed_kmh = (uint16_t)(hEncoder.send_rpm / 33);
    if (hEncoder.send_speed_kmh > 300) hEncoder.send_speed_kmh = 300;
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_CAN_Init();
  MX_TIM1_Init();
  MX_TIM2_Init();
  /* USER CODE BEGIN 2 */

  /* CAN Filter -- Accept-All (32-bit mask mode, mask=0x0000 -> all IDs pass).
   * Without an active filter the STM32 CAN peripheral will not acknowledge any
   * frame on the bus, causing Bus-Off on the first dominant-bit error.
   * ABOM is enabled in CubeMX, so no manual Stop/Start recovery is needed. */
  CAN_FilterTypeDef sFilterConfig;
  sFilterConfig.FilterBank           = 0;
  sFilterConfig.FilterMode           = CAN_FILTERMODE_IDMASK;
  sFilterConfig.FilterScale          = CAN_FILTERSCALE_32BIT;
  sFilterConfig.FilterIdHigh         = 0x0000;
  sFilterConfig.FilterIdLow          = 0x0000;
  sFilterConfig.FilterMaskIdHigh     = 0x0000;   /* mask=0 -> all ID bits are don't-care */
  sFilterConfig.FilterMaskIdLow      = 0x0000;
  sFilterConfig.FilterFIFOAssignment = CAN_RX_FIFO0;
  sFilterConfig.FilterActivation     = ENABLE;
  sFilterConfig.SlaveStartFilterBank = 14;       /* STM32F103 single-CAN: irrelevant but required */
  if (HAL_CAN_ConfigFilter(&hcan, &sFilterConfig) != HAL_OK)
  {
    Error_Handler();
  }

  if (HAL_CAN_Start(&hcan) != HAL_OK)
  {
    /* 5 Hz fast blink on PC13: CAN_Start failed.
     * PC13 is active-LOW: reset=on, set=off. */
    while (1)
    {
      HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
      HAL_Delay(100);   /* 100 ms -> 5 Hz */
    }
  }

  Encoder_Init();

  /* Start TIM2 -- triggers HAL_TIM_PeriodElapsedCallback() every 10 ms */
  HAL_TIM_Base_Start_IT(&htim2);

  CAN_TxHeaderTypeDef TxHeader;
  uint32_t TxMailbox;
  TxHeader.IDE                = CAN_ID_STD;
  TxHeader.RTR                = CAN_RTR_DATA;
  TxHeader.DLC                = 2;   /* speed and RPM frames carry exactly 2 bytes */
  TxHeader.TransmitGlobalTime = DISABLE;  /* must be explicit — uninitialized stack
                                           * garbage could equal ENABLE (1) and set
                                           * the TGT bit, corrupting TX data bytes  */

  uint32_t last_led_tick = 0;

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* Non-blocking 1 Hz LED heartbeat -- PC13 active-LOW */
    uint32_t now_led = HAL_GetTick();
    if ((now_led - last_led_tick) >= 500)   /* toggle every 500 ms = 1 Hz blink */
    {
      last_led_tick = now_led;
      HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
    }

    if (hEncoder.data_ready == 1)
    {
      hEncoder.data_ready = 0;

      Encoder_Update_Calculations();

      /* Trip integration in main loop (NOT ISR).
       * Soft-float on no-FPU STM32F103 takes ~150 cycles -- too slow for ISR. */
      trip_distance_km += (double)hEncoder.send_speed_kmh * (0.01 / 3600.0);

      /* TX: 0x100 -- Speed (km/h)
       * Encoding: big-endian uint16
       *   byte[0] = high byte (MSB), byte[1] = low byte (LSB) */
      if (HAL_CAN_GetTxMailboxesFreeLevel(&hcan) > 0)
      {
          TxHeader.StdId = 0x100;
          uint8_t TxData_SPD[8] = {0};
          TxData_SPD[0] = (uint8_t)(hEncoder.send_speed_kmh >> 8);
          TxData_SPD[1] = (uint8_t)(hEncoder.send_speed_kmh & 0xFF);
          HAL_CAN_AddTxMessage(&hcan, &TxHeader, TxData_SPD, &TxMailbox);
      }

      /* TX: 0x101 -- RPM (same big-endian uint16 encoding) */
      if (HAL_CAN_GetTxMailboxesFreeLevel(&hcan) > 0)
      {
          TxHeader.StdId = 0x101;
          uint8_t TxData_RPM[8] = {0};
          TxData_RPM[0] = (uint8_t)(hEncoder.send_rpm >> 8);
          TxData_RPM[1] = (uint8_t)(hEncoder.send_rpm & 0xFF);
          HAL_CAN_AddTxMessage(&hcan, &TxHeader, TxData_RPM, &TxMailbox);
      }

      /* TX: 0x106 -- Trip Distance @ 10 Hz (every 10th tick)
       * Encoding: big-endian uint16 where 1 LSB = 0.1 km */
      static uint8_t dist_tx_counter = 0;
      if (++dist_tx_counter >= 10)
      {
          dist_tx_counter = 0;
          if (HAL_CAN_GetTxMailboxesFreeLevel(&hcan) > 0)
          {
              uint16_t dist_encoded = (uint16_t)(trip_distance_km * 10.0);
              uint8_t TxData_DIST[2] = {0};
              TxData_DIST[0] = (uint8_t)(dist_encoded >> 8);
              TxData_DIST[1] = (uint8_t)(dist_encoded & 0xFF);
              TxHeader.StdId = 0x106;
              TxHeader.DLC   = 2;
              HAL_CAN_AddTxMessage(&hcan, &TxHeader, TxData_DIST, &TxMailbox);
              /* TxHeader.DLC stays 2 -- speed/RPM frames use the same 2-byte DLC */
          }
      }
    }

    /* TX: 0x1F1 -- Node 1 Heartbeat @ 500 ms
     * byte[0] = 0x01 if TIM1 encoder counter changed since last heartbeat tick (encoder moving)
     *           0xFF if counter frozen (encoder stationary or disconnected) */
    {
      static uint32_t last_hb1_tick = 0;
      uint32_t now_hb1 = HAL_GetTick();
      if ((now_hb1 - last_hb1_tick) >= 500)
      {
        last_hb1_tick = now_hb1;
        /* Node 1 alive = encoder ok. A dead/disconnected STM32 produces no
         * 0x1F1 frame at all; the Pi-side watchdog fires after 3 s and sets
         * node1Heartbeat=false. Encoder stationary (RPM=0) is NOT an error. */
        uint8_t enc_health = 0x01;
        if (HAL_CAN_GetTxMailboxesFreeLevel(&hcan) > 0)
        {
          uint8_t HB1[8] = {enc_health, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
          TxHeader.StdId = 0x1F1;
          TxHeader.DLC   = 8;
          HAL_CAN_AddTxMessage(&hcan, &TxHeader, HB1, &TxMailbox);
          TxHeader.DLC   = 2;
        }
      }
    }

    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */
/*
 * HAL_TIM_PeriodElapsedCallback -- TIM2 interrupt, 10 ms period.
 * Sets data_ready -- main loop reads encoder and integrates trip next pass.
 * Trip integration removed from ISR: soft-float on no-FPU STM32F103 blocks
 * lower-priority IRQs at 100 Hz. Integration now in while(1).
 */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  if (htim->Instance == TIM2)
  {
      hEncoder.data_ready = 1;   /* only flag set -- no float math in ISR */
  }
}
/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
