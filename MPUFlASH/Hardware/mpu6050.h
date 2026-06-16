#ifndef __MPU6050_H__
#define __MPU6050_H__

#include "stm32f1xx_hal.h"
#include "main.h"  // 包含CubeMX生成的句柄声明

/* USER CODE BEGIN Includes */
#include <math.h>
/* USER CODE END Includes */

// MPU6050结构体定义（修复缺失）
typedef struct {
    float ax, ay, az;
    float gx, gy, gz;
    float roll, pitch, yaw;
    float temperature;
} MPU6050_Data;

// MPU6050 I2C地址
#define MPU6050_ADDR 0xD0  // 实际传输时右移1位，变成0x68

// MPU6050寄存器地址
#define MPU6050_PWR_MGMT_1    0x6B
#define MPU6050_ACCEL_XOUT_H  0x3B
#define MPU6050_ACCEL_XOUT_L  0x3C
#define MPU6050_ACCEL_YOUT_H  0x3D
#define MPU6050_ACCEL_YOUT_L  0x3E
#define MPU6050_ACCEL_ZOUT_H  0x3F
#define MPU6050_ACCEL_ZOUT_L  0x40
#define MPU6050_TEMP_OUT_H    0x41
#define MPU6050_TEMP_OUT_L    0x42
#define MPU6050_GYRO_XOUT_H   0x43
#define MPU6050_GYRO_XOUT_L   0x44
#define MPU6050_GYRO_YOUT_H   0x45
#define MPU6050_GYRO_YOUT_L   0x46
#define MPU6050_GYRO_ZOUT_H   0x47
#define MPU6050_GYRO_ZOUT_L   0x48

// 函数声明
void MPU6050_Init(void);
void MPU6050_GetData(int16_t* Accel_X, int16_t* Accel_Y, int16_t* Accel_Z, 
                     int16_t* Gyro_X, int16_t* Gyro_Y, int16_t* Gyro_Z);
void MPU6050_Calculate(MPU6050_Data *data);
void MPU6050_Update(MPU6050_Data *data);

/* USER CODE BEGIN 0 */
/* USER CODE END 0 */

#endif