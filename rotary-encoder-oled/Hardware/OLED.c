#include "OLED.h"
#include "OLED_Font.h"
#include "main.h"

#define OLED_I2C_ADDR 0x78  // 如无效，后续尝试 0x7A

extern I2C_HandleTypeDef hi2c1;
static uint8_t OLED_GRAM[8][128]; // [页][列]：8页 × 128列

// ============ 修复1：命令/数据发送（简化+合并） ============
void OLED_WriteCommand(uint8_t cmd) {
    uint8_t buf[2] = {0x00, cmd}; // 0x00=命令
    HAL_I2C_Master_Transmit(&hi2c1, OLED_I2C_ADDR, buf, 2, 100);
}

void OLED_WriteData(uint8_t dat) {
    uint8_t buf[2] = {0x40, dat}; // 0x40=数据
    HAL_I2C_Master_Transmit(&hi2c1, OLED_I2C_ADDR, buf, 2, 100);
}

// ============ 修复2：初始化增加延时 ============
void OLED_Init(void) {
    HAL_Delay(800); // ★ 关键：确保模块上电稳定 ★
    
    OLED_WriteCommand(0xAE); // Display Off
    OLED_WriteCommand(0x8D); OLED_WriteCommand(0x14); // Charge Pump On
    OLED_WriteCommand(0x20); OLED_WriteCommand(0x00); // Horizontal Addressing Mode
    OLED_WriteCommand(0x21); OLED_WriteCommand(0x00); OLED_WriteCommand(0x7F); // Column Address
    OLED_WriteCommand(0x22); OLED_WriteCommand(0x00); OLED_WriteCommand(0x07); // Page Address
    OLED_WriteCommand(0xA1); // Segment Re-map
    OLED_WriteCommand(0xC8); // COM Output Scan Direction
    OLED_WriteCommand(0xA6); // Normal Display
    OLED_WriteCommand(0xAF); // Display On
    
    OLED_Clear();
    OLED_Refresh_Gram();
}

// ============ 修复3：显存操作维度全部修正 ============
void OLED_Clear(void) {
    for (uint8_t page = 0; page < 8; page++)
        for (uint8_t col = 0; col < 128; col++)
            OLED_GRAM[page][col] = 0; // ✅ [页][列]
}

void OLED_Refresh_Gram(void) {
    OLED_WriteCommand(0x21); OLED_WriteCommand(0x00); OLED_WriteCommand(0x7F);
    OLED_WriteCommand(0x22); OLED_WriteCommand(0x00); OLED_WriteCommand(0x07);
    
    for (uint8_t page = 0; page < 8; page++)      // 先遍历页
        for (uint8_t col = 0; col < 128; col++)   // 再遍历列
            OLED_WriteData(OLED_GRAM[page][col]); // ✅ 顺序正确
}

void OLED_ShowChar(uint8_t Line, uint8_t Column, char Char) {
    uint8_t col_start = (Column - 1) * 8;
    uint8_t page_start = Line - 1;
    uint8_t index = Char - 32;
    
    // 上半部分（8行）→ 当前页
    for (uint8_t i = 0; i < 8; i++)
        OLED_GRAM[page_start][col_start + i] = Font8x16[index][i]; // ✅
    
    // 下半部分（8行）→ 下一页
    for (uint8_t i = 0; i < 8; i++)
        OLED_GRAM[page_start + 1][col_start + i] = Font8x16[index][i + 8]; // ✅
}

void OLED_ShowString(uint8_t Line, uint8_t Column, char *str) {
    uint8_t col = Column;
    while (*str) {
        OLED_ShowChar(Line, col, *str);
        col++;
        str++;
    }
}