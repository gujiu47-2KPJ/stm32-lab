#include "stm32f10x.h"                  // Device header



void Timer_Init(void)
{
	RCC_APB1PeriphClockCmd (RCC_APB1Periph_TIM2,ENABLE );
	RCC_APB2PeriphClockCmd (RCC_APB2Periph_GPIOA,ENABLE );
	
	GPIO_InitTypeDef  GPIO_InitStruture;
	GPIO_InitStruture.GPIO_Mode = GPIO_Mode_IPU;
	GPIO_InitStruture.GPIO_Pin = GPIO_Pin_0;
	GPIO_InitStruture.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOA,&GPIO_InitStruture);
	
	
	TIM_ETRClockMode2Config(TIM2,TIM_ExtTRGPSC_OFF,TIM_ExtTRGPolarity_NonInverted,0x00);
	
	TIM_TimeBaseInitTypeDef  TIM_TimeBaseInitStructure;
	 TIM_TimeBaseInitStructure.TIM_ClockDivision = TIM_CKD_DIV1 ;
	 TIM_TimeBaseInitStructure.TIM_CounterMode = TIM_CounterMode_Up;
	 TIM_TimeBaseInitStructure.TIM_Period = 10 - 1;
	 TIM_TimeBaseInitStructure.TIM_Prescaler =1 - 1;
	 TIM_TimeBaseInitStructure.TIM_RepetitionCounter = 0;
	 
	 TIM_TimeBaseInit(TIM2, & TIM_TimeBaseInitStructure);
	
	TIM_ClearFlag(TIM2,TIM_IT_Update);
	TIM_ITConfig (TIM2,TIM_IT_Update ,ENABLE );
	TIM_ETRClockMode2Config(TIM2, TIM_ExtTRGPSC_OFF, TIM_ExtTRGPolarity_NonInverted, 0xF); 
	 
	NVIC_PriorityGroupConfig (NVIC_PriorityGroup_2);
	
	NVIC_InitTypeDef NVIC_InitStructer;
	NVIC_InitStructer.NVIC_IRQChannel = TIM2_IRQn;
	NVIC_InitStructer.NVIC_IRQChannelCmd = ENABLE ;
	NVIC_InitStructer.NVIC_IRQChannelPreemptionPriority = 2 ;
	NVIC_InitStructer.NVIC_IRQChannelSubPriority = 1;
	NVIC_Init(&NVIC_InitStructer );
	
	TIM_Cmd (TIM2,ENABLE );
}	
uint16_t Timer_GetCounter(void)
{
	return TIM_GetCounter (TIM2);
}
	
/*
void TIM2_IRQHandler(void)
{
	if(TIM_GetITStatus(TIM2,TIM_IT_Update )==SET )
	{
		Num++;
		TIM_ClearITPendingBit(TIM2,TIM_IT_Update);
	}
}*/
