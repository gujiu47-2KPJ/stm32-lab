/**
 ******************************************************************************
 * @file    ssd1306.c
 * @brief   SSD1306 OLED 128x64 显示驱动实现
 * @note    I2C 通信
 *          命令字节: 控制字节 0x00 + 命令
 *          数据字节: 控制字节 0x40 + 数据
 *          页寻址模式: 8 页 x 128 列, 每页 8 行像素
 ******************************************************************************
 */

#include "ssd1306.h"
#include <string.h>
#include "oledinfont.h"

extern I2C_HandleTypeDef hi2c1;

/** @brief OLED I2C 8 位写地址 (7bit 0x3C << 1) */
#define OLED_ADDR 0x78


/**
 * @brief  向 OLED 发送单字节命令
 * @param  cmd SSD1306 命令字节
 * @note   协议: [0x00][cmd], 0x00 表示命令模式
 */
static void WriteCmd(uint8_t cmd)
{
    HAL_I2C_Mem_Write(&hi2c1, OLED_ADDR, 0x00,
                      I2C_MEMADD_SIZE_8BIT, &cmd, 1, 100);
}

/**
 * @brief  向 OLED 发送多字节显存数据
 * @param  buf 数据缓冲区指针
 * @param  len 数据长度 (字节)
 * @note   协议: [0x40][data...], 0x40 表示数据模式
 */
static void WriteData(uint8_t *buf, uint16_t len)
{
    HAL_I2C_Mem_Write(&hi2c1, OLED_ADDR, 0x40,
                      I2C_MEMADD_SIZE_8BIT, buf, len, 100);
}

/**
 * @brief  OLED 初始化序列
 * @note   配置流程:
 *         1. 关闭显示 (0xAE)
 *         2. 设置寻址模式 / 复用比 / 电荷泵
 *         3. 设置显示方向 (段重映射 / COM 扫描)
 *         4. 设置对比度 / 预充电 / VCOMH
 *         5. 打开显示 (0xAF)
 */
void OLED_Init(void)
{
    WriteCmd(0xAE);   /* 关闭显示, 配置期间防止花屏 */

    WriteCmd(0x20);   /* 设置内存寻址模式 */
    WriteCmd(0x02);   /* 页寻址模式 */

    WriteCmd(0xA8);   /* 设置多路复用比 */
    WriteCmd(0x3F);   /* 64 行 (HEIGHT - 1) */

    WriteCmd(0x8D);   /* 电荷泵设置 */
    WriteCmd(0x14);   /* 使能电荷泵 (3.3V 供电必须) */

    WriteCmd(0xD3);   /* 显示偏移 */
    WriteCmd(0x00);   /* 偏移 0 */

    WriteCmd(0x40);   /* 显示起始行 0 */

    WriteCmd(0xA1);   /* 段重映射 (列 127 映射到 SEG0, 左右翻转) */

    WriteCmd(0xC8);   /* COM 扫描方向 (COM[N-1] 到 COM0, 上下翻转) */

    WriteCmd(0xDA);   /* COM 引脚硬件配置 */
    WriteCmd(0x12);   /* 128x64 模式 */

    WriteCmd(0x81);   /* 对比度设置 */
    WriteCmd(0x7F);   /* 对比度值 (0 ~ 255) */

    WriteCmd(0xD9);   /* 预充电周期 */
    WriteCmd(0xF1);

    WriteCmd(0xDB);   /* VCOMH 电压 */
    WriteCmd(0x40);

    WriteCmd(0xA4);   /* 正常显示 (非全屏点亮) */

    WriteCmd(0xA6);   /* 正常模式 (非反色) */

    WriteCmd(0xAF);   /* 打开显示 */
}

/**
 * @brief  清除 OLED 全屏 (填充 0x00)
 * @note   逐页写入 128 字节全 0
 */
void OLED_Clear(void)
{
    uint8_t page;
    uint8_t zeros[128] = {0};

    for (page = 0; page < 8; page++) {
        WriteCmd(0xB0 + page);          /* 设置页地址 */
        WriteCmd(0x00);                 /* 设置列低 4 位 */
        WriteCmd(0x10);                 /* 设置列高 4 位 */
        WriteData(zeros, 128);          /* 填充整行 0 */
    }
}

/**
 * @brief  在 OLED 上显示 ASCII 字符串
 * @param  x   横坐标 (0~121)
 * @param  y   纵坐标页号 (0~7)
 * @param  str ASCII 字符串 (以 '\0' 结尾)
 * @note   使用 6x8 点阵字库, 每字符 8 字节
 *         字符间距固定 6 像素, 超宽自动截断
 */
void OLED_ShowString(uint8_t x, uint8_t y, char *str)
{
    while (*str) {
        uint8_t idx = *str - ' ';           /* 字库偏移量, 空格为 0 */
        WriteCmd(0xB0 + y);                 /* 设置页 */
        WriteCmd(x & 0x0F);                 /* 列低 4 位 */
        WriteCmd(0x10 | (x >> 4));          /* 列高 4 位 */
        WriteData((uint8_t *)Font6x8[idx], 8);  /* 写 8 字节字模 */
        str++;
        x += 6;                             /* 字符宽度 6 像素 */
    }
}

/**
 * @brief  在 OLED 上显示无符号整数
 * @param  x   横坐标
 * @param  y   纵坐标页号
 * @param  num 数值
 * @param  len 显示位数 (不足左补空格, 超出截低位)
 */
void OLED_ShowNum(uint8_t x, uint8_t y, uint32_t num, uint8_t len)
{
    char buf[11];
    uint8_t i;
    for (i = 0; i < len; i++) {
        buf[len - 1 - i] = (num % 10) + '0';    /* 个位先存到末尾 */
        num /= 10;
    }
    buf[len] = '\0';
    OLED_ShowString(x, y, buf);
}