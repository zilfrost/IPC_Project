/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    : main.c
  * @brief   : Node 2 -- BCM/BMS Simulator
  *            Reads 3x 10kOhm potentiometers via ADC with DMA.
  *            Transmits CAN: Battery SoC, Gear index, Drive Mode index.
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "adc.h"    /* MX_ADC1_Init(), hadc1 handle                            */
#include "dma.h"    /* MX_DMA_Init() -- must be initialised BEFORE ADC          */
#include "can.h"    /* MX_CAN_Init(), hcan handle                               */
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdint.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/*
 * Zone mapping constants.
 * Algorithm: zone = adc_value * N / 4096
 * Using 4096 (not 4095) as divisor prevents zone from ever reaching N.
 *   4095 * 7 / 4096 = 6  (D3, last zone)
 *   4095 * 3 / 4096 = 2  (SPORT, last zone)
 */
#define ADC_GEAR_ZONES   7
#define ADC_MODE_ZONES   3
#define ADC_MAX_12BIT    4096UL   /* divisor for zone mapping (NOT 4095)        */
#define ADC_FULL_SCALE   4095UL   /* divisor for SoC % mapping                  */

/* ADC rail-check thresholds for 0x1F2 heartbeat health bytes.
 * A pot reading outside [LOW_RAIL, HIGH_RAIL] indicates disconnected or
 * shorted hardware; within range it is considered healthy regardless of position. */
#define ADC_POT_LOW_RAIL    50    /* below this -> pot disconnected / shorted to GND */
#define ADC_POT_HIGH_RAIL 4045   /* above this -> pot shorted to VCC                */
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
/*
 * adc_raw[3] -- DMA destination buffer (circular mode).
 *   adc_raw[0] = CH0 -> PA0 -> Battery SoC potentiometer
 *   adc_raw[1] = CH1 -> PA1 -> Gear selector potentiometer
 *   adc_raw[2] = CH2 -> PA2 -> Drive mode potentiometer
 *
 * WARNING: DMA Memory Increment (MINC) MUST be enabled in CubeMX.
 * If left unchecked (default), all three transfers land on &adc_raw[0]:
 * all three elements appear to mirror the same potentiometer ("mirroring" bug).
 *
 * 'volatile': DMA writes this array asynchronously; compiler must not cache.
 * 'uint32_t': matches DMA configuration (Data Width = Word/Word).
 */
volatile uint32_t adc_raw[3] = {0};

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);

/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/*
 * map_battery_soc -- 12-bit ADC -> battery state-of-charge %.
 *   soc = adc * 100 / 4095
 *   Multiply first to preserve integer precision (max: 409500 < 2^32).
 *   Clamped to [0, 100] to guard against ADC noise.
 */
static inline uint8_t map_battery_soc(uint32_t adc)
{
    uint32_t soc = (adc * 100UL) / ADC_FULL_SCALE;
    if (soc > 100) soc = 100;
    return (uint8_t)soc;
}

/*
 * map_gear -- 12-bit ADC -> gear index 0-6.
 * ADC range    | Zone | Gear
 * 0    -  584  |  0   |  P
 * 585  - 1169  |  1   |  R
 * 1170 - 1754  |  2   |  N
 * 1755 - 2339  |  3   |  D
 * 2340 - 2924  |  4   |  D1
 * 2925 - 3509  |  5   |  D2
 * 3510 - 4095  |  6   |  D3
 */
static inline uint8_t map_gear(uint32_t adc)
{
    uint32_t zone = (adc * (uint32_t)ADC_GEAR_ZONES) / ADC_MAX_12BIT;
    if (zone >= ADC_GEAR_ZONES) zone = ADC_GEAR_ZONES - 1;
    return (uint8_t)zone;
}

/*
 * map_drive_mode -- 12-bit ADC -> drive mode index 0-2.
 * ADC range    | Zone | Mode   | Dashboard theme color
 * 0    - 1364  |  0   | ECO    | Lime    #B8FF01
 * 1365 - 2729  |  1   | NORMAL | White   #FFFFFF
 * 2730 - 4095  |  2   | SPORT  | Red-Org #FF3300
 */
static inline uint8_t map_drive_mode(uint32_t adc)
{
    uint32_t zone = (adc * (uint32_t)ADC_MODE_ZONES) / ADC_MAX_12BIT;
    if (zone >= ADC_MODE_ZONES) zone = ADC_MODE_ZONES - 1;
    return (uint8_t)zone;
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

  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* CRITICAL ORDER: DMA must init before ADC */
  MX_GPIO_Init();
  MX_DMA_Init();    /* <- MUST come before MX_ADC1_Init()                       */
  MX_ADC1_Init();
  MX_CAN_Init();

  /* USER CODE BEGIN 2 */

  /* ADC self-calibration -- run once after power-on, before HAL_ADC_Start_DMA */
  HAL_ADCEx_Calibration_Start(&hadc1);

  /* Start ADC in DMA mode -- adc_raw[] updates continuously in the background */
  HAL_ADC_Start_DMA(&hadc1, (uint32_t *)adc_raw, 3);

  /* CAN Filter -- Accept-All (32-bit mask mode, mask=0x0000 -> all IDs pass).
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
    /* 5 Hz fast blink on PC13: CAN_Start failed. PC13 is active-LOW. */
    while (1)
    {
      HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
      HAL_Delay(100);   /* 100 ms -> 5 Hz */
    }
  }

  CAN_TxHeaderTypeDef TxHeader;
  uint32_t TxMailbox;
  TxHeader.IDE = CAN_ID_STD;
  TxHeader.RTR = CAN_RTR_DATA;
  TxHeader.DLC = 1;    /* all 3 Node 2 frames carry exactly 1 data byte        */
  TxHeader.TransmitGlobalTime = DISABLE;

  uint32_t last_tx_tick  = 0;
  uint32_t last_led_tick = 0;

  /* USER CODE END 2 */

  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* Non-blocking 1 Hz LED heartbeat -- PC13 active-LOW */
    uint32_t now = HAL_GetTick();
    if ((now - last_led_tick) >= 500)
    {
      last_led_tick = now;
      HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
    }

    /* 10 Hz rate limiter via uint32 subtraction.
     * Subtraction handles HAL_GetTick() rollover correctly (~49 days). */
    if ((now - last_tx_tick) >= 100)   /* 100 ms -> 10 Hz                       */
    {
      last_tx_tick = now;

      /* Snapshot adc_raw[] once to avoid torn reads from async DMA updates */
      uint32_t raw_batt = adc_raw[0];
      uint32_t raw_gear = adc_raw[1];
      uint32_t raw_mode = adc_raw[2];

      uint8_t soc  = map_battery_soc(raw_batt);
      uint8_t gear = map_gear(raw_gear);

      uint8_t mode = map_drive_mode(raw_mode);

      /* TX: 0x102 -- Battery SoC % (byte[0] = uint8, 0-100) */
      if (HAL_CAN_GetTxMailboxesFreeLevel(&hcan) > 0)
      {
        TxHeader.StdId = 0x102;
        HAL_CAN_AddTxMessage(&hcan, &TxHeader, &soc, &TxMailbox);
      }

      /* TX: 0x104 -- Gear index (byte[0] = uint8 0-6: P/R/N/D/D1/D2/D3) */
      if (HAL_CAN_GetTxMailboxesFreeLevel(&hcan) > 0)
      {
        TxHeader.StdId = 0x104;
        HAL_CAN_AddTxMessage(&hcan, &TxHeader, &gear, &TxMailbox);
      }

      /* TX: 0x108 -- Drive Mode index (byte[0] = uint8 0-2: ECO/NORMAL/SPORT) */
      if (HAL_CAN_GetTxMailboxesFreeLevel(&hcan) > 0)
      {
        TxHeader.StdId = 0x108;
        HAL_CAN_AddTxMessage(&hcan, &TxHeader, &mode, &TxMailbox);
      }

    } /* end 100ms block */

    /* TX: 0x1F2 -- Node 2 Heartbeat @ 500 ms (ADC rail check)
     * byte[0] = SoC  pot (PA0) health   -- 0x01 if in range (50..4045), 0xFF if at rail
     * byte[1] = Gear pot (PA1) health   -- 0x01 if in range (50..4045), 0xFF if at rail
     * byte[2] = Mode pot (PA2) health   -- 0x01 if in range (50..4045), 0xFF if at rail */
    {
      static uint32_t last_hb2_tick = 0;
      uint32_t now_hb2 = HAL_GetTick();
      if ((now_hb2 - last_hb2_tick) >= 500)
      {
        last_hb2_tick = now_hb2;
        if (HAL_CAN_GetTxMailboxesFreeLevel(&hcan) > 0)
        {
          uint8_t HB2[8] = {
            (adc_raw[0] > ADC_POT_LOW_RAIL && adc_raw[0] < ADC_POT_HIGH_RAIL) ? 0x01 : 0xFF,
            (adc_raw[1] > ADC_POT_LOW_RAIL && adc_raw[1] < ADC_POT_HIGH_RAIL) ? 0x01 : 0xFF,
            (adc_raw[2] > ADC_POT_LOW_RAIL && adc_raw[2] < ADC_POT_HIGH_RAIL) ? 0x01 : 0xFF,
            0x00, 0x00, 0x00, 0x00, 0x00
          };
          TxHeader.StdId = 0x1F2;
          TxHeader.DLC   = 8;
          HAL_CAN_AddTxMessage(&hcan, &TxHeader, HB2, &TxMailbox);
          TxHeader.DLC   = 1;   /* restore: normal Node 2 frames use DLC=1 */
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
  *
  * FIX: PeriphClkInit sets ADC clock to PCLK2/6 = 72/6 = 12 MHz.
  * Without this the ADC defaults to PCLK2/2 = 36 MHz, exceeding the
  * STM32F103 14 MHz ADC clock maximum -> incorrect ADC readings.
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct       = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct       = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInit     = {0};

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState       = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState       = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState   = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource  = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL     = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) { Error_Handler(); }

  RCC_ClkInitStruct.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                                   | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK) { Error_Handler(); }

  /* ADC peripheral clock: PCLK2 / 6 = 72 / 6 = 12 MHz (max allowed: 14 MHz) */
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_ADC;
  PeriphClkInit.AdcClockSelection    = RCC_ADCPCLK2_DIV6;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK) { Error_Handler(); }
}

/* USER CODE BEGIN 4 */
/*
 * HAL_ADC_ConvCpltCallback -- fires when DMA completes one full 3-channel scan.
 * Left empty: main loop reads adc_raw[] directly without needing notification.
 */
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
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
