/**
 ******************************************************************************
 * @file    w25qxx.h
 * @brief   W25QXX 系列 SPI Flash 驱动接口
 * @note    通信协议: SPI, CS 由独立 GPIO 控制
 *          支持单字节读写, 写入前自动擦除扇区
 ******************************************************************************
 */

#ifndef __W25QXX_H
#define __W25QXX_H

#include "main.h"

/**
 * @brief  初始化 Flash (读取 JEDEC ID 验证通信)
 */
void W25QXX_Init(void);

/**
 * @brief  从指定地址读取 1 字节数据
 * @param  addr 24 位地址 (0 ~ 16M-1, 取决于芯片容量)
 * @param  data 读取结果输出指针
 */
void W25QXX_ReadByte(uint32_t addr, uint8_t *data);

/**
 * @brief  向指定地址写入 1 字节数据 (自动处理擦除)
 * @param  addr 24 位地址
 * @param  data 写入的数据
 * @note   内部流程: 写使能 -> 扇区擦除 -> 等待忙 -> 写使能 -> 页写入 -> 等待忙
 */
void W25QXX_WriteByte(uint32_t addr, uint8_t data);

#endif