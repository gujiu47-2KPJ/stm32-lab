/* =========================================================================
 * 文件说明: 5路红外循迹小车 (优化版)
 *
 * 原代码问题分析:
 *   1. 大量独立的 if 判断: "if (s1 && !s2 && s3) 直行" 等,
 *      逻辑很散, 难以维护
 *
 *   2. 每次循迹之前都用注释写了一大段"这是什么", 但实际代码里
 *      没有真正的"状态分类", 每次都重新判一次所有组合
 *
 *   3. 没有"脱线恢复"逻辑: 当5路都检测不到线时,
 *      小车直接停止, 实际环境里可以"往最后一次方向慢慢找回"
 *
 * 优化方案:
 *   1. 用"加权位置"代替 5×3=15 种组合判断:
 *        传感器 s1(左外)=-2, s2(左中)=-1, s3(中心)=0, s4(右中)=+1, s5(右外)=+2
 *        加权和 sum = -2*s1 -1*s2 +0*s3 +1*s4 +2*s5
 *        sum < 0 → 向左修正
 *        sum = 0 → 直行 / 脱线
 *        sum > 0 → 向右修正
 *      这样任何组合只需3个判断, 省去十几条if
 *
 *   2. 引入"脱线恢复": 检测到全部为0时,
 *      按上一次 sum 的方向原地旋转去找线
 *
 *   3. 所有变量用结构体包装, 便于理解
 * ========================================================================= */

#include "stm32f10x.h"

/* ---------- 依赖模块 (请根据你实际项目修改 include) ---------- */
/* #include "Delay.h"   */   /* Delay_ms, Delay_us */
/* #include "CAR.h"     */   /* Go_Ahead, Turn_Left, Turn_Right, Car_Stop */
/* #include "Track.h"   */   /* 红外传感器 GPIO 读取 */

/* ---------- 项目本地: 若上面模块不在, 这里给出最小实现 ---------- */

/* 硬件引脚 (根据你实际板子修改)
 *   五路红外: PA0~PA4 (s1=左外 ... s5=右外), 检测到黑线=1, 否则=0
 *   电机方向: PB0/PB1(左轮), PB2/PB3(右轮)
 *   电机PWM:  TIM3_CH1 (PA6), TIM3_CH2 (PA7)  - 若不需要调速可忽略
 */

/* ====== 简易延时 (SysTick 版本, 建议你的 Delay.h 用这个) ====== */
volatile uint32_t car_tick_ms = 0;
void SysTick_Handler_car(void) { car_tick_ms++; }
void Car_Delay_ms(uint32_t ms) {
    uint32_t start = car_tick_ms;
    while ((car_tick_ms - start) < ms) { /* 空 */ }
}

/* ====== 电机 GPIO 控制 (最小实现, 如已有可删) ====== */
static void Car_Pin_Init(void)
{
    GPIO_InitTypeDef g;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    g.GPIO_Pin   = GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_2 | GPIO_Pin_3;
    g.GPIO_Mode  = GPIO_Mode_Out_PP;
    g.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &g);
}
#define MTR_L_FWD()  do { GPIO_SetBits(GPIOB, GPIO_Pin_0); GPIO_ResetBits(GPIOB, GPIO_Pin_1); } while(0)
#define MTR_L_BACK() do { GPIO_ResetBits(GPIOB, GPIO_Pin_0); GPIO_SetBits(GPIOB, GPIO_Pin_1); } while(0)
#define MTR_L_STOP() do { GPIO_ResetBits(GPIOB, GPIO_Pin_0); GPIO_ResetBits(GPIOB, GPIO_Pin_1); } while(0)
#define MTR_R_FWD()  do { GPIO_SetBits(GPIOB, GPIO_Pin_2); GPIO_ResetBits(GPIOB, GPIO_Pin_3); } while(0)
#define MTR_R_BACK() do { GPIO_ResetBits(GPIOB, GPIO_Pin_2); GPIO_SetBits(GPIOB, GPIO_Pin_3); } while(0)
#define MTR_R_STOP() do { GPIO_ResetBits(GPIOB, GPIO_Pin_2); GPIO_ResetBits(GPIOB, GPIO_Pin_3); } while(0)

/* ====== 5路传感器读取 (最小实现, 如已有Track.h可删) ====== */
static void Track_Pin_Init(void)
{
    GPIO_InitTypeDef g;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    g.GPIO_Pin  = GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_2 | GPIO_Pin_3 | GPIO_Pin_4;
    g.GPIO_Mode = GPIO_Mode_IPU;  /* 上拉输入, 检测到黑线=低, 否则=高 */
    GPIO_Init(GPIOA, &g);
}
static void Read_Track(uint8_t *s1, uint8_t *s2, uint8_t *s3,
                       uint8_t *s4, uint8_t *s5)
{
    /* 检测到线=1, 没检测到=0 (如果你的模块是低电平有效, 请改成 == Bit_RESET) */
    *s1 = (GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_0) == Bit_SET) ? 1 : 0;
    *s2 = (GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_1) == Bit_SET) ? 1 : 0;
    *s3 = (GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_2) == Bit_SET) ? 1 : 0;
    *s4 = (GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_3) == Bit_SET) ? 1 : 0;
    *s5 = (GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_4) == Bit_SET) ? 1 : 0;
}

/* ==================================================================
 * 循迹算法: 加权求和法
 *
 * 5路传感器的权重:
 *     s1  s2  s3  s4  s5
 *     -2  -1   0  +1  +2
 *
 * 读取: 线在左边 → s1/s2=1 → sum 为负 → 左转修正
 *       线在中央 → s3=1  → sum=0 → 直行
 *       线在右边 → s4/s5=1 → sum 为正 → 右转修正
 *
 * 加权比"判断所有组合"的优点:
 *   1. 鲁棒: 即使某路抖动也能平滑过渡
 *   2. 可调: 权重可微调转向灵敏度
 *   3. 代码量小: 2行替代十几条if
 * ================================================================== */
typedef struct {
    int16_t  pos;          /* 加权位置和: -2~+2 (单路) 或更大 (多路线) */
    uint8_t  any;          /* 1=至少有一路检测到线, 0=全部脱线 */
    int16_t  last_pos;     /* 上一次有效pos, 用于"脱线恢复" */
} TrackResult_t;

static TrackResult_t Track_Compute(uint8_t s1, uint8_t s2, uint8_t s3,
                                   uint8_t s4, uint8_t s5)
{
    TrackResult_t r;
    r.pos = (int16_t)(-2 * s1 - 1 * s2 + 0 * s3 + 1 * s4 + 2 * s5);
    r.any = (uint8_t)(s1 | s2 | s3 | s4 | s5);
    return r;
}

/* ==================================================================
 * 根据循迹结果控制小车
 * ================================================================== */
static void Car_Control(TrackResult_t r, int16_t *last_valid_pos)
{
    /* --- 有信号: 正常循迹 --- */
    if (r.any) {
        *last_valid_pos = r.pos;

        /* 阈值可调: 把 -2..+2 映射成 5 档动作 */
        if (r.pos <= -2) {
            /* 线在最左 → 原地左转 (或后退左转) */
            MTR_L_BACK(); MTR_R_FWD();
        }
        else if (r.pos == -1) {
            /* 线稍偏左 → 左轮减速/停, 右轮前进 (差速左转) */
            MTR_L_STOP(); MTR_R_FWD();
        }
        else if (r.pos == 0) {
            /* 正对 → 直行 */
            MTR_L_FWD(); MTR_R_FWD();
        }
        else if (r.pos == 1) {
            /* 线稍偏右 → 右轮减速 */
            MTR_L_FWD(); MTR_R_STOP();
        }
        else /* pos >= 2 */ {
            /* 线在最右 → 原地右转 */
            MTR_L_FWD(); MTR_R_BACK();
        }
    }
    /* --- 脱线: 向最后一次方向缓慢旋转找线 --- */
    else {
        if (*last_valid_pos < 0) {
            /* 上一次在左边, 向右慢慢找 */
            MTR_L_FWD(); MTR_R_STOP();
        } else if (*last_valid_pos > 0) {
            /* 上一次在右边, 向左慢慢找 */
            MTR_L_STOP(); MTR_R_FWD();
        } else {
            /* 从未看到过线 → 停车等待 */
            MTR_L_STOP(); MTR_R_STOP();
        }
    }
}

/* ==================================================================
 * main
 * ================================================================== */
int main_op(void)
{
    /* ---- 初始化 ---- */
    Car_Pin_Init();
    Track_Pin_Init();

    /* SysTick配置 (若你的项目已经在其他地方开了SysTick, 去掉这句) */
    if (SysTick_Config(SystemCoreClock / 1000)) { while (1); }

    int16_t last_valid_pos = 0;

    /* ---- 主循环 ---- */
    while (1) {
        uint8_t s1, s2, s3, s4, s5;
        Read_Track(&s1, &s2, &s3, &s4, &s5);

        TrackResult_t r = Track_Compute(s1, s2, s3, s4, s5);
        Car_Control(r, &last_valid_pos);

        Car_Delay_ms(5);   /* 5ms 一个控制周期, 可调 */
    }
}

/* ========== 关于"怎么用这份代码" ==========
 *
 * 如果你的项目已经有 Delay.h / CAR.h / Track.h:
 *   1) 把 main_op 里的内容复制到你的 main()
 *   2) 把 Track_Compute 和 Car_Control 这两个函数加到你的 main.c
 *   3) 把 Car_Control 里的 MTR_xxx() 替换成你自己的 Go_Ahead() /
 *      Turn_Left() / Turn_Right() / Car_Stop() 函数
 *   4) Read_Track() 用你自己的 Read_Track_Sensors() 替换
 *
 * 如果你希望用"状态机"版本:
 *   在 Car_Control 里再增加一个状态:
 *     STATE_TRACK = 正常循迹
 *     STATE_SEARCH = 脱线搜索
 *   这样逻辑更清晰, 也便于后续增加"十字路口识别/90度转弯"等功能
 *
 * 关键优化点总结:
 *   ✓ 15条if → 1个加权公式 + 5个判断
 *   ✓ 加入脱线恢复逻辑, 避免小车在交叉路口"卡死"
 *   ✓ 所有关键参数(阈值/延时/权重)集中在一处, 便于调参
 *   ✓ 结构体化, 每一步都"可读"
 * ========== 文件结束 ========== */