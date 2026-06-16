#include "stm32f10x.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

// 引脚定义
#define SERVO_PIN GPIO_Pin_0        // PA0 - TIM2_CH1
#define MOTOR_AIN1_PIN GPIO_Pin_1   // PA1 
#define MOTOR_AIN2_PIN GPIO_Pin_2   // PA2
#define MOTOR_PWMA_PIN GPIO_Pin_3   // PA3 - TIM2_CH4
#define MOTOR_STBY_PIN GPIO_Pin_4   // PA4
#define GREEN_LED_PIN GPIO_Pin_6    // PA6 - TIM3_CH1
#define RED_LED_PIN GPIO_Pin_7      // PA7 - 普通GPIO
#define EMERGENCY_BTN_PIN GPIO_Pin_8 // PA8 - 输入

#define SERVO_PORT GPIOA
#define MOTOR_PORT GPIOA
#define LED_PORT GPIOA
#define BTN_PORT GPIOA

// 系统参数
#define MAX_PASSWORD_ATTEMPTS 3
#define LOCK_DURATION 30000
#define PASSWORD_LENGTH 8

// LED亮度参数
#define GREEN_MAX_BRIGHTNESS 80    // 最大亮度（0-255）
#define GREEN_MIN_BRIGHTNESS 5     // 最小亮度
#define RED_OFF_LEVEL 0            // 红灯完全关闭

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
char correct_password[] = "87654321";

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

// 系统初始化
void System_Initialization(void) {
    GPIO_Configuration();
    USART_Configuration();
    TIM_Configuration();
    NVIC_Configuration();
    
    Servo_SetAngle(0);
    Motor_Coast();
    GreenLED_On();
    RedLED_Off();  // 确保红灯完全关闭
    
    Send_Status_Message("System initialized - Door CLOSED, Green LED ON, Motor STOP");
}

// GPIO配置
void GPIO_Configuration(void) {
    GPIO_InitTypeDef GPIO_InitStructure;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    
    // 舵机PWM (PA0)
    GPIO_InitStructure.GPIO_Pin = SERVO_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);
    
    // 电机控制 (PA1, PA2, PA3, PA4)
    GPIO_InitStructure.GPIO_Pin = MOTOR_AIN1_PIN | MOTOR_AIN2_PIN | MOTOR_PWMA_PIN | MOTOR_STBY_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);
    
    // 绿色LED PWM (PA6)
    GPIO_InitStructure.GPIO_Pin = GREEN_LED_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);
    
    // 红色LED (PA7) - 确保配置为推挽输出
    GPIO_InitStructure.GPIO_Pin = RED_LED_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);
    
    // 紧急按钮 (PA8)
    GPIO_InitStructure.GPIO_Pin = EMERGENCY_BTN_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
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

// 定时器配置 - 优化版
void TIM_Configuration(void) {
    TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure;
    TIM_OCInitTypeDef TIM_OCInitStructure;
    
    // 定时器2用于舵机和电机PWM
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);
    
    // TIM2基础配置 - 20ms周期(50Hz)
    TIM_TimeBaseStructure.TIM_Period = 19999;
    TIM_TimeBaseStructure.TIM_Prescaler = 71;
    TIM_TimeBaseStructure.TIM_ClockDivision = 0;
    TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM2, &TIM_TimeBaseStructure);
    
    // 通道1 - 舵机PWM (PA0)
    TIM_OCInitStructure.TIM_OCMode = TIM_OCMode_PWM1;
    TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Enable;
    TIM_OCInitStructure.TIM_OCPolarity = TIM_OCPolarity_High;
    TIM_OCInitStructure.TIM_Pulse = 500;
    TIM_OC1Init(TIM2, &TIM_OCInitStructure);
    TIM_OC1PreloadConfig(TIM2, TIM_OCPreload_Enable);
    
    // 通道4 - 电机PWM (PA3)
    TIM_OCInitStructure.TIM_Pulse = 0;
    TIM_OC4Init(TIM2, &TIM_OCInitStructure);
    TIM_OC4PreloadConfig(TIM2, TIM_OCPreload_Enable);
    
    TIM_ARRPreloadConfig(TIM2, ENABLE);
    TIM_Cmd(TIM2, ENABLE);
    
    // 定时器3用于系统滴答和LED呼吸灯 - 优化配置
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);
    
    // TIM3配置 - 适合呼吸灯的PWM频率
    TIM_TimeBaseStructure.TIM_Period = 255;        // 8位分辨率
    TIM_TimeBaseStructure.TIM_Prescaler = 279;     // 72MHz/280=257kHz, 257kHz/256≈1kHz PWM
    TIM_TimeBaseStructure.TIM_ClockDivision = 0;
    TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM3, &TIM_TimeBaseStructure);
    
    // 通道1 - 绿色LED呼吸灯PWM (PA6)
    TIM_OCInitStructure.TIM_OCMode = TIM_OCMode_PWM1;
    TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Enable;
    TIM_OCInitStructure.TIM_OCPolarity = TIM_OCPolarity_High;
    TIM_OCInitStructure.TIM_Pulse = GREEN_MAX_BRIGHTNESS / 2;  // 初始中等亮度
    TIM_OC1Init(TIM3, &TIM_OCInitStructure);
    TIM_OC1PreloadConfig(TIM3, TIM_OCPreload_Enable);
    
    TIM_ARRPreloadConfig(TIM3, ENABLE);
    TIM_ITConfig(TIM3, TIM_IT_Update, ENABLE);
    TIM_Cmd(TIM3, ENABLE);
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
    uint16_t pulse_width = 500 + (angle * 2000 / 180);
    TIM_SetCompare1(TIM2, pulse_width);
}

// 电机控制函数
void Motor_Forward(uint8_t speed) {
    GPIO_SetBits(MOTOR_PORT, MOTOR_STBY_PIN);
    GPIO_SetBits(MOTOR_PORT, MOTOR_AIN1_PIN);
    GPIO_ResetBits(MOTOR_PORT, MOTOR_AIN2_PIN);
    TIM_SetCompare4(TIM2, speed);
}

void Motor_Backward(uint8_t speed) {
    GPIO_SetBits(MOTOR_PORT, MOTOR_STBY_PIN);
    GPIO_ResetBits(MOTOR_PORT, MOTOR_AIN1_PIN);
    GPIO_SetBits(MOTOR_PORT, MOTOR_AIN2_PIN);
    TIM_SetCompare4(TIM2, speed);
}

void Motor_Stop(void) {
    GPIO_SetBits(MOTOR_PORT, MOTOR_AIN1_PIN);
    GPIO_SetBits(MOTOR_PORT, MOTOR_AIN2_PIN);
    TIM_SetCompare4(TIM2, 255);
}

void Motor_Coast(void) {
    GPIO_ResetBits(MOTOR_PORT, MOTOR_AIN1_PIN);
    GPIO_ResetBits(MOTOR_PORT, MOTOR_AIN2_PIN);
    GPIO_ResetBits(MOTOR_PORT, MOTOR_STBY_PIN);
    TIM_SetCompare4(TIM2, 0);
}

// 安全电机启动
void Motor_Forward_Safe(uint8_t speed) {
    Send_Status_Message("Safe motor start - preventing reset");
    
    // 分步启动避免电流冲击
    GPIO_SetBits(MOTOR_PORT, MOTOR_STBY_PIN);
    Delay_ms(100);
    
    GPIO_SetBits(MOTOR_PORT, MOTOR_AIN1_PIN);
    GPIO_ResetBits(MOTOR_PORT, MOTOR_AIN2_PIN);
    Delay_ms(100);
    
    // 缓慢加速
    for (int i = 0; i < speed; i += 10) {
        TIM_SetCompare4(TIM2, i);
        Delay_ms(50);
    }
    Send_Status_Message("Motor safely started");
}

// LED控制函数 - 优化版
void GreenLED_On(void) {
    TIM_SetCompare1(TIM3, GREEN_MAX_BRIGHTNESS);  // 使用限制的最大亮度
}

void GreenLED_Off(void) {
    TIM_SetCompare1(TIM3, 0);
}

void GreenLED_Blink(uint8_t times) {
    for (uint8_t i = 0; i < times; i++) {
        GreenLED_Off();
        Delay_ms(400);  // 延长关闭时间，让呼吸效果更明显
        GreenLED_On();
        Delay_ms(400);
    }
}

void RedLED_On(void) {
    GPIO_SetBits(LED_PORT, RED_LED_PIN);
}

void RedLED_Off(void) {
    GPIO_ResetBits(LED_PORT, RED_LED_PIN);  // 确保完全关闭
}

void RedLED_Blink(uint8_t times) {
    for (uint8_t i = 0; i < times; i++) {
        RedLED_On();
        Delay_ms(300);
        RedLED_Off();
        Delay_ms(300);
    }
}

// 呼吸灯效果 - 优化版（更明显的呼吸效果）
void GreenLED_Breathing(void) {
    static uint8_t brightness = GREEN_MIN_BRIGHTNESS;
    static bool increasing = true;
    static uint32_t last_breath_time = 0;
    
    // 每50ms更新，更明显的呼吸效果
    if (system_tick - last_breath_time > 50) {
        if (increasing) {
            brightness += 4;  // 加快变化速度
            if (brightness >= GREEN_MAX_BRIGHTNESS) {
                brightness = GREEN_MAX_BRIGHTNESS;
                increasing = false;
            }
        } else {
            brightness -= 4;  // 加快变化速度
            if (brightness <= GREEN_MIN_BRIGHTNESS) {
                brightness = GREEN_MIN_BRIGHTNESS;
                increasing = true;
            }
        }
        TIM_SetCompare1(TIM3, brightness);
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
    // LED亮度测试命令
    if (strcmp(command, "LEDBRIGHT") == 0) {
        Send_Status_Message("=== LED BRIGHTNESS TEST ===");
        
        // 测试不同亮度级别
        for (int brightness = 10; brightness <= GREEN_MAX_BRIGHTNESS; brightness += 20) {
            char msg[60];
            sprintf(msg, "Brightness level: %d/%d", brightness, GREEN_MAX_BRIGHTNESS);
            Send_Status_Message(msg);
            TIM_SetCompare1(TIM3, brightness);
            Delay_ms(2000);
        }
        
        GreenLED_On();  // 回到正常亮度
        Send_Status_Message("Brightness test complete");
        return;
    }
    
    // LED测试命令
    if (strcmp(command, "LEDTEST") == 0) {
        Send_Status_Message("=== LED TEST ===");
        
        // 确保红灯完全关闭
        RedLED_Off();
        
        Send_Status_Message("1. Green LED at normal brightness");
        GreenLED_On();
        Delay_ms(2000);
        
        Send_Status_Message("2. Green LED OFF - Check red LED is completely off");
        GreenLED_Off();
        Delay_ms(2000);
        
        Send_Status_Message("3. Green LED Blink 3 times");
        GreenLED_Blink(3);
        
        Send_Status_Message("4. Green LED Breathing for 8 seconds - Watch carefully!");
        uint32_t start = system_tick;
        while(system_tick - start < 8000) {
            GreenLED_Breathing();
            Delay_ms(10);
        }
        
        Send_Status_Message("5. Red LED Blink 3 times");
        RedLED_Blink(3);
        
        GreenLED_On();
        Send_Status_Message("=== LED TEST COMPLETE ===");
        return;
    }
    
    // 电机测试命令
    if (strcmp(command, "MOTORTEST") == 0) {
        Send_Status_Message("=== MOTOR TEST ===");
        Send_Status_Message("Testing with safe start");
        Motor_Forward_Safe(100);
        Delay_ms(3000);
        Motor_Coast();
        Send_Status_Message("Motor test complete");
        return;
    }
    
    if (strcmp(command, "MOTORON") == 0) {
        Send_Status_Message("Starting motor - safe mode");
        Motor_Forward_Safe(120);
        return;
    }
    
    if (strcmp(command, "MOTOROFF") == 0) {
        Send_Status_Message("Stopping motor");
        Motor_Coast();
        return;
    }
    
    // 系统诊断
    if (strcmp(command, "DIAG") == 0) {
        Send_Status_Message("=== SYSTEM DIAGNOSTIC ===");
        Send_Status_Message("Green LED: Limited brightness for visibility");
        Send_Status_Message("Red LED: Force-off when not active");
        Send_Status_Message("Motor: Safe start enabled");
        Send_Status_Message("Servo: Working normally");
        return;
    }
    
    // 检查密码尝试次数限制
    if (wrong_password_count >= MAX_PASSWORD_ATTEMPTS) {
        System_Lock(LOCK_DURATION);
        Send_Status_Message("Too many wrong attempts! System locked");
        return;
    }
    
    if (strcmp(command, "RECOVER") == 0) {
        Emergency_Recovery();
        return;
    }
    
    if (strcmp(command, "TEST") == 0) {
        Send_Status_Message("System test - Blinking LEDs");
        // 确保红灯在绿灯闪烁时完全关闭
        RedLED_Off();
        GreenLED_Blink(2);
        RedLED_Blink(2);
        return;
    }
    
    if (strcmp(command, "OPEN") == 0) {
        if (current_state != STATE_LOCKED && !emergency_stop) {
            current_state = STATE_OPEN;
            Servo_SetAngle(180);
            Motor_Forward_Safe(120);
            Send_Status_Message("Manual OPEN - Safe mode");
        }
        return;
    }
    
    if (strcmp(command, "CLOSE") == 0) {
        if (current_state != STATE_LOCKED && !emergency_stop) {
            current_state = STATE_CLOSED;
            Servo_SetAngle(0);
            Motor_Coast();
            Send_Status_Message("Manual CLOSE");
        }
        return;
    }
    
    if (current_state == STATE_LOCKED) {
        Send_Status_Message("System LOCKED - Please wait");
        return;
    }
    
    if (emergency_stop) {
        Send_Status_Message("Emergency stop active - Use RECOVER");
        return;
    }
    
    // 密码命令处理
    if (command[0] == 'A' && command[strlen(command)-1] == 'E') {
        char password[20];
        strncpy(password, command + 1, strlen(command) - 2);
        password[strlen(command) - 2] = '\0';
        
        if (strcmp(password, correct_password) == 0) {
            Send_Status_Message("Password CORRECT! Opening system...");
            wrong_password_count = 0;
            current_state = STATE_OPEN;
            Servo_SetAngle(180);
            Motor_Forward_Safe(120);
            // 确保红灯在绿灯闪烁时完全关闭
            RedLED_Off();
            GreenLED_Blink(3);
        } else {
            Send_Status_Message("Password WRONG! Closing system...");
            wrong_password_count++;
            Servo_SetAngle(0);
            Motor_Coast();
            // 确保绿灯在红灯闪烁时完全关闭
            GreenLED_Off();
            RedLED_Blink(2);
            GreenLED_On();  // 闪烁后恢复绿灯
        }
        return;
    }
    
    Send_Status_Message("ERROR: Unknown command");
}

// 系统锁机
void System_Lock(uint32_t duration) {
    current_state = STATE_LOCKED;
    lock_timer = duration / 1000;
    wrong_password_count = 0;
    
    Motor_Coast();
    Servo_SetAngle(0);
    GreenLED_Off();
    RedLED_On();  // 锁机时红灯亮
    
    Send_Status_Message("System LOCKED");
}

// 紧急停止
void Emergency_Stop(void) {
    if (current_state != STATE_LOCKED) {
        emergency_stop = true;
        current_state = STATE_EMERGENCY;
        Motor_Coast();
        Servo_SetAngle(0);
        GreenLED_Off();
        RedLED_Off();  // 紧急停止时所有灯灭
        Send_Status_Message("EMERGENCY STOP");
    }
}

// 紧急恢复
void Emergency_Recovery(void) {
    if (emergency_stop) {
        emergency_stop = false;
        current_state = STATE_CLOSED;
        Servo_SetAngle(0);
        Motor_Coast();
        GreenLED_On();
        RedLED_Off();  // 确保红灯完全关闭
        Send_Status_Message("System RECOVERED");
    } else {
        Send_Status_Message("No emergency stop active");
    }
}

// 检查紧急按钮
void Check_Emergency_Button(void) {
    static uint32_t last_check = 0;
    static bool last_state = true;
    
    if (system_tick - last_check > 50) {
        bool current_btn_state = GPIO_ReadInputDataBit(BTN_PORT, EMERGENCY_BTN_PIN);
        if (!current_btn_state && last_state) {
            Emergency_Stop();
        }
        last_state = current_btn_state;
        last_check = system_tick;
    }
}

// 更新系统状态
void Update_System_Status(void) {
    static uint32_t last_status_update = 0;
    static SystemState last_reported_state = STATE_CLOSED;
    
    // 状态特定的LED控制
    switch(current_state) {
        case STATE_OPEN:
            GreenLED_Breathing();
            RedLED_Off();  // 确保红灯在呼吸模式下完全关闭
            break;
        case STATE_CLOSED:
            GreenLED_On();
            RedLED_Off();  // 确保红灯完全关闭
            break;
        case STATE_LOCKED:
            if ((system_tick % 1000) < 500) {
                RedLED_On();
            } else {
                RedLED_Off();
            }
            GreenLED_Off();  // 确保绿灯完全关闭
            break;
        case STATE_EMERGENCY:
            if ((system_tick % 200) < 100) {
                RedLED_On();
            } else {
                RedLED_Off();
            }
            GreenLED_Off();  // 确保绿灯完全关闭
            break;
    }
    
    // 减少状态消息频率
    if (current_state != last_reported_state || (system_tick - last_status_update > 30000)) {
        switch(current_state) {
            case STATE_CLOSED:
                Send_Status_Message("System: CLOSED - Ready for commands");
                break;
            case STATE_OPEN:
                Send_Status_Message("System: OPEN - Ventilation active");
                break;
            case STATE_LOCKED:
                Send_Status_Message("System: LOCKED - Please wait");
                break;
            case STATE_EMERGENCY:
                Send_Status_Message("System: EMERGENCY STOP");
                break;
        }
        last_reported_state = current_state;
        last_status_update = system_tick;
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
            Send_Status_Message("System UNLOCKED");
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
            rx_index = 0;
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
    Send_Status_Message("Password: A87654321E");
    Send_Status_Message("Commands: OPEN, CLOSE, TEST, DIAG, RECOVER");
    Send_Status_Message("LEDTEST, LEDBRIGHT, MOTORTEST, MOTORON, MOTOROFF");
    Send_Status_Message("Green LED: Limited brightness for better visibility");
    Send_Status_Message("System READY");
    
    while (1) {
        __WFI();
    }
}