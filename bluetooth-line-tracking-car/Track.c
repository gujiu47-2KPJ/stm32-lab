#include "Track.h"
//循迹模块的驱动代码
void Infrared_Init(void)
{
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);//唤醒GPIOB的引脚
    
    GPIO_InitTypeDef GPIO_InitStruct;
    GPIO_InitStruct.GPIO_Pin = TRACK_PIN_1 | TRACK_PIN_2 | 
                               TRACK_PIN_3 | TRACK_PIN_4 | TRACK_PIN_5;//一次性配置五个引脚
    GPIO_InitStruct.GPIO_Mode = GPIO_Mode_IPD; // 下拉输入
    GPIO_Init(TRACK_PORT, &GPIO_InitStruct);
}

// 读取5路传感器状态（0=检测到黑线，1=白色）
void Read_Track_Sensors(uint8_t* data)
{
    data[0] = GPIO_ReadInputDataBit(TRACK_PORT, TRACK_PIN_1);
    data[1] = GPIO_ReadInputDataBit(TRACK_PORT, TRACK_PIN_2);
    data[2] = GPIO_ReadInputDataBit(TRACK_PORT, TRACK_PIN_3);
    data[3] = GPIO_ReadInputDataBit(TRACK_PORT, TRACK_PIN_4);
    data[4] = GPIO_ReadInputDataBit(TRACK_PORT, TRACK_PIN_5);
}
