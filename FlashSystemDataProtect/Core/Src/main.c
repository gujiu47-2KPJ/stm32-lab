/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2023 STMicroelectronics.
  * All rights reserved.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "w25qxx.h"
#include "OLED.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define CLEAR_BUTTON_PIN    GPIO_PIN_0
#define CLEAR_BUTTON_PORT   GPIOA
#define COUNT_BUTTON_PIN    GPIO_PIN_1
#define COUNT_BUTTON_PORT   GPIOA

#define FLASH_MAGIC          0xA5    // Flash数据有效标记
#define FLASH_DATA_ADDR      0x000000

#define DEBOUNCE_CNT_MAX     3       // 连续稳定次数确认(3×30ms=90ms)
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */
typedef __packed struct {
    uint8_t magic;              // 数据有效标记(0xA5=有效)
    uint32_t button_count;      // 累计按钮次数
    uint8_t checksum;           // 校验和(sum of magic+button_count bytes)
} SystemData_TypeDef;

SystemData_TypeDef current_data;    // 当前运行数据
SystemData_TypeDef saved_data;      // 从Flash读回的数据

uint8_t flash_ok = 0;               // Flash初始化成功标志
uint8_t data_restored = 0;          // 上电恢复成功标志
uint8_t display_dirty = 1;          // OLED需要刷新标志
uint32_t flash_read_id = 0;         // 实际读到的Flash ID
/* USER CODE END PV */

I2C_HandleTypeDef hi2c1;
SPI_HandleTypeDef hspi1;

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_I2C1_Init(void);
static void MX_SPI1_Init(void);
/* USER CODE BEGIN PFP */
/* Private function prototypes */
void System_Init(void);
void Button_Process(void);
void OLED_Display_Status(void);
uint8_t Calculate_Checksum(SystemData_TypeDef *data);
void Save_Data_To_Flash(void);
void Read_Data_From_Flash(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* USER CODE BEGIN 1 */
  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* Configure the system clock */
  SystemClock_Config();

  /* Initialize all configured peripherals */
  System_Init();
  
  /* USER CODE BEGIN 2 */
  // 初始化当前数据
  current_data.magic = FLASH_MAGIC;
  current_data.button_count = 0;
  current_data.checksum = 0;

  // 从Flash恢复上次的计数
  if (flash_ok) {
    Read_Data_From_Flash();
  }

  if (data_restored) {
    // 恢复成功：在上次计数基础上继续累加
    current_data.button_count = saved_data.button_count;
  } else {
    // 恢复失败：清除saved_data，避免OLED显示Flash擦除后的0xFF垃圾值
    saved_data.button_count = 0;
  }

  // 显示启动状态
  OLED_Display_Status();
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    // 按键检测和处理
    Button_Process();

    // 只在数据变化时刷新OLED（否则I2C传输阻塞按键响应）
    if (display_dirty) {
      OLED_Display_Status();
      display_dirty = 0;
    }

    HAL_Delay(30);
  }
  /* USER CODE END 3 */
}

/**
  * @brief System initialization function
  * @param None
  * @retval None
  */
void System_Init(void)
{
  MX_GPIO_Init();
  MX_I2C1_Init();
  MX_SPI1_Init();

  // 先初始化OLED，以便显示错误信息
  OLED_Init();
  OLED_Clear();

  // 初始化W25QXX Flash
  if (W25QXX_Init() == 0) {
    flash_ok = 1;
    flash_read_id = W25Q64_ID;   // Init成功，肯定是W25Q64
  } else {
    flash_ok = 0;
    flash_read_id = W25QXX_ReadID();   // 失败后再读一次实际ID用于调试
  }
}

/**
  * @brief Button processing function
  * @param None
  * @retval None
  */
void Button_Process(void)
{
  /* 每个按键独立消抖状态机 */
  static uint8_t  clear_last = 1;       // PA0上次电平(上拉=1)
  static uint8_t  count_last = 1;       // PA1上次电平(上拉=1)
  static uint8_t  clear_cnt = 0;        // PA0稳定计数
  static uint8_t  count_cnt = 0;        // PA1稳定计数

  uint8_t clear_now = HAL_GPIO_ReadPin(CLEAR_BUTTON_PORT, CLEAR_BUTTON_PIN);
  uint8_t count_now = HAL_GPIO_ReadPin(COUNT_BUTTON_PORT, COUNT_BUTTON_PIN);

  /* ── PA0: 清零键 ── */
  if (clear_now == clear_last) {
    if (clear_cnt < DEBOUNCE_CNT_MAX) clear_cnt++;
    if (clear_cnt == DEBOUNCE_CNT_MAX && clear_last == 0) {
      // 确认按下：清零计数并保存
      current_data.button_count = 0;
      if (flash_ok) Save_Data_To_Flash();
      display_dirty = 1;              // 标记刷新
      clear_cnt++;  // 防止重复触发
    }
  } else {
    clear_cnt = 0;
    clear_last = clear_now;
  }

  /* ── PA1: 计数键 ── */
  if (count_now == count_last) {
    if (count_cnt < DEBOUNCE_CNT_MAX) count_cnt++;
    if (count_cnt == DEBOUNCE_CNT_MAX && count_last == 0) {
      // 确认按下：计数+1 并立即保存到Flash
      current_data.button_count++;
      if (flash_ok) Save_Data_To_Flash();
      display_dirty = 1;              // 标记刷新
      count_cnt++;  // 防止重复触发
    }
  } else {
    count_cnt = 0;
    count_last = count_now;
  }
}

/**
  * @brief Display system status on OLED
  * @param None
  * @retval None
  */
void OLED_Display_Status(void)
{
  char buf[32];

  OLED_Clear();

  // 第1行（页0-1）：标题
  OLED_ShowString(1, 1, "--Btn Counter--");

  // 第3行（页2-3）：当前累计次数
  sprintf(buf, "Count:%lu", (unsigned long)current_data.button_count);
  OLED_ShowString(3, 1, buf);

  // 第5行（页4-5）：上次保存的次数
  sprintf(buf, "Saved:%lu", (unsigned long)saved_data.button_count);
  OLED_ShowString(5, 1, buf);

  // 第7行（页6-7）：Flash状态 + 按键提示
  if (flash_ok) {
    OLED_ShowString(7, 1, "FlashOK PA0:Clr");
  } else {
    sprintf(buf, "ID:%06lX", (unsigned long)flash_read_id);
    OLED_ShowString(7, 1, buf);  // 显示实际读到的ID
  }

  OLED_Refresh_Gram();
}

/**
  * @brief Calculate checksum for system data
  * @param data: Pointer to system data structure
  * @retval Checksum value
  */
uint8_t Calculate_Checksum(SystemData_TypeDef *data)
{
  uint8_t checksum = 0;
  uint8_t *ptr = (uint8_t*)data;
  
  // 对数据结构进行校验和计算（除了checksum字段本身）
  for(int i = 0; i < sizeof(SystemData_TypeDef) - 1; i++) {
    checksum += ptr[i];
  }
  
  return checksum;
}

/**
  * @brief Save system data to external Flash
  * @param None
  * @retval None
  */
void Save_Data_To_Flash(void)
{
  current_data.magic = FLASH_MAGIC;
  current_data.checksum = Calculate_Checksum(&current_data);

  // 擦除扇区后写入
  W25QXX_SectorErase(FLASH_DATA_ADDR);
  W25QXX_BufferWrite((uint8_t*)&current_data, FLASH_DATA_ADDR, sizeof(SystemData_TypeDef));
  // 注意：不更新saved_data，OLED的Saved行保持显示"上次断电时的值"
}

/**
  * @brief Read system data from external Flash
  * @param None
  * @retval None
  */
void Read_Data_From_Flash(void)
{
  W25QXX_BufferRead((uint8_t*)&saved_data, FLASH_DATA_ADDR, sizeof(SystemData_TypeDef));

  // 三重校验：magic + 校验和
  if (saved_data.magic != FLASH_MAGIC) {
    data_restored = 0;
    return;
  }

  uint8_t calc = Calculate_Checksum(&saved_data);
  if (calc == saved_data.checksum) {
    data_restored = 1;   // 校验通过，数据有效
  } else {
    data_restored = 0;   // 校验失败
  }
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

/**
  * @brief I2C1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C1_Init(void)
{

  /* USER CODE BEGIN I2C1_Init 0 */

  /* USER CODE END I2C1_Init 0 */

  /* USER CODE BEGIN I2C1_Init 1 */

  /* USER CODE END I2C1_Init 1 */
  hi2c1.Instance = I2C1;
  hi2c1.Init.ClockSpeed = 100000;
  hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C1_Init 2 */

  /* USER CODE END I2C1_Init 2 */

}

/**
  * @brief SPI1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI1_Init(void)
{

  /* USER CODE BEGIN SPI1_Init 0 */

  /* USER CODE END SPI1_Init 0 */

  /* USER CODE BEGIN SPI1_Init 1 */

  /* USER CODE END SPI1_Init 1 */
  /* SPI1 parameter configuration*/
  hspi1.Instance = SPI1;
  hspi1.Init.Mode = SPI_MODE_MASTER;
  hspi1.Init.Direction = SPI_DIRECTION_2LINES;
  hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_HIGH;  // W25QXX requires Mode 3 (CPOL=1, CPHA=1)
  hspi1.Init.CLKPhase = SPI_PHASE_2EDGE;       // W25QXX requires Mode 3 (CPOL=1, CPHA=1)
  hspi1.Init.NSS = SPI_NSS_SOFT;
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_8;  // ~9MHz with 72MHz APB2
  hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi1.Init.CRCPolynomial = 10;
  if (HAL_SPI_Init(&hspi1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI1_Init 2 */

  /* USER CODE END SPI1_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();  // 使能GPIOB时钟（I2C1在PB6/PB7）

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_SET);  // Initialize CS high

  /*Configure GPIO pin : PC13 */
  GPIO_InitStruct.Pin = GPIO_PIN_13;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pin : PA4 (W25QXX Chip Select) */
  GPIO_InitStruct.Pin = GPIO_PIN_4;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure I2C1 pins: PB6(SCL) and PB7(SDA) */
  GPIO_InitStruct.Pin = GPIO_PIN_6|GPIO_PIN_7;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_OD;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pins for buttons: PA0(Clear Button) and PA1(Count Button) */
  GPIO_InitStruct.Pin = CLEAR_BUTTON_PIN | COUNT_BUTTON_PIN;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;  // 上拉输入，按下为低电平
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

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
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
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
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
