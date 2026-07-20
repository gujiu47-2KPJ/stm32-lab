#include "stm32f10x.h" 
#include "Delay.h" //Device header
#include "OLED.h"
#include "Serial.h"


int main(void)
{
	Key_Init ();  

	Serial_Init();
	OLED_ShowString(1,1,"TxPacket");
	OLED_ShowString(3,1,"RxPacket");
	
	Serial_TxPacket[0] = 0x01;
	Serial_TxPacket[1] = 0x02;
	Serial_TxPacket[2] = 0x03;
	Serial_TxPacket[3] = 0x04;
	
	while(1)
{
	
}

}
