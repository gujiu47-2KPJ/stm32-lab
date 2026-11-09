/**
 ******************************************************************************
 * @file    jdy3xx.c
 * @brief   JDY-3x 系列蓝牙模块驱动实现
 * @note    UART 透传模式, 默认 9600 8N1
 *          AT 指令必须以 "\r\n" 结尾模块才会响应
 *          上电默认进入透传模式, 可直接收发数据
 ******************************************************************************
 */

#include "jdy31.h"
#include <string.h>

extern UART_HandleTypeDef huart1;

/**
 * @brief  通过蓝牙透传发送字符串到远端设备
 * @param  str 要发送的字符串 (以 '\0' 结尾)
 * @note   直接调用 UART 发送, 数据原样透传
 */
void JDY31_SendString(char *str)
{
    HAL_UART_Transmit(&huart1, (uint8_t *)str, strlen(str), 100);
}

/**
 * @brief  发送 AT 指令到蓝牙模块
 * @param  cmd AT 指令内容 (不含 \r\n)
 * @note   自动追加 "\r\n" 作为 AT 指令结束符
 *         例如: JDY31_SendAT("AT") 发送 "AT\r\n"
 */
void JDY31_SendAT(char *cmd)
{
    HAL_UART_Transmit(&huart1, (uint8_t *)cmd, strlen(cmd), 100);
    HAL_UART_Transmit(&huart1, (uint8_t *)"\r\n", 2, 100);
}