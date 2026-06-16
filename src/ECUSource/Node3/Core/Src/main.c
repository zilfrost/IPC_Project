/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    : main.c
  * @brief   : Node 3 -- Environment & Controls
  *            Reads BMP180 temperature and DS3231 RTC via I2C.
  *            Reads Left/Right turn signal GPIO inputs.
  *            Transmits CAN: Temperature, DateTime YYMMDDHHMM, Turn Signals.
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "can.h"    /* MX_CAN_Init(), hcan handle (defined in can.c)            */
#include "i2c.h"    /* MX_I2C1_Init(), hi2c1 handle (defined in i2c.c)         */
#include "gpio.h"   /* MX_GPIO_Init()                                           */

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdint.h>
#include <stdbool.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* I2C 8-bit addresses (HAL requires left-shifted 7-bit address)               */
#define BMP180_ADDR_W     (0x77 << 1)    /* 0xEE -- Write                      */
#define BMP180_ADDR_R     (0x77 << 1)    /* Same handle; HAL adds R/W bit      */
#define DS3231_ADDR_W     (0x68 << 1)    /* 0xD0 -- Write                      */
#define DS3231_ADDR_R     (0x68 << 1)    /* Same -- HAL adds R/W bit internally */

#define BMP180_REG_CTRL   0xF4
#define BMP180_REG_OUT_H  0xF6
#define BMP180_CMD_TEMP   0x2E
#define BMP180_CALIB_AC5  0xB2
#define BMP180_CALIB_AC6  0xB4
#define BMP180_CALIB_MC   0xBC
#define BMP180_CALIB_MD   0xBE

#define DS3231_REG_SEC    0x00
#define DEBOUNCE_MS       20U
#define I2C_TIMEOUT_MS    20U

/* Compile-time date/time — expands to build machine local time (Vietnam UTC+7).
 * __DATE__ = "Mmm DD YYYY"  __TIME__ = "HH:MM:SS"
 * Used to initialize DS3231 on first power-up (year == 0). */
#define BUILD_YEAR  ((__DATE__[9] -'0')*10 + (__DATE__[10]-'0'))
#define BUILD_MON   (__DATE__[0]=='J'&&__DATE__[1]=='a'?1: \
                     __DATE__[0]=='F'?2:                   \
                     __DATE__[0]=='M'&&__DATE__[2]=='r'?3: \
                     __DATE__[0]=='A'&&__DATE__[1]=='p'?4: \
                     __DATE__[0]=='M'&&__DATE__[2]=='y'?5: \
                     __DATE__[0]=='J'&&__DATE__[2]=='n'?6: \
                     __DATE__[0]=='J'&&__DATE__[2]=='l'?7: \
                     __DATE__[0]=='A'?8:                   \
                     __DATE__[0]=='S'?9:                   \
                     __DATE__[0]=='O'?10:                  \
                     __DATE__[0]=='N'?11:12)
#define BUILD_DAY   ((__DATE__[4]==' '?0:(__DATE__[4]-'0'))*10 + (__DATE__[5]-'0'))
#define BUILD_HOUR  ((__TIME__[0]-'0')*10 + (__TIME__[1]-'0'))
#define BUILD_MIN   ((__TIME__[3]-'0')*10 + (__TIME__[4]-'0'))
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* NOTE: hcan is declared in can.c / can.h -- do NOT redeclare here.           */
/* NOTE: hi2c1 is declared in i2c.c / i2c.h -- do NOT redeclare here.         */

/* USER CODE BEGIN PV */
static uint16_t bmp_AC5      = 0;
static uint16_t bmp_AC6      = 0;
static int16_t  bmp_MC       = 0;
static int16_t  bmp_MD       = 0;
static bool     bmp_calibrated = false;

typedef struct {
    GPIO_PinState raw_state;
    GPIO_PinState debounced_state;
    uint32_t      last_change_tick;
} Debounce_t;

static Debounce_t sig_left  = { GPIO_PIN_SET, GPIO_PIN_SET, 0 };
static Debounce_t sig_right = { GPIO_PIN_SET, GPIO_PIN_SET, 0 };
/* Default 0xFF: assume absent until HAL_OK confirms presence */
static uint8_t hb_bmp180 = 0xFF;
static uint8_t hb_ds3231 = 0xFF;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

static uint8_t debounce_read(Debounce_t *sig, GPIO_TypeDef *port, uint16_t pin)
{
    GPIO_PinState raw = HAL_GPIO_ReadPin(port, pin);
    uint32_t now = HAL_GetTick();
    if (raw != sig->raw_state) { sig->raw_state = raw; sig->last_change_tick = now; }
    if ((now - sig->last_change_tick) >= DEBOUNCE_MS) sig->debounced_state = sig->raw_state;
    return (sig->debounced_state == GPIO_PIN_RESET) ? 1U : 0U;
}

static HAL_StatusTypeDef bmp180_read_calibration(void)
{
    uint8_t buf[2]; HAL_StatusTypeDef ret; uint8_t reg;
    reg = BMP180_CALIB_AC5;
    ret = HAL_I2C_Master_Transmit(&hi2c1, BMP180_ADDR_W, &reg, 1, I2C_TIMEOUT_MS); if (ret != HAL_OK) return ret;
    ret = HAL_I2C_Master_Receive(&hi2c1, BMP180_ADDR_R | 1, buf, 2, I2C_TIMEOUT_MS); if (ret != HAL_OK) return ret;
    bmp_AC5 = ((uint16_t)buf[0] << 8) | buf[1];
    reg = BMP180_CALIB_AC6;
    ret = HAL_I2C_Master_Transmit(&hi2c1, BMP180_ADDR_W, &reg, 1, I2C_TIMEOUT_MS); if (ret != HAL_OK) return ret;
    ret = HAL_I2C_Master_Receive(&hi2c1, BMP180_ADDR_R | 1, buf, 2, I2C_TIMEOUT_MS); if (ret != HAL_OK) return ret;
    bmp_AC6 = ((uint16_t)buf[0] << 8) | buf[1];
    reg = BMP180_CALIB_MC;
    ret = HAL_I2C_Master_Transmit(&hi2c1, BMP180_ADDR_W, &reg, 1, I2C_TIMEOUT_MS); if (ret != HAL_OK) return ret;
    ret = HAL_I2C_Master_Receive(&hi2c1, BMP180_ADDR_R | 1, buf, 2, I2C_TIMEOUT_MS); if (ret != HAL_OK) return ret;
    bmp_MC = (int16_t)(((uint16_t)buf[0] << 8) | buf[1]);
    reg = BMP180_CALIB_MD;
    ret = HAL_I2C_Master_Transmit(&hi2c1, BMP180_ADDR_W, &reg, 1, I2C_TIMEOUT_MS); if (ret != HAL_OK) return ret;
    ret = HAL_I2C_Master_Receive(&hi2c1, BMP180_ADDR_R | 1, buf, 2, I2C_TIMEOUT_MS); if (ret != HAL_OK) return ret;
    bmp_MD = (int16_t)(((uint16_t)buf[0] << 8) | buf[1]);
    bmp_calibrated = true;
    return HAL_OK;
}

static HAL_StatusTypeDef bmp180_read_temperature(int8_t *out_temp_c)
{
    if (!bmp_calibrated) return HAL_ERROR;
    uint8_t buf[2]; HAL_StatusTypeDef ret;
    uint8_t cmd[2] = { BMP180_REG_CTRL, BMP180_CMD_TEMP };
    ret = HAL_I2C_Master_Transmit(&hi2c1, BMP180_ADDR_W, cmd, 2, I2C_TIMEOUT_MS); if (ret != HAL_OK) return ret;
    HAL_Delay(5);   /* BMP180 needs >= 4.5 ms conversion time (OSS=0) */
    uint8_t reg = BMP180_REG_OUT_H;
    ret = HAL_I2C_Master_Transmit(&hi2c1, BMP180_ADDR_W, &reg, 1, I2C_TIMEOUT_MS); if (ret != HAL_OK) return ret;
    ret = HAL_I2C_Master_Receive(&hi2c1, BMP180_ADDR_R | 1, buf, 2, I2C_TIMEOUT_MS); if (ret != HAL_OK) return ret;
    int32_t UT = ((int32_t)buf[0] << 8) | buf[1];
    int32_t X1 = ((UT - (int32_t)bmp_AC6) * (int32_t)bmp_AC5) >> 15;
    int32_t X2 = ((int32_t)bmp_MC << 11) / (X1 + (int32_t)bmp_MD);
    int32_t B5 = X1 + X2;
    int32_t T  = (B5 + 8) >> 4;   /* units: 0.1 degC */
    int32_t temp = T / 10;
    if (temp < -40) temp = -40;
    if (temp > 120) temp = 120;
    *out_temp_c = (int8_t)temp;
    return HAL_OK;
}

static inline uint8_t bcd_to_dec(uint8_t bcd) { return (uint8_t)((bcd >> 4) * 10U + (bcd & 0x0FU)); }
static inline uint8_t dec_to_bcd(uint8_t dec) { return (uint8_t)(((dec / 10) << 4) | (dec % 10)); }

static HAL_StatusTypeDef ds3231_read_datetime(uint8_t *out_buf)
{
    uint8_t rtc_buf[7] = {0}; uint8_t reg = DS3231_REG_SEC; HAL_StatusTypeDef ret;
    ret = HAL_I2C_Master_Transmit(&hi2c1, DS3231_ADDR_W, &reg, 1, I2C_TIMEOUT_MS); if (ret != HAL_OK) return ret;
    ret = HAL_I2C_Master_Receive(&hi2c1, DS3231_ADDR_R | 1, rtc_buf, 7, I2C_TIMEOUT_MS); if (ret != HAL_OK) return ret;
    out_buf[0] = bcd_to_dec(rtc_buf[6]);           /* YY */
    out_buf[1] = bcd_to_dec(rtc_buf[5] & 0x1FU);   /* MM */
    out_buf[2] = bcd_to_dec(rtc_buf[4] & 0x3FU);   /* DD */
    out_buf[3] = bcd_to_dec(rtc_buf[2] & 0x3FU);   /* HH */
    out_buf[4] = bcd_to_dec(rtc_buf[1] & 0x7FU);   /* Min */
    return HAL_OK;
}
static HAL_StatusTypeDef ds3231_set_datetime(uint8_t yy, uint8_t mo, uint8_t dd,
                                              uint8_t hh, uint8_t mn)
{
    uint8_t buf[8] = {
        DS3231_REG_SEC,
        0x00,               /* seconds = 0 */
        dec_to_bcd(mn),     /* minutes */
        dec_to_bcd(hh),     /* hours (24 h) */
        0x01,               /* day-of-week (unused by dashboard) */
        dec_to_bcd(dd),     /* date */
        dec_to_bcd(mo),     /* month */
        dec_to_bcd(yy)      /* year (2-digit) */
    };
    return HAL_I2C_Master_Transmit(&hi2c1, DS3231_ADDR_W, buf, 8, I2C_TIMEOUT_MS);
}

static void i2c_bus_reset(void)
{
    /* Release stuck I2C bus after sensor failure: de-init, short pause, re-init.
     * Calibration is deferred to the next BLOCK B cycle via the !bmp_calibrated guard. */
    HAL_I2C_DeInit(&hi2c1);
    HAL_Delay(10);
    MX_I2C1_Init();
    bmp_calibrated = false;
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
  MX_I2C1_Init();

  /* USER CODE BEGIN 2 */

  if (bmp180_read_calibration() == HAL_OK) {
      hb_bmp180 = 0x01;   /* sensor present at boot; first heartbeat shows correct state */
  } else {
      /* Failed calibration may leave I2C in error state; recover before DS3231 init read */
      HAL_I2C_DeInit(&hi2c1);
      HAL_Delay(10);
      MX_I2C1_Init();
  }

  /* Set RTC if uninitialized (year == 0: chip never set or lost battery backup) */
  {
      uint8_t dt_init[5];
      if (ds3231_read_datetime(dt_init) == HAL_OK) {
          hb_ds3231 = 0x01;   /* RTC present at boot; first heartbeat shows correct state */
          if (dt_init[0] == 0)
              ds3231_set_datetime(BUILD_YEAR, BUILD_MON, BUILD_DAY, BUILD_HOUR, BUILD_MIN);
      }
  }

  /* CAN Filter -- Accept-All (32-bit mask mode, mask=0x0000 -> all IDs pass).
   * Required before HAL_CAN_Start(). Without it the STM32 CAN peripheral will
   * not acknowledge any frame on the bus, causing Bus-Off. */
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
  TxHeader.DLC = 1;
  TxHeader.TransmitGlobalTime = DISABLE;

  uint32_t last_env_tx  = 0;
  uint32_t last_sig_tx  = 0;
  uint32_t last_led_tick = 0;

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    uint32_t now = HAL_GetTick();

    /* Non-blocking 1 Hz LED heartbeat -- PC13 active-LOW */
    if ((now - last_led_tick) >= 500) { last_led_tick = now; HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13); }

    /* BLOCK A -- Turn Signals @ 20 Hz (every 50 ms) */
    if ((now - last_sig_tx) >= 50) {
      last_sig_tx = now;
      uint8_t left  = debounce_read(&sig_left,  RIGHT_SIGNAL_GPIO_Port, RIGHT_SIGNAL_Pin);
      uint8_t right = debounce_read(&sig_right, LEFT_SIGNAL_GPIO_Port,  LEFT_SIGNAL_Pin);
      if (HAL_CAN_GetTxMailboxesFreeLevel(&hcan) > 0) {
        uint8_t sig_byte = (uint8_t)((right << 1) | left);
        TxHeader.StdId = 0x107; TxHeader.DLC = 1;
        HAL_CAN_AddTxMessage(&hcan, &TxHeader, &sig_byte, &TxMailbox);
      }
    }

    /* BLOCK B -- Temperature + RTC @ 1 Hz (every 1000 ms) */
    if ((now - last_env_tx) >= 1000) {
      last_env_tx = now;

      /* Re-calibrate BMP180 if calibration was lost (sensor was temporarily
       * absent). Benign no-op when sensor is still absent; succeeds immediately
       * when sensor is re-plugged so the next temperature read works. */
      if (!bmp_calibrated) {
          bmp180_read_calibration();
      }

      /* TX: 0x103 -- Temperature (byte[0] = signed int8 cast to uint8) */
      int8_t temp_c = 0;
      if (bmp180_read_temperature(&temp_c) == HAL_OK) {
        hb_bmp180 = 0x01;
        if (HAL_CAN_GetTxMailboxesFreeLevel(&hcan) > 0) {
          uint8_t temp_byte = (uint8_t)temp_c;
          TxHeader.StdId = 0x103; TxHeader.DLC = 1;
          HAL_CAN_AddTxMessage(&hcan, &TxHeader, &temp_byte, &TxMailbox);
        }
      } else {
        hb_bmp180 = 0xFF;
        i2c_bus_reset();   /* BMP180 failure leaves I2C bus stuck; reset before DS3231 read */
      }

      /* TX: 0x105 -- DateTime YYMMDDHHMM (5 bytes) */
      uint8_t dt_buf[5];
      if (ds3231_read_datetime(dt_buf) == HAL_OK) {
        hb_ds3231 = 0x01;
        if (HAL_CAN_GetTxMailboxesFreeLevel(&hcan) > 0) {
          TxHeader.StdId = 0x105; TxHeader.DLC = 5;
          HAL_CAN_AddTxMessage(&hcan, &TxHeader, dt_buf, &TxMailbox);
        }
      } else {
        hb_ds3231 = 0xFF;
      }

      /* Bus reset only when DS3231 fails — that indicates the I2C bus is stuck.
       * BMP180-only failure means the sensor is absent; the bus is still clean
       * and will recover automatically via the re-calibration guard above. */
      if (hb_ds3231 == 0xFF)
          i2c_bus_reset();
    }
    /* TX: 0x1F3 -- Node 3 Heartbeat @ 500 ms
     * byte[0] = BMP180 health (0x01=HAL_OK, 0xFF=absent/error)
     * byte[1] = DS3231 health (0x01=HAL_OK, 0xFF=absent/error)
     * byte[2] = Signal GPIO  (0x01 always -- GPIO failure is chip-level fault) */
    {
      static uint32_t last_hb3_tick = 0;
      uint32_t now_hb3 = HAL_GetTick();
      if ((now_hb3 - last_hb3_tick) >= 500)
      {
        last_hb3_tick = now_hb3;
        if (HAL_CAN_GetTxMailboxesFreeLevel(&hcan) > 0)
        {
          uint8_t HB3[8] = {hb_bmp180, hb_ds3231, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00};
          TxHeader.StdId = 0x1F3;
          TxHeader.DLC   = 8;
          HAL_CAN_AddTxMessage(&hcan, &TxHeader, HB3, &TxMailbox);
          TxHeader.DLC   = 1;   /* restore: normal Node 3 frames use DLC=1 */
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
}

/* USER CODE BEGIN 4 */
/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  __disable_irq();
  while (1) {}
  /* USER CODE END Error_Handler_Debug */
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
