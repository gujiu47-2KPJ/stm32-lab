/**
 ******************************************************************************
 * @file    encode.c
 * @brief   旋转编码器驱动实现
 * @note    TIM2 编码器模式: CH1=PA0, CH2=PA1
 *          编码器旋转时 CNT 自动加减, 无需中断
 ******************************************************************************
 */

#include "encode.h"

extern TIM_HandleTypeDef htim2;

/** @brief 编码器累计值 (应用层可见) */
static int16_t acc = 0;
/** @brief 上一次 CNT 值, 用于计算增量 */
static uint16_t last_count = 0;

/**
 * @brief  启动编码器模式, 内部状态清零
 */
void Encode_Init(void)
{
    HAL_TIM_Encoder_Start(&htim2, TIM_CHANNEL_ALL);
    acc = 0;
    last_count = __HAL_TIM_GET_COUNTER(&htim2);
}

/**
 * @brief  读取并累计编码器增量
 * @retval 累计值 (0 ~ MAX_ENCODE)
 * @note   实现双向增数，需要的数字范围是0:3，考虑到tim是四分频，所以要delta/4
 *         每次调用时更新 last_count 基准
 */
int16_t Encode_ReadDelta(void)
{
    uint16_t cnt = __HAL_TIM_GET_COUNTER(&htim2);
    int16_t delta = cnt - last_count;

    acc += delta/4;//注意uint8会触底反弹
    
    last_count = cnt;
	if(acc>MAX_ENCODE) acc = MAX_ENCODE;
    return acc;
}

/**
 * @brief  获取当前累计值
 * @retval 编码器累计值
 */
int16_t Encode_GetValue(void)
{
    return acc;
}

/**
 * @brief  设置累计值 (超出上限自动裁剪)
 * @param  val 设定的初始值
 */
void Encode_SetValue(uint8_t val)
{
    acc = val;
    if (val > MAX_ENCODE) {
        acc = MAX_ENCODE;
    }
}

/**
 * @brief  累计值清零
 */
void Encode_Reset(void)
{
    acc = 0;
}

/**
 * @brief  检测编码器是否有旋动
 * @retval 1 旋动过, 0 未动
 * @note   比较当前 CNT 与上一次记录值, 用于翻页触发
 */
uint8_t Encode_Moved(void)
{
    static uint16_t last = 0;//static 修饰的局部变量特性是：= 0 只在程序启动时执行一次，不是每次调用都归零
    uint16_t now = __HAL_TIM_GET_COUNTER(&htim2);

    if (now != last) {
        last = now;
        return 1;
    }
    last = now;
    return 0;
}