/**
 ******************************************************************************
 * @file    mpu6050.h
 * @brief   MPU6xxx 六轴惯性传感器驱动接口
 * @note    兼容 MPU6050 / MPU6500 / MPU9250
 *          通信协议: I2C, 默认 I2C 地址 0x68(AD0=0) / 0x69(AD0=1)
 ******************************************************************************
 */

#ifndef  __MPU6050_H
#define __MPU6050_H

#include "stm32f1xx_hal.h"

/** @brief MPU 模块 1 默认 I2C 地址 (AD0 接 GND) */
#define MPU6050_ADDR1 0x68
/** @brief MPU 模块 2 默认 I2C 地址 (AD0 接 VCC) */
#define MPU6050_ADDR2 0x69

/**
 * @brief  检测指定 I2C 地址是否存在六轴模块
 * @param  addr 传感器 7 位 I2C 地址 (0x68 或 0x69)
 * @retval 1 存在, 0 不存在
 */
uint8_t MPU6050_Detect(uint8_t addr);

/**
 * @brief  初始化六轴传感器 (唤醒模块, 默认量程)
 * @param  addr 传感器 7 位 I2C 地址
 */
void MPU6050_Init(uint8_t addr);

/**
 * @brief  读取 6 轴原始 ADC 数据
 * @param  addr 传感器 7 位 I2C 地址
 * @param  ax   加速度 X 轴输出指针 (LSB, ±2g 量程下 16384 LSB/g)
 * @param  ay   加速度 Y 轴输出指针
 * @param  az   加速度 Z 轴输出指针
 * @param  gx   陀螺仪 X 轴输出指针 (LSB, ±250°/s 量程下 131 LSB/(°/s))
 * @param  gy   陀螺仪 Y 轴输出指针
 * @param  gz   陀螺仪 Z 轴输出指针
 */
void MPU6050_ReadAccel(uint8_t addr, int16_t *ax, int16_t *ay, int16_t *az,
                       int16_t *gx, int16_t *gy, int16_t *gz);

#endif