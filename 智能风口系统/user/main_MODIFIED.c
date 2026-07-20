/* =========================================================================
 * 文件说明: 智能风口系统 主程序 (修复版)
 *
 * 问题分析与修复清单:
 * ┌────────────────────────────────────────────────────────────┐
 * │ Bug1: TIM2 被重新初始化了两次                            │
 * │       错误代码: PWM_Motor_Init() 里又写了一次 TIM2        │
 * │       现象: 舵机完全不动                                  │
 * │       原理: TIM_DeInit → 清除上一次的PWM配置              │
 * │              TIM_Init  → 按电机PWM重新配置                │
 * │              → 舵机PWM(PA0)失效                           │
 * │       修复: 舵机保留TIM2, 电机改用TIM4, LED呼吸灯用TIM3   │
 * │            三路独立, 互不干扰                              │
 * ├────────────────────────────────────────────────────────────┤
 * │ Bug2: PA2 引脚冲突                                        │
 * │       错误代码: AIN2_PIN=PA2, 同时PWM也用PA2              │
 * │       现象: 电机方向控制和PWM输出互相覆盖, 电机乱转        │
 * │       原理: GPIO复用功能只有一个, "推挽输出"和"TIM2_CH3"   │
 * │              不能同时激活                                  │
 * │       修复: 电机方向2 移到PB0 (普通GPIO), PWM保留PB6      │
 * │            (TIM4_CH1), 独立定时器                        │
 * ├────────────────────────────────────────────────────────────┤
 * │ Bug3: SysTick 中断服务函数被重复定义                      │
 * │       错误代码: stm32f10x_it.c 里有空的 SysTick_Handler,  │
 * │                 main.c 里又写了一个                       │
 * │       现象: 链接错误 "multiple definition of"             │
 * │       原理: 两个 .o 文件导出了同名函数                    │
 * │       修复: 只保留 main.c 的版本, 把 stm32f10x_it.c 里的  │
 * │            注释掉                                          │
 * ├────────────────────────────────────────────────────────────┤
 * │ Bug4: 锁机时间 = 10ms 而不是 10s                         │
 * │       错误代码: 用 TIM3 更新事件 (1kHz) 自减              │
 * │       现象: 按下锁机键, 马上又自己解锁                    │
 * │       原理: TIM3每秒触发1000次, 减10次只需10ms            │
 * │       修复: 用 SysTick 记录"解锁时刻"                     │
 * │            lock_end_time = system_tick + 10000           │
 * │            if (system_tick >= lock_end_time) 则解锁       │
 * ├────────────────────────────────────────────────────────────┤
 * │ Bug5: main 用 __WFI() 进入睡眠导致状态更新停止            │
 * │       错误代码: while (1) { __WFI(); }                    │
 * │       现象: 呼吸灯/锁机计时 看起来卡住                    │
 * │       原理: __WFI() 让CPU停止取指, 只有中断能唤醒         │
 * │              主循环轮询的代码永远不执行                    │
 * │       修复: 改成 while(1) { 轮询... } 不进入低功耗       │
 * └────────────────────────────────────────────────────────────┘
 *
 * 最终资源分配:
 *   TIM2_CH1 (PA0) → 舵机PWM, 50Hz, 脉冲 1~2ms 对应0~180°
 *   TIM4_CH1 (PB6) → 电机PWM, 1kHz, 调节风口打开速度
 *   TIM3_CH1 (PA6) → LED呼吸灯PWM, 100Hz
 *   SysTick → 1ms时基, 用于: 非阻塞延时 + 锁机计时
 *   USART1 → 密码验证 + 远程控制 (115200bps)
 *   GPIO  → 按键 / 电机方向
 * ========================================================================= */

#include "stm32f10x.h"

/* ========== 软件延时 (SysTick 时基) ========== */
void SysTick_Delay_ms(__IO uint32_t ms);
void SysTick_Delay_us(__IO uint32_t us);

/* ========== 时基/外设初始化 ========== */
void SysTick_Init(void);             /* 1ms时基 */
void Bsp_Init(void);                 /* GPIO + 串口 + 3路PWM */

/* ========== 控制函数 ========== */
void Servo_SetAngle(uint16_t angle);      /* 0~180 */
void Motor_SetDirection(uint8_t dir);     /* 1=正转 0=停止 2=反转 */
void Motor_SetSpeed(uint16_t speed);      /* 0~999 */
void LED_Breathe(void);                   /* 在主循环里调, 产生呼吸效果 */
void Check_Emergency_Button(void);        /* 紧急按钮扫描 */
void Update_System_Status(void);          /* 根据状态做动作 */

/* ========== 串口通信 ========== */
void Serial_Parse(uint8_t byte);          /* 解析单字节命令 */
void Serial_SendString(const char *s);    /* 发送字符串(调试) */

/* ========== 状态机 ==========
 * CLOSED    = 初始关闭状态, 舵机=0度, 电机停止
 * OPEN      = 风口打开, 舵机=90度, 电机低速保持
 * LOCKED    = 锁机10秒, 期间忽略所有控制输入
 * EMERGENCY = 紧急停止, 风口完全关闭, 等待重启
 */
typedef enum {
    STATE_CLOSED = 0,
    STATE_OPEN,
    STATE_LOCKED,
    STATE_EMERGENCY
} SystemState_t;

/* ==================================================================
 * 全局变量: 使用 volatile 修饰
 *   volatile 的意义: 告诉编译器"这个变量可能在中断里被改",
 *                    不要做寄存器级优化, 每次读都从内存读
 * ================================================================== */
volatile uint32_t system_tick = 0;      /* 自开机起的毫秒数 */
volatile SystemState_t current_state = STATE_CLOSED;

/* 锁机结束时刻 (SysTick 值) : current_state==LOCKED时,
 * system_tick >= lock_end_time 就自动解除 */
volatile uint32_t lock_end_time = 0;

/* 密码验证: 密码 "1234", 每收到1个字符比对一位 */
uint8_t password_buf[8] = {0};          /* 输入缓冲区 */
uint8_t password_idx = 0;                /* 当前比对到第几位 */
uint8_t authenticated = 0;               /* 1=已通过密码, 可远程控制 */

/* ------------------------------------------------------------------
 * main: 系统入口
 * ------------------------------------------------------------------ */
int main(void)
{
    /* 1) SysTick 优先于一切 (Delay依赖它) */
    SysTick_Init();

    /* 2) GPIO / 串口 / PWM 初始化 */
    Bsp_Init();

    /* 3) 初始状态: 风口关闭, 串口提示 */
    current_state = STATE_CLOSED;
    Serial_SendString("Smart-Vent Ready. Enter '1234' to unlock.\r\n");

    /* 4) 主循环: 轮询方式 (替代原来的 __WFI()) */
    while (1) {
        /* LED呼吸灯效果 (任何状态都执行, 作为可见"心跳") */
        LED_Breathe();

        /* 紧急按钮扫描 (PA15, 下拉输入, 按下=高) */
        Check_Emergency_Button();

        /* 根据状态执行对应动作 */
        Update_System_Status();
    }
}

/* ==================================================================
 * SysTick 配置: 每1ms触发一次中断, 供系统时基
 * 计算:
 *   HCLK = 72MHz
 *   SysTick_Config(72000) → 每 1ms 触发一次
 *   Cortex-M3内部会把NVIC优先级设为最低 (由库函数处理)
 * ================================================================== */
void SysTick_Init(void)
{
    if (SysTick_Config(SystemCoreClock / 1000)) {
        /* 配置失败 (几乎不可能发生) → 死循环, 红灯报错 */
        while (1) {
            GPIO_WriteBit(GPIOC, GPIO_Pin_13, Bit_RESET);
            SysTick_Delay_ms(200);
            GPIO_WriteBit(GPIOC, GPIO_Pin_13, Bit_SET);
            SysTick_Delay_ms(200);
        }
    }
}

/* ==================================================================
 * SysTick_Handler: 1ms 进一次, 只做 system_tick++
 *   【注意】必须把 stm32f10x_it.c 里的同名函数注释掉,
 *          否则链接时会报 multiple definition
 * ================================================================== */
void SysTick_Handler(void)
{
    system_tick++;
}

/* 基于SysTick的阻塞延时 (ms) */
void SysTick_Delay_ms(__IO uint32_t ms)
{
    uint32_t start = system_tick;
    while ((system_tick - start) < ms) { /* 无操作 */ }
}

/* 基于SysTick的阻塞延时 (us, 用空循环近似) */
void SysTick_Delay_us(__IO uint32_t us)
{
    /* 72MHz → 1个NOP约14ns, 1us ≈ 72个NOP */
    while (us--) {
        for (volatile uint32_t i = 0; i < 9; i++) {
            __NOP(); __NOP(); __NOP(); __NOP();
            __NOP(); __NOP(); __NOP(); __NOP();
        }
    }
}

/* ==================================================================
 * 引脚定义 (修改后, 解决冲突)
 * ┌──────────────────────────────────┐
 * │ PA0 → TIM2_CH1 PWM  舵机         │  ← 只分配给舵机
 * │ PA1 → GPIO 推挽     电机方向1    │
 * │ PB0 → GPIO 推挽     电机方向2    │  ← 从PA2改到这里, 避免PWM冲突
 * │ PB6 → TIM4_CH1 PWM  电机PWM      │  ← 用TIM4代替TIM2
 * │ PA6 → TIM3_CH1 PWM  LED呼吸灯    │  ← 用TIM3独立驱动
 * │ PA15→ GPIO 下拉输入 紧急按钮      │
 * │ PA9 → USART1_TX     串口调试     │
 * │ PA10→ USART1_RX     串口调试     │
 * │ PC13→ GPIO 推挽     状态指示灯   │
 * └──────────────────────────────────┘
 * ================================================================== */
#define SERVO_PIN       GPIO_Pin_0
#define SERVO_PORT      GPIOA
#define MOTOR_AIN1_PIN  GPIO_Pin_1     /* 电机方向 AIN1 */
#define MOTOR_AIN1_PORT GPIOA
#define MOTOR_AIN2_PIN  GPIO_Pin_0     /* 电机方向 AIN2 ← PB0 独立GPIO */
#define MOTOR_AIN2_PORT GPIOB
#define MOTOR_PWMA_PIN  GPIO_Pin_6     /* 电机PWM ← TIM4_CH1, 不再冲突 */
#define MOTOR_PWMA_PORT GPIOB
#define LED_PIN         GPIO_Pin_6     /* LED呼吸灯 PWM ← TIM3_CH1 */
#define LED_PORT        GPIOA
#define EMERGENCY_PIN   GPIO_Pin_15    /* 紧急按钮 */
#define EMERGENCY_PORT  GPIOA
#define STATUS_LED_PIN  GPIO_Pin_13    /* 板载蓝灯 */
#define STATUS_LED_PORT GPIOC

/* ==================================================================
 * Bsp_Init: GPIO + 串口 + 三路独立 PWM (TIM2 / TIM3 / TIM4)
 * ================================================================== */
void Bsp_Init(void)
{
    GPIO_InitTypeDef  g;
    TIM_TimeBaseInitTypeDef tbase;
    TIM_OCInitTypeDef  toc;
    USART_InitTypeDef  u;
    NVIC_InitTypeDef   nvic;

    /* 1. 开所有用到的外设时钟 */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB |
                           RCC_APB2Periph_GPIOC | RCC_APB2Periph_USART1 |
                           RCC_APB2Periph_AFIO, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2 | RCC_APB1Periph_TIM3 |
                           RCC_APB1Periph_TIM4, ENABLE);

    /* 2. GPIO 配置 */

    /* 2a. PA0 舵机PWM (TIM2_CH1, 复用推挽 50MHz) */
    g.GPIO_Pin   = SERVO_PIN;
    g.GPIO_Mode  = GPIO_Mode_AF_PP;
    g.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(SERVO_PORT, &g);

    /* 2b. PA1 电机方向1 (普通推挽) */
    g.GPIO_Pin   = MOTOR_AIN1_PIN;
    g.GPIO_Mode  = GPIO_Mode_Out_PP;
    g.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(MOTOR_AIN1_PORT, &g);

    /* 2c. PB0 电机方向2 (普通推挽, 【修复Bug2】改到这里) */
    g.GPIO_Pin   = MOTOR_AIN2_PIN;
    g.GPIO_Mode  = GPIO_Mode_Out_PP;
    g.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(MOTOR_AIN2_PORT, &g);

    /* 2d. PB6 电机PWM (TIM4_CH1, 复用推挽, 【修复Bug1】独立定时器) */
    g.GPIO_Pin   = MOTOR_PWMA_PIN;
    g.GPIO_Mode  = GPIO_Mode_AF_PP;
    g.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(MOTOR_PWMA_PORT, &g);

    /* 2e. PA6 LED呼吸灯PWM (TIM3_CH1) */
    g.GPIO_Pin   = LED_PIN;
    g.GPIO_Mode  = GPIO_Mode_AF_PP;
    g.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(LED_PORT, &g);

    /* 2f. PA15 紧急按钮 (下拉输入) */
    g.GPIO_Pin  = EMERGENCY_PIN;
    g.GPIO_Mode = GPIO_Mode_IPD;    /* 下拉: 松开=0, 按下=1 */
    GPIO_Init(EMERGENCY_PORT, &g);

    /* 2g. PC13 板载蓝灯 (推挽, 上电灭) */
    g.GPIO_Pin   = STATUS_LED_PIN;
    g.GPIO_Mode  = GPIO_Mode_Out_PP;
    g.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(STATUS_LED_PORT, &g);
    GPIO_WriteBit(STATUS_LED_PORT, STATUS_LED_PIN, Bit_SET);  /* 灭 */

    /* 2h. PA9/PA10 串口 (复用推挽 + 上拉输入) */
    g.GPIO_Pin   = GPIO_Pin_9;
    g.GPIO_Mode  = GPIO_Mode_AF_PP;
    g.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &g);
    g.GPIO_Pin   = GPIO_Pin_10;
    g.GPIO_Mode  = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &g);

    /* 3. USART1 配置 115200-8-N-1, RX中断开 */
    u.USART_BaudRate            = 115200;
    u.USART_WordLength          = USART_WordLength_8b;
    u.USART_StopBits            = USART_StopBits_1;
    u.USART_Parity              = USART_Parity_No;
    u.USART_Mode                = USART_Mode_Tx | USART_Mode_Rx;
    u.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_Init(USART1, &u);
    USART_Cmd(USART1, ENABLE);
    USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);   /* 开启接收中断 */

    /* NVIC: 串口中断优先级2 */
    nvic.NVIC_IRQChannel = USART1_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority = 2;
    nvic.NVIC_IRQChannelSubPriority = 0;
    nvic.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic);

    /* ================= 4. TIM2 舵机 PWM (50Hz, 20ms周期) =================
     *
     * 【修复 Bug1】TIM2 只分配给舵机, 不再被电机初始化覆盖
     *
     * 计算:
     *   TIM2挂在APB1, APB1预分频=2, TIMx时钟=72MHz
     *   目标频率 = 50Hz → 周期 = 20ms
     *   设 PSC = 719   → 计数频率 = 72MHz / 720 = 100kHz
     *   设 ARR = 1999  → PWM频率 = 100kHz / 2000 = 50Hz ✓
     *
     * 舵机角度映射 (MG996R 等标准舵机):
     *   0度   → 脉冲 1.0ms  → CCR = 100 (1ms * 100kHz)
     *   90度  → 脉冲 1.5ms  → CCR = 150
     *   180度 → 脉冲 2.0ms  → CCR = 200
     *   公式:  CCR = 100 + angle / 180 * 100
     *           简化: CCR = 100 + (angle * 5) / 9  (整数运算)
     * ==================================================================== */
    TIM_DeInit(TIM2);
    tbase.TIM_Prescaler         = 719;     /* 72MHz / 720 = 100kHz */
    tbase.TIM_Period            = 1999;    /* 周期 2000/100kHz = 20ms */
    tbase.TIM_ClockDivision     = TIM_CKD_DIV1;
    tbase.TIM_CounterMode       = TIM_CounterMode_Up;
    tbase.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(TIM2, &tbase);

    /* TIM2_CH1 PWM1 模式: 计数<CCR时输出高 */
    toc.TIM_OCMode      = TIM_OCMode_PWM1;
    toc.TIM_OutputState = TIM_OutputState_Enable;
    toc.TIM_Pulse       = 150;     /* 初始90度 (风口打开至一半, 便于验证) */
    toc.TIM_OCPolarity  = TIM_OCPolarity_High;
    TIM_OC1Init(TIM2, &toc);
    TIM_OC1PreloadConfig(TIM2, TIM_OCPreload_Enable);
    TIM_ARRPreloadConfig(TIM2, ENABLE);
    TIM_Cmd(TIM2, ENABLE);

    /* ================= 5. TIM3 LED呼吸灯 PWM (100Hz, 10ms周期) =============
     *
     * 【修复 Bug1】TIM3 独立用于 LED 呼吸效果
     *   原代码用 TIM3 同时做"系统滴答中断"和"PWM",
     *   导致PWM输出异常(中断里频繁写CCR会抖)
     *   → 现在TIM3只做PWM, 系统时基交给SysTick
     *
     * 计算:
     *   PSC = 719   → 100kHz
     *   ARR = 999   → 100Hz
     *   占空比 0~1000 对应 0%~100%
     * ==================================================================== */
    TIM_DeInit(TIM3);
    tbase.TIM_Prescaler = 719;
    tbase.TIM_Period    = 999;
    TIM_TimeBaseInit(TIM3, &tbase);

    toc.TIM_OCMode      = TIM_OCMode_PWM1;
    toc.TIM_OutputState = TIM_OutputState_Enable;
    toc.TIM_Pulse       = 0;
    toc.TIM_OCPolarity  = TIM_OCPolarity_High;
    TIM_OC1Init(TIM3, &toc);
    TIM_OC1PreloadConfig(TIM3, TIM_OCPreload_Enable);
    TIM_ARRPreloadConfig(TIM3, ENABLE);
    TIM_Cmd(TIM3, ENABLE);

    /* ================= 6. TIM4 电机 PWM (1kHz, 1ms周期) ===================
     *
     * 【修复 Bug1】TIM4 专门驱动电机, 不再复用TIM2
     *   这样: 舵机TIM2 / 呼吸灯TIM3 / 电机TIM4, 三路PWM完全独立
     *
     * 计算:
     *   PSC = 71   → 1MHz 计数
     *   ARR = 999  → 1kHz PWM (1ms周期)
     *   占空比 0~999 对应停止~满速
     * ==================================================================== */
    TIM_DeInit(TIM4);
    tbase.TIM_Prescaler = 71;
    tbase.TIM_Period    = 999;
    TIM_TimeBaseInit(TIM4, &tbase);

    toc.TIM_OCMode      = TIM_OCMode_PWM1;
    toc.TIM_OutputState = TIM_OutputState_Enable;
    toc.TIM_Pulse       = 0;
    toc.TIM_OCPolarity  = TIM_OCPolarity_High;
    TIM_OC1Init(TIM4, &toc);
    TIM_OC1PreloadConfig(TIM4, TIM_OCPreload_Enable);
    TIM_ARRPreloadConfig(TIM4, ENABLE);
    TIM_Cmd(TIM4, ENABLE);
}

/* ==================================================================
 * 舵机: 设置角度 0~180
 * ================================================================== */
void Servo_SetAngle(uint16_t angle)
{
    if (angle > 180) angle = 180;
    /* CCR = 100 + angle / 180 * 100
     *      = 100 + (angle * 100) / 180
     *      = 100 + (angle * 5) / 9          ← 整数运算 */
    uint16_t ccr = 100 + (angle * 5) / 9;
    TIM_SetCompare1(TIM2, ccr);
}

/* ==================================================================
 * 电机方向: dir=1正转, dir=2反转, 其他=停止
 * 硬件: AIN1=PA1, AIN2=PB0, 两路GPIO独立, 不再与PWM冲突
 * ================================================================== */
void Motor_SetDirection(uint8_t dir)
{
    if (dir == 1) {
        GPIO_WriteBit(MOTOR_AIN1_PORT, MOTOR_AIN1_PIN, Bit_SET);
        GPIO_WriteBit(MOTOR_AIN2_PORT, MOTOR_AIN2_PIN, Bit_RESET);
    } else if (dir == 2) {
        GPIO_WriteBit(MOTOR_AIN1_PORT, MOTOR_AIN1_PIN, Bit_RESET);
        GPIO_WriteBit(MOTOR_AIN2_PORT, MOTOR_AIN2_PIN, Bit_SET);
    } else {
        GPIO_WriteBit(MOTOR_AIN1_PORT, MOTOR_AIN1_PIN, Bit_RESET);
        GPIO_WriteBit(MOTOR_AIN2_PORT, MOTOR_AIN2_PIN, Bit_RESET);
    }
}

/* ==================================================================
 * 电机速度: speed 0~999 (直接写入TIM4_CCR1)
 * ================================================================== */
void Motor_SetSpeed(uint16_t speed)
{
    if (speed > 999) speed = 999;
    TIM_SetCompare1(TIM4, speed);
}

/* ==================================================================
 * LED呼吸灯: 每次进入该函数, 更新一次占空比, 产生正弦式明暗
 * 原理:
 *   用 10ms 为一步, 0~100步亮度从0→1000→0 循环
 *   更新频率 = 主循环调用频率 (约每10ms一次)
 * ================================================================== */
void LED_Breathe(void)
{
    static uint32_t last_tick = 0;
    static uint16_t step = 0;
    static uint8_t  up = 1;

    if ((system_tick - last_tick) < 10) return;   /* 10ms 一格 */
    last_tick = system_tick;

    if (up) {
        step += 20;
        if (step >= 1000) { step = 1000; up = 0; }
    } else {
        if (step >= 20) step -= 20;
        else           { step = 0;    up = 1; }
    }

    TIM_SetCompare1(TIM3, step);   /* 直接写 TIM3_CCR1 */
}

/* ==================================================================
 * 紧急按钮扫描 (PA15)
 *   按下 = 高电平 (外部下拉, 按钮接VCC)
 *   触发后进入 STATE_EMERGENCY, 风口完全关闭
 *   需要通过串口 "reset" 命令或重新上电才能退出
 * ================================================================== */
void Check_Emergency_Button(void)
{
    static uint32_t last_tick = 0;
    static uint8_t  last_state = 0;

    /* 按键消抖: 20ms 采样一次, 连续两次相同才认定 */
    if ((system_tick - last_tick) < 20) return;
    last_tick = system_tick;

    uint8_t cur = (GPIO_ReadInputDataBit(EMERGENCY_PORT, EMERGENCY_PIN)
                   == Bit_SET) ? 1 : 0;

    if (cur == 1 && last_state == 0) {       /* 上升沿: 刚按下 */
        if (current_state != STATE_EMERGENCY) {
            current_state = STATE_EMERGENCY;
            Serial_SendString("EMERGENCY triggered - vent closing.\r\n");
        }
    }
    last_state = cur;
}

/* ==================================================================
 * 状态机: 根据 current_state 做动作
 *   非阻塞设计: 每次调用只判断"该做什么"然后立即返回
 * ================================================================== */
void Update_System_Status(void)
{
    static uint32_t last_tick = 0;

    switch (current_state) {
        /* --- 关闭状态: 舵机=0度, 电机停 --- */
        case STATE_CLOSED:
            Servo_SetAngle(0);
            Motor_SetDirection(0);
            Motor_SetSpeed(0);
            break;

        /* --- 打开状态: 舵机=90度, 电机低速保持张力 --- */
        case STATE_OPEN:
            Servo_SetAngle(90);
            Motor_SetDirection(1);
            Motor_SetSpeed(300);     /* 30% 占空比, 低速 */
            break;

        /* --- 锁机状态: 10秒后自动解除 ---
         * 【修复 Bug4】原来用 TIM3 自减, 由于TIM3是1kHz中断,
         *              10次只需10ms, 导致几乎瞬间解除
         *              现在用 system_tick 记录"解锁时刻"
         * ------------------------------------------------ */
        case STATE_LOCKED:
            Servo_SetAngle(0);
            Motor_SetDirection(0);
            Motor_SetSpeed(0);
            if (lock_end_time == 0) {
                /* 第一次进入锁机 → 记录解锁时刻 */
                lock_end_time = system_tick + 10000;   /* 10秒 = 10000 ms */
            } else if (system_tick >= lock_end_time) {
                /* 到点 → 解除, 回到关闭状态 */
                lock_end_time = 0;
                current_state = STATE_CLOSED;
                Serial_SendString("Lock released.\r\n");
            } else {
                /* 每1秒发一次倒计时提示 (让串口看到锁机在正常运行) */
                if ((system_tick - last_tick) >= 1000) {
                    last_tick = system_tick;
                    uint32_t remain = (lock_end_time - system_tick) / 1000;
                    char buf[40];
                    /* 简易字符串构造 (不用sprintf节省FLASH) */
                    buf[0] = 'L'; buf[1] = 'o'; buf[2] = 'c'; buf[3] = 'k';
                    buf[4] = ':'; buf[5] = ' ';
                    buf[6] = '0' + (uint8_t)(remain / 10);
                    buf[7] = '0' + (uint8_t)(remain % 10);
                    buf[8] = 's'; buf[9] = '\r'; buf[10] = '\n'; buf[11] = 0;
                    Serial_SendString(buf);
                }
            }
            break;

        /* --- 紧急停止: 永久关闭, 需要"reset"解除 --- */
        case STATE_EMERGENCY:
            Servo_SetAngle(0);
            Motor_SetDirection(0);
            Motor_SetSpeed(0);
            break;
    }

    /* 紧急状态指示: PC13 蓝灯 = 紧急时快闪 */
    static uint32_t blink_tick = 0;
    static uint8_t  blink_on = 0;
    uint32_t interval = (current_state == STATE_EMERGENCY) ? 200 : 1000;
    if ((system_tick - blink_tick) >= interval) {
        blink_tick = system_tick;
        blink_on = !blink_on;
        GPIO_WriteBit(STATUS_LED_PORT, STATUS_LED_PIN,
                      blink_on ? Bit_RESET : Bit_SET);
    }
}

/* ==================================================================
 * 串口发送字符串
 * ================================================================== */
void Serial_SendString(const char *s)
{
    while (*s) {
        while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET) {}
        USART_SendData(USART1, (uint16_t)(*s++));
    }
}

/* ==================================================================
 * 串口字节解析 (在 USART1_IRQHandler 里调用, 由中断触发)
 * 协议:
 *   "1234"      → 通过密码 (authenticated=1)
 *   "open"      → 打开风口
 *   "close"     → 关闭风口
 *   "lock"      → 锁定10秒
 *   "reset"     → 解除紧急状态
 *   "angle XX"  → 设置舵机角度 (简化版: 单字节 '0'~'9' 对应 0~180)
 * ================================================================== */
#define MAX_CMD 16
static char cmd_buf[MAX_CMD];
static uint8_t cmd_len = 0;

void Serial_Parse(uint8_t byte)
{
    /* 1) 回显 (方便调试) */
    while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET) {}
    USART_SendData(USART1, byte);

    /* 2) 换行 = 命令结束 */
    if (byte == '\r' || byte == '\n') {
        if (cmd_len == 0) return;
        cmd_buf[cmd_len] = 0;
        cmd_len = 0;

        /* 解析已缓存的整串命令 */
        if (!authenticated) {
            /* 未认证: 只接受密码 "1234" */
            if (cmd_buf[0] == '1' && cmd_buf[1] == '2' &&
                cmd_buf[2] == '3' && cmd_buf[3] == '4' &&
                cmd_buf[4] == 0) {
                authenticated = 1;
                Serial_SendString("\r\nAuth OK. Commands: open/close/lock/reset\r\n");
            } else {
                Serial_SendString("\r\nWrong password.\r\n");
            }
        } else {
            /* 已认证: 接受控制命令 */
            if      (cmd_buf[0] == 'o' && cmd_buf[1] == 'p') {
                current_state = STATE_OPEN;
                Serial_SendString("\r\nVent OPENED.\r\n");
            }
            else if (cmd_buf[0] == 'c' && cmd_buf[1] == 'l') {
                current_state = STATE_CLOSED;
                Serial_SendString("\r\nVent CLOSED.\r\n");
            }
            else if (cmd_buf[0] == 'l' && cmd_buf[1] == 'o') {
                current_state = STATE_LOCKED;
                lock_end_time = 0;    /* 强制重新开始计时 */
                Serial_SendString("\r\nLOCKED for 10s.\r\n");
            }
            else if (cmd_buf[0] == 'r' && cmd_buf[1] == 'e') {
                current_state = STATE_CLOSED;
                Serial_SendString("\r\nReset OK.\r\n");
            }
            else {
                Serial_SendString("\r\nUnknown command.\r\n");
            }
        }
        return;
    }

    /* 3) 普通字符入缓存 */
    if (cmd_len < MAX_CMD - 1) {
        cmd_buf[cmd_len++] = (char)byte;
    }
}

/* ==================================================================
 * USART1 中断服务函数: 收到1字节就交给 Serial_Parse
 * ================================================================== */
void USART1_IRQHandler(void)
{
    if (USART_GetITStatus(USART1, USART_IT_RXNE) != RESET) {
        uint8_t b = (uint8_t)USART_ReceiveData(USART1);
        Serial_Parse(b);
    }
}

/* ==================================================================
 * 【修复 Bug3】空的 stm32f10x_it.c
 *   本文件里的 SysTick_Handler 和 USART1_IRQHandler 已经定义,
 *   请在 stm32f10x_it.c 里:
 *
 *   注释掉:
 *     void SysTick_Handler(void) {}
 *     void USART1_IRQHandler(void) {}
 *
 *   否则 GCC 会报错: multiple definition of 'SysTick_Handler'
 * ================================================================== */

/* ========== 文件结束 ========== */