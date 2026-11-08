/**
 ******************************************************************************
 * @file    ssd1306.h
 * @brief   SSD1306 OLED 128x64 显示驱动接口
 * @note    通信协议: I2C, 默认地址 0x3C (7bit) -> 0x78 (8bit 写)
 *          页寻址模式, 每页 8 行 x 128 列, 共 8 页
 *          字库: 6x8 ASCII 点阵 (oledinfont.h)
 ******************************************************************************
 */

#ifndef __OLED_H
#define __OLED_H

#include "main.h"

/** @brief OLED 像素宽度 */
#define OLED_WIDTH  128
/** @brief OLED 像素高度 */
#define OLED_HEIGHT 64

/**
 * @brief  初始化 OLED 模块
 * @note   上电后必须调用, 配置寻址模式、电荷泵、对比度等
 */
void OLED_Init(void);

/**
 * @brief  清除整个屏幕 (全部填充 0x00)
 */
void OLED_Clear(void);

/**
 * @brief  在指定位置显示 ASCII 字符串
 * @param  x   横坐标 (0~121, 步进 6 像素/字符)
 * @param  y   纵坐标页号 (0~7, 每页 8 行)
 * @param  str 以 '\0' 结尾的字符串指针
 */
void OLED_ShowString(uint8_t x, uint8_t y, char *str);

/**
 * @brief  在指定位置显示无符号整数
 * @param  x   横坐标 (0~121)
 * @param  y   纵坐标页号 (0~7)
 * @param  num 要显示的数值
 * @param  len 显示位数 (不足左补空)
 */
void OLED_ShowNum(uint8_t x, uint8_t y, uint32_t num, uint8_t len);

#endif