/* =========================================================================
 * 文件说明: BuleUSCar (蓝牙避障小车) 主程序 (修复版)
 *
 * 修改前问题:
 *   1. 第173行附近: HAL_Init() 被调用了两次
 *      第174行附近: SystemClock_Config() 也被调用了两次
 *      → CubeMX已经自动生成一次, 用户代码又手写了一次
 *      → 重复调用虽然不会直接崩, 但会浪费时钟周期, 并且第二次配置
 *        PLL时会锁死 (HAL检测到PLL已启会返回错误)
 *      修复: 删除重复调用, 只保留CubeMX自动生成的那一次
 *
 *   2. 电机控制函数不对称:
 *      Motor_Left(dir)  → 有前进/后退/停止 (完整)
 *      Motor_Right_Forward() → 只有前进
 *      缺少: Motor_Right_Backward(), Motor_Right_Stop()
 *      → 右转/倒车时右轮动作不对
 *      修复: 补足对称的右轮控制函数 (在主循环里补上完整逻辑)
 *
 *   3. Get_Distance()超时机制:
 *      capture_done 和 timeout 的判断依赖 TIM1中断回调,
 *      而TIM1输入捕获回调里没有状态重置, 可能第一次没收到回波
 *      会导致永远读不到距离
 *
 * 引脚功能:
 *   TIM1_CH1 (PA8) → 超声波HC-SR04的ECHO信号输入捕获
 *   GPIOB → 触发TRIG + 两路直流电机方向+PWM (TB6612/L298N)
 *   USART1 (PA9/PA10) → HC-05蓝牙模块, 115200bps
 * ========================================================================= */

/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main_MODIFIED.c
  * @brief          : Main program body (修复版 - 去除重复初始化)
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
#include "stdio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "Motor.h"
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
TIM_HandleTypeDef htim1;
TIM_HandleTypeDef htim3;

UART_HandleTypeDef huart1;

/* USER CODE BEGIN PV */
/* ===== 超声波测距 输入捕获 变量 ===== */
volatile uint8_t capture_done = 0;     /* 1=已完成一次ECHO高电平测量 */
volatile uint8_t capture_rising = 1;   /* 1=等待上升沿, 0=等待下降沿 */
volatile uint32_t ic_val1 = 0;         /* 上升沿时刻 (TIM1计数值) */
volatile uint32_t ic_val2 = 0;         /* 下降沿时刻 (TIM1计数值) */

/* ===== 蓝牙串口通信 ===== */
volatile uint8_t rx_cmd = 0;           /* 收到的单字节指令 */
volatile uint8_t cmd_received = 0;     /* 1=有新指令待处理 */

/* ===== 电机方向引脚定义 (如果Motor.h里没有暴露, 可直接操作) ===== */
/* 这里假设Motor.h定义了 Motor_Left(int dir), Motor_Right_Forward() 等
 * 为了"右轮控制对称", 我们在USER CODE里补右轮的Stop/Backward函数 */
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_TIM1_Init(void);
static void MX_TIM3_Init(void);
/* USER CODE BEGIN PFP */
float Get_Distance(void);        /* 超声波测距一次, 返回距离(cm), 无回波=999 */
void Motor_Right_Stop(void);     /* 右轮停止 (补充原来缺失的函数) */
void Motor_Right_Backward(void); /* 右轮后退 (补充原来缺失的函数) */
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* =========================================================================
 * HC-SR04触发: 给TRIG引脚发一个 ≥10us 的高电平脉冲
 * 超声波模块收到此脉冲后, 会发8个40kHz声波, 然后把ECHO拉高,
 * 直到收到回波时ECHO变低 → ECHO高电平持续时间就是声波往返时间
 * ========================================================================= */
void HC_SR04_Trigger(void)
{
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, GPIO_PIN_SET);
    /* 延时至少10us: SysTick精度是1ms, 用简单空循环 */
    for (volatile uint32_t i = 0; i < 200; i++) { __NOP(); }
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, GPIO_PIN_RESET);
}

/* =========================================================================
 * 超声波测距一次
 *   返回值: cm (浮点数), 超时或无回波 → 999
 *
 * 计算原理:
 *   声速 = 343 m/s = 0.0343 cm/us
 *   距离 = 声速 × 时间 / 2    (÷2 因为是"往返")
 *   简化公式: 距离(cm) = ECHO高电平时间(us) / 58
 *
 * TIM1设置: 以1MHz频率计数 (PSC=71, 72MHz/72=1MHz),
 *            所以每个计数值 = 1us, 两次捕获差值 = 高电平us数
 * ========================================================================= */
float Get_Distance(void)
{
    /* ---- 重置捕获状态 ---- */
    capture_done = 0;
    capture_rising = 1;           /* 先等上升沿 */
    ic_val1 = 0;
    ic_val2 = 0;
    __HAL_TIM_SET_COUNTER(&htim1, 0);                           /* 清计数器 */
    __HAL_TIM_SET_CAPTUREPOLARITY(&htim1, TIM_CHANNEL_1,
                                  TIM_INPUTCHANNELPOLARITY_RISING);

    /* ---- 启动TIM1输入捕获中断 ----
     * 如果HAL_TIM_IC_Start_IT已经在别处启动过, 这里也可以直接发TRIG
     * 但为了"每次测量保证状态正确", 显式重开一次更稳
     */
    HAL_TIM_IC_Start_IT(&htim1, TIM_CHANNEL_1);
    HC_SR04_Trigger();

    /* ---- 等捕获完成 (非阻塞超时: 10ms, 最多检测 343cm) ---- */
    uint32_t timeout = 10000;   /* 每1us循环一次, 这里用1us的空循环 */
    while (capture_done == 0 && timeout-- > 0) {
        /* 不需要HAL_Delay: 让主循环继续跑, 用空循环做短延时 */
        for (volatile uint32_t i = 0; i < 72; i++) { __NOP(); } /* ~1us */
    }

    if (capture_done == 0) return 999.0f;  /* 超时: 前方无障碍物 */

    uint32_t high_time_us = ic_val2 - ic_val1;
    return ((float)high_time_us) / 58.0f;  /* us/58 = cm */
}

/* =========================================================================
 * 补充右轮控制函数 (原来只有 Motor_Right_Forward, 不对称)
 * 思路: 直接操作GPIO电平, 与左轮保持一致的控制逻辑
 *   右轮AIN1=PB12  AIN2=PB13  PWMB=TIM3_CH2 (假设)
 *   实际引脚要看你自己的硬件, 这里给出"模板式"写法, 可按需修改
 * ========================================================================= */
void Motor_Right_Stop(void)
{
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_13, GPIO_PIN_RESET);
    /* 如果用PWM调速, 这里同时把占空比设为0 */
}

void Motor_Right_Backward(void)
{
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12, GPIO_PIN_SET);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_13, GPIO_PIN_RESET);
    /* 这里配合PWM控制转速, 可在PWM上做占空比调整 */
}

/* =========================================================================
 * 串口接收完成回调: 读到1字节就置标志, 主循环里再处理
 * ========================================================================= */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1) {
        rx_cmd = huart->pRxBuffPtr[0];  /* 单字节: 'F'/'L'/'R'/'S' */
        cmd_received = 1;
        /* 重开下一次接收 (HAL中断接收是"一次性"的) */
        HAL_UART_Receive_IT(&huart1, &rx_cmd, 1);
    }
}

/* =========================================================================
 * TIM1输入捕获回调: 依次记录"ECHO上升沿时刻"和"ECHO下降沿时刻"
 * ========================================================================= */
void HAL_TIM_IC_CaptureCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance != TIM1) return;
    if (htim->Channel  != HAL_TIM_ACTIVE_CHANNEL_1) return;

    if (capture_rising) {
        /* 上升沿 → 记录起始时刻, 切到下降沿捕获 */
        ic_val1 = HAL_TIM_ReadCapturedValue(htim, TIM_CHANNEL_1);
        __HAL_TIM_SET_CAPTUREPOLARITY(htim, TIM_CHANNEL_1,
                                      TIM_INPUTCHANNELPOLARITY_FALLING);
        capture_rising = 0;
    } else {
        /* 下降沿 → 记录结束时刻, 完成一次测量 */
        ic_val2 = HAL_TIM_ReadCapturedValue(htim, TIM_CHANNEL_1);
        capture_done = 1;
        capture_rising = 1;
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
  /* ============================================================
   * 【修复 1】删除重复的 HAL_Init() 和 SystemClock_Config() 调用
   *
   * 原来的代码在 USER CODE BEGIN 2 之前又写了:
   *     HAL_Init();
   *     SystemClock_Config();
   * 这两句和 USER CODE BEGIN 1 之后的 CubeMX 自动生成代码重复!
   *
   * 为什么危险:
   *   - 第二次 HAL_Init() 会重置HAL状态机, 把前面初始化好的
   *     SysTick、优先级分组等全部推翻重来
   *   - 第二次 SystemClock_Config() 会尝试再次使能PLL,
   *     HAL_RCC_OscConfig() 检测到PLL已启用会返回 HAL_ERROR,
   *     虽然程序不会崩, 但时钟树配置就"靠不住"了
   *
   * 修改方案: 只保留 CubeMX 自动生成的那一次,
   *            用户代码区 不再写初始化调用
   * ============================================================ */

  MX_GPIO_Init();
  MX_TIM3_Init();
  MX_USART1_UART_Init();
  MX_TIM1_Init();

  /* USER CODE BEGIN 2 */
  /* 启动蓝牙中断接收: 1字节/次 */
  HAL_UART_Receive_IT(&huart1, &rx_cmd, 1);

  /* 初始状态: 双轮停止 */
  Motor_Stop();
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    static uint32_t last_measure = 0;  /* 上一次测距时间戳 */
    float dist = 999.0f;

    /* ---- 超声波测距: 每100ms一次 (HC-SR04最快只能每秒~20次) ---- */
    if (HAL_GetTick() - last_measure > 100) {
        last_measure = HAL_GetTick();
        dist = Get_Distance();
    }

    /* ---- 避障: 前方距离小于20cm强制停车 ---- */
    if (dist < 20.0f) {
        Motor_Stop();
        cmd_received = 0;   /* 忽略蓝牙指令, 安全优先 */
    }

    /* ---- 处理蓝牙指令 (无障碍时响应) ---- */
    if (cmd_received != 0 && dist >= 20.0f) {
        cmd_received = 0;
        switch (rx_cmd) {
            case 'F':                   /* 前进: 双轮都正转 */
                Motor_Left(1);
                Motor_Right_Forward();
                break;
            case 'B':                   /* 后退: 双轮都反转 (补全原来没有的分支) */
                Motor_Left(-1);
                Motor_Right_Backward();
                break;
            case 'L':                   /* 左转: 左轮停, 右轮转 (原地转) */
                Motor_Left(0);
                Motor_Right_Forward();
                break;
            case 'R':                   /* 右转: 左轮反转, 右轮正转 (差速) */
                Motor_Left(-1);
                Motor_Right_Forward();
                break;
            case 'S':                   /* 停止: 双轮都停 */
                Motor_Stop();
                break;
            default:
                /* 收到未知字符: 忽略但不停, 保持当前动作 */
                break;
        }
    }

    HAL_Delay(10);   /* 主循环节奏控制, 10ms一次, 避免空跑耗电 */
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

/**
  * @brief TIM1 Initialization Function (HC-SR04 ECHO输入捕获)
  *
  * 时基设计:
  *   TIM1挂在APB2上, APB2预分频=1 → TIM1时钟 = SYSCLK = 72MHz
  *   PSC = 71   → 计数频率 = 72MHz / 72 = 1MHz  → 每个计数 = 1us
  *   ARR = 65535 → 最大测量时间 = 65535us ≈ 65ms
  *              → 最大单程距离 = 65ms × 343m/s / 2 ≈ 11m
  *              (HC-SR04实际最大测距只有4m, 绰绰有余)
  *
  * @param None
  * @retval None
  */
static void MX_TIM1_Init(void)
{

  /* USER CODE BEGIN TIM1_Init 0 */

  /* USER CODE END TIM1_Init 0 */

  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_IC_InitTypeDef sConfigIC = {0};

  /* USER CODE BEGIN TIM1_Init 1 */

  /* USER CODE END TIM1_Init 1 */
  htim1.Instance = TIM1;
  htim1.Init.Prescaler = 71;          /* 72MHz / 72 = 1MHz计数 */
  htim1.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim1.Init.Period = 65535;           /* 16位全量程 */
  htim1.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim1.Init.RepetitionCounter = 0;
  htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_IC_Init(&htim1) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim1, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigIC.ICPolarity = TIM_INPUTCHANNELPOLARITY_RISING;
  sConfigIC.ICSelection = TIM_ICSELECTION_DIRECTTI;
  sConfigIC.ICPrescaler = TIM_ICPSC_DIV1;
  sConfigIC.ICFilter = 0;              /* 如抖动大可改为15 */
  if (HAL_TIM_IC_ConfigChannel(&htim1, &sConfigIC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM1_Init 2 */

  /* USER CODE END TIM1_Init 2 */

}

/**
  * @brief TIM3 Initialization Function (电机PWM调速)
  *
  * 时基设计:
  *   TIM3挂在APB1上, APB1预分频=2, 但TIM时钟会×2 → 72MHz
  *   PSC = 71  → 计数频率 = 1MHz
  *   ARR = 999 → PWM频率 = 1MHz / 1000 = 1kHz (直流电机常用)
  *
  * 注意: 原始代码里TIM3做了PWM, 但Motor_Left/Right函数里
  *       可能只是切换GPIO方向, 没调用占空比设置
  *       → 需要在Motor_Left()里加 __HAL_TIM_SET_COMPARE()
  *       来真正调速
  *
  * @param None
  * @retval None
  */
static void MX_TIM3_Init(void)
{

  /* USER CODE BEGIN TIM3_Init 0 */

  /* USER CODE END TIM3_Init 0 */

  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};

  /* USER CODE BEGIN TIM3_Init 1 */

  /* USER CODE END TIM3_Init 1 */
  htim3.Instance = TIM3;
  htim3.Init.Prescaler = 71;
  htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim3.Init.Period = 999;
  htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_PWM_Init(&htim3) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim3, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_PWM_ConfigChannel(&htim3, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_ConfigChannel(&htim3, &sConfigOC, TIM_CHANNEL_2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM3_Init 2 */

  /* USER CODE END TIM3_Init 2 */
  HAL_TIM_MspPostInit(&htim3);

}

/**
  * @brief USART1 Initialization Function (蓝牙HC-05通信)
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
  huart1.Init.BaudRate = 115200;
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
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  *
  * 引脚说明:
  *   PB1  → TRIG (推挽输出, 触发超声波模块)
  *   PB12 → 右轮AIN1  (推挽输出, 电机方向)
  *   PB13 → 右轮AIN2  (推挽输出, 电机方向)
  *   PA8  → ECHO (TIM1_CH1, 复用输入, 在TIM_Init里配置)
  *   左轮方向引脚 (在Motor.c里配置)
  *
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level: 初始低电平 */
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1 | GPIO_PIN_12 | GPIO_PIN_13, GPIO_PIN_RESET);

  /*Configure GPIO pins: PB1 (TRIG) + PB12/PB13 (右轮方向) */
  GPIO_InitStruct.Pin = GPIO_PIN_1 | GPIO_PIN_12 | GPIO_PIN_13;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

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