#ifndef __OLED_H
#define __OLED_H

#include "stdint.h"

// SSD1306 I2C 地址（7-bit: 0x3C）
#define OLED_I2C_ADDR    (0x3C << 1)

void OLED_Init(void);
void OLED_Clear(void);
void OLED_Refresh_Gram(void);
void OLED_ShowChar(uint8_t Line, uint8_t Column, char Char);
void OLED_ShowString(uint8_t Line, uint8_t Column, char *String);

#endif