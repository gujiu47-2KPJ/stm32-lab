#include "mpu6050.h"
#include "OLED.h"

/* USER CODE BEGIN Includes */
#include <math.h>
/* USER CODE END Includes */

void MPU6050_Init(void) {
    uint8_t buf = 0x00;
    HAL_I2C_Mem_Write(&hi2c1, MPU6050_ADDR, 0x6B, 1, &buf, 1, 100);
    buf = 0x00;
    HAL_I2C_Mem_Write(&hi2c1, MPU6050_ADDR, 0x1C, 1, &buf, 1, 100);
    buf = 0x00;
    HAL_I2C_Mem_Write(&hi2c1, MPU6050_ADDR, 0x1B, 1, &buf, 1, 100);
}

void MPU6050_GetData(int16_t* Accel_X, int16_t* Accel_Y, int16_t* Accel_Z, 
                     int16_t* Gyro_X, int16_t* Gyro_Y, int16_t* Gyro_Z) {
    uint8_t data[14];
    HAL_I2C_Mem_Read(&hi2c1, MPU6050_ADDR, 0x3B, 1, data, 14, 100);
    
    *Accel_X = (int16_t)(data[0] << 8 | data[1]);
    *Accel_Y = (int16_t)(data[2] << 8 | data[3]);
    *Accel_Z = (int16_t)(data[4] << 8 | data[5]);
    *Gyro_X = (int16_t)(data[8] << 8 | data[9]);
    *Gyro_Y = (int16_t)(data[10] << 8 | data[11]);
    *Gyro_Z = (int16_t)(data[12] << 8 | data[13]);
}

void MPU6050_Calculate(MPU6050_Data *data) {
    int16_t ax, ay, az, gx, gy, gz;
    MPU6050_GetData(&ax, &ay, &az, &gx, &gy, &gz);
    
    data->ax = ax / 16384.0f;  // ±2g范围
    data->ay = ay / 16384.0f;
    data->az = az / 16384.0f;
    data->gx = gx / 131.0f;    // ±250°/s范围
    data->gy = gy / 131.0f;
    data->gz = gz / 131.0f;
    
    // 简单的角度计算（实际应用中需要卡尔曼滤波）
    data->pitch = atan2f(data->ax, sqrtf(data->ay * data->ay + data->az * data->az)) * 180.0f / M_PI;
    data->roll = atan2f(data->ay, data->az) * 180.0f / M_PI;
}

void MPU6050_Update(MPU6050_Data *data) {
    MPU6050_Calculate(data);
}

/* USER CODE BEGIN 0 */
/* USER CODE END 0 */