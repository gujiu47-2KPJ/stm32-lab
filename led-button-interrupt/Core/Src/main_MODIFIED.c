/* =========================================================================
 * 项目: LED1  (HAL库, PA0按键中断切换LED)
 * 原文件: Core/Src/main.c
 * 修复版: Core/Src/main_MODIFIED.c
 *
 * Bug #1: PA0 没有被配置为 EXTI 中断输入 (关键问题)
 *   原始: MX_GPIO_Init() 只配置了 LED_Pin 为输出, 完全没配置 PA0
 *   后果: HAL_GPIO_EXTI_Callback() 永远不会被调用, button_flag永远是0
 *
 * 修复: 在 MX_GPIO_Init 中加入 PA0 的中断配置:
 *   GPIO_InitStruct.Pin = GPIO_PIN_0;
 *   GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
 *   GPIO_InitStruct.Pull = GPIO_PULLUP;
 *   HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
 *   HAL_NVIC_SetPriority(EXTI0_IRQn, 0, 0);
 *   HAL_NVIC_EnableIRQ(EXTI0_IRQn);
 *
 * Bug #2: 主循环里"LED_Toggle + HAL_Delay(500)"和"按键切换"功能冲突
 *   原始: while(1) 里同时做两件事:
 *         (a) 每 500ms 无条件翻转LED (b) button_flag 时翻转LED
 *   后果: 用户会看到 LED 一直乱闪, 按键体验很差
 *
 * 修复: 移除主循环里的无条件翻转, 只做按键驱动, 逻辑更清晰
 *
 * 原理: STM32 EXTI 外部中断需要三步:
 *       1. GPIO 配置为输入+上拉/下拉
 *       2. GPIO 配置为中断模式(上升沿/下降沿/双边沿)
 *       3. NVIC 使能对应的 EXTI 通道
 * ========================================================================= */

/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main_MODIFIED.c
  * @brief          : Main program body (修复版)
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
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
volatile uint8_t button_flag = 0;      /* 按键按下标志, 在中断里置1       */
static uint8_t led_state = 0;          /* 当前LED状态: 0=PA9亮, 1=PA10亮   */
static uint32_t last_debounce_tick = 0;/* 按键消抖时间戳                   */
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/**
 * @brief  EXTI中断回调函数: PA0 按键下降沿触发
 *
 * 注意: 中断函数里不能 HAL_Delay(), 所以用时间戳方式消抖
 *       真正的消抖和LED翻转放在主循环里做, 中断只置 flag
 */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == GPIO_PIN_0)
    {
        /* 50ms 消抖: 两次触发间隔太短就忽略 (机械按键抖动) */
        if ((HAL_GetTick() - last_debounce_tick) > 50)
        {
            button_flag = 1;             /* 通知主循环处理按键事件         */
            last_debounce_tick = HAL_GetTick();
        }
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

  /* USER CODE BEGIN 2 */
  /* 初始状态: PA9 亮, PA10 灭
   * 假设 LED 是共阴接法: 高电平点亮 */
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_9,  GPIO_PIN_SET);   /* PA9 = 亮 */
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_10, GPIO_PIN_RESET); /* PA10 = 灭 */
  led_state = 0;
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    /* ================== 修复后的核心逻辑 ==================
     * 原代码在这里有 HAL_GPIO_TogglePin + HAL_Delay(500),
     * 导致 LED 一直乱闪, 按键体验很差.
     * 现在: 只响应按键事件, 平时不做任何事 */

    if (button_flag)
    {
        button_flag = 0;    /* 先清标志, 防止中断重入导致重复处理 */
        led_state = !led_state;

        if (led_state == 0)
        {
            /* 模式 0: PA9 亮, PA10 灭 */
            HAL_GPIO_WritePin(GPIOA, GPIO_PIN_9,  GPIO_PIN_SET);
            HAL_GPIO_WritePin(GPIOA, GPIO_PIN_10, GPIO_PIN_RESET);
        }
        else
        {
            /* 模式 1: PA9 灭, PA10 亮 */
            HAL_GPIO_WritePin(GPIOA, GPIO_PIN_9,  GPIO_PIN_RESET);
            HAL_GPIO_WritePin(GPIOA, GPIO_PIN_10, GPIO_PIN_SET);
        }

        /* PC13 蓝灯翻转一下, 作为"按键已响应"的视觉反馈 */
        HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
    }
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
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
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

  /* USER CODE BEGIN MX_GPIO_Init_1 */
  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();

  /* ---------- 1. LED 输出: PA9, PA10 (推挽输出) ---------- */
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_9 | GPIO_PIN_10, GPIO_PIN_RESET);
  GPIO_InitStruct.Pin = GPIO_PIN_9 | GPIO_PIN_10;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_MEDIUM;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* ---------- 2. PC13 蓝灯 (开漏输出) ---------- */
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
  GPIO_InitStruct.Pin = GPIO_PIN_13;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /* ==================== 关键修复 ====================
   * Bug: 原代码完全没有配置 PA0!
   * 虽然写了 HAL_GPIO_EXTI_Callback(GPIO_PIN_0),
   * 但是 GPIO 硬件根本没被配置成中断模式,
   * NVIC 也没使能, 所以中断永远不会触发.
   * ================================================ */

  /* ---------- 3. PA0 按键: 上拉输入 + 下降沿中断 ---------- */
  GPIO_InitStruct.Pin = GPIO_PIN_0;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;  /* 下降沿触发中断     */
  GPIO_InitStruct.Pull = GPIO_PULLUP;           /* 内部上拉, 未按下时是高电平 */
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* ---------- 4. NVIC: 使能 EXTI0 中断 ---------- */
  HAL_NVIC_SetPriority(EXTI0_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI0_IRQn);

  /* USER CODE BEGIN MX_GPIO_Init_2 */
  /* USER CODE END MX_GPIO_Init_2 */
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