/* =========================================================================
 * 文件说明: vscode_stm32 主程序 (修复版)
 *
 * 修改前问题:
 *   1. 第346行: "key_press = 0;帮我检查当前代码..."
 *      → 中文文本直接混入C代码, 编译器完全无法解析, 直接报错
 *      → 属于"编辑失误", 应该把中文内容单独放到注释中
 *
 *   2. 原始main.c的 while(1) 逻辑是通的, 不需要大改
 *
 * 修改内容:
 *   - 删除混入代码的中文, 恢复正常C语法
 *   - 给关键变量/状态转换加中文注释, 方便理解
 *
 * 引脚功能:
 *   PA8/PA9/PA10 → TIM1_CH1/2/3 PWM输出 (LED灯珠)
 *   PA15 → TIM2_CH1 (TIM2做定时中断, 定时上报状态)
 *   PA6/PA7 → TIM3编码器接口 (旋钮输入)
 *   PB5 → EXTI中断 (编码器按键, 切换模式)
 *   PA0/PA1/PA2 → EXTI中断 (板载按键, 直接切LED模式)
 *   PB6/PB7 → I2C1 (OLED显示)
 *   PA9/PA10 → USART1 (串口通信)
 * ========================================================================= */

/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main_MODIFIED.c
  * @brief          : Main program body (修复版)
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
#include "stm32f1xx_hal_i2c.h"
#include "stm32f1xx_hal_uart.h"
#include "stm32f1xx_hal_tim.h"
#include "stm32f1xx_hal_tim_ex.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/_intsup.h>
#include "OLED.h"
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
I2C_HandleTypeDef hi2c1;

TIM_HandleTypeDef htim1;
TIM_HandleTypeDef htim2;
TIM_HandleTypeDef htim3;

UART_HandleTypeDef huart1;

/* USER CODE BEGIN PV */
/* ===== 串口数字输入用 ===== */
uint8_t num_buf[10];    // 串口收到的数字字符缓存
uint8_t num_idx = 0;    // 缓冲区当前写入位置

/* ===== LED模式枚举: 控制3路灯珠的动画 ===== */
typedef enum {
    mode_idle = 0,       // 全灭
    mode_running = 1,    // 流水灯 (PA8→PA9→PA10依次亮灭)
    mode_load = 2,       // 全亮
    mode_shark = 3       // 两端-中间闪烁 (PA8+PA10亮 ↔ PA9亮)
} system_mode_t;

/* ===== 编码器模式枚举: 控制旋钮做什么 ===== */
typedef enum {
    led_default_mode = 0, // 旋钮不响应 (防止误触)
    led_brights_mode,     // 旋钮调节亮度
    led_turn_mode         // 旋钮切换LED动画模式
} turn_mode_t;

/* ===== 编码器 / PWM 控制变量 ===== */
volatile turn_mode_t led_mode = led_default_mode;  // 当前编码器模式
volatile uint32_t t3_last_cnt = 0;                // 上次读到的TIM3计数值
volatile uint8_t mode_press_flag = 0;              // 编码器按键按下标志
volatile uint8_t led_bright = 50;                 // 当前亮度 (0~99)
volatile uint8_t encoder_flag = 0;                 // 编码器动作标志 (用于LED闪烁反馈)

/* ===== 当前LED动画模式 ===== */
volatile system_mode_t current_mode = mode_idle;
system_mode_t last_mode = (system_mode_t)99;       /* 初始设成"不等于任何模式",
                                                      让OLED首次刷新时强制重绘 */

/* ===== 按键中断变量 ===== */
volatile uint8_t key_press = 0;      // 按键编号 1/2/3 (对应PA0/PA1/PA2)
volatile uint8_t system_state = 0;   // 防抖标志: 0=空闲可触发, 1=正在处理

/* ===== 串口通信变量 ===== */
volatile uint8_t receive_status = 0;           // 收到一个字符标志
const char *mode_name[] = {"IDLE", "RUNNING", "LOAD", "SHARK"};
uint8_t rx_buffer[1];                          // 单字节接收缓冲
uint32_t last_uart_tick = 0;                   // 定时上报的时间戳

/* ===== OLED按需刷新 =====
 * 原理: OLED通过I2C刷新整屏需要约10~20ms,
 *       如果每次主循环都刷新, 会拖慢按键和编码器响应速度
 *       → 只在"值确实变了"时才调用OLED刷新
 */
uint8_t last_bright = 0;                 // 上次OLED显示的亮度
turn_mode_t last_led_mode = (turn_mode_t)99; /* 上次OLED显示的编码器模式 */

/* ===== 板载LED非阻塞闪烁 =====
 * 原理: 用系统时间戳代替HAL_Delay, 主循环不被阻塞
 */
volatile uint16_t bright_speed = 500;    // 闪烁间隔 (ms)
uint32_t last_led_tick = 0;              // 上次翻转PC13的时间戳
uint32_t blink_timer = 0;                // 临时变量: 用于延时恢复
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_I2C1_Init(void);
static void MX_TIM1_Init(void);
static void MX_TIM3_Init(void);
static void MX_TIM2_Init(void);
/* USER CODE BEGIN PFP */
void Serial_ProcessChar(uint8_t ch);      // 串口单字符命令处理
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
  MX_USART1_UART_Init();
  MX_I2C1_Init();
  MX_TIM1_Init();
  MX_TIM3_Init();
  MX_TIM2_Init();

  /* USER CODE BEGIN 2 */
  /* =======================================================================
   * 关键修复: 强制把EXTI线5映射到GPIOB
   *
   * 问题描述:
   *   STM32F1的EXTI线是"复用"的:
   *     EXTI0 → 可以是PA0/PB0/PC0... 但同一时间只能选一个
   *     EXTI1 → 可以是PA1/PB1/PC1...
   *     ...
   *     EXTI5 → 可以是PA5/PB5/PC5...
   *
   *   默认映射是GPIOA (000). 本项目里:
   *     PA0/PA1/PA2 已被 EXTI0/EXTI1/EXTI2 占用 (板载按键)
   *     PB5 被用作 EXTI5 (编码器按键)
   *
   *   如果不显式写 AFIO->EXTICR[1], EXTI5 会连到PA5, 而不是PB5!
   *   → 按下PB5永远触发不了中断, 这是很多新人容易忽略的点
   *
   * 寄存器详解:
   *   AFIO_EXTICR2 = bit[15:0], 控制 EXTI7~EXTI4
   *   其中 bit[10:8] 控制 EXTI5:
   *     000 = GPIOA  (默认)
   *     001 = GPIOB  ← 我们需要 ↓↓↓
   *     010 = GPIOC
   *     ...
   * ======================================================================= */
  __HAL_RCC_AFIO_CLK_ENABLE();             /* 先开AFIO时钟 (CubeMX可能漏了) */
  AFIO->EXTICR[1] &= ~(0x7 << 8);           /* 清bit[10:8] */
  AFIO->EXTICR[1] |=  (0x1 << 8);           /* 设为001 = 映射到GPIOB */

  /* OLED初始化: 清屏 → 显示启动信息 */
  OLED_Init();
  OLED_ShowString(1, 1, "LED CONTROL");

  /* 启动TIM2定时中断 (每2秒触发一次, 上报当前状态给串口) */
  HAL_TIM_Base_Start_IT(&htim2);

  /* 启动TIM3硬件编码器模式:
   *   - PA6 接编码器 A 相 (TIM3_CH1)
   *   - PA7 接编码器 B 相 (TIM3_CH2)
   *   - TIM3内部计数器 TIM3->CNT 会随旋钮转动自动增减,
   *     无需软件判方向
   */
  HAL_TIM_Encoder_Start(&htim3, TIM_CHANNEL_ALL);

  /* 启动TIM1 PWM: 3路PWM分别驱动3颗LED灯珠 */
  HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);    // PA8
  HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_2);    // PA9
  HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_3);    // PA10

  /* 设置初始占空比 = led_bright(50) */
  __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, led_bright);
  __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_2, led_bright);
  __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_3, led_bright);

  /* 开启串口中断接收 (每次只收1字节, 收完在回调里重开) */
  HAL_UART_Receive_IT(&huart1, rx_buffer, 1);

  /* 记录编码器初始值 (后续用"当前值 vs 初始值"的差来判断旋转) */
  t3_last_cnt = TIM3->CNT;
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* ---- 读取当前状态 ---- */
    uint32_t now_cnt = TIM3->CNT;            // TIM3硬件计数器值 (旋钮位置)
    uint32_t now = HAL_GetTick();            // SysTick时间戳 (用于非阻塞延时)

    /* ---- 板载LED(PC13) 非阻塞闪烁 ----
     * 原理: 用时间戳差值代替HAL_Delay(), 即使后面有耗时操作也能及时响应 */
    if (now - last_led_tick >= bright_speed) {
        HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
        last_led_tick = now;
    }

    /* 编码器操作后的"快闪反馈"结束后, 恢复默认500ms慢闪 */
    if (bright_speed == 300 && (now - blink_timer) > 2000) {
        bright_speed = 500;
    }

    /* ---- 编码器处理: 根据 led_mode 执行不同操作 ----
     * 硬件原理: TIM3_ENCODER模式下, A/B两相的相位差决定CNT增减
     *           → 顺时针时CNT++, 逆时针时CNT-- (取决于接线)
     */
    switch (led_mode) {
        case led_default_mode:
            /* 不响应旋转 (防止误触) */
            break;

        case led_brights_mode:
            /* 旋转 → 调亮度 (步进5, 范围 0~99) */
            if (now_cnt > t3_last_cnt) {
                if (led_bright < 99) led_bright += 5;
                encoder_flag = 1;
            } else if (now_cnt < t3_last_cnt) {
                if (led_bright > 0)  led_bright -= 5;
                encoder_flag = 1;
            }
            /* 实时把新亮度写入TIM1的3个比较寄存器 */
            __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, led_bright);
            __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_2, led_bright);
            __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_3, led_bright);
            break;

        case led_turn_mode:
            /* 旋转 → 切换LED动画模式 */
            if (now_cnt > t3_last_cnt) {
                current_mode = (current_mode + 1) % 4;
                encoder_flag = 1;
            } else if (now_cnt < t3_last_cnt) {
                current_mode = (current_mode - 1 + 4) % 4;
                encoder_flag = 1;
            }
            break;

        default:
            break;
    }
    t3_last_cnt = now_cnt;   /* 更新"上一次计数值" */

    /* ---- 编码器按键(PB5): 切换 led_mode ----
     * 注意: mode_press_flag 是在 EXTI 中断回调里置 1 的
     *       这里检测到后立即处理并清零, 同时串口反馈
     */
    if (mode_press_flag != 0) {
        led_mode = (led_mode + 1) % 3;    /* 循环切换: IDLE→BRIGHT→MODE→IDLE */
        encoder_flag = 1;                  /* 触发一次"快闪反馈" */

        /* 串口同步通知电脑: 告知当前编码器模式 */
        {
            char send_buf[60];
            switch (led_mode) {
                case led_default_mode:
                    sprintf(send_buf, "Encoder: IDLE (rotary does nothing)\r\n");
                    break;
                case led_brights_mode:
                    sprintf(send_buf, "Encoder: BRIGHT (rotary adjusts brightness)\r\n");
                    break;
                case led_turn_mode:
                    sprintf(send_buf, "Encoder: MODE (rotary changes LED pattern)\r\n");
                    break;
            }
            HAL_UART_Transmit(&huart1, (uint8_t*)send_buf, strlen(send_buf), HAL_MAX_DELAY);
        }

        mode_press_flag = 0;     /* 标志清零 */
        system_state = 0;        /* 重置防抖, 允许下一次按键进入中断 */
    }

    /* ---- 编码器操作成功反馈: 板载LED快闪2秒 ---- */
    if (encoder_flag != 0) {
        bright_speed = 300;        /* 300ms翻转一次 → 看起来"快闪" */
        blink_timer = HAL_GetTick();
        encoder_flag = 0;
    }

    /* ---- OLED刷新: 只有"值真的变了"才重绘 (I2C刷新很慢, 要省着用) ---- */

    /* 1) LED动画模式变化 → 重绘 */
    if (current_mode != last_mode) {
        OLED_Clear();
        OLED_ShowString(1, 1, "LED CONTROL");
        switch (current_mode) {
            case mode_idle:    OLED_ShowString(2, 1, "Mode: IDLE   "); break;
            case mode_running: OLED_ShowString(2, 1, "Mode: RUNNING"); break;
            case mode_load:    OLED_ShowString(2, 1, "Mode: LOAD   "); break;
            case mode_shark:   OLED_ShowString(2, 1, "Mode: SHARK  "); break;
        }
        last_mode = current_mode;
    }

    /* 2) 亮度变化 → 重绘 */
    if (led_bright != last_bright) {
        char buf[20];
        sprintf(buf, "BRIGHT: %d   ", led_bright);
        OLED_ShowString(3, 1, buf);
        last_bright = led_bright;
    }

    /* 3) 编码器模式变化 → 重绘 */
    if (led_mode != last_led_mode) {
        switch (led_mode) {
            case led_default_mode:
                OLED_ShowString(4, 1, "ENC: IDLE    ");
                break;
            case led_brights_mode:
                OLED_ShowString(4, 1, "ENC: BRIGHT  ");
                break;
            case led_turn_mode:
                OLED_ShowString(4, 1, "ENC: MODE    ");
                break;
        }
        last_led_mode = led_mode;
    }

    /* ---- 板载按键(PA0/PA1/PA2): 直接切LED动画模式 ----
     * key_press 在 EXTI 中断里置位, 这里处理完后清零
     */
    if (key_press != 0) {
        uint8_t key = key_press;
        HAL_Delay(20);       /* 简单软件防抖: 20ms稳定后再读 */
        GPIO_PinState level = GPIO_PIN_SET;
        if (key == 1)       level = HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0);
        else if (key == 2)  level = HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_1);
        else if (key == 3)  level = HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_2);

        /* 按键确实还处于"按下"状态才执行 */
        if (level == GPIO_PIN_RESET) {
            switch (key) {
                case 1: current_mode = mode_running; break;   /* PA0: 流水灯 */
                case 2: current_mode = mode_load;    break;   /* PA1: 全亮 */
                case 3: current_mode = mode_shark;   break;   /* PA2: 两端/中间 */
            }
        }
        system_state = 0;   /* 重置防抖标志 */
        key_press = 0;      /* 按键事件处理完毕 */
    }

    /* ---- LED灯珠动画执行: 根据current_mode做不同动作 ----
     * 动画模式的占空比直接写入 TIM1_CCR1/2/3,
     * HAL_Delay只在这里用, 保证动画节奏稳定
     */
    switch (current_mode) {
        case mode_idle:
            /* 全灭: 3路占空比都=0 */
            __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, 0);
            __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_2, 0);
            __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_3, 0);
            break;

        case mode_running:
            /* 流水灯: PA8亮 → PA9亮 → PA10亮, 每步200ms */
            __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, led_bright);
            HAL_Delay(200);
            __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, 0);

            __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_2, led_bright);
            HAL_Delay(200);
            __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_2, 0);

            __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_3, led_bright);
            HAL_Delay(200);
            __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_3, 0);

            HAL_Delay(300);    /* 一组动画后的停顿 */
            break;

        case mode_shark:
            /* 两端 vs 中间: PA8+PA10 同时亮 ↔ 只有PA9亮 */
            __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, led_bright);
            __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_3, led_bright);
            __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_2, 0);
            HAL_Delay(300);

            __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, 0);
            __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_3, 0);
            __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_2, led_bright);
            HAL_Delay(300);
            break;

        case mode_load:
            /* 全亮: 3路占空比=led_bright */
            __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, led_bright);
            __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_2, led_bright);
            __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_3, led_bright);
            break;
    }

    /* ---- 串口命令处理: receive_status 在HAL_UART_RxCpltCallback里置1 ---- */
    if (receive_status != 0) {
        Serial_ProcessChar(rx_buffer[0]);
        receive_status = 0;
    }

    /* ---- 串口定时上报: 每2秒输出一次亮度值 ---- */
    if (HAL_GetTick() - last_uart_tick >= 2000) {
        char buf1[40];
        sprintf(buf1, "Current Brightness = %d\r\n", led_bright);
        HAL_UART_Transmit(&huart1, (uint8_t*)buf1, strlen(buf1), HAL_MAX_DELAY);
        last_uart_tick = HAL_GetTick();
    }
  }
  /* USER CODE END WHILE */

  /* USER CODE BEGIN 3 */
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
  hi2c1.Init.ClockSpeed = 400000;
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
  * @brief TIM1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM1_Init(void)
{

  /* USER CODE BEGIN TIM1_Init 0 */

  /* USER CODE END TIM1_Init 0 */

  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};
  TIM_BreakDeadTimeConfigTypeDef sBreakDeadTimeConfig = {0};

  /* USER CODE BEGIN TIM1_Init 1 */

  /* USER CODE END TIM1_Init 1 */
  htim1.Instance = TIM1;
  htim1.Init.Prescaler = 7199;
  htim1.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim1.Init.Period = 99;
  htim1.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim1.Init.RepetitionCounter = 0;
  htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
  if (HAL_TIM_PWM_Init(&htim1) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim1, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCNPolarity = TIM_OCNPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  sConfigOC.OCIdleState = TIM_OCIDLESTATE_RESET;
  sConfigOC.OCNIdleState = TIM_OCNIDLESTATE_RESET;
  if (HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_2) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_3) != HAL_OK)
  {
    Error_Handler();
  }
  sBreakDeadTimeConfig.OffStateRunMode = TIM_OSSR_DISABLE;
  sBreakDeadTimeConfig.OffStateIDLEMode = TIM_OSSI_DISABLE;
  sBreakDeadTimeConfig.LockLevel = TIM_LOCKLEVEL_OFF;
  sBreakDeadTimeConfig.DeadTime = 0;
  sBreakDeadTimeConfig.BreakState = TIM_BREAK_DISABLE;
  sBreakDeadTimeConfig.BreakPolarity = TIM_BREAKPOLARITY_HIGH;
  sBreakDeadTimeConfig.AutomaticOutput = TIM_AUTOMATICOUTPUT_DISABLE;
  if (HAL_TIMEx_ConfigBreakDeadTime(&htim1, &sBreakDeadTimeConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM1_Init 2 */

  /* USER CODE END TIM1_Init 2 */
  HAL_TIM_MspPostInit(&htim1);

}

/**
  * @brief TIM2 Initialization Function (定时中断, 每2秒上报状态)
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
  /* 时基计算:
   *   APB1时钟 = 36MHz (TIM2挂在APB1, 但APB1分频后会×2, 实际时钟=72MHz)
   *   PSC = 7199  → 计数频率 = 72MHz / 7200 = 10kHz
   *   ARR = 9999  → 溢出频率 = 10kHz / 10000 = 1Hz
   *   所以 TIM2 每 1 秒触发一次中断
   *
   * 注意: HAL_TIM_PeriodElapsedCallback 里用的是"手动计数"来实现2秒间隔
   *       所以这里只要1秒溢出一次即可, 回调函数自己再数2秒
   */
  htim2.Instance = TIM2;
  htim2.Init.Prescaler = 7199;
  htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim2.Init.Period = 9999;
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
  * @brief TIM3 Initialization Function (硬件编码器模式)
  *
  * 底层原理:
  *   TIM3配置为"编码器接口模式TI1+TI2"时,
  *   硬件会同时监视CH1(PA6)和CH2(PA7)的边沿,
  *   根据"哪个通道的边沿先出现"来自动判断旋转方向:
  *     - A相先于B相 → CNT 递增
  *     - B相先于A相 → CNT 递减
  *   完全不需要软件轮询GPIO, 节省大量CPU资源
  *
  * @param None
  * @retval None
  */
static void MX_TIM3_Init(void)
{

  /* USER CODE BEGIN TIM3_Init 0 */

  /* USER CODE END TIM3_Init 0 */

  TIM_Encoder_InitTypeDef sConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM3_Init 1 */

  /* USER CODE END TIM3_Init 1 */
  htim3.Instance = TIM3;
  htim3.Init.Prescaler = 0;                    /* 不分频, 每一个脉冲都计数 */
  htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim3.Init.Period = 65535;                   /* 16位全量程: 0 ~ 65535 */
  htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;

  /* 编码器模式3: TI1和TI2两路信号都计数 (每格脉冲CNT变4, 精度最高) */
  sConfig.EncoderMode = TIM_ENCODERMODE_TI12;
  sConfig.IC1Polarity = TIM_ICPOLARITY_RISING;
  sConfig.IC1Selection = TIM_ICSELECTION_DIRECTTI;
  sConfig.IC1Prescaler = TIM_ICPSC_DIV1;
  sConfig.IC1Filter = 0;                      /* 可以改成 15 增强抗干扰 */
  sConfig.IC2Polarity = TIM_ICPOLARITY_RISING;
  sConfig.IC2Selection = TIM_ICSELECTION_DIRECTTI;
  sConfig.IC2Prescaler = TIM_ICPSC_DIV1;
  sConfig.IC2Filter = 0;
  if (HAL_TIM_Encoder_Init(&htim3, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim3, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM3_Init 2 */

  /* USER CODE END TIM3_Init 2 */

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
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);

  /*Configure GPIO pin : PC13 (板载蓝灯, 推挽输出, 用于可见反馈) */
  GPIO_InitStruct.Pin = GPIO_PIN_13;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pins : PA0 PA1 PA2 (板载按键, 下拉+EXTI下降沿) */
  GPIO_InitStruct.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pin : PB5 (编码器按键, 下拉+EXTI下降沿) */
  GPIO_InitStruct.Pin = GPIO_PIN_5;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* EXTI interrupt init (优先级都设为0, 同等优先级排队处理) */
  HAL_NVIC_SetPriority(EXTI0_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI0_IRQn);

  HAL_NVIC_SetPriority(EXTI1_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI1_IRQn);

  HAL_NVIC_SetPriority(EXTI2_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI2_IRQn);

  HAL_NVIC_SetPriority(EXTI9_5_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* =========================================================================
 * 串口单字符命令处理 (主循环中调用)
 *   '1' → 流水灯    '2' → 全亮    '3' → 两端/中间    '0' → 全灭
 *   '+'/'u' → 亮度+5   '-'/'d' → 亮度-5
 *   '数字序列...' + 回车 → 直接设置亮度值
 * ========================================================================= */
void Serial_ProcessChar(uint8_t ch)
{
    switch (ch) {
        case '1': current_mode = mode_running; break;
        case '2': current_mode = mode_load;    break;
        case '3': current_mode = mode_shark;   break;
        case '0': current_mode = mode_idle;    break;
        case '+':
        case 'u':
            if (led_bright < 99) led_bright += 5;
            break;
        case '-':
        case 'd':
            if (led_bright > 0)  led_bright -= 5;
            break;
        default:
            return;  /* 非法命令直接返回 */
    }

    /* 命令处理成功后, 回传当前状态到电脑 (帮助调试) */
    char send_buf[64];
    sprintf(send_buf, "Mode=%s, Brightness=%d\r\n", mode_name[current_mode], led_bright);
    HAL_UART_Transmit(&huart1, (uint8_t*)send_buf, strlen(send_buf), HAL_MAX_DELAY);
}

/* =========================================================================
 * TIM2溢出中断回调: 定时上报当前状态 (LED模式/编码器模式/亮度)
 * 注意: TIM2每1秒溢出一次, 这里无条件上报, 方便电脑观察系统状态
 * ========================================================================= */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM2) {
        char send_buf[50];
        {
            char enc_name[10];
            if (led_mode == led_default_mode)       strcpy(enc_name, "IDLE");
            else if (led_mode == led_brights_mode)  strcpy(enc_name, "BRIGHT");
            else                                     strcpy(enc_name, "MODE");
            sprintf(send_buf, "LED=%s, Enc=%s, Bright=%d\r\n",
                    mode_name[current_mode], enc_name, led_bright);
        }
        HAL_UART_Transmit(&huart1, (uint8_t*)send_buf, strlen(send_buf), HAL_MAX_DELAY);
    }
}

/* =========================================================================
 * 串口接收完成中断 (逐个字节接收)
 * 处理流程:
 *   - 收到数字字符 → 先缓存 (num_buf)
 *   - 收到'\r'或'\n' → 把缓存里的数字转成整数设为亮度
 *   - 收到其他字符 → 调用Serial_ProcessChar做单字符命令
 *   - 每次收到字符都重开中断接收 (中断方式接收是"一次性"的)
 * ========================================================================= */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance != USART1)
        return;

    uint8_t ch = rx_buffer[0];

    /* 1) 回车/换行 → 把缓存里累计的数字转成亮度值 */
    if (ch == '\r' || ch == '\n') {
        if (num_idx > 0) {
            num_buf[num_idx] = '\0';
            uint32_t val = 0;
            for (int i = 0; num_buf[i] != '\0'; i++) {
                if (num_buf[i] >= '0' && num_buf[i] <= '9') {
                    val = val * 10 + (num_buf[i] - '0');  /* 字符→数字 */
                }
            }
            if (val > 99) val = 99;    /* 限制范围: PWM的ARR=99 */
            led_bright = (uint8_t)val;

            /* 回传确认 */
            char ack[40];
            sprintf(ack, "OK, Brightness=%d\r\n", led_bright);
            HAL_UART_Transmit(&huart1, (uint8_t*)ack, strlen(ack), HAL_MAX_DELAY);

            num_idx = 0;
        }
    }
    /* 2) 数字字符 → 放进缓冲区 (累计多位数) */
    else if (ch >= '0' && ch <= '9' && num_idx < 9) {
        num_buf[num_idx++] = ch;
    }
    /* 3) 其他字符 → 当作单字符命令处理 */
    else {
        Serial_ProcessChar(ch);
        num_idx = 0;
    }

    /* 重新开启下一次中断接收 (必须每次重开, 这是HAL_UART_Receive_IT的特性) */
    HAL_UART_Receive_IT(&huart1, rx_buffer, 1);
}

/* =========================================================================
 * EXTI外部中断回调 (按键按下时触发, 下降沿)
 * 设计原则: 中断里只做"置标志", 实际处理放到主循环执行,
 *           避免在中断里写复杂逻辑 (中断上下文限制很多)
 * ========================================================================= */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    /* PB5 (编码器按键) → 切换 led_mode
     * 最高优先级处理: 不受 system_state 防抖限制 (独立按键) */
    if (GPIO_Pin == GPIO_PIN_5) {
        mode_press_flag = 1;
        return;
    }

    /* 板载按键(PA0/PA1/PA2): 用 system_state 做软件防抖
     * system_state=1 表示"正在处理中, 同一按键短期内再次按下会被忽略" */
    if (system_state != 0)
        return;

    if (GPIO_Pin == GPIO_PIN_0) {          /* 按键1 (PA0) */
        key_press = 1;
        system_state = 1;
    } else if (GPIO_Pin == GPIO_PIN_1) {   /* 按键2 (PA1) */
        key_press = 2;
        system_state = 1;
    } else if (GPIO_Pin == GPIO_PIN_2) {   /* 按键3 (PA2) */
        key_press = 3;
        system_state = 1;
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