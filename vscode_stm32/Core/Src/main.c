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
#include "stm32f103xb.h"
#include "stm32f1xx_hal.h"
#include "stm32f1xx_hal_gpio.h"
#include <stdint.h>
#include <sys/types.h>

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
typedef enum{
   mode_idle = 0,
   mode_shark,
   mode_load,
   mode_running
} system_mode_t;
volatile system_mode_t current_mode = mode_idle;
volatile uint8_t key_press = 0;
volatile uint8_t system_state = 0;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
/* USER CODE BEGIN PFP */

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

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while(1){
      // 闁挎瑨顕?閿涙witch鐠囶厼褰炴惔鏃囶嚉閸︺劍鐦″▎鈥虫儕閻滎垶鍏橀幍褑顢戦敍宀冣偓灞肩瑝閺勵垰褰ч崷鈺y_press != 0閺冭埖澧界悰?
      // 鏉╂瑦鐗辨导姘嚤閼峰瓨瀵滈柨顔碱槱閻炲棗鐣幋鎰倵閿涘ED濡€崇础閸掑洦宕查柅鏄忕帆鐞氼偉鐑︽潻?
      // 娣囶喗鏁奸敍姘殺switch鐠囶厼褰炵粔璇插煂if婢舵牠娼伴敍宀€鈥樻穱婵囩槨濞嗏€虫儕閻滎垶鍏橀幍褑顢?
      
      if(key_press !=0){
          uint8_t key = key_press;
          
          HAL_Delay(20);
          
          GPIO_PinState level = GPIO_PIN_SET;
          
          if (key ==1) {
              level = HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0);
          }
          else if (key == 2)
          {
              level = HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_1);
          }
          else if (key == 3)
          {
              level = HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_2);
          }
          
          if (level == GPIO_PIN_RESET){
              switch (key) {
                  case 1 : 
                      current_mode = mode_running;
                      break;
                  case 2:
                      current_mode = mode_load;
                      break;
                  case 3:
                      current_mode = mode_shark;
                      break;
              }
          }
          system_state = 0;
          key_press = 0;
      }
      
      // LED 模式显示
      switch (current_mode)
          {
              case mode_idle:
                  HAL_GPIO_TogglePin(GPIOC,   GPIO_PIN_13);
                  HAL_Delay(500);
                  break;

              case mode_running:  // 流水灯：逐个亮灭，慢速可见
                  HAL_GPIO_WritePin(GPIOA,  GPIO_PIN_8,  SET);
                  HAL_Delay(200);
                  HAL_GPIO_WritePin(GPIOA,   GPIO_PIN_8,  RESET);

                  HAL_GPIO_WritePin(GPIOA,   GPIO_PIN_9,  SET);
                  HAL_Delay(200);
                  HAL_GPIO_WritePin(GPIOA,   GPIO_PIN_9,  RESET);

                  HAL_GPIO_WritePin(GPIOA,   GPIO_PIN_10,  SET);
                  HAL_Delay(200);
                  HAL_GPIO_WritePin(GPIOA,   GPIO_PIN_10, RESET);

                  HAL_Delay(300);  // 间隔，让肉眼看清一轮结束
                  break;

              case mode_load:  // 两端 vs 中间：PA8+PA10 ↔ PA9
                  HAL_GPIO_WritePin(GPIOA,   GPIO_PIN_8|GPIO_PIN_10,  SET);
                  HAL_GPIO_WritePin(GPIOA,   GPIO_PIN_9,  RESET);
                  HAL_Delay(300);

                  HAL_GPIO_WritePin(GPIOA,   GPIO_PIN_8|GPIO_PIN_10,  RESET);
                  HAL_GPIO_WritePin(GPIOA,   GPIO_PIN_9,  SET);
                  HAL_Delay(300);
                  break;

              case mode_shark:  // 三个一起闪
                  HAL_GPIO_WritePin(GPIOA,   GPIO_PIN_8|GPIO_PIN_9|GPIO_PIN_10,
                                     SET);
                  HAL_Delay(250);
                  HAL_GPIO_WritePin(GPIOA,   GPIO_PIN_8|GPIO_PIN_9|GPIO_PIN_10,
                                     RESET);
                  HAL_Delay(250);
                  break;
          }
  }
  /* USER CODE END WHILE */

  /* USER CODE BEGIN 3 */
}
  /* USER CODE END 3 */

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

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_8|GPIO_PIN_9|GPIO_PIN_10, GPIO_PIN_RESET);

  /*Configure GPIO pin : PC13 */
  GPIO_InitStruct.Pin = GPIO_PIN_13;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pins : PA0 PA1 PA2 */
  GPIO_InitStruct.Pin = GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_2;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : PA8 PA9 PA10 */
  GPIO_InitStruct.Pin = GPIO_PIN_8|GPIO_PIN_9|GPIO_PIN_10;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* EXTI interrupt init*/
  HAL_NVIC_SetPriority(EXTI0_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI0_IRQn);

  HAL_NVIC_SetPriority(EXTI1_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI1_IRQn);

  HAL_NVIC_SetPriority(EXTI2_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI2_IRQn);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_PIN){ 
   if (system_state == 0) {
    if (GPIO_PIN == GPIO_PIN_0) {
       key_press = 1;
       system_state = 1;
    }
    else if (GPIO_PIN == GPIO_PIN_1)
    {
        key_press = 2;
        system_state = 1;
    }
    else if (GPIO_PIN == GPIO_PIN_2)
    {
        key_press = 3;
        system_state = 1;
    }
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
  /* User can add his own implementation to report the HAL error return state */
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
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */

