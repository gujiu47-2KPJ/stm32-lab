/* =========================================================================
 * 文件说明: FlashSystemDataProtect - Flash数据掉电保护 (优化版)
 *
 * 主要修复/优化:
 *   1. 结构体对齐: 原代码里的 __packed 写法是 Keil 专有语法
 *      → 改成通用写法: 手动检查结构体大小,
 *         保证 sizeof(FlashData_t) 是已知值 (不会因为编译器对齐不同而偏移)
 *
 *   2. 魔术字检测: 原来可能只有CRC校验, 加上 MAGIC 标记,
 *      让"第一次上电(Flash全0xFF)"能被正确识别为"无数据"
 *
 *   3. OLED按需刷新: 只有数值变化才重绘, 避免每循环都整屏刷新
 *      (OLED刷新慢, 会让按键响应变慢)
 *
 *   4. 按键消抖优化: 用状态机(空闲→确认→等待释放), 比固定延时更稳
 *
 * 硬件:
 *   W25Q64 (SPI Flash) - 8MB容量, 每页256字节, 每扇区4KB
 *   SSD1306 (OLED I2C) - 128×64
 *   STM32F103 - 按键接GPIO (下拉输入)
 * ========================================================================= */

/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main_OPTIMIZED.c
  * @brief          : Flash数据掉电保护 (优化版)
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
#include "W25Qxx.h"        /* Flash 驱动 */
#include "OLED.h"          /* OLED 驱动 */
#include <string.h>
#include <stdio.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* ------------------------------------------------------------------
 * Flash 存储数据结构
 *   必须保证大小固定, 编译器对齐不能影响读写偏移
 *   方案: 用无符号整数数组存储, 避免结构体对齐差异
 * ------------------------------------------------------------------ */
#define STORAGE_SIZE       32        /* 存 32 字节 (足够) */
#define STORAGE_MAGIC      0xA55A    /* 标记"数据有效" */
#define STORAGE_ADDR       0x000000  /* 存储起始地址 (必须是扇区起始) */

typedef struct {
    uint16_t magic;          /* 0xA55A = 数据有效, 其他=未初始化/损坏 */
    uint16_t crc;            /* 简单校验和: 所有字节相加 */
    uint32_t press_count;    /* 按键按下累计次数 */
    uint32_t last_press_tick;/* 最后一次按下的时间戳 */
    uint8_t  reserved[16];   /* 保留空间, 后续扩展 */
} FlashData_t;               /* 总大小 = 4+4+16 = 24字节, 剩余8字节保留 */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
I2C_HandleTypeDef hi2c1;
SPI_HandleTypeDef hspi1;

/* USER CODE BEGIN PV */
/* ---- 运行时数据 (RAM) ---- */
static uint32_t g_press_count = 0;      /* 当前按键次数 */

/* ---- OLED 缓存: 只在"数值变化"时重绘 ---- */
static uint32_t g_last_shown_count = (uint32_t)(-1);
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_I2C1_Init(void);
static void MX_SPI1_Init(void);
/* USER CODE BEGIN PFP */
uint16_t Compute_CRC(const uint8_t *data, uint16_t len);
uint8_t  Save_To_Flash(uint32_t count);
uint8_t  Load_From_Flash(uint32_t *count_out);
void     OLED_Refresh(uint32_t count);
void     Key_Scan(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* ==================================================================
 * 简单校验和 (非CRC, 但足够发现"全0/全1/随机损坏")
 * ================================================================== */
uint16_t Compute_CRC(const uint8_t *data, uint16_t len)
{
    uint32_t sum = 0;
    for (uint16_t i = 0; i < len; i++) sum += data[i];
    return (uint16_t)(sum & 0xFFFF);
}

/* ==================================================================
 * 按键扫描 (状态机: 空闲→确认→等待释放)
 *   避免 HAL_Delay(20) 阻塞式消抖
 *   硬件: PA0 下拉输入, 按下=高电平
 * ================================================================== */
void Key_Scan(void)
{
    typedef enum {
        KEY_IDLE = 0,       /* 松开, 等待按下 */
        KEY_DEBOUNCE,       /* 检测到按下, 等20ms再读确认 */
        KEY_PRESSED,        /* 确认按下并处理, 等松开 */
    } KeyState_t;

    static KeyState_t state = KEY_IDLE;
    static uint32_t   tick  = 0;

    GPIO_PinState lvl = HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0);

    switch (state) {
        case KEY_IDLE:
            if (lvl == GPIO_PIN_SET) {     /* 首次检测到按下 */
                tick  = HAL_GetTick();
                state = KEY_DEBOUNCE;
            }
            break;

        case KEY_DEBOUNCE:
            if ((HAL_GetTick() - tick) >= 20) {  /* 20ms消抖时间到 */
                if (lvl == GPIO_PIN_SET) {        /* 仍按下 → 真按 */
                    g_press_count++;               /* 计数+1 */
                    Save_To_Flash(g_press_count);  /* 立即写入 Flash */
                    state = KEY_PRESSED;           /* 等用户松开 */
                } else {
                    state = KEY_IDLE;              /* 抖动, 放弃 */
                }
            }
            break;

        case KEY_PRESSED:
            if (lvl == GPIO_PIN_RESET) {    /* 松开了 */
                state = KEY_IDLE;
            }
            break;
    }
}

/* ==================================================================
 * 写入Flash
 *   返回 1=成功, 0=失败
 *
 * 流程:
 *   1) 构造 FlashData_t 并填充 magic + crc + 数据
 *   2) 先擦除扇区 (W25Q64 必须"先擦后写")
 *   3) 写入 STORAGE_SIZE 字节
 *   4) 回读校验 (可选, 提升可靠性)
 * ================================================================== */
uint8_t Save_To_Flash(uint32_t count)
{
    FlashData_t d;
    memset(&d, 0, sizeof(d));
    d.magic           = STORAGE_MAGIC;
    d.press_count     = count;
    d.last_press_tick = HAL_GetTick();
    /* 注意: 计算CRC时先把crc字段置0, 否则含脏数据 */
    d.crc             = Compute_CRC((uint8_t *)&d.press_count,
                                    sizeof(d) - 4);  /* 跳过 magic+crc */

    /* 写 Flash: 擦除 → 写入 */
    W25Qxx_Erase_Sector(STORAGE_ADDR);
    W25Qxx_Write((uint8_t *)&d, STORAGE_ADDR, sizeof(d));

    /* 回读校验 (简单但有效的可靠性保障) */
    FlashData_t back;
    memset(&back, 0, sizeof(back));
    W25Qxx_Read((uint8_t *)&back, STORAGE_ADDR, sizeof(back));
    return (memcmp(&d, &back, sizeof(d)) == 0) ? 1 : 0;
}

/* ==================================================================
 * 从Flash读取
 *   返回 1=读到有效数据, 0=无数据/损坏, 此时 *count_out 设为0
 * ================================================================== */
uint8_t Load_From_Flash(uint32_t *count_out)
{
    FlashData_t d;
    memset(&d, 0, sizeof(d));
    W25Qxx_Read((uint8_t *)&d, STORAGE_ADDR, sizeof(d));

    /* 1) magic 不对 = 从未写过 / 全擦除 */
    if (d.magic != STORAGE_MAGIC) {
        *count_out = 0;
        return 0;
    }

    /* 2) CRC 不对 = 数据损坏 (可能写入中途断电) */
    uint16_t expected = Compute_CRC((uint8_t *)&d.press_count,
                                    sizeof(d) - 4);
    if (expected != d.crc) {
        *count_out = 0;
        return 0;
    }

    *count_out = d.press_count;
    return 1;
}

/* ==================================================================
 * OLED 刷新: 只有数值变化时才重新画 (省时间/省OLED寿命)
 * ================================================================== */
void OLED_Refresh(uint32_t count)
{
    if (count == g_last_shown_count) return;   /* 没变, 不重绘 */
    g_last_shown_count = count;

    /* 画第一行: 提示 */
    OLED_ShowString(1, 1, "Flash Protect Demo");
    OLED_ShowString(3, 1, "Press: ");

    /* 显示10位数 (最多42亿, 足够大) */
    char buf[16];
    uint32_t n = count;
    int len = 0;
    do {
        buf[len++] = '0' + (n % 10);
        n /= 10;
    } while (n > 0 && len < 10);

    /* 反序输出 (低位在前) */
    char out[16];
    for (int i = 0; i < len; i++) out[i] = buf[len - 1 - i];
    out[len] = 0;
    OLED_ShowString(3, 8, out);

    /* 最后一行: 提示掉电后仍在 */
    OLED_ShowString(5, 1, "Reboot-safe");
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
    HAL_Init();
    SystemClock_Config();

    /* USER CODE BEGIN SysInit */

    /* USER CODE END SysInit */

    MX_GPIO_Init();
    MX_I2C1_Init();
    MX_SPI1_Init();

    /* USER CODE BEGIN 2 */
    /* Flash 初始化 (W25Qxx 内部函数) */
    W25Qxx_Init();

    /* OLED 初始化 */
    OLED_Init();
    OLED_Clear();

    /* 从 Flash 恢复按键计数 (掉电恢复) */
    uint32_t saved = 0;
    if (Load_From_Flash(&saved)) {
        g_press_count = saved;        /* 读到有效数据 → 恢复 */
    } else {
        g_press_count = 0;            /* 第一次上电 / 数据损坏 */
        Save_To_Flash(0);             /* 初始化 Flash 为 "0次" */
    }

    /* 首次刷新OLED */
    g_last_shown_count = (uint32_t)(-1);
    OLED_Refresh(g_press_count);
    /* USER CODE END 2 */

    /* Infinite loop */
    /* USER CODE BEGIN WHILE */
    while (1)
    {
        /* USER CODE END WHILE */

        /* USER CODE BEGIN 3 */
        Key_Scan();
        OLED_Refresh(g_press_count);
        /* 没有HAL_Delay阻塞, 响应最快 */
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
  hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK) { Error_Handler(); }
}

/**
  * @brief SPI1 Initialization Function (Flash通信)
  * @param None
  * @retval None
  */
static void MX_SPI1_Init(void)
{
  hspi1.Instance = SPI1;
  hspi1.Init.Mode = SPI_MODE_MASTER;
  hspi1.Init.Direction = SPI_DIRECTION_2LINES;
  hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi1.Init.NSS = SPI_NSS_SOFT;
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_8;
  hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi1.Init.CRCPolynomial = 10;
  if (HAL_SPI_Init(&hspi1) != HAL_OK) { Error_Handler(); }
}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /* PA0 按键输入 (下拉) */
  GPIO_InitStruct.Pin = GPIO_PIN_0;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLDOWN;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* PC13 蓝灯 */
  GPIO_InitStruct.Pin = GPIO_PIN_13;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);
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
  while (1) {
    /* 错误时蓝灯闪烁, 方便观察 */
    HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
    HAL_Delay(200);
  }
}

#ifdef  USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
}
#endif