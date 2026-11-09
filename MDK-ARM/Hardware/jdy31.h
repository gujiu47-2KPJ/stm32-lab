/**
 ******************************************************************************
 * @file    jdy31.h
 * @brief   JDY-3x 系列蓝牙模块驱动接口
 * @note    通信协议: UART, 默认波特率 9600
 *          AT 指令以 "\r\n" 结尾
 *          透传模式下直接转发串口数据到远程设备
 ******************************************************************************
 */

#ifndef __JDYXX_H
#define __JDYXX_H

#include "main.h"

/**
 * @brief  蓝牙透传发送字符串
 * @param  str 要发送的字符串 (以 '\0' 结尾)
 * @note   调用 HAL_UART_Transmit 发送, 不附加换行
 */
void JDY31_SendString(char *str);

/**
 * @brief  发送 AT 指令 (自动附加 "\r\n")
 * @param  cmd AT 指令内容 (不含 \r\n), 如 "AT"
 */
void JDY31_SendAT(char *cmd);

#endif