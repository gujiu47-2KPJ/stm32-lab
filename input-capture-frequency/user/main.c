#include "stm32f10x.h"
#include "Delay.h"
#include "OLED.h"
#include "PWM.h"
#include "IC.h"

uint8_t i;

int main(void)
{
    OLED_Init();
    PWM_Init();
    IC_Init ();
	
	OLED_ShowString (1,1,"Freq:00000Hz");
	
	PWM_SetPrescaler(720-1);   //Freq = 73M/(PSC+1)/100
	PWM_SetCompare1 (50);  //Duty = CCR /100
	
    while(1)
    {
       OLED_ShowNum(1,6,IC_GetFreq (),5);
   }
}
