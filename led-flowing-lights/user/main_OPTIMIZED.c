/* =========================================================================
 * 项目: LED 流水灯 (标准库, PA0~PA7 轮流点亮)
 * 原文件: user/main.c
 * 优化版: user/main_OPTIMIZED.c
 *
 * 优化点: GPIO_Mode_Out_OD → GPIO_Mode_Out_PP
 *   原:  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_OD;
 *         开漏输出: 只能拉低, 拉高靠外部上拉
 *
 *   改:  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
 *         推挽输出: 高/低电平都由芯片主动驱动, LED 亮度一致
 *
 * 原理:
 *   Out_OD (开漏): 输出 0 时内部 MOS 拉低到 GND; 输出 1 时内部是高阻
 *                  如果没有外部上拉电阻, "高电平"实际上是浮空, LED 可能不亮
 *   Out_PP (推挽): 输出 0 时内部 MOS 拉低到 GND; 输出 1 时内部 MOS 拉高到 VCC
 *                  高低电平都有保证, LED 亮度一致, 最稳妥
 *
 * 附加优化: 把 8 次独立 Write 改成 for 循环, 更简洁
 * ========================================================================= */

#include "stm32f10x.h"
#include "Delay.h"     /* Device header */

int main(void)
{
    /* ---------- 1. 开启 GPIOA 时钟 ---------- */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

    /* ---------- 2. 配置 PA0~PA7 为推挽输出 ---------- */
    GPIO_InitTypeDef GPIO_InitStructure;
    /* 关键优化: Out_OD → Out_PP
     * 推挽输出: 高/低电平都由芯片内部主动驱动, LED 亮度稳定 */
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_2
                                  | GPIO_Pin_3 | GPIO_Pin_4 | GPIO_Pin_5
                                  | GPIO_Pin_6 | GPIO_Pin_7;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /* ---------- 3. 主循环: PA0→PA7 依次点亮 ---------- */
    while(1)
    {
        uint8_t i;
        /* 用循环代替 8 次重复代码 */
        for (i = 0; i < 8; i++)
        {
            /* ~(1 << i): 除了第i位是 0, 其他位都是 1
             * GPIO_Write 一次性写 16 位, 对应 0~15 号引脚 */
            GPIO_Write(GPIOA, (uint16_t)~(1 << i));
            Delay_ms(100);
        }
    }
}