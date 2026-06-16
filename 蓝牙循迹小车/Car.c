#include "stm32f10x.h"                  // Device header(指令库)
#include "Motor.h"
#include "Delay.h"
void Car_Init(void)
{
    Motor_Init();
}

void Go_Ahead(void)
{
    Motor_SetLeftSpeed(30);
    Motor_SetRightSpeed(30);
}

void Go_Back(void)
{
    Motor_SetLeftSpeed(-30);
    Motor_SetRightSpeed(-30);
}

void Turn_Left(void)
{
    Motor_SetLeftSpeed(10);
    Motor_SetRightSpeed(70);
}

void Turn_Right(void)
{
    Motor_SetLeftSpeed(70);
    Motor_SetRightSpeed(10);
}

void Self_Left(void)
{
    Motor_SetLeftSpeed(-50);
    Motor_SetRightSpeed(50);
}

void Self_Right(void)
{
    Motor_SetLeftSpeed(50);
    Motor_SetRightSpeed(-50);
}

void Car_Stop(void)
{
    Motor_SetLeftSpeed(0);
    Motor_SetRightSpeed(0);
}
