#ifndef __TRACK_H
#define __TRACK_H
#include "stm32f10x.h"

// 5路传感器引脚定义（根据实际硬件调整）就是改变变量名称
#define TRACK_PIN_1     GPIO_Pin_8    // PB8 - 最左
#define TRACK_PIN_2     GPIO_Pin_6    // PB6 - 左中
#define TRACK_PIN_3     GPIO_Pin_7    // PB7 - 中心
#define TRACK_PIN_4     GPIO_Pin_15   // PB15 - 右中
#define TRACK_PIN_5     GPIO_Pin_9    // PB9 - 最右
#define TRACK_PORT      GPIOB

void Infrared_Init(void);
void Read_Track_Sensors(uint8_t* data); // 读取5路传感器状态

#endif
