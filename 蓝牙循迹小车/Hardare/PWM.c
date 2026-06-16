#include "stm32f10x.h"

void PWM_Init(void)
{
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);//开启APB1总线上的TIM2（定时器2）时钟。
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);//开启APB2总线上的GPIOA（通用输入输出端口A）时钟。
    //提前配置所需的通道和初始化pwm配置
    GPIO_InitTypeDef GPIO_InitStructure;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_2 | GPIO_Pin_3;  // PA2:TIM2_CH3, PA3:TIM2_CH4
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);
    
    TIM_InternalClockConfig(TIM2);//强制TIM2使用内部时钟（APB1总线时钟）。
    
    TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStructure;
    TIM_TimeBaseInitStructure.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseInitStructure.TIM_CounterMode = TIM_CounterMode_Up;//向上计数
    TIM_TimeBaseInitStructure.TIM_Period = 100 - 1;        // ARR
    TIM_TimeBaseInitStructure.TIM_Prescaler = 36 - 1;      // PSC
    TIM_TimeBaseInitStructure.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(TIM2, &TIM_TimeBaseInitStructure);
    
    // 配置TIM2_CH3 (PA2) - 左电机PWM,将配置写入TIM2的通道3，对应PA2引脚。
    TIM_OCInitTypeDef TIM_OCInitStructure;//定义一个结构体
    TIM_OCStructInit(&TIM_OCInitStructure);//初始化结构体成员
    TIM_OCInitStructure.TIM_OCMode = TIM_OCMode_PWM1;//设置pwm模式
    TIM_OCInitStructure.TIM_OCPolarity = TIM_OCPolarity_High;///设置有效电平
    TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Enable;//开启pwm通道
    TIM_OCInitStructure.TIM_Pulse = 0;//初始占空比
    TIM_OC3Init(TIM2, &TIM_OCInitStructure);//应用到TIM2CH3
    
    // 配置TIM2_CH4 (PA3) - 右电机PWM,复用结构体
    TIM_OCStructInit(&TIM_OCInitStructure);
    TIM_OCInitStructure.TIM_OCMode = TIM_OCMode_PWM1;
    TIM_OCInitStructure.TIM_OCPolarity = TIM_OCPolarity_High;
    TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Enable;
    TIM_OCInitStructure.TIM_Pulse = 0;
    TIM_OC4Init(TIM2, &TIM_OCInitStructure);
    
    TIM_Cmd(TIM2, ENABLE);//使能定时器TIM2
}

//声明设置操控占空比的函数
void PWM_SetCompare3(uint16_t Compare)
{
    TIM_SetCompare3(TIM2, Compare);
}

void PWM_SetCompare4(uint16_t Compare)
{
    TIM_SetCompare4(TIM2, Compare);
}
