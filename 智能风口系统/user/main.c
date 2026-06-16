#include "stm32f10x.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

// 引脚定义
#define SERVO_PIN GPIO_Pin_0        // PA0 - TIM2_CH1
#define MOTOR_AIN1_PIN GPIO_Pin_1   // PA1 
#define MOTOR_AIN2_PIN GPIO_Pin_2   // PA2
#define MOTOR_PWMA_PIN GPIO_Pin_2   // PA2 - TIM2_CH3
#define MOTOR_STBY_PIN GPIO_Pin_4   // PA4
#define GREEN_LED_PIN GPIO_Pin_6    // PA6 - TIM3_CH1
#define RED_LED_PIN GPIO_Pin_7      // PA7 - 普通GPIO
#define EMERGENCY_BTN_PIN GPIO_Pin_3 // PA3 - 输入 (修改为PA3)

#define SERVO_PORT GPIOA
#define MOTOR_PORT GPIOA
#define LED_PORT GPIOA
#define BTN_PORT GPIOA

// 系统参数
#define MAX_PASSWORD_ATTEMPTS 3
#define LOCK_DURATION 10000        // 10秒锁机
#define PASSWORD_LENGTH 8

// LED亮度参数
#define GREEN_MAX_BRIGHTNESS 90    // 最大亮度（0-99）
#define GREEN_MIN_BRIGHTNESS 5     // 最小亮度
#define MOTOR_MAX_SPEED 80         // 电机最大速度

// 系统状态
typedef enum {
    STATE_CLOSED,
    STATE_OPEN,
    STATE_LOCKED,
    STATE_EMERGENCY
} SystemState;

// 全局变量
volatile SystemState current_state = STATE_CLOSED;
volatile uint8_t wrong_password_count = 0;
volatile bool emergency_stop = false;
volatile uint32_t lock_timer = 0;
volatile uint32_t system_tick = 0;
char correct_password[] = "87654321";  // 8位密码

// 函数声明
void System_Initialization(void);
void GPIO_Configuration(void);
void USART_Configuration(void);
void TIM_Configuration(void);
void NVIC_Configuration(void);
void Delay_ms(uint32_t ms);

void Servo_SetAngle(uint16_t angle);
void Motor_Forward(uint8_t speed);
void Motor_Backward(uint8_t speed);
void Motor_Stop(void);
void Motor_Coast(void);
void Motor_Forward_Safe(uint8_t speed);
void Motor_Backward_Safe(uint8_t speed);
void GreenLED_Breathing(void);
void GreenLED_On(void);
void GreenLED_Off(void);
void GreenLED_Blink(uint8_t times);
void RedLED_On(void);
void RedLED_Off(void);
void RedLED_Blink(uint8_t times);
void Process_UART_Command(char* command);
void Send_Status_Message(const char* message);
void System_Lock(uint32_t duration);
void Emergency_Stop(void);
void Emergency_Recovery(void);
void Check_Emergency_Button(void);
void Update_System_Status(void);
void System_Tick_Handler(void);

// PWM函数
void PWM_Motor_Init(void);
void PWM_SetCompare3(uint16_t Compare);
void PWM_LED_Init(void);
void PWM_LED_SetCompare1(uint16_t Compare);

// 简单延时函数
void Delay_ms(uint32_t ms) {
    volatile uint32_t i, j;
    for (i = 0; i < ms; i++) {
        for (j = 0; j < 7200; j++);
    }
}

// 串口发送函数
void USART_SendString(USART_TypeDef* USARTx, char* str) {
    while (*str) {
        while (USART_GetFlagStatus(USARTx, USART_FLAG_TXE) == RESET);
        USART_SendData(USARTx, *str++);
    }
}

// 电机PWM初始化 (TIM2_CH3 - PA2)
void PWM_Motor_Init(void) {
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

    // GPIO初始化 - PA2 for TIM2_CH3
    GPIO_InitTypeDef GPIO_InitStructure;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_2;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);
    
    // 定时器时基配置
    TIM_InternalClockConfig(TIM2);
    
    TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStructure;
    TIM_TimeBaseInitStructure.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseInitStructure.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInitStructure.TIM_Period = 100 - 1;      // ARR = 99
    TIM_TimeBaseInitStructure.TIM_Prescaler = 720 - 1;   // PSC = 719
    TIM_TimeBaseInitStructure.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(TIM2, &TIM_TimeBaseInitStructure);
    
    // 输出比较配置 - 通道3 for 电机PWM
    TIM_OCInitTypeDef TIM_OCInitStructure;
    TIM_OCStructInit(&TIM_OCInitStructure);
    TIM_OCInitStructure.TIM_OCMode = TIM_OCMode_PWM1;
    TIM_OCInitStructure.TIM_OCPolarity = TIM_OCPolarity_High;
    TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Enable;
    TIM_OCInitStructure.TIM_Pulse = 0;  // CCR初始为0
    TIM_OC3Init(TIM2, &TIM_OCInitStructure);
    
    TIM_Cmd(TIM2, ENABLE);
}

void PWM_SetCompare3(uint16_t Compare) {
   TIM_SetCompare3(TIM2, Compare);
}

// LED PWM初始化 (TIM3_CH1 - PA6)
void PWM_LED_Init(void) {
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

    // GPIO初始化 - PA6 for TIM3_CH1
    GPIO_InitTypeDef GPIO_InitStructure;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_6;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);
    
    // 定时器时基配置
    TIM_InternalClockConfig(TIM3);
    
    TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStructure;
    TIM_TimeBaseInitStructure.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseInitStructure.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInitStructure.TIM_Period = 100 - 1;      // ARR = 99
    TIM_TimeBaseInitStructure.TIM_Prescaler = 720 - 1;   // PSC = 719
    TIM_TimeBaseInitStructure.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(TIM3, &TIM_TimeBaseInitStructure);
    
    // 输出比较配置 - 通道1 for LED PWM
    TIM_OCInitTypeDef TIM_OCInitStructure;
    TIM_OCStructInit(&TIM_OCInitStructure);
    TIM_OCInitStructure.TIM_OCMode = TIM_OCMode_PWM1;
    TIM_OCInitStructure.TIM_OCPolarity = TIM_OCPolarity_High;
    TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Enable;
    TIM_OCInitStructure.TIM_Pulse = GREEN_MAX_BRIGHTNESS;  // 初始为最大亮度
    TIM_OC1Init(TIM3, &TIM_OCInitStructure);
    
    TIM_Cmd(TIM3, ENABLE);
}

void PWM_LED_SetCompare1(uint16_t Compare) {
   TIM_SetCompare1(TIM3, Compare);
}

// 系统初始化
void System_Initialization(void) {
    GPIO_Configuration();
    USART_Configuration();
    TIM_Configuration();
    NVIC_Configuration();
    
    // 初始状态：风门关闭，电机停止，绿灯常亮
    Servo_SetAngle(0);      // 风门关闭
    Motor_Coast();          // 电机停止
    GreenLED_On();          // 绿灯常亮
    RedLED_Off();           // 红灯关闭
    
    Send_Status_Message("System initialized - Door CLOSED, Green LED ON, Motor STOP");
}

// GPIO配置
void GPIO_Configuration(void) {
    GPIO_InitTypeDef GPIO_InitStructure;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    
    // 舵机PWM (PA0) - TIM2_CH1
    GPIO_InitStructure.GPIO_Pin = SERVO_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);
    
    // 电机控制 (PA1, PA4) - PA2已在PWM初始化中配置
    GPIO_InitStructure.GPIO_Pin = MOTOR_AIN1_PIN | MOTOR_STBY_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);
    
    // 红色LED (PA7) - 普通GPIO输出
    GPIO_InitStructure.GPIO_Pin = RED_LED_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);
    
    // 紧急按钮 (PA3) - 输入下拉 (按下时接到3.3V)
    GPIO_InitStructure.GPIO_Pin = EMERGENCY_BTN_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPD;  // 输入下拉
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);
}

// USART配置
void USART_Configuration(void) {
    USART_InitTypeDef USART_InitStructure;
    GPIO_InitTypeDef GPIO_InitStructure;
    
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1 | RCC_APB2Periph_GPIOA, ENABLE);
    
    // USART1 TX (PA9)
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOA, &GPIO_InitStructure);
    
    // USART1 RX (PA10)
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &GPIO_InitStructure);
    
    USART_InitStructure.USART_BaudRate = 115200;
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    USART_InitStructure.USART_Parity = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
    
    USART_Init(USART1, &USART_InitStructure);
    USART_Cmd(USART1, ENABLE);
    USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);
}

// 定时器配置
void TIM_Configuration(void) {
    TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure;
    TIM_OCInitTypeDef TIM_OCInitStructure;
    
    // 定时器2用于舵机PWM (50Hz)
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);
    
    // TIM2基础配置 - 20ms周期(50Hz) for 舵机
    TIM_TimeBaseStructure.TIM_Period = 19999;
    TIM_TimeBaseStructure.TIM_Prescaler = 71;
    TIM_TimeBaseStructure.TIM_ClockDivision = 0;
    TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM2, &TIM_TimeBaseStructure);
    
    // 通道1 - 舵机PWM (PA0)
    TIM_OCInitStructure.TIM_OCMode = TIM_OCMode_PWM1;
    TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Enable;
    TIM_OCInitStructure.TIM_OCPolarity = TIM_OCPolarity_High;
    TIM_OCInitStructure.TIM_Pulse = 500;  // 初始0度
    TIM_OC1Init(TIM2, &TIM_OCInitStructure);
    TIM_OC1PreloadConfig(TIM2, TIM_OCPreload_Enable);
    
    TIM_ARRPreloadConfig(TIM2, ENABLE);
    TIM_Cmd(TIM2, ENABLE);
    
    // 初始化电机PWM和LED PWM
    PWM_Motor_Init();
    PWM_LED_Init();
    
    // 配置TIM3中断用于系统滴答
    TIM_ITConfig(TIM3, TIM_IT_Update, ENABLE);
}

// NVIC配置
void NVIC_Configuration(void) {
    NVIC_InitTypeDef NVIC_InitStructure;
    
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
    
    // USART1中断
    NVIC_InitStructure.NVIC_IRQChannel = USART1_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);
    
    // 定时器3中断
    NVIC_InitStructure.NVIC_IRQChannel = TIM3_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);
}

// 舵机角度设置
void Servo_SetAngle(uint16_t angle) {
    uint16_t pulse_width = 500 + (angle * 2000 / 180); // 0.5ms to 2.5ms
    TIM_SetCompare1(TIM2, pulse_width);
}

// 电机控制函数
void Motor_Forward(uint8_t speed) {
    GPIO_SetBits(MOTOR_PORT, MOTOR_STBY_PIN);
    GPIO_SetBits(MOTOR_PORT, MOTOR_AIN1_PIN);
    GPIO_ResetBits(MOTOR_PORT, MOTOR_AIN2_PIN);
    PWM_SetCompare3(speed);
}

void Motor_Backward(uint8_t speed) {
    GPIO_SetBits(MOTOR_PORT, MOTOR_STBY_PIN);
    GPIO_ResetBits(MOTOR_PORT, MOTOR_AIN1_PIN);
    GPIO_SetBits(MOTOR_PORT, MOTOR_AIN2_PIN);
    PWM_SetCompare3(speed);
}

void Motor_Stop(void) {
    GPIO_SetBits(MOTOR_PORT, MOTOR_AIN1_PIN);
    GPIO_SetBits(MOTOR_PORT, MOTOR_AIN2_PIN);
    PWM_SetCompare3(99);
}

void Motor_Coast(void) {
    GPIO_ResetBits(MOTOR_PORT, MOTOR_AIN1_PIN);
    GPIO_ResetBits(MOTOR_PORT, MOTOR_AIN2_PIN);
    GPIO_ResetBits(MOTOR_PORT, MOTOR_STBY_PIN);
    PWM_SetCompare3(0);
}

void Motor_Forward_Safe(uint8_t speed) {
    GPIO_SetBits(MOTOR_PORT, MOTOR_STBY_PIN);
    Delay_ms(50);
    GPIO_SetBits(MOTOR_PORT, MOTOR_AIN1_PIN);
    GPIO_ResetBits(MOTOR_PORT, MOTOR_AIN2_PIN);
    Delay_ms(50);
    
    for (int i = 0; i < speed; i += 5) {
        PWM_SetCompare3(i);
        Delay_ms(20);
    }
}

void Motor_Backward_Safe(uint8_t speed) {
    GPIO_SetBits(MOTOR_PORT, MOTOR_STBY_PIN);
    Delay_ms(50);
    GPIO_ResetBits(MOTOR_PORT, MOTOR_AIN1_PIN);
    GPIO_SetBits(MOTOR_PORT, MOTOR_AIN2_PIN);
    Delay_ms(50);
    
    for (int i = 0; i < speed; i += 5) {
        PWM_SetCompare3(i);
        Delay_ms(20);
    }
}

// LED控制函数
void GreenLED_On(void) {
    PWM_LED_SetCompare1(GREEN_MAX_BRIGHTNESS);
}

void GreenLED_Off(void) {
    PWM_LED_SetCompare1(0);
}

void GreenLED_Blink(uint8_t times) {
    for (uint8_t i = 0; i < times; i++) {
        GreenLED_Off();
        Delay_ms(200);
        GreenLED_On();
        Delay_ms(200);
    }
}

void RedLED_On(void) {
    GPIO_SetBits(LED_PORT, RED_LED_PIN);
}

void RedLED_Off(void) {
    GPIO_ResetBits(LED_PORT, RED_LED_PIN);
}

void RedLED_Blink(uint8_t times) {
    for (uint8_t i = 0; i < times; i++) {
        RedLED_On();
        Delay_ms(200);
        RedLED_Off();
        Delay_ms(200);
    }
}

// 呼吸灯效果
void GreenLED_Breathing(void) {
    static uint16_t brightness = GREEN_MIN_BRIGHTNESS;
    static bool increasing = true;
    static uint32_t last_breath_time = 0;
    
    if (system_tick - last_breath_time > 30) {
        if (increasing) {
            brightness += 2;
            if (brightness >= GREEN_MAX_BRIGHTNESS) {
                brightness = GREEN_MAX_BRIGHTNESS;
                increasing = false;
            }
        } else {
            brightness -= 2;
            if (brightness <= GREEN_MIN_BRIGHTNESS) {
                brightness = GREEN_MIN_BRIGHTNESS;
                increasing = true;
            }
        }
        PWM_LED_SetCompare1(brightness);
        last_breath_time = system_tick;
    }
}

// 发送状态信息
void Send_Status_Message(const char* message) {
    char buffer[100];
    sprintf(buffer, "[STATUS] %s\r\n", message);
    USART_SendString(USART1, buffer);
}

// 处理串口命令
void Process_UART_Command(char* command) {
    // 检查是否在锁机状态
    if (current_state == STATE_LOCKED) {
        Send_Status_Message("System LOCKED - Please wait");
        return;
    }
    
    // 检查紧急停止状态
    if (emergency_stop) {
        Send_Status_Message("Emergency stop active - Use RECOVER command");
        return;
    }
    
    // 密码命令处理 (A密码E格式)
    if (command[0] == 'A' && command[strlen(command)-1] == 'E') {
        char password[20];
        strncpy(password, command + 1, strlen(command) - 2);
        password[strlen(command) - 2] = '\0';
        
        if (strcmp(password, correct_password) == 0) {
            // 密码正确
            Send_Status_Message("Password CORRECT!");
            wrong_password_count = 0;
            
            // 绿灯快速闪烁三下
            GreenLED_Blink(3);
            
            // 打开风门，电机正转进风
            current_state = STATE_OPEN;
            Servo_SetAngle(180);  // 风门打开
            Motor_Forward_Safe(MOTOR_MAX_SPEED);  // 电机正转进风
            
            Send_Status_Message("Door OPEN, Motor FORWARD (Air IN), Green LED Breathing");
        } else {
            // 密码错误
            Send_Status_Message("Password WRONG!");
            wrong_password_count++;
            
            // 绿灯熄灭，红灯快速闪烁三下
            GreenLED_Off();
            RedLED_Blink(3);
            
            // 关闭风门，电机反转出风
            current_state = STATE_CLOSED;
            Servo_SetAngle(0);  // 风门关闭
            Motor_Backward_Safe(MOTOR_MAX_SPEED);  // 电机反转出风
            Delay_ms(1000);  // 反转1秒
            Motor_Coast();   // 停止电机
            
            // 恢复绿灯常亮
            GreenLED_On();
            
            Send_Status_Message("Door CLOSED, Motor REVERSE (Air OUT), Red LED Blink");
            
            // 检查是否达到错误次数限制
            if (wrong_password_count >= MAX_PASSWORD_ATTEMPTS) {
                System_Lock(LOCK_DURATION);
            }
        }
        return;
    }
    
    // 恢复命令
    if (strcmp(command, "RECOVER") == 0) {
        Emergency_Recovery();
        return;
    }
    
    // 测试命令
    if (strcmp(command, "TEST") == 0) {
        Send_Status_Message("System test - Current state info");
        switch(current_state) {
            case STATE_CLOSED: Send_Status_Message("State: CLOSED"); break;
            case STATE_OPEN: Send_Status_Message("State: OPEN"); break;
            case STATE_LOCKED: Send_Status_Message("State: LOCKED"); break;
            case STATE_EMERGENCY: Send_Status_Message("State: EMERGENCY"); break;
        }
        return;
    }
    
    Send_Status_Message("ERROR: Unknown command format. Use: ApasswordE");
}

// 系统锁机
void System_Lock(uint32_t duration) {
    current_state = STATE_LOCKED;
    lock_timer = duration / 1000;  // 转换为秒
    
    // 停止所有设备
    Motor_Coast();
    // 风门保持当前状态
    GreenLED_Off();
    RedLED_On();  // 红灯常亮
    
    Send_Status_Message("SYSTEM LOCKED for 10 seconds - Too many wrong attempts!");
}

// 紧急停止
void Emergency_Stop(void) {
    if (current_state != STATE_LOCKED) {
        emergency_stop = true;
        current_state = STATE_EMERGENCY;
        
        // 停止电机，保持风门状态，关闭所有LED
        Motor_Coast();
        GreenLED_Off();
        RedLED_Off();
        
        Send_Status_Message("EMERGENCY STOP - All devices stopped");
    }
}

// 紧急恢复
void Emergency_Recovery(void) {
    if (emergency_stop) {
        emergency_stop = false;
        current_state = STATE_CLOSED;
        
        // 恢复到关闭状态
        Servo_SetAngle(0);
        Motor_Coast();
        GreenLED_On();
        RedLED_Off();
        
        Send_Status_Message("System RECOVERED - Back to normal operation");
    } else {
        Send_Status_Message("No emergency stop active");
    }
}

// 检查紧急按钮
void Check_Emergency_Button(void) {
    static uint32_t last_check = 0;
    static bool last_state = false;
    
    if (system_tick - last_check > 50) {
        bool current_btn_state = GPIO_ReadInputDataBit(BTN_PORT, EMERGENCY_BTN_PIN);
        if (current_btn_state && !last_state) {  // 按下按钮（从低到高）
            Emergency_Stop();
        }
        last_state = current_btn_state;
        last_check = system_tick;
    }
}

// 更新系统状态
void Update_System_Status(void) {
    static uint32_t last_status_update = 0;
    
    // 状态特定的LED控制
    switch(current_state) {
        case STATE_OPEN:
            GreenLED_Breathing();  // 呼吸灯效果
            RedLED_Off();
            break;
        case STATE_CLOSED:
            GreenLED_On();  // 绿灯常亮
            RedLED_Off();
            break;
        case STATE_LOCKED:
            GreenLED_Off();
            RedLED_On();  // 红灯常亮
            break;
        case STATE_EMERGENCY:
            GreenLED_Off();
            RedLED_Off();  // 所有灯灭
            break;
    }
    
    // 锁机计时器
    if (current_state == STATE_LOCKED) {
        if (lock_timer > 0) {
            lock_timer--;
        } else {
            current_state = STATE_CLOSED;
            wrong_password_count = 0;
            emergency_stop = false;
            RedLED_Off();
            GreenLED_On();
            Send_Status_Message("System UNLOCKED - Ready for commands");
        }
    }
}

// 系统滴答处理
void System_Tick_Handler(void) {
    system_tick++;
    Check_Emergency_Button();
    Update_System_Status();
}

// USART1中断处理
void USART1_IRQHandler(void) {
    static char rx_buffer[50];
    static uint8_t rx_index = 0;
    
    if (USART_GetITStatus(USART1, USART_IT_RXNE) != RESET) {
        char received_char = USART_ReceiveData(USART1);
        
        if (received_char == '\r' || received_char == '\n') {
            if (rx_index > 0) {
                rx_buffer[rx_index] = '\0';
                Process_UART_Command(rx_buffer);
                rx_index = 0;
            }
        } else if (rx_index < sizeof(rx_buffer) - 1) {
            rx_buffer[rx_index++] = received_char;
        } else {
            rx_index = 0;  // 缓冲区溢出，重置
        }
        
        USART_ClearITPendingBit(USART1, USART_IT_RXNE);
    }
}

// 定时器3中断
void TIM3_IRQHandler(void) {
    if (TIM_GetITStatus(TIM3, TIM_IT_Update) != RESET) {
        System_Tick_Handler();
        TIM_ClearITPendingBit(TIM3, TIM_IT_Update);
    }
}

// 主函数
int main(void) {
    SystemInit();
    System_Initialization();
    
    Send_Status_Message("=== SMART VENT SYSTEM ===");
    Send_Status_Message("Password format: A87654321E");
    Send_Status_Message("Default state: Door CLOSED, Green LED ON, Motor STOP");
    Send_Status_Message("Correct password: Door OPEN, Motor FORWARD, Green LED Breathing");
    Send_Status_Message("Wrong password: Door CLOSED, Motor REVERSE, Red LED Blink");
    Send_Status_Message("3 wrong attempts: System LOCKED 10s, Red LED ON");
    Send_Status_Message("Emergency button: Stops all devices");
    Send_Status_Message("System READY");
    
    while (1) {
        __WFI();  // 等待中断
    }
}
