/* =========================================================================
 * 项目: 串口发送 (标准库, 串口回显)
 * 原文件: user/main.c
 * 修复版: user/main_MODIFIED.c
 *
 * Bug: 第19行逻辑错误 - 用错了Serial库函数
 *   原始:  if (Serial_GetRxData() == 1)      // ❌
 *
 *   分析:  Serial_GetRxData() 返回的是"接收缓冲区里的数据字节"
 *          上面那句的意思是: "如果收到的数据字节恰好是0x01, 才处理"
 *          那只有收到 0x01 这个特定值才会进入循环, 其他数据都被忽略!
 *
 *   正确:  Serial_GetRXFlag() 返回的是"有没有新数据到达" (1=有, 0=没有)
 *          if (Serial_GetRXFlag() == 1)       // ✅
 *
 *   原理:  串口通信分两步:
 *          1. 中断到达时, USART1_IRQHandler 把收到的字节写入缓冲区,
 *             同时把一个"有新数据"的标志位置 1
 *          2. 主循环里检测这个标志, 若为 1, 则取出数据字节
 *          Serial_GetRXFlag() → 拿标志
 *          Serial_GetRxData() → 拿数据
 *          这两个不能搞混
 * ========================================================================= */

#include "stm32f10x.h"
#include "Delay.h"     /* Device header */
#include "OLED.h"
#include "Serial.h"

uint8_t RxData;       /* 存放收到的字节 */

int main(void)
{
    /* ---------- 初始化顺序: OLED → Serial ---------- */
    OLED_Init();
    OLED_ShowString(1, 1, "RxData:");

    Serial_Init();

    while(1)
    {
        /* ================ 关键修复 ================
         * 原始: if (Serial_GetRxData() == 1)
         *       ↑ 这里判断的是"数据值是不是0x01", 完全不对
         *
         * 修复: if (Serial_GetRXFlag() == 1)
         *       ↑ 这里判断"有没有新数据到达", 这才是正确的用法
         * ============================================ */
        if (Serial_GetRXFlag() == 1)
        {
            /* 有新数据: 先拿数据, 再回传给上位机, 再显示到OLED */
            RxData = Serial_GetRxData();
            Serial_SendByte(RxData);                 /* 回显 */
            OLED_ShowHexNum(1, 8, RxData, 2);        /* OLED 显示十六进制 */
        }
    }
}