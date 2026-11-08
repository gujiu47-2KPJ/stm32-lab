/**
 ******************************************************************************
 * @file    encode.h
 * @brief   旋转编码器驱动接口
 * @note    使用 TIM2 编码器模式 (CH1+CH2)
 *          CNT 自动加减计数, 通过比较前后值检测旋转方向和增量
 ******************************************************************************
 */

#ifndef __ENCODE_H
#define __ENCODE_H

#include "main.h"

/** @brief 编码器累计值上限 */
#define MAX_ENCODE 2
/**
 * @brief  初始化编码器 (启动 TIM2 编码器模式, 清零累计值)
 */
void Encode_Init(void);

/**
 * @brief  读取编码器累计增量 (仅累加正向, 上限 100)
 * @retval 编码器累计值 (0 ~ MAX_ENCODE)
 */
int16_t Encode_ReadDelta(void);

/**
 * @brief  获取编码器当前累计值
 * @retval 编码器累计值
 */
int16_t Encode_GetValue(void);

/**
 * @brief  设置编码器累计值 (范围裁剪到 0 ~ MAX_ENCODE)
 * @param  val 设置的值
 */
void Encode_SetValue(uint8_t val);

/**
 * @brief  编码器累计值清零
 */
void Encode_Reset(void);

/**
 * @brief  检测编码器是否被转动
 * @retval 1 转动过 (CNT 变化), 0 未动
 * @note   内部用 static 变量记录上一次 CNT 值
 */
uint8_t Encode_Moved(void);

#endif