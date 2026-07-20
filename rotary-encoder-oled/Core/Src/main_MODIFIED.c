/* =========================================================================
 * 项目: roundoled (OLED Dirve) - 旋转编码器角度显示 (HAL库)
 * 原文件: Core/Src/main.c
 * 修复版: Core/Src/main_MODIFIED.c
 *
 * Bug: main() 函数被复制粘贴了 3 次, SystemClock_Config 和其他函数也重复了
 *   后果: 链接器报 "multiple definition of 'main'" / "multiple definition" 错误,
 *         项目根本编不过
 *
 * 修复: 只保留第一份完整的 main + 各初始化函数, 删除后两份重复的代码
 *       同时清理一些零散的注释问题, 让代码可读
 *
 * 功能梳理 (从原始代码中提取):
 *   - TIM3 硬件编码器模式: PA6=CH1, PA7=CH2
 *   - I2C OLED 显示当前角度 (0~360)
 *   - PB0 按键: 归零 (EXTI 下降沿中断)
 *
 * 硬件连接:
 *   - 编码器 A 相 → PA6 (TIM3_CH1)
 *   - 编码器 B 相 → PA7 (TIM3_CH2)
 *   - 编码器 SW  → PB0 (按键, 按下时接地, 需上拉)
 *   - OLED I2C   → PB6(SCL), PB7(SDA) (按你OLED实际接线)
 * ========================================================================= */

/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main_MODIFIED.c
  * @brief          : 旋转编码器角度显示器 (修复版 - 移除重复代码)
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

#include "main.h"
#include "OLED.h"
#include <stdio.h>
#include <math.h>

/* ===================== 私有常量 ===================== */
#define ENCODER_MECHANICAL_STEPS  20    /* 编码器一圈的机械刻度数        */
#define ENCODER_TIM_PPR           (ENCODER_MECHANICAL_STEPS * 4)  /* 四倍频后 */
#define ANGLE_PER_COUNT           (360.0f / ENCODER_TIM_PPR)       /* 每计数对应的角度 */
#define DEBOUNCE_MS               30    /* 按键消抖时长 (ms)            */
#define DISPLAY_INTERVAL_MS       50    /* OLED 刷新间隔 (ms)           */
#define RESET_DISPLAY_TIME        300   /* 归零后 "RESET" 显示时长 (ms) */

/* ===================== 私有变量 ===================== */
I2C_HandleTypeDef  hi2c1;
TIM_HandleTypeDef  htim3;

/* USER CODE BEGIN PV */
volatile int32_t   total_pulses = 0;         /* 累计脉冲数 (编码器计数) */
volatile uint16_t  last_counter = 32768;     /* 上次 TIM3 计数器值 (中点起始) */
volatile uint8_t   reset_flag   = 0;         /* 按键归零标志 (中断置位, 主循环清零) */
volatile uint32_t  button_time  = 0;         /* 按键触发时间戳 (用于消抖) */
volatile uint32_t  last_display_time = 0;    /* 上次 OLED 刷新时间戳   */
volatile uint32_t  reset_display_until = 0;  /* 归零视觉反馈截止时间   */
float               last_angle = -1000.0f;   /* 上次显示的角度 (用于变化检测) */
/* USER CODE END PV */

/* ===================== 函数声明 ===================== */
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_TIM3_Init(void);
static void MX_I2C1_Init(void);

/* USER CODE BEGIN PFP */
void ProcessEncoder(void);
void ProcessButton(void);
void UpdateDisplay(void);
/* USER CODE END PFP */

/* ===================== 用户代码区 0 ===================== */
/* USER CODE BEGIN 0 */

/**
 * @brief  EXTI 外部中断回调 (PB0 按键触发)
 *
 * 设计原则: 中断函数只做最简处理 —— 记录时间戳, 消抖和归零逻辑放主循环
 *           这样可以避免在中断里做 long operation (比如 OLED 刷新)
 */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == GPIO_PIN_0)   /* PB0 按键 */
    {
        button_time = HAL_GetTick();  /* 仅记录时间戳, 交给主循环处理 */
    }
}

/**
 * @brief  读取 TIM3 编码器, 更新累计脉冲数
 *
 * 原理: TIM3 编码器模式下, CNT 寄存器会自动递增/递减
 *       我们每次读 CNT, 做差得到 delta, 累加到 total_pulses
 *       last_counter 初始化为 32768, 是为了让正反向都有足够的范围,
 *       避免刚上电就下溢 (uint16_t 做差后转 int16_t, 要注意符号)
 */
void ProcessEncoder(void)
{
    uint16_t current = (uint16_t)__HAL_TIM_GET_COUNTER(&htim3);
    int16_t   delta   = (int16_t)(current - last_counter);
    total_pulses    += delta;
    last_counter     = current;
}

/**
 * @brief  按键处理: 消抖 + 归零
 *
 * 分两步:
 *   1. 若中断记录了 button_time, 且已过 DEBOUNCE_MS, 置 reset_flag = 1
 *   2. 若 reset_flag = 1, 执行归零 (清零 + OLED 反馈)
 */
void ProcessButton(void)
{
    /* ---- 步骤 1: 时间窗口消抖 ---- */
    if ((button_time != 0) && ((HAL_GetTick() - button_time) >= DEBOUNCE_MS))
    {
        reset_flag  = 1;    /* 触发归零 */
        button_time = 0;    /* 清事件, 避免重复 */
        return;
    }

    /* ---- 步骤 2: 执行归零 ---- */
    if (reset_flag)
    {
        /* 2a. 重置计数状态: total_pulses + TIM3 CNT */
        total_pulses = 0;
        __HAL_TIM_SET_COUNTER(&htim3, 32768);
        last_counter = 32768;

        /* 2b. 视觉反馈: 先显示 "RESET" 300ms, 让用户明确感知归零成功 */
        OLED_Clear();
        OLED_ShowString(3, 3, "RESET");
        OLED_Refresh_Gram();
        HAL_Delay(RESET_DISPLAY_TIME);

        /* 2c. 显示 0.0 */
        OLED_Clear();
        OLED_ShowString(4, 3, "0.0");
        OLED_Refresh_Gram();

        /* 2d. 重置显示缓存, 并锁定 200ms 内不让 UpdateDisplay 覆盖 */
        last_angle = -1000.0f;
        reset_display_until = HAL_GetTick() + 200;

        reset_flag = 0;
        last_display_time = HAL_GetTick();
    }
}

/**
 * @brief  OLED 刷新: 把 total_pulses 换算成角度, 显示到 OLED
 *
 * 两个小优化:
 *   - 归零视觉反馈期间跳过刷新, 避免 "0.0" 刚显示就被覆盖
 *   - 角度变化小于 0.1 度时不刷新, 减少不必要的 I2C 通信
 */
void UpdateDisplay(void)
{
    /* 防覆盖: 归零视觉反馈期间跳过 */
    if (HAL_GetTick() < reset_display_until) return;

    /* 换算: 累计脉冲 → 角度 (0 ~ 360) */
    float raw_angle = total_pulses * ANGLE_PER_COUNT;
    float angle     = fmodf(raw_angle, 360.0f);
    if (angle < 0) angle += 360.0f;

    /* 变化检测: 小于 0.1 度就不刷 */
    if (fabsf(angle - last_angle) < 0.1f) return;

    /* 格式化并显示 (行4, 列3 起始) */
    char buf[8];
    snprintf(buf, sizeof(buf), "%5.1f", angle);

    OLED_Clear();
    OLED_ShowString(4, 3, buf);
    OLED_Refresh_Gram();

    last_angle = angle;
}

/* USER CODE END 0 */

/* ===================== main 入口 ===================== */
int main(void)
{
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_TIM3_Init();
    MX_I2C1_Init();

    /* USER CODE BEGIN 2 */

    /* ---- 启动 TIM3 硬件编码器 ---- */
    if (HAL_TIM_Encoder_Start(&htim3, TIM_CHANNEL_ALL) != HAL_OK)
    {
        Error_Handler();
    }

    /* TIM3 CNT 从 32768 起始 (中点), 正反方向都有足够范围 */
    __HAL_TIM_SET_COUNTER(&htim3, 32768);
    last_counter = 32768;

    /* ---- OLED 初始化 + 启动提示 ---- */
    OLED_Init();
    OLED_Clear();
    OLED_ShowString(4, 3, "READY");
    OLED_Refresh_Gram();
    HAL_Delay(800);

    /* 正式进入 "0.0" 显示状态 */
    OLED_Clear();
    OLED_ShowString(4, 3, "0.0");
    OLED_Refresh_Gram();

    last_display_time    = HAL_GetTick();
    last_angle           = -1000.0f;
    reset_display_until  = 0;

    /* USER CODE END 2 */

    /* ===================== 主循环 ===================== */
    while (1)
    {
        ProcessEncoder();                  /* 读编码器           */
        ProcessButton();                   /* 处理按键 (含归零)  */

        /* 按 DISPLAY_INTERVAL_MS 刷新 OLED */
        if ((HAL_GetTick() - last_display_time) >= DISPLAY_INTERVAL_MS)
        {
            UpdateDisplay();
            last_display_time = HAL_GetTick();
        }

        HAL_Delay(1);   /* 轻微空转, 避免 CPU 100% 占用 */
    }
}

/* ===================== 系统时钟配置 ===================== */
void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    /* HSE 外部晶振 8MHz, 倍频到 72MHz */
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState       = RCC_HSE_ON;
    RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
    RCC_OscInitStruct.HSIState       = RCC_HSI_ON;
    RCC_OscInitStruct.PLL.PLLState   = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource  = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLMUL     = RCC_PLL_MUL9;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
    {
        Error_Handler();
    }

    RCC_ClkInitStruct.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                                     | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;   /* APB1 36MHz */
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;   /* APB2 72MHz */
    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
    {
        Error_Handler();
    }
}

/* ===================== I2C1 初始化 (OLED 通信) ===================== */
static void MX_I2C1_Init(void)
{
    hi2c1.Instance             = I2C1;
    hi2c1.Init.ClockSpeed      = 400000;      /* 400kHz (Fast Mode) */
    hi2c1.Init.DutyCycle       = I2C_DUTYCYCLE_2;
    hi2c1.Init.OwnAddress1     = 0;
    hi2c1.Init.AddressingMode  = I2C_ADDRESSINGMODE_7BIT;
    hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c1.Init.OwnAddress2     = 0;
    hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c1.Init.NoStretchMode   = I2C_NOSTRETCH_DISABLE;
    if (HAL_I2C_Init(&hi2c1) != HAL_OK)
    {
        Error_Handler();
    }
}

/* ===================== TIM3 编码器模式初始化 ===================== */
static void MX_TIM3_Init(void)
{
    TIM_Encoder_InitTypeDef sConfig      = {0};
    TIM_MasterConfigTypeDef sMasterConfig = {0};

    htim3.Instance               = TIM3;
    htim3.Init.Prescaler         = 0;
    htim3.Init.CounterMode       = TIM_COUNTERMODE_UP;
    htim3.Init.Period            = 65535;     /* uint16_t 全范围 */
    htim3.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;

    /* 编码器模式 3: 两路都计数 (四倍频) */
    sConfig.EncoderMode          = TIM_ENCODERMODE_TI12;
    sConfig.IC1Polarity          = TIM_ICPOLARITY_RISING;
    sConfig.IC1Selection         = TIM_ICSELECTION_DIRECTTI;
    sConfig.IC1Prescaler         = TIM_ICPSC_DIV1;
    sConfig.IC1Filter            = 15;        /* 输入滤波: 减少机械噪声 */
    sConfig.IC2Polarity          = TIM_ICPOLARITY_RISING;
    sConfig.IC2Selection         = TIM_ICSELECTION_DIRECTTI;
    sConfig.IC2Prescaler         = TIM_ICPSC_DIV1;
    sConfig.IC2Filter            = 15;

    if (HAL_TIM_Encoder_Init(&htim3, &sConfig) != HAL_OK)
    {
        Error_Handler();
    }

    sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
    sMasterConfig.MasterSlaveMode     = TIM_MASTERSLAVEMODE_DISABLE;
    if (HAL_TIMEx_MasterConfigSynchronization(&htim3, &sMasterConfig) != HAL_OK)
    {
        Error_Handler();
    }
}

/* ===================== GPIO 初始化 ===================== */
static void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /* 开启相关时钟 */
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    /* ---- PB0 按键: 上拉 + 下降沿中断 ---- */
    GPIO_InitStruct.Pin  = GPIO_PIN_0;
    GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
    GPIO_InitStruct.Pull = GPIO_PULLUP;     /* 内部上拉: 未按下时=高电平 */
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    /* NVIC: 使能 EXTI0 (PB0 对应 EXTI0) */
    HAL_NVIC_SetPriority(EXTI0_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(EXTI0_IRQn);
}

/* ===================== 错误处理 ===================== */
void Error_Handler(void)
{
    __disable_irq();
    while (1)
    {
        /* 如果 I2C 已经初始化, 可以在 OLED 上显示 ERROR 提示 */
        if (hi2c1.State == HAL_I2C_STATE_READY)
        {
            OLED_Clear();
            OLED_ShowString(2, 3, "SYSTEM ERROR");
            OLED_Refresh_Gram();
        }
    }
}

/* ===================== USE_FULL_ASSERT ===================== */
#ifdef  USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
    (void)file;
    (void)line;
    /* 预留: 可以 printf 到串口输出错误信息 */
}
#endif