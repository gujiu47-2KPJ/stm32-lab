#ifndef __OLED_H__
#define __OLED_H__

#include "stm32f1xx_hal.h"
#include "main.h"  // 包含CubeMX生成的句柄声明

/* USER CODE BEGIN Includes */
#include <math.h>
/* USER CODE END Includes */

// OLED I2C地址
#define OLED_I2C_ADDR 0x78  // 实际传输时右移1位，变成0x3C

// OLED控制命令
#define OLED_CMD 0x00  // 命令
#define OLED_DATA 0x40 // 数据

// OLED尺寸
#define OLED_WIDTH 128
#define OLED_HEIGHT 64

// 函数声明
void OLED_Init(void);
void OLED_Clear(void);
void OLED_DrawPoint(uint8_t x, uint8_t y, uint8_t color);
void OLED_ShowChar(uint8_t x, uint8_t y, uint8_t chr, uint8_t size);
void OLED_ShowString(uint8_t x, uint8_t y, char *str);
void OLED_ShowNum(uint8_t x, uint8_t y, uint32_t num, uint8_t len);
void OLED_ShowFloat(uint8_t x, uint8_t y, float num, uint8_t decimal_places);
void OLED_Refresh(void);
void OLED_SetPos(uint8_t x, uint8_t y);

/* USER CODE BEGIN 0 */
/* USER CODE END 0 */

#endif