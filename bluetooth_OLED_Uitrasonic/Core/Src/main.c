/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
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
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "OLED.h"
#include <stdio.h>
#include <string.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define TRIG_PIN GPIO_PIN_0
#define TRIG_PORT GPIOA
#define ECHO_PIN GPIO_PIN_1
#define ECHO_PORT GPIOA

// 电机控制引脚（右电机 BIN1 改用 PB4）
#define AIN1_PIN GPIO_PIN_0   // PB0 - 左前
#define AIN2_PIN GPIO_PIN_1   // PB1 - 左后
#define BIN1_PIN GPIO_PIN_4   // PB4 - 右前 ✅
#define BIN2_PIN GPIO_PIN_3   // PB3 - 右后（受限）
#define MOTOR_PORT GPIOB

// 小车动作
#define STOP        0
#define FORWARD     1
#define BACKWARD    2
#define LEFT        3
#define RIGHT       4
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
I2C_HandleTypeDef hi2c1;
UART_HandleTypeDef huart1;

/* USER CODE BEGIN PV */
volatile uint8_t bluetooth_cmd = 's'; // 默认停止
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_I2C1_Init(void);
static void MX_USART1_UART_Init(void);
/* USER CODE BEGIN PFP */

#include "core_cm3.h"

void delay_us(uint32_t us) {
    static int dwt_init = 0;
    if (!dwt_init) {
        CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
        DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
        dwt_init = 1;
    }
    uint32_t start = DWT->CYCCNT;
    uint32_t cycles = us * (SystemCoreClock / 1000000);
    while ((DWT->CYCCNT - start) < cycles);
}

float get_distance_cm(void) {
    HAL_GPIO_WritePin(TRIG_PORT, TRIG_PIN, GPIO_PIN_SET);
    delay_us(10);
    HAL_GPIO_WritePin(TRIG_PORT, TRIG_PIN, GPIO_PIN_RESET);

    uint32_t timeout = 5000;
    while (HAL_GPIO_ReadPin(ECHO_PORT, ECHO_PIN) == GPIO_PIN_RESET) {
        if (--timeout == 0) return -1.0f;
        delay_us(1);
    }

    uint32_t start = DWT->CYCCNT;
    timeout = 30000;
    while (HAL_GPIO_ReadPin(ECHO_PORT, ECHO_PIN) == GPIO_PIN_SET) {
        if (--timeout == 0) return -1.0f;
        delay_us(1);
    }

    float time_us = (float)(DWT->CYCCNT - start) / (SystemCoreClock / 1000000.0f);
    return time_us * 0.017f;
}

void Car_Move(uint8_t direction)
{
    switch(direction) {
        case FORWARD:
            HAL_GPIO_WritePin(MOTOR_PORT, AIN1_PIN, GPIO_PIN_SET);
            HAL_GPIO_WritePin(MOTOR_PORT, AIN2_PIN, GPIO_PIN_RESET);
            HAL_GPIO_WritePin(MOTOR_PORT, BIN1_PIN, GPIO_PIN_SET);
            HAL_GPIO_WritePin(MOTOR_PORT, BIN2_PIN, GPIO_PIN_RESET);
            break;

        case BACKWARD:
            // 右电机不能后退 → 仅左轮后退，右轮刹车
            HAL_GPIO_WritePin(MOTOR_PORT, AIN1_PIN, GPIO_PIN_RESET);
            HAL_GPIO_WritePin(MOTOR_PORT, AIN2_PIN, GPIO_PIN_SET);
            HAL_GPIO_WritePin(MOTOR_PORT, BIN1_PIN, GPIO_PIN_RESET);
            HAL_GPIO_WritePin(MOTOR_PORT, BIN2_PIN, GPIO_PIN_RESET);
            break;

        case LEFT:
            // 左轮后退，右轮前进
            HAL_GPIO_WritePin(MOTOR_PORT, AIN1_PIN, GPIO_PIN_RESET);
            HAL_GPIO_WritePin(MOTOR_PORT, AIN2_PIN, GPIO_PIN_SET);
            HAL_GPIO_WritePin(MOTOR_PORT, BIN1_PIN, GPIO_PIN_SET);
            HAL_GPIO_WritePin(MOTOR_PORT, BIN2_PIN, GPIO_PIN_RESET);
            break;

        case RIGHT:
            // 左轮前进，右轮刹车（因不能后退）
            HAL_GPIO_WritePin(MOTOR_PORT, AIN1_PIN, GPIO_PIN_SET);
            HAL_GPIO_WritePin(MOTOR_PORT, AIN2_PIN, GPIO_PIN_RESET);
            HAL_GPIO_WritePin(MOTOR_PORT, BIN1_PIN, GPIO_PIN_RESET);
            HAL_GPIO_WritePin(MOTOR_PORT, BIN2_PIN, GPIO_PIN_RESET);
            break;

        case STOP:
        default:
            HAL_GPIO_WritePin(MOTOR_PORT, AIN1_PIN, GPIO_PIN_RESET);
            HAL_GPIO_WritePin(MOTOR_PORT, AIN2_PIN, GPIO_PIN_RESET);
            HAL_GPIO_WritePin(MOTOR_PORT, BIN1_PIN, GPIO_PIN_RESET);
            HAL_GPIO_WritePin(MOTOR_PORT, BIN2_PIN, GPIO_PIN_RESET);
            break;
    }
}
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
uint8_t rx_byte;

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1) {
        bluetooth_cmd = rx_byte;
        HAL_UART_Receive_IT(&huart1, &rx_byte, 1);
    }
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* USER CODE BEGIN 1 */
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
  /* USER CODE END 1 */

  HAL_Init();
  SystemClock_Config();

  /* USER CODE BEGIN Init */
  /* USER CODE END Init */

  MX_GPIO_Init();
  MX_I2C1_Init();
  MX_USART1_UART_Init();

  /* USER CODE BEGIN 2 */
  OLED_Init();
  OLED_Clear();
  OLED_ShowString(1, 1, "Ready");
  OLED_Refresh_Gram();

  rx_byte = 0;
  HAL_UART_Receive_IT(&huart1, &rx_byte, 1);
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
      float dist = get_distance_cm();
      char buffer[20];

      if (dist > 0 && dist < 400) {
          sprintf(buffer, "Dist:%.1fcm", dist);
      } else {
          strcpy(buffer, "Error");
          dist = 500; // 视为无障碍
      }

      OLED_Clear();
      OLED_ShowString(1, 1, buffer);
      OLED_Refresh_Gram();

      // 自动避障：距离 < 20cm 强制停车（最高优先级）
      if (dist < 20.0f) {
          Car_Move(STOP);
      } else {
          switch(bluetooth_cmd) {
              case 'f': Car_Move(FORWARD); break;
              case 'b': Car_Move(BACKWARD); break;
              case 'l': Car_Move(LEFT); break;
              case 'r': Car_Move(RIGHT); break;
              case 's':
              default: Car_Move(STOP); break;
          }
      }

      HAL_Delay(100);
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
  hi2c1.Instance = I2C1;
  hi2c1.Init.ClockSpeed = 400000;
  hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_16_9;
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
}

/**
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 9600;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  // Trig: output
  GPIO_InitStruct.Pin = TRIG_PIN;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(TRIG_PORT, &GPIO_InitStruct);

  // Echo: input
  GPIO_InitStruct.Pin = ECHO_PIN;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(ECHO_PORT, &GPIO_InitStruct);

  // 电机控制：PB0, PB1, PB3, PB4
  GPIO_InitStruct.Pin = AIN1_PIN | AIN2_PIN | BIN1_PIN | BIN2_PIN;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(MOTOR_PORT, &GPIO_InitStruct);

  // 初始状态：全部刹车
  HAL_GPIO_WritePin(MOTOR_PORT, AIN1_PIN | AIN2_PIN | BIN1_PIN | BIN2_PIN, GPIO_PIN_RESET);
}

/* USER CODE BEGIN 4 */
/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  __disable_irq();
  while (1)
  {
  }
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
}
#endif /* USE_FULL_ASSERT */
