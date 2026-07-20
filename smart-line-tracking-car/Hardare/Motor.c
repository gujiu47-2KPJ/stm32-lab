#include "stm32f10x.h"//综合GPIO口和pwm信号驱动小车
#include "PWM.h"

void Motor_Init(void)
{
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);//唤醒总线上的的GPIOA引脚
    
    GPIO_InitTypeDef GPIO_InitStructure;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;//推挽输出模式
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_4 | GPIO_Pin_5 | GPIO_Pin_6 | GPIO_Pin_7;//配置电机驱动引脚
	//PA4和PA5是用来调整左电机方向
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);
    
    PWM_Init();//初始化pwm代码,应用pwm代码来对电机速度进行调整
}

void Motor_SetLeftSpeed(int8_t Speed)//左电机转速调整的逻辑代码
{
    if (Speed > 0)
    {
        // 正转(PA4为高电平,PA5为低电平)
        GPIO_SetBits(GPIOA, GPIO_Pin_4);
        GPIO_ResetBits(GPIOA, GPIO_Pin_5);
        PWM_SetCompare3(Speed);  // 左电机使用TIM2_CH3
    }
    else if (Speed == 0)
    {
        // 刹车
        GPIO_SetBits(GPIOA, GPIO_Pin_4);
        GPIO_SetBits(GPIOA, GPIO_Pin_5);
        PWM_SetCompare3(0);
    }
    else
    {
        // 反转
        GPIO_ResetBits(GPIOA, GPIO_Pin_4);
        GPIO_SetBits(GPIOA, GPIO_Pin_5);
        PWM_SetCompare3(-Speed);
    }
}

void Motor_SetRightSpeed(int8_t Speed)//与左电机的逻辑基本相同,除了使用的是PA6和PA7引脚6
	
{
    if (Speed > 0)
    {
        // 正转
        GPIO_SetBits(GPIOA, GPIO_Pin_6);
        GPIO_ResetBits(GPIOA, GPIO_Pin_7);
        PWM_SetCompare4(Speed);  // 右电机使用TIM2_CH4
    }
    else if (Speed == 0)
    {
        // 刹车
        GPIO_SetBits(GPIOA, GPIO_Pin_6);
        GPIO_SetBits(GPIOA, GPIO_Pin_7);
        PWM_SetCompare4(0);
    }
    else
    {
        // 反转
        GPIO_ResetBits(GPIOA, GPIO_Pin_6);
        GPIO_SetBits(GPIOA, GPIO_Pin_7);
        PWM_SetCompare4(-Speed);
    }
}
