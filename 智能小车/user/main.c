#include "stm32f10x.h"
#include "Delay.h"
#include "CAR.h"
#include "Track.h"
/*循迹小车分为俩部分:循迹模块和电机模块以及最后的主函数
循迹模块主要是根据循迹模块从GPIOB口通过循迹模块输入的电平来输出决策信息
电机模块主要是要层层包装成为较为方便使用的函数
先配置pwm函数为控制电机转动速度打下硬件基础
然后通过MORTOR函数实现分别对左右电机的宏观操控
最后使用CAR算法函数对小车的行动产生指令
主函数主要是通过循迹模块提供的决策信息来操纵小车电机模块的行动
*/
int main(void)
{ 
    Car_Init();
	
    Infrared_Init();
    
    uint8_t data[5];
    uint8_t s1, s2, s3, s4, s5;

    while (1)
    {
        // 读取5路传感器
        Read_Track_Sensors(data);
        s1 = data[0];  // 左外
        s2 = data[1];  // 左中
        s3 = data[2];  // 中心
        s4 = data[3];  // 右中
        s5 = data[4];  // 右外
if (s1 == 0 && s2 == 0 && s3 == 0 && s4 == 0 && s5 == 0)
{
   Go_Ahead();//直行
}
else if (s1 == 0 && s2 == 1 && s3 == 1 && s4 == 1 && s5 == 0) {
    Go_Ahead();//直行
}
else if (s1 == 0 && s2 == 0 && s3 == 1 && s4 == 0 && s5 == 0) {
	 Go_Ahead();//直行
}
/*else if (s1 == 0 && s2 == 1 && s3 == 1 && s4 == 0 && s5 == 0) {
    
     Go_Ahead();
}
else if (s1 == 0 && s2 == 0&& s3 == 1 && s4 == 1 && s5 == 0) {
    
     Go_Ahead();
}*/
else if (s1 == 0 && s2 == 1 && s3 == 0 && s4 == 0 && s5 == 0) {
    
     Go_Ahead();//直行
}
/*else if (s1 == 0 && s2 == 1 && s3 == 0 && s4 == 0 && s5 == 0) {
    
    Turn_Left();
}*/
else if (s1 == 1 && s2 == 1 && s3 == 0 && s4 == 0 && s5 == 0) {
    
     Turn_Left();
}
else if (s1 == 0&& s2 == 1 && s3 == 1 && s4 == 0 && s5 == 0) {
    
     Turn_Left();
}
else if (s1 == 0 && s2 == 0 && s3 == 0 && s4 == 1 && s5 == 1) {
     Turn_Right();
}
else if (s1 == 0 && s2 == 0 && s3 == 0 && s4 == 1 && s5 == 0) {
    Turn_Right();
}
else if (s1 == 0 && s2 == 0 && s3 == 1 && s4 == 1 && s5 == 0) {
    Turn_Right();
}
else if (s1 == 1 && s2 == 0 && s3 == 0 && s4 == 0 && s5 == 0) {
    Turn_Left();
}
else if (s1 == 1 && s2 == 1 && s3 == 1 && s4 == 0 && s5 == 0) {
    Turn_Left();
}
else if (s1 == 0 && s2 == 0 && s3 == 0 && s4 == 0 && s5 == 1) {
    Turn_Right();
}
else if (s1 == 1 && s2 == 1 && s3 == 0 && s4 == 0 && s5 == 0) {
   Turn_Left();
}
/*else if (s1 == 0 && s2 == 1 && s3 == 1 && s4 == 1 && s5 == 1) {
    // 黑线靠近右外，微调右转
    Self_Right();
}
else if (s1 == 1 && s2 == 1 && s3 == 1 && s4 == 1 && s5 == 0) {
    // 黑线靠近左外，微调左转
    Self_Left();
}
else if (s1 == 1 && s2 == 0 && s3 == 0 && s4 == 1 && s5 == 1) {
    // 黑线在中间偏左或偏右，继续前进
    Go_Ahead();
}*/
else {
    // 其他异常情况，停止
    Car_Stop();
}
     
        // 循迹逻辑
        /*if (s1 == 0 && s2 == 0 && s3 == 0 && s4 == 0 && s5 == 0)
        {
            Go_Ahead();
        }
        else if (s1 == 0 && s2 == 0 && s3 == 1 && s4 == 1 && s5 == 1)
        {
            Turn_Left();
        }
        else if (s1 == 1 && s2 == 1 && s3 == 1 && s4 == 0 && s5 == 0)
        {
            Turn_Right();
        }
        else if (s1 == 0 && s2 == 1 && s3 == 1 && s4 == 1 && s5 == 1)
        {
            Self_Right();
        }
        else if (s1 == 1 && s2 == 1 && s3 == 1 && s4 == 1 && s5 == 0)
        {
            Self_Left();
        }
        else if (s1 == 1 && s2 == 0 && s3 == 0 && s4 == 1 && s5 == 1)
        {
            Go_Ahead();
        }
        else
        {
            Car_Stop();
        }*/

        Delay_ms(5);  // 小延时去抖
    }
}
