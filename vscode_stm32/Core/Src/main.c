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
#include "stm32f103xb.h"
#include "stm32f1xx_hal.h"
#include "stm32f1xx_hal_def.h"
#include "stm32f1xx_hal_gpio.h"
#include "stm32f1xx_hal_uart.h"
#include "stm32f1xx_hal_tim.h"
#include "stm32f1xx_hal_tim_ex.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/_intsup.h>
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
TIM_HandleTypeDef htim2;          // 定时器2句柄

UART_HandleTypeDef huart1;        // 串口1句柄

/* USER CODE BEGIN PV */
// 系统工作模式枚举定义
// 逻辑优化: 枚举值 = 按键编号 = 串口命令字符,实现一一对应
typedef enum{
   mode_idle = 0,      // 0 = 空闲模式(字符'0')
   mode_running = 1,   // 1 = 运行模式/流水灯(按键1/字符'1')
   mode_load = 2,      // 2 = 负载模式/三灯全灭(按键2/字符'2')
   mode_shark = 3      // 3 = 鲨鱼模式/两端vs中间(按键3/字符'3')
} system_mode_t;
volatile system_mode_t current_mode = mode_idle;  // 当前工作模式,默认空闲
volatile uint8_t key_press = 0;                   // 按键按下标志,0=无按键,1/2/3=按键编号
volatile uint8_t system_state = 0;                // 系统状态,0=空闲等待按键,1=正在处理按键

volatile uint8_t receive_status = 0;            // 串口接收标志位(中断回调中修改,主循环中读取,需volatile防优化)

// 逻辑优化: mode_name数组索引必须与枚举值严格对应
// mode_name[0]="IDLE"     → mode_idle(0)
// mode_name[1]="running"  → mode_running(1)
// mode_name[2]="load"     → mode_load(2)
// mode_name[3]="shark"    → mode_shark(3)
const char* mode_name[]= {"IDLE","running","load","shark"};  // 模式名称字符串数组,用于串口输出
uint8_t rx_buffer[1];                                         // 串口接收缓冲区(单字节)
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_TIM2_Init(void);
static void MX_USART1_UART_Init(void);
/* USER CODE BEGIN PFP */
void Serial_ProcessChar(uint8_t ch);  // 串口命令处理函数声明
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
  MX_TIM2_Init();
  
  MX_USART1_UART_Init();
  /* USER CODE BEGIN 2 */
    HAL_TIM_Base_Start_IT(&htim2);                      // 启动定时器2中断,定时周期触发回调
    HAL_UART_Receive_IT(&huart1, rx_buffer, 1);         // 启动串口1中断接收,每次接收1字节
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while(1){
      // 检测按键按下，如果key_press不为0，则进行消抖处理
      // 读取按键电平，如果为低电平，则根据按键值切换LED模式
      // 使用switch语句判断按键值，并设置对应的LED模式
      
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

              case mode_shark:  // 两端 vs 中间：PA8+PA10 ↔ PA9
                  HAL_GPIO_WritePin(GPIOA,   GPIO_PIN_8|GPIO_PIN_10,  SET);
                  HAL_GPIO_WritePin(GPIOA,   GPIO_PIN_9,  RESET);
                  HAL_Delay(300);

                  HAL_GPIO_WritePin(GPIOA,   GPIO_PIN_8|GPIO_PIN_10,  RESET);
                  HAL_GPIO_WritePin(GPIOA,   GPIO_PIN_9,  SET);
                  HAL_Delay(300);
                  break;

              case mode_load:  // 逻辑优化: 三个LED全灭(负载模式-待机状态)
                  
                  HAL_GPIO_WritePin(GPIOA,   GPIO_PIN_8|GPIO_PIN_9|GPIO_PIN_10,
                                     RESET);
                  
                  break;
          }
         if(receive_status != 0){
      Serial_ProcessChar(rx_buffer[0]);
          receive_status = 0;
          }         
    /* USER CODE END WHILE */
    /* USER CODE BEGIN 3 */
  /* USER CODE END 3 */
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
  * @brief TIM2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM2_Init(void)
{

  /* USER CODE BEGIN TIM2_Init 0 */

  /* USER CODE END TIM2_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM2_Init 1 */

  /* USER CODE END TIM2_Init 1 */
  htim2.Instance = TIM2;
  htim2.Init.Prescaler = 7199;                              // 预分频: 72MHz/(7199+1) = 10KHz
  htim2.Init.CounterMode = TIM_COUNTERMODE_UP;              // 向上计数模式
  htim2.Init.Period = 999;                                  // 自动重装载值: 10KHz/(999+1) = 10Hz,即100ms周期
  htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim2) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim2, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim2, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM2_Init 2 */

  /* USER CODE END TIM2_Init 2 */

}

/**
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;                            // 波特率: 115200bps
  huart1.Init.WordLength = UART_WORDLENGTH_8B;              // 数据位: 8位
  huart1.Init.StopBits = UART_STOPBITS_1;                   // 停止位: 1位
  huart1.Init.Parity = UART_PARITY_NONE;                    // 校验位: 无
  huart1.Init.Mode = UART_MODE_TX_RX;                       // 模式: 收发双工
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;              // 硬件流控: 无
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;          // 过采样: 16倍
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

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
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);       // PC13初始电平拉高(LED默认熄灭)

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_8|GPIO_PIN_9|GPIO_PIN_10, GPIO_PIN_RESET);  // PA8/PA9/PA10初始拉低

  /*Configure GPIO pin : PC13 (板载LED) */
  GPIO_InitStruct.Pin = GPIO_PIN_13;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pins : PA0 PA1 PA2 (按键输入,下降沿中断) */
  GPIO_InitStruct.Pin = GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_2;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : PA8 PA9 PA10 (LED输出) */
  GPIO_InitStruct.Pin = GPIO_PIN_8|GPIO_PIN_9|GPIO_PIN_10;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* EXTI interrupt init (按键外部中断配置) */
  HAL_NVIC_SetPriority(EXTI0_IRQn, 0, 0);                  // PA0外部中断,优先级0-0
  HAL_NVIC_EnableIRQ(EXTI0_IRQn);

  HAL_NVIC_SetPriority(EXTI1_IRQn, 0, 0);                  // PA1外部中断,优先级0-0
  HAL_NVIC_EnableIRQ(EXTI1_IRQn);

  HAL_NVIC_SetPriority(EXTI2_IRQn, 0, 0);                  // PA2外部中断,优先级0-0
  HAL_NVIC_EnableIRQ(EXTI2_IRQn);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
void Serial_ProcessChar(uint8_t ch){
  // 逻辑优化: 主循环已判断 receive_status != 0 才调用此函数,函数内无需重复判断
  switch(ch){
    case '1':
      current_mode = mode_running;
      break;
    case '2':
      current_mode = mode_load;
      break;
    case '3':
      current_mode = mode_shark;
      break;
    case '0':
      current_mode = mode_idle;
      break;
    default:
      return;  // 逻辑优化: 非法命令直接返回,不发送确认消息
  }
  // 逻辑优化: 模式切换成功后,发送确认消息回电脑
  char send_buf[50];
  sprintf(send_buf, "Mode changed to: %s\r\n", mode_name[current_mode]);
  HAL_UART_Transmit(&huart1, (uint8_t*)send_buf, strlen(send_buf), HAL_MAX_DELAY);
}
/**
  * @brief  定时器周期中断回调函数
  * @param  htim: 定时器句柄指针
  * @retval None
  * @note   当定时器2计数溢出到达周期值时自动调用
  *         定时通过串口上报当前系统工作模式
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  if (htim->Instance==TIM2) {
    char send_buf[50];
    // 格式化当前模式名称,准备发送
    sprintf(send_buf, "current_mode = %s\r\n",mode_name[current_mode]);
    // 通过串口1阻塞发送模式信息
    HAL_UART_Transmit(&huart1,(uint8_t*)send_buf,strlen(send_buf),HAL_MAX_DELAY);
  
  }
}

/**
  * @brief  串口接收完成中断回调函数
  * @param  huart: 串口句柄指针
  * @retval None
  * @note   当串口接收指定数据量后自动调用,接收完成后重新开启中断接收
  *         实现串口数据的持续接收
  */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart){
  if (huart->Instance == USART1) {
    // 重新开启串口中断接收,实现持续接收(每次接收1字节)
    HAL_UART_Receive_IT(&huart1,rx_buffer, 1);
    receive_status = 1;
  }
}

/**
  * @brief  外部中断(EXTI)回调函数
  * @param  GPIO_PIN: 触发中断的GPIO引脚编号
  * @retval None
  * @note   当PA0/PA1/PA2按键按下时触发下降沿中断
  *         在空闲状态下记录按键编号并进入处理状态
  */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_PIN){ 
   if (system_state == 0) {                               // 仅在空闲状态响应按键
    if (GPIO_PIN == GPIO_PIN_0) {                         // 按键1按下(PA0)
       key_press = 1;
       system_state = 1;                                   // 进入处理状态,防止重复触发
    }
    else if (GPIO_PIN == GPIO_PIN_1)                      // 按键2按下(PA1)
    {
        key_press = 2;
        system_state = 1;
    }
    else if (GPIO_PIN == GPIO_PIN_2)                      // 按键3按下(PA2)
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