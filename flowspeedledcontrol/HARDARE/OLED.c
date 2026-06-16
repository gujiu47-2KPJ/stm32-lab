/**
  ******************************************************************************
  * @file    OLED.c
  * @author  Your Name
  * @brief   SSD1306 OLED 驱动（适配 CubeMX 所有外设代码在 main.c 的情况）
  ******************************************************************************
  */

#include "OLED.h"
#include "OLED_Font.h"
#include "main.h"  // 👈 关键！包含 main.h 即可访问 hi2c1
extern I2C_HandleTypeDef hi2c1;
/* 私有变量 */
static uint8_t OLED_GRAM[128][8]; // 显存：128列 x 8页

// SSD1306 的 I2C 地址（7-bit: 0x3C）
//#define OLED_I2C_ADDR (0x3C << 1)

/**
  * @brief  通过硬件 I2C 发送命令
  */
void OLED_WriteCommand(uint8_t Command)
{
    uint8_t buffer[2] = {0x00, Command}; // 0x00 = 命令模式
    HAL_I2C_Master_Transmit(&hi2c1, OLED_I2C_ADDR, buffer, 2, HAL_MAX_DELAY);
}

/**
  * @brief  通过硬件 I2C 发送数据
  */
void OLED_WriteData(uint8_t Data)
{
    uint8_t buffer[2] = {0x40, Data}; // 0x40 = 数据模式
    HAL_I2C_Master_Transmit(&hi2c1, OLED_I2C_ADDR, buffer, 2, HAL_MAX_DELAY);
}

/**
  * @brief  OLED 初始化
  */
void OLED_Init(void)
{
    HAL_Delay(100);

    OLED_WriteCommand(0xAE); // Display OFF
    OLED_WriteCommand(0xD5); // Set Display Clock Divide Ratio
    OLED_WriteCommand(0x80);
    OLED_WriteCommand(0xA8); // Set Multiplex Ratio
    OLED_WriteCommand(0x3F);
    OLED_WriteCommand(0xD3); // Set Display Offset
    OLED_WriteCommand(0x00);
    OLED_WriteCommand(0x40); // Set Display Start Line
    OLED_WriteCommand(0x8D); // Charge Pump
    OLED_WriteCommand(0x14);
    OLED_WriteCommand(0x20); // Memory Addressing Mode
    OLED_WriteCommand(0x02);
    OLED_WriteCommand(0xA1); // Segment Re-map
    OLED_WriteCommand(0xC8); // COM Output Scan Direction
    OLED_WriteCommand(0xDA); // Set COM Pins
    OLED_WriteCommand(0x12);
    OLED_WriteCommand(0x81); // Contrast
    OLED_WriteCommand(0xCF);
    OLED_WriteCommand(0xD9); // Pre-charge Period
    OLED_WriteCommand(0xF1);
    OLED_WriteCommand(0xDB); // VCOMH Deselect Level
    OLED_WriteCommand(0x30);
    OLED_WriteCommand(0xA4); // Entire Display ON
    OLED_WriteCommand(0xA6); // Normal Display
    OLED_WriteCommand(0xAF); // Display ON

    OLED_Clear();
}

/**
  * @brief  清空显存
  */
void OLED_Clear(void)
{
    for (uint8_t j = 0; j < 8; j++)
        for (uint8_t i = 0; i < 128; i++)
            OLED_GRAM[i][j] = 0;
}

/**
  * @brief  刷新显存到屏幕
  */
void OLED_Refresh_Gram(void)
{
    for (uint8_t j = 0; j < 8; j++) {
        OLED_WriteCommand(0xB0 + j);
        OLED_WriteCommand(0x00);
        OLED_WriteCommand(0x10);
        for (uint8_t i = 0; i < 128; i++) {
            OLED_WriteData(OLED_GRAM[i][j]);
        }
    }
}

/**
  * @brief  显示一个字符（8x16）
  */
void OLED_ShowChar(uint8_t Line, uint8_t Column, char Char)
{
    uint8_t col_start = (Column - 1) * 8;
    uint8_t index = (uint8_t)Char - 32;

    // 上半部分
    for (uint8_t i = 0; i < 8; i++) {
        OLED_GRAM[col_start + i][Line - 1] = Font8x16[index][i];
    }
    // 下半部分
    for (uint8_t i = 0; i < 8; i++) {
        OLED_GRAM[col_start + i][Line] = Font8x16[index][i + 8];
    }
}

/**
  * @brief  显示字符串
  */
void OLED_ShowString(uint8_t Line, uint8_t Column, char *String)
{
    uint8_t i = 0;
    while (String[i] != '\0') {
        OLED_ShowChar(Line, Column + i, String[i]);
        i++;
    }
}
