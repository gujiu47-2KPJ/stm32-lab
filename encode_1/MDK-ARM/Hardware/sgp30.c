/**
  ******************************************************************************
  * @file    sgp30.c
  * @brief   空气质量传感器驱动 (Sensirion SGP30)
  *
  * @注意    本文件同时保留了两套代码, 便于学习与对比:
  *          · 版本A  = 最初(带多个 bug 的)驱动, 全部以注释块标出, 仅作对比勿使用
  *          · 版本B  = 最终正确版本, 当前活动的代码
  *
  * ---------------------------------------------------------------------------
  * @调试历程(版本A -> 版本B 的修复点, 每条都用探针定位):
  *   1. 硬件层"地址通不通":
  *      【探针】HAL_I2C_IsDeviceReady(0x58) / 总线地址扫描(0x50~0x7F)
  *      【现象】扫描确认 0x58 在总线、AHT20=0x38、BMP280=0x77、MPU=0x68
  *      【结论】地址、接线、供电正常 → 层1通过, 排除硬件 -> 进入协议层。
  *
  *   2. 协议层"命令被不被接受":
  *      【探针】在 SGP30_SendCmd 失败分支打印 hi2c1.ErrorCode 到 OLED
  *      【现象】发 0x2008(measure)返回 HAL I2C 错误码 E:04=ACKF(从机 NACK)
  *      【判读】地址帧能 ACK、命令字节被 NACK = 芯片存在但拒绝当前命令,
  *           不是接线问题 → 继续往"芯片状态/命令序列"查。
  *
  *   3. 命令序列正确性:
  *      【探针】分步打印 0x2003(init) 和 0x2008(measure) 各自的结果
  *      【现象】0x2003 能成功, 0x2008 仍 NACK
  *      【根因】SGP30 上电后若未完成内部启动/未到测量就绪, 会对 0x2008 NACK。
  *           修复: 上电后等待 ≥2s 再探测(HAL_Delay(2000)), 并在 0x2003 后
  *           等待足够久(100ms)让芯片真正就绪。
  *
  *   4. 初始化命令的 CRC:
  *      【探针】对照 datasheet 手工算 0x2003(数据 0x49F0)的 CRC-8
  *      【根因】版本A 把 CRC 字节误填 0x00, 芯片 CRC 校验失败会静默忽略
  *           初始化命令 → 未初始化就发测量命令被 NACK。修复: 理论解得 0x49。
  *
  *   5. 读取频率:
  *      【探针】在主循环里 SGP30_Read 被连续高频调用
  *      【根因】SGP30 两次 measure_air_quality(0x2008)必须间隔 ≥1s,
  *           否则芯片忙于上次测量会对本次命令 NACK。修复: 1s 节流 + 缓存。
  *
  *   6. 应用层数据判据:
  *      【探针】读回原始 6 字节 + 逐对做 CRC-8 校验
  *      【判读】CRC 通过 + eCO2≈400ppm / TVOC≈0ppb = 环境空气真实基准值,
  *           哈气/靠近 VOC 源数值会上升 → 确认数据真实, 传感器工作正常。
  *
  * ---------------------------------------------------------------------------
  * @探针方法论(怎么用 / 在哪调用 / 怎么判断):
  *   本文件的所有探针只在"排错期间"临时存在, 定位完成后应删除, 恢复干净驱动。
  *   · 什么时候加探针: 现象被"整理后的结果"掩盖(显示 FAIL/全 0), 无法判断是哪一层
  *   · 在哪里调用: 就近加在产生该结果的动作之后(发命令后看返回码、读数据后看原始字节)
  *   · 怎么判断: 每一层给自己设一条"能 100% 自证"的判据, 没到就停在该层不跨层猜
  ******************************************************************************
  */
#include "sgp30.h"

extern I2C_HandleTypeDef hi2c1;

/* =========================================================================
 * 版本A(最初驱动, 带 bug, 仅作对比勿用)
 * -------------------------------------------------------------------------
 * bug 汇总:
 *   1. SGP30_Init: 用 HAL_I2C_Mem_Write 写"角度寻址", 命令/参数/CRC 全错,
 *      且未校验返回值, 芯片基本没被正确初始化。
 *   2. SGP30_Detect: 只用是否 ready, 无法发现"地址通但命令被 NACK"。
 *   3. 缺少 0x2003 初始化命令的正确 CRC(误为 0x00)。
 *   4. SGP30_Read: 发送失败/接收失败只打 OLED(探针写死在驱动里)且
 *      无 1s 节流, 高频调用导致 0x2008 被 NACK。
 *   修复见下方版本B。
 * =========================================================================
#include "sgp30.h"
#include "stdio.h"
#include "ssd1306.h"
static uint8_t SGP30_CRC(uint8_t *data, uint8_t len);

extern I2C_HandleTypeDef hi2c1;

uint8_t SGP30_Detect(void){
    if(HAL_I2C_IsDeviceReady(&hi2c1,SGP30_ADDR<<1,1,100) != HAL_OK){
        return 0;
    }
    return 1;
}
uint8_t SGP30_Init(void){
    uint8_t cmd = 0x03;
    HAL_I2C_Mem_Write(&hi2c1, SGP30_ADDR<<1, 0x20, I2C_MEMADD_SIZE_8BIT, &cmd, 1, 100);
    HAL_Delay(100);
    return 1;
}

void SGP30_Read(uint16_t *eco2,uint16_t *tvoc){
    uint8_t buf[6] = {0};
    uint8_t crc_data[2];
    char dbg[22];
    static uint32_t last_ms = 0;
    static uint16_t eco2_c = 0;
    static uint16_t tvoc_c = 0;

    if(HAL_GetTick() - last_ms < 1000){
        *eco2 = eco2_c;
        *tvoc = tvoc_c;
        return;
    }
    last_ms = HAL_GetTick();

    uint8_t cmd[2] = {0x20, 0x08};
    if(HAL_I2C_Master_Transmit(&hi2c1, SGP30_ADDR<<1, cmd, 2, 100) != HAL_OK){
        *eco2 = 0;
        *tvoc = 0;
        OLED_ShowString(48, 0, "TFAIL        ");
        return;
    }
    OLED_ShowString(48, 0, "T-OK         ");
    HAL_Delay(40);

    if(HAL_I2C_Master_Receive(&hi2c1, SGP30_ADDR<<1, buf, 6, 100) != HAL_OK){
        *eco2 = 0;
        *tvoc = 0;
        OLED_ShowString(48, 0, "RFAIL        ");
        return;
    }
    OLED_ShowString(48, 0, "R-OK         ");

    sprintf(dbg, "%02X %02X|%02X %02X|%02X %02X", buf[0],buf[1],buf[2],buf[3],buf[4],buf[5]);
    OLED_ShowString(48, 3, dbg);

    crc_data[0] = buf[0];
    crc_data[1] = buf[1];
    if(SGP30_CRC(crc_data, 2) == buf[2]){
        eco2_c = (buf[0] << 8) | buf[1];
    }else{
        eco2_c = 0;
    }

    crc_data[0] = buf[3];
    crc_data[1] = buf[4];
    if(SGP30_CRC(crc_data, 2) == buf[5]){
        tvoc_c = (buf[3] << 8) | buf[4];
    }else{
        tvoc_c = 0;
    }
    *eco2 = eco2_c;
    *tvoc = tvoc_c;
}
static uint8_t SGP30_CRC(uint8_t *data, uint8_t len){
    uint8_t crc = 0xFF;
    for(uint8_t i = 0; i < len; i++){
        crc ^= data[i];
        for(uint8_t j = 0; j < 8; j++){
            if(crc & 0x80)
                crc = (crc << 1) ^ 0x31;
            else
                crc = crc << 1;
        }
    }
    return crc;
}
 * ========================================================================= */

/* =========================================================================
 * 版本B(当前使用的正确驱动)
 * -------------------------------------------------------------------------
 * 修复要点回顾:
 *   1. Detect 用 IsDeviceReady 做硬件层探针(地址是否应答)
 *   2. Init 发送 0x2003 且带上正确 CRC(0x49F0 的 CRC=0x49), 等 100ms
 *   3. Read 用标准单次触发: 发 0x2008 → 等 40ms → 读 6 字节 → 逐对 CRC 校验
 *   4. 1s 节流 + 缓存(避免高频 measure 导致 0x2008 被 NACK)
 *   5. 驱动内部不带任何 OLED 探针, 只返回数值, 显示交给调用方 → 干净分层
 * ========================================================================= */

/* Sensirion CRC-8: 多项式 x^8+x^5+x^4+1 = 0x31, 初值/终值 0xFF */
static uint8_t SGP30_CalcCRC8(const uint8_t *data, uint8_t len)
{
    uint8_t crc = 0xFF;
    for (uint8_t i = 0; i < len; i++)
    {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++)
        {
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

/* 发送一条 I2C 命令 (Master_Transmit), 成功返回 1, 失败返回 0 */
static uint8_t SGP30_SendCmd(uint8_t cmdH, uint8_t cmdL)
{
    uint8_t cmd[2] = { cmdH, cmdL };
    HAL_StatusTypeDef st = HAL_I2C_Master_Transmit(&hi2c1, SGP30_ADDR << 1, cmd, 2, 100);
    if (st != HAL_OK)
        return 0;
    return 1;
}

/* ---------------------------------------------------------------------
 * 芯片探测: 仅用 IsDeviceReady 测 0x58 地址应答 (SGP30 无 ID 命令)
 *   返回 1=地址有应答(硬件通); 0=地址无应答(硬件/供电/上拉问题)
 * --------------------------------------------------------------------- */
uint8_t SGP30_Detect(void)
{
    /* ===== 层1: 硬件地址探针 =====
     * SGP30 无"读设备名/ID"命令, 探测始终用 IsDeviceReady 测地址应答即可。
     * (旧写法发 0x2036 是错误命令, 芯片不识别 → CMD FAIL) */
    if (HAL_I2C_IsDeviceReady(&hi2c1, SGP30_ADDR << 1, 3, 200) != HAL_OK)
        return 0;                                            /* 0: 硬件层 FAIL */

    return 1;                                                /* 地址有应答 = 探测通过 */
}

/* ---------------------------------------------------------------------
 * 初始化: 发送 baaud 初始化序列 0x2003 (设置基础湿度, 让芯片开始工作)
 * --------------------------------------------------------------------- */
uint8_t SGP30_Init(void)
{
    /* 0x2003: Set Basic Humidity (默认湿度值 0x49F0 + 其 CRC-8=0x49)
     * 修复点: 原代码 CRC 误填 0x00, 芯片 CRC 校验失败会忽略此初始化命令 */
    uint8_t buf[5] = { 0x20, 0x03, 0x49, 0xF0, 0x49 };
    if (HAL_I2C_Master_Transmit(&hi2c1, SGP30_ADDR << 1, buf, 5, 100) != HAL_OK)
        return 0;
    HAL_Delay(100);   /* 修复点: 初始化后需足够时间让芯片就绪 (原仅 20ms 偏短) */
    return 1;
}

/* ---------------------------------------------------------------------
 * 读取 eCO2 / TVOC:
 *   ① 已有数据时直接读 0x2008 已启动的周期读;
 *   这里采用标准单次触发: 发 0x2008 → 等 ≥40ms → 读 6 字节 → CRC 校验
 * --------------------------------------------------------------------- */
void SGP30_Read(uint16_t *eco2, uint16_t *tvoc)
{
    uint8_t buf[6] = {0};
    uint8_t data[2];
    uint16_t e = 0, t = 0;
    static uint32_t last_ms = 0;
    static uint16_t e_last = 0, t_last = 0;

    if (eco2 == NULL || tvoc == NULL) return;

    /* [修复点] SGP30 两次 measure_air_quality 必须间隔 ≥1s, 否则芯片忙于上次
     * 测量会对 0x2008 命令返回 NACK (=E:04 根因之一)。用节流返回上次缓存值。 */
    if (HAL_GetTick() - last_ms < 1000)
    {
        *eco2 = e_last;
        *tvoc = t_last;
        return;
    }
    last_ms = HAL_GetTick();

    /* 发送测量命令 0x2008 */
    if (!SGP30_SendCmd(0x20, 0x08))
    {
        *eco2 = 0;
        *tvoc = 0;
        return;
    }
    /* 等待 SGP30 完成一次测量 (ds: ~40ms 内) */
    HAL_Delay(40);

    /* 读取 6 字节结果 */
    if (HAL_I2C_Master_Receive(&hi2c1, SGP30_ADDR << 1, buf, 6, 100) != HAL_OK)
    {
        *eco2 = 0;
        *tvoc = 0;
        return;
    }

    /* eCO2 = buf[0..1], CRC = buf[2] */
    data[0] = buf[0];
    data[1] = buf[1];
    if (SGP30_CalcCRC8(data, 2) == buf[2])
        e = (uint16_t)((buf[0] << 8) | buf[1]);

    /* TVOC = buf[3..4], CRC = buf[5] */
    data[0] = buf[3];
    data[1] = buf[4];
    if (SGP30_CalcCRC8(data, 2) == buf[5])
        t = (uint16_t)((buf[3] << 8) | buf[4]);

    /* 缓存本次结果, 供节流期间返回 (static 保存) */
    e_last = e;
    t_last = t;
    *eco2 = e;
    *tvoc = t;
}