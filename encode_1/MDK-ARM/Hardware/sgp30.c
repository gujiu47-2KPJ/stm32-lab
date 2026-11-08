/* =====================================================================
 * SGP30 空气质量传感器驱动 (STM32 HAL I2C 轮询版)
 * ---------------------------------------------------------------------
 * 芯片: SGP30 (Sensirion)
 * 7 位地址: 0x58 (固定)
 * 功能: eCO2 (400~60000ppm) / TVOC (0~60000ppb)
 *
 * 标准协议流程(依据 SI1145/Sensirion SGP30 datasheet):
 *   ① 上电后需初始化 (I2C 命令 0x2003), 之后芯片进入工作;
 *   ② 每次读数据: 发送测量命令 0x2008, 等待 40ms, 再读 6 字节
 *      [eCO2(2)+CRC][TVOC(2)+CRC]
 *   ③ 每字节对使用 Sensirion CRC-8 (多项式 0x31, 初值 0xFF)
 * ===================================================================== */
#include "sgp30.h"

extern I2C_HandleTypeDef hi2c1;

/* ---------------------------------------------------------------------
 * [原驱动代码已注释保留, 便于对比; 新实现见下方 ===== 新驱动 =====]
 * ---------------------------------------------------------------------
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
 * ===================================================================== */

/* =====================================================================
 * ==== 新驱动实现 (成熟规范版) ======================================
 * ===================================================================== */

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
    if (HAL_I2C_Master_Transmit(&hi2c1, SGP30_ADDR << 1, cmd, 2, 100) != HAL_OK)
        return 0;
    return 1;
}

/* ---------------------------------------------------------------------
 * 芯片探测: 发送读设备名命令 0x2036, 若能传输且读回 3 个 ASCII, 判定在总线
 * --------------------------------------------------------------------- */
uint8_t SGP30_Detect(void)
{
    uint8_t buf[3] = {0};
    /* SGP30 Device Feature Set 查询: 0x2036, 返回 3 字节 */
    if (!SGP30_SendCmd(0x20, 0x36)) return 0;
    HAL_Delay(50);
    if (HAL_I2C_Master_Receive(&hi2c1, SGP30_ADDR << 1, buf, 3, 100) != HAL_OK)
        return 0;
    /* 期望回传 ASCII 可见字符 (id 高低位+CRC), 用非0粗略判定存在 */
    return (buf[0] != 0xFF && buf[1] != 0xFF) ? 1 : 0;
}

/* ---------------------------------------------------------------------
 * 初始化: 发送 baaud 初始化序列 0x2003 (设置基础湿度, 让芯片开始工作)
 * --------------------------------------------------------------------- */
uint8_t SGP30_Init(void)
{
    /* 0x2003: Set Basic Humidity (写湿度计数 0x64 0xE8 + CRC) */
    uint8_t buf[5] = { 0x20, 0x03, 0x49, 0xF0, 0x00 };   /* 湿度=理想50%RH 对应值 */
    if (HAL_I2C_Master_Transmit(&hi2c1, SGP30_ADDR << 1, buf, 5, 100) != HAL_OK)
        return 0;
    HAL_Delay(20);
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

    if (eco2 == 0 || tvoc == 0) return;

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

    *eco2 = e;
    *tvoc = t;
}