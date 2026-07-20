#include "stm32f10x.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

// 引脚定义（修复引脚冲突）
#define SERVO_PIN GPIO_Pin_0         // PA0 - TIM2_CH1 舵机PWM
#define MOTOR_AIN1_PIN GPIO_Pin_1    // PA1 - 电机方向1
#define MOTOR_AIN2_PIN GPIO_Pin_0    // PB0 - 电机方向2 (从PA2改到PB0，避免PWM冲突)
#define MOTOR_PWMA_PIN GPIO_Pin_6    // PB6 - TIM4_CH1 电机PWM
#define MOTOR_STBY_PIN GPIO_Pin_4    // PA4 - 电机待机
#define GREEN_LED_PIN GPIO_Pin_6     // PA6 - TIM3_CH1 LED PWM
#define RED_LED_PIN GPIO_Pin_7       // PA7 - 红色LED
#define EMERGENCY_BTN_PIN GPIO_Pin_3 // PA3 - 紧急按钮输入

#define SERVO_PORT GPIOA
#define MOTOR_AIN1_PORT GPIOA
#define MOTOR_AIN2_PORT GPIOB
#define MOTOR_PWMA_PORT GPIOB
#define MOTOR_STBY_PORT GPIOA
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

// ========== 全局变量 ==========
volatile SystemState current_state = STATE_CLOSED;
volatile uint8_t wrong_password_count = 0;
volatile bool emergency_stop = false;
volatile uint32_t lock_timer_ms = 0;   // 锁机剩余时间(毫秒)
volatile uint32_t system_tick = 0;      // SysTick 毫秒计数器
char correct_password[] = "87654321";   // 8位密码

// ========== 函数声明 ==========
void System_Initialization(void);
void GPIO_Configuration(void);
void USART_Configuration(void);
void NVIC_Configuration(void);

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
void System_Lock(uint32_t duration_ms);
void Emergency_Stop(void);
void Emergency_Recovery(void);
void Check_Emergency_Button(void);
void Update_System_Status(void);

// ========== PWM函数声明 ==========
void PWM_Servo_Init(void);           // TIM2_CH1 (PA0) - 舵机PWM 50Hz
void PWM_Servo_SetCompare(uint16_t Compare);
void PWM_Motor_Init(void);           // TIM4_CH1 (PB6) - 电机PWM 1kHz
void PWM_Motor_SetCompare(uint16_t Compare);
void PWM_LED_Init(void);             // TIM3_CH1 (PA6) - LED PWM 100Hz
void PWM_LED_SetCompare(uint16_t Compare);

// ========== 系统滴答 (SysTick 1ms) ==========
// system_tick 已在全局变量区声明

void SysTick_Init(void) {
    // SysTick_Config: 每 1ms 触发一次中断 (内部已配置NVIC优先级)
    // SystemCoreClock 由 SystemInit() 设置，默认 72MHz (外部8MHz晶振×9)
    if (SysTick_Config(SystemCoreClock / 1000)) {
        while (1);  // 配置失败，死循环
    }
}

void SysTick_Handler(void) {
    system_tick++;
}

void Delay_ms(uint32_t ms) {
    uint32_t start = system_tick;
    while ((system_tick - start) < ms);
}

// ========== 串口发送 ==========
void USART_SendString(USART_TypeDef* USARTx, char* str) {
    while (*str) {
        while (USART_GetFlagStatus(USARTx, USART_FLAG_TXE) == RESET);
        USART_SendData(USARTx, *str++);
    }
}

// ========== 舵机PWM初始化 (TIM2_CH1 - PA0, 50Hz) ==========
void PWM_Servo_Init(void) {
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

    // PA0 配置为复用推挽输出 (TIM2_CH1)
    GPIO_InitTypeDef GPIO_InitStructure;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    // 时基: 72MHz / 72(PSC) / 20000(ARR) = 50Hz (20ms周期)
    TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStructure;
    TIM_TimeBaseInitStructure.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseInitStructure.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInitStructure.TIM_Period = 20000 - 1;    // ARR = 19999
    TIM_TimeBaseInitStructure.TIM_Prescaler = 72 - 1;    // PSC = 71
    TIM_TimeBaseInitStructure.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(TIM2, &TIM_TimeBaseInitStructure);

    // 通道1 PWM1 模式
    TIM_OCInitTypeDef TIM_OCInitStructure;
    TIM_OCStructInit(&TIM_OCInitStructure);
    TIM_OCInitStructure.TIM_OCMode = TIM_OCMode_PWM1;
    TIM_OCInitStructure.TIM_OCPolarity = TIM_OCPolarity_High;
    TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Enable;
    TIM_OCInitStructure.TIM_Pulse = 500;  // 初始0度 (0.5ms)
    TIM_OC1Init(TIM2, &TIM_OCInitStructure);
    TIM_OC1PreloadConfig(TIM2, TIM_OCPreload_Enable);

    TIM_ARRPreloadConfig(TIM2, ENABLE);
    TIM_Cmd(TIM2, ENABLE);
}

void PWM_Servo_SetCompare(uint16_t Compare) {
    TIM_SetCompare1(TIM2, Compare);
}

// ========== 电机PWM初始化 (TIM4_CH1 - PB6, 1kHz) ==========
void PWM_Motor_Init(void) {
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM4, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);

    // PB6 配置为复用推挽输出 (TIM4_CH1)
    GPIO_InitTypeDef GPIO_InitStructure;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_6;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    // 时基: 72MHz / 720(PSC) / 100(ARR) = 1kHz (1ms周期)
    TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStructure;
    TIM_TimeBaseInitStructure.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseInitStructure.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInitStructure.TIM_Period = 100 - 1;      // ARR = 99
    TIM_TimeBaseInitStructure.TIM_Prescaler = 720 - 1;   // PSC = 719
    TIM_TimeBaseInitStructure.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(TIM4, &TIM_TimeBaseInitStructure);

    // 通道1 PWM1 模式
    TIM_OCInitTypeDef TIM_OCInitStructure;
    TIM_OCStructInit(&TIM_OCInitStructure);
    TIM_OCInitStructure.TIM_OCMode = TIM_OCMode_PWM1;
    TIM_OCInitStructure.TIM_OCPolarity = TIM_OCPolarity_High;
    TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Enable;
    TIM_OCInitStructure.TIM_Pulse = 0;  // 初始停止
    TIM_OC1Init(TIM4, &TIM_OCInitStructure);
    TIM_OC1PreloadConfig(TIM4, TIM_OCPreload_Enable);

    TIM_ARRPreloadConfig(TIM4, ENABLE);
    TIM_Cmd(TIM4, ENABLE);
}

void PWM_Motor_SetCompare(uint16_t Compare) {
    TIM_SetCompare1(TIM4, Compare);
}

// ========== LED PWM初始化 (TIM3_CH1 - PA6, 100Hz) ==========
void PWM_LED_Init(void) {
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

    // PA6 配置为复用推挽输出 (TIM3_CH1)
    GPIO_InitTypeDef GPIO_InitStructure;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_6;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    // 时基: 72MHz / 7200(PSC) / 100(ARR) = 100Hz (10ms周期)
    TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStructure;
    TIM_TimeBaseInitStructure.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseInitStructure.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInitStructure.TIM_Period = 100 - 1;       // ARR = 99
    TIM_TimeBaseInitStructure.TIM_Prescaler = 7200 - 1;   // PSC = 7199
    TIM_TimeBaseInitStructure.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(TIM3, &TIM_TimeBaseInitStructure);

    // 通道1 PWM1 模式
    TIM_OCInitTypeDef TIM_OCInitStructure;
    TIM_OCStructInit(&TIM_OCInitStructure);
    TIM_OCInitStructure.TIM_OCMode = TIM_OCMode_PWM1;
    TIM_OCInitStructure.TIM_OCPolarity = TIM_OCPolarity_High;
    TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Enable;
    TIM_OCInitStructure.TIM_Pulse = GREEN_MAX_BRIGHTNESS;  // 初始最大亮度
    TIM_OC1Init(TIM3, &TIM_OCInitStructure);
    TIM_OC1PreloadConfig(TIM3, TIM_OCPreload_Enable);

    TIM_ARRPreloadConfig(TIM3, ENABLE);
    TIM_Cmd(TIM3, ENABLE);
}

void PWM_LED_SetCompare(uint16_t Compare) {
    TIM_SetCompare1(TIM3, Compare);
}

// ========== 系统初始化 ==========
void System_Initialization(void) {
    // 1. 先初始化 SysTick (Delay_ms依赖它)
    SysTick_Init();

    // 2. GPIO初始化 (所有数字IO)
    GPIO_Configuration();

    // 3. 串口
    USART_Configuration();

    // 4. 三个独立PWM (舵机/电机/LED各用不同定时器)
    PWM_Servo_Init();   // TIM2_CH1 - PA0 - 50Hz 舵机
    PWM_Motor_Init();   // TIM4_CH1 - PB6 - 1kHz 电机
    PWM_LED_Init();     // TIM3_CH1 - PA6 - 100Hz LED

    // 5. 中断 (串口接收)
    NVIC_Configuration();

    // 初始状态
    Servo_SetAngle(0);      // 风门关闭
    Motor_Coast();          // 电机停止
    GreenLED_On();          // 绿灯常亮
    RedLED_Off();           // 红灯关闭

    Send_Status_Message("System initialized - Door CLOSED, Green LED ON, Motor STOP");
}

// ========== GPIO配置 ==========
void GPIO_Configuration(void) {
    GPIO_InitTypeDef GPIO_InitStructure;

    // 开启 GPIOA 和 GPIOB 时钟
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB, ENABLE);

    // 舵机PWM (PA0) 由 PWM_Servo_Init 配置
    // 电机PWM (PB6) 由 PWM_Motor_Init 配置
    // LED PWM (PA6) 由 PWM_LED_Init 配置

    // 电机方向控制: AIN1 (PA1), AIN2 (PB0), STBY (PA4) - 普通推挽输出
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;

    GPIO_InitStructure.GPIO_Pin = MOTOR_AIN1_PIN | MOTOR_STBY_PIN;
    GPIO_Init(MOTOR_AIN1_PORT, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Pin = MOTOR_AIN2_PIN;
    GPIO_Init(MOTOR_AIN2_PORT, &GPIO_InitStructure);

    // 红色LED (PA7) - 普通GPIO输出
    GPIO_InitStructure.GPIO_Pin = RED_LED_PIN;
    GPIO_Init(LED_PORT, &GPIO_InitStructure);

    // 紧急按钮 (PA3) - 输入下拉 (按下时接到3.3V)
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPD;
    GPIO_InitStructure.GPIO_Pin = EMERGENCY_BTN_PIN;
    GPIO_Init(BTN_PORT, &GPIO_InitStructure);
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

// ========== NVIC配置 (只有串口中断，系统滴答用SysTick) ==========
void NVIC_Configuration(void) {
    NVIC_InitTypeDef NVIC_InitStructure;

    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);

    // USART1中断 (接收密码)
    NVIC_InitStructure.NVIC_IRQChannel = USART1_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    // 注意: 系统滴答现在用 SysTick (无需NVIC手动配置, SysTick_Config已处理)
    // 注意: TIM2/TIM3/TIM4 只用于PWM输出，不使用中断
}

// ========== 舵机角度设置 ==========
void Servo_SetAngle(uint16_t angle) {
    // 0度=0.5ms(CCR=500), 180度=2.5ms(CCR=2500)
    uint16_t pulse_width = 500 + (angle * 2000 / 180);
    PWM_Servo_SetCompare(pulse_width);
}

// ========== 电机控制函数 ==========
// AIN1=1, AIN2=0 → 正转
// AIN1=0, AIN2=1 → 反转
// AIN1=0, AIN2=0, STBY=0 → 滑行停止
void Motor_Forward(uint8_t speed) {
    GPIO_SetBits(MOTOR_STBY_PORT, MOTOR_STBY_PIN);
    GPIO_SetBits(MOTOR_AIN1_PORT, MOTOR_AIN1_PIN);
    GPIO_ResetBits(MOTOR_AIN2_PORT, MOTOR_AIN2_PIN);
    PWM_Motor_SetCompare(speed);
}

void Motor_Backward(uint8_t speed) {
    GPIO_SetBits(MOTOR_STBY_PORT, MOTOR_STBY_PIN);
    GPIO_ResetBits(MOTOR_AIN1_PORT, MOTOR_AIN1_PIN);
    GPIO_SetBits(MOTOR_AIN2_PORT, MOTOR_AIN2_PIN);
    PWM_Motor_SetCompare(speed);
}

void Motor_Stop(void) {
    GPIO_SetBits(MOTOR_STBY_PORT, MOTOR_STBY_PIN);
    GPIO_SetBits(MOTOR_AIN1_PORT, MOTOR_AIN1_PIN);
    GPIO_SetBits(MOTOR_AIN2_PORT, MOTOR_AIN2_PIN);
    PWM_Motor_SetCompare(99);
}

void Motor_Coast(void) {
    GPIO_ResetBits(MOTOR_STBY_PORT, MOTOR_STBY_PIN);
    GPIO_ResetBits(MOTOR_AIN1_PORT, MOTOR_AIN1_PIN);
    GPIO_ResetBits(MOTOR_AIN2_PORT, MOTOR_AIN2_PIN);
    PWM_Motor_SetCompare(0);
}

// 缓启动正转
void Motor_Forward_Safe(uint8_t speed) {
    GPIO_SetBits(MOTOR_STBY_PORT, MOTOR_STBY_PIN);
    Delay_ms(50);
    GPIO_SetBits(MOTOR_AIN1_PORT, MOTOR_AIN1_PIN);
    GPIO_ResetBits(MOTOR_AIN2_PORT, MOTOR_AIN2_PIN);
    Delay_ms(50);
    for (uint8_t i = 0; i < speed; i += 5) {
        PWM_Motor_SetCompare(i);
        Delay_ms(20);
    }
}

// 缓启动反转
void Motor_Backward_Safe(uint8_t speed) {
    GPIO_SetBits(MOTOR_STBY_PORT, MOTOR_STBY_PIN);
    Delay_ms(50);
    GPIO_ResetBits(MOTOR_AIN1_PORT, MOTOR_AIN1_PIN);
    GPIO_SetBits(MOTOR_AIN2_PORT, MOTOR_AIN2_PIN);
    Delay_ms(50);
    for (uint8_t i = 0; i < speed; i += 5) {
        PWM_Motor_SetCompare(i);
        Delay_ms(20);
    }
}

// ========== LED控制函数 ==========
void GreenLED_On(void) {
    PWM_LED_SetCompare(GREEN_MAX_BRIGHTNESS);
}

void GreenLED_Off(void) {
    PWM_LED_SetCompare(0);
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

// ========== 呼吸灯效果 (每30ms调整一次亮度) ==========
void GreenLED_Breathing(void) {
    static uint16_t brightness = GREEN_MIN_BRIGHTNESS;
    static bool increasing = true;
    static uint32_t last_breath_time = 0;

    if (system_tick - last_breath_time >= 30) {
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
        PWM_LED_SetCompare(brightness);
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

// ========== 系统锁机 (duration_ms = 锁机毫秒数) ==========
void System_Lock(uint32_t duration_ms) {
    current_state = STATE_LOCKED;
    lock_timer_ms = duration_ms;  // 直接存毫秒数

    Motor_Coast();
    GreenLED_Off();
    RedLED_On();

    char buf[80];
    sprintf(buf, "SYSTEM LOCKED for %lu seconds - Too many wrong attempts!",
            (unsigned long)(duration_ms / 1000));
    Send_Status_Message(buf);
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

// ========== 更新系统状态 (在主循环中轮询调用) ==========
void Update_System_Status(void) {
    static uint32_t lock_end_time = 0;   // 锁机结束时刻(ms)
    static bool lock_timer_set = false;

    // 状态特定的LED控制
    switch (current_state) {
        case STATE_OPEN:
            GreenLED_Breathing();
            RedLED_Off();
            break;
        case STATE_CLOSED:
            GreenLED_On();
            RedLED_Off();
            break;
        case STATE_LOCKED:
            // 进入锁机时设置结束时刻
            if (!lock_timer_set) {
                lock_end_time = system_tick + lock_timer_ms;
                lock_timer_set = true;
            }
            GreenLED_Off();
            RedLED_On();
            // 检查是否到时间
            if ((int32_t)(system_tick - lock_end_time) >= 0) {
                current_state = STATE_CLOSED;
                wrong_password_count = 0;
                emergency_stop = false;
                lock_timer_set = false;
                RedLED_Off();
                GreenLED_On();
                Send_Status_Message("System UNLOCKED - Ready for commands");
            }
            break;
        case STATE_EMERGENCY:
            GreenLED_Off();
            RedLED_Off();
            lock_timer_set = false;
            break;
        default:
            lock_timer_set = false;
            break;
    }
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

// ========== 主函数 ==========
int main(void) {
    SystemInit();                         // 标准库启动 (SystemCoreClock设置)
    System_Initialization();              // GPIO/USART/SysTick/PWM全部初始化

    Send_Status_Message("=== SMART VENT SYSTEM ===");
    Send_Status_Message("Password format: A87654321E");
    Send_Status_Message("Default state: Door CLOSED, Green LED ON, Motor STOP");
    Send_Status_Message("Correct password: Door OPEN, Motor FORWARD, Green LED Breathing");
    Send_Status_Message("Wrong password: Door CLOSED, Motor REVERSE, Red LED Blink");
    Send_Status_Message("3 wrong attempts: System LOCKED 10s, Red LED ON");
    Send_Status_Message("Emergency button (PA3 to 3.3V): Stops all devices");
    Send_Status_Message("System READY");

    while (1) {
        // 轮询：紧急按钮检测 + 呼吸灯/锁机计时更新
        Check_Emergency_Button();
        Update_System_Status();
    }
}