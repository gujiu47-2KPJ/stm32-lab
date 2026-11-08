/**
 ******************************************************************************
 * @file    mpu6050.c
 * @brief   MPU6xxx 六轴惯性传感器驱动实现
 * @note    I2C 通信, 连续读取 14 字节寄存器块获取 6 轴数据
 *          寄存器映射:
 *           0x3B~0x40: ACCEL_X~ACCEL_Z (6 字节)
 *           0x41~0x42: TEMP         (2 字节, 本驱动跳过)
 *           0x43~0x48: GYRO_X~GYRO_Z (6 字节)
 *          0x6B PWR_MGMT_1: 写入 0x00 唤醒模块
 *          0x75 WHO_AM_I:   身份寄存器, 0x68/0x70/0x71
 ******************************************************************************
 */

#include "mpu6050.h"
#include "main.h"

extern I2C_HandleTypeDef hi2c1;

/**
 * @brief  通过 I2C 握手检测模块是否存在
 * @param  addr 传感器 7 位 I2C 地址
 * @retval 1 存在, 0 不存在
 * @note   不依赖 WHO_AM_I 寄存器, 兼容 MPU6050/MPU6500/MPU9250
 */
uint8_t MPU6050_Detect(uint8_t addr)
{
    if (HAL_I2C_IsDeviceReady(&hi2c1, addr << 1, 3, 100) != HAL_OK) {
        return 0;
    }
    return 1;
}

/**
 * @brief  唤醒传感器并设置为默认量程
 * @param  addr 传感器 7 位 I2C 地址
 * @note   寄存器 0x6B (PWR_MGMT_1) 写入 0x00
 *         默认量程: 加速度 ±2g / 陀螺仪 ±250°/s
 */
void MPU6050_Init(uint8_t addr)
{
    uint8_t buffer[1];
    buffer[0] = 0x00;
    HAL_I2C_Mem_Write(&hi2c1, addr << 1, 0x6B,
                      I2C_MEMADD_SIZE_8BIT, buffer, 1, 100);
    HAL_Delay(20);   /* 等待模块稳定 */
}

/**
 * @brief  从传感器连续读取 6 轴原始数据
 * @param  addr 传感器 7 位 I2C 地址
 * @param  ax   加速度 X 轴 (LSB)
 * @param  ay   加速度 Y 轴 (LSB)
 * @param  az   加速度 Z 轴 (LSB)
 * @param  gx   陀螺仪 X 轴 (LSB)
 * @param  gy   陀螺仪 Y 轴 (LSB)
 * @param  gz   陀螺仪 Z 轴 (LSB)
 * @note   从寄存器 0x3B 起连续读 14 字节
 *         每轴占 2 字节 (高字节在前, 大端序)
 *         跳过中间 2 字节温度数据 (buf[6], buf[7])
 */
void MPU6050_ReadAccel(uint8_t addr, int16_t *ax, int16_t *ay, int16_t *az,
                       int16_t *gx, int16_t *gy, int16_t *gz)
{
    uint8_t buf[14];
    HAL_I2C_Mem_Read(&hi2c1, addr << 1, 0x3B,
                     I2C_MEMADD_SIZE_8BIT, buf, 14, 100);

    /* 大端拼接: 高字节左移 8 位 | 低字节 */
    *ax = buf[0] << 8 | buf[1];
    *ay = buf[2] << 8 | buf[3];
    *az = buf[4] << 8 | buf[5];
    *gx = buf[8] << 8 | buf[9];
    *gy = buf[10] << 8 | buf[11];
    *gz = buf[12] << 8 | buf[13];
}