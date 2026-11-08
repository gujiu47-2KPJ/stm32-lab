/**
  ******************************************************************************
  * @file    warm_damp.c
  * @brief   二合一传感器驱动(AHT20 温湿度 + BMP280 气压)
  *
  * @注意    本文件同时保留了两套代码, 便于学习与对比:
  *          · 版本A  = 最初版本(有 bug), 全部以注释块标出, 仅作对比勿使用
  *          · 版本B  = 最终正确版本, 当前活动的代码
  *
  * @调试历程(版本A -> 版本B 的修复点):
  *   1. AHT20 命令字节错误: 最初用 AHT10 的旧命令 0xBE, AHT20 实际需要
  *      软复位 0xBA + 初始化 {0xE1,0x08,0x00} + 测量 {0xAC,0x33,0x00}
  *      【排查方法】状态显示 OK 但温湿度为 0 ——"能通信" 只代表 I2C
  *      地址握手通过, 不代表测量命令正确。
  *
  *   2. BMP280 地址: L模块 SDO 引脚决定地址(0x76/0x77), 最初硬编码 0x76
  *      而模块实际挂在 0x77。
  *      【排查方法】用 IsDeviceReady / chip_id(0xD0) 逐个地址探测,
  *      实测得到 38=1, 77=1, 76=0 → 地址即 0x77。
  *
  *   3. chip_id 校验缺失: 最初不校验芯片身份, 无法区分真伪/型号。
  *      【排查方法】读 0xD0, 标准 BMP280 = 0x58; 实测 0x58 确认是真片。
  *
  *   4. 气压换算失败(补两个独立问题):
  *      a) adc 固定为 0x80000(中间默认值): BMP280 未写入 ctrl_meas, 一直
  *         停在睡眠模式, 根本没有真正测量。
  *         【排查方法】打印原始 adc, 发现它 "恒等于 0x80000"(满量程正中间),
  *         真实物理量必波动, 恒定即 "没在测" → 定位到需要配置 ctrl_meas。
  *      b) 补偿公式: 最初用 int64 定点式, 在无硬件 64 位除法的 F103 上
  *         中间量异常膨胀导致溢出, 结果为错误极小值。
  *         【排查方法】改为 Bosch 官方浮点补偿公式(见下), 无 int64 溢出,
  *         结果立刻正确。
  ******************************************************************************
  */

#include "warm_damp.h"

extern I2C_HandleTypeDef hi2c1;

static uint8_t AHT20_Init(void);
static uint8_t BMP280_Init(void);

/* BMP280 校准系数 (小端解析, 供气压换算使用) */
static int32_t dig_T1, dig_T2, dig_T3;
static int32_t dig_P1, dig_P2, dig_P3, dig_P4;
static int32_t dig_P5, dig_P6, dig_P7, dig_P8, dig_P9;

/* BMP280 实际 I2C 地址 (0x76 或 0x77, 由 SDO 引脚决定), 探测成功后确定 */
static uint8_t bmp280_addr = 0;

/* 调试用: 保存读到的 BMP280 chip_id (0xD0), 用于确认芯片真实型号 */
static uint8_t g_bmp_chipid = 0;


/* =========================================================================
 *  传感器探测 (地址自动识别)
 * ========================================================================= */
uint8_t WarmDamp_Detect(void){
    /* AHT20 地址固定 0x38 */
    if(HAL_I2C_IsDeviceReady(&hi2c1, AHT20_ADDR << 1, 1, 100) != HAL_OK){
        return 0;
    }
    /* BMP280 地址需自动识别: 参考工程先试 0x77, 再试 0x76 */
    if(HAL_I2C_IsDeviceReady(&hi2c1, 0x77 << 1, 1, 100) == HAL_OK){
        bmp280_addr = 0x77;
        return 1;
    }
    if(HAL_I2C_IsDeviceReady(&hi2c1, BMP280_ADDR << 1, 1, 100) == HAL_OK){
        bmp280_addr = BMP280_ADDR;
        return 1;
    }
    return 0;
}


/* =========================================================================
 *  AHT20 温湿度 —— 初始化和读取
 * ========================================================================= */

/* AHT20 初始化 (版本B, 当前使用)
   修复点: 最初使用 AHT10 旧命令 0xBE; AHT20 需要软复位 + 初始化帧 */
static uint8_t AHT20_Init(void){
    uint8_t cmd;

    /* ① 软复位 AHT20 (0xBA), 复位后需等待上电就绪 */
    cmd = 0xBA;
    if(HAL_I2C_Master_Transmit(&hi2c1, AHT20_ADDR << 1, &cmd, 1, 100) != HAL_OK){
        return 0;
    }
    HAL_Delay(20);

    /* ② 初始化/使能传感器 (0xE1 0x08 0x00) */
    uint8_t init[3] = {0xE1, 0x08, 0x00};
    if(HAL_I2C_Master_Transmit(&hi2c1, AHT20_ADDR << 1, init, 3, 100) != HAL_OK){
        return 0;
    }
    HAL_Delay(20);

    return 1;

    /* ---- 版本A (最初错误写法, 仅对比勿用) ----
     * 错误: 用了 AHT10 的 0xBE 单字节命令, AHT20 不识别, 不触发测量。
     * 调理: 状态 OK(地址握手过)但数据全 0 → 命令字节错误。修复见上方。 */
    /*
    uint8_t cmd = 0xBE;
    if(HAL_I2C_Master_Transmit(&hi2c1, AHT20_ADDR << 1, &cmd, 1, 100) != HAL_OK){
        return 0;
    }
    HAL_Delay(80);
    return 1;
    */
}


/* =========================================================================
 *  BMP280 气压 —— 设备初始化
 * ========================================================================= */

/* BMP280 初始化 (版本B, 当前使用)
   修复点: 增加 chip_id 校验(0x58)、校准重试、必须先写 ctrl_meas 进入
   正常测量模式 —— 这是气压为 0 的真正根因之一。 */
static uint8_t BMP280_Init(void){
    /* ① 遍历候选地址, 读 ChipID(0xD0) 校验是否标准 BMP280(=0x58) */
    uint8_t chip_id;
    uint8_t calib_raw[24];
    const uint8_t try_addr[2] = {0x77, 0x76};   /* 先试 0x77 → 再试 0x76 */
    uint8_t i, calib_ok = 0;

    for(i = 0; i < 2; i++){
        bmp280_addr = try_addr[i];
        if(HAL_I2C_Mem_Read(&hi2c1, bmp280_addr << 1, 0xD0, I2C_MEMADD_SIZE_8BIT,
                            &chip_id, 1, 100) == HAL_OK && chip_id == 0x58){
            break;      /* 找到有效 BMP280 */
        }
    }
    if(i >= 2){
        g_bmp_chipid = chip_id;      /* 记录最后一次 ID 供调试 */
        return 0;
    }
    g_bmp_chipid = chip_id;

    /* ② 读 24 字节校准系数 (0x88 起), 失败/无效则重试
       注: 参考工程在校准前"无需软复位", 软复位后校准区未就绪会读到 0。 */
    for(uint8_t trial = 0; trial < 5; trial++){
        if(HAL_I2C_Mem_Read(&hi2c1, bmp280_addr << 1, 0x88, I2C_MEMADD_SIZE_8BIT,
                            calib_raw, 24, 100) != HAL_OK){
            continue;   /* 读失败重试 */
        }
        dig_P1 = (uint16_t)((calib_raw[7] << 8) | calib_raw[6]);
        if(dig_P1 != 0){ calib_ok = 1; break; }     /* 校准有效 */
        HAL_Delay(10);
    }
    if(!calib_ok){
        return 0;
    }

    /* ③ 解析校准系数 (小端) */
    dig_T1 = (int32_t)(uint16_t)((calib_raw[1] << 8) | calib_raw[0]);
    dig_T2 = (int32_t)(int16_t)((calib_raw[3] << 8) | calib_raw[2]);
    dig_T3 = (int32_t)(int16_t)((calib_raw[5] << 8) | calib_raw[4]);
    dig_P1 = (int32_t)(uint16_t)((calib_raw[7] << 8) | calib_raw[6]);
    dig_P2 = (int32_t)(int16_t)((calib_raw[9] << 8) | calib_raw[8]);
    dig_P3 = (int32_t)(int16_t)((calib_raw[11] << 8) | calib_raw[10]);
    dig_P4 = (int32_t)(int16_t)((calib_raw[13] << 8) | calib_raw[12]);
    dig_P5 = (int32_t)(int16_t)((calib_raw[15] << 8) | calib_raw[14]);
    dig_P6 = (int32_t)(int16_t)((calib_raw[17] << 8) | calib_raw[16]);
    dig_P7 = (int32_t)(int16_t)((calib_raw[19] << 8) | calib_raw[18]);
    dig_P8 = (int32_t)(int16_t)((calib_raw[21] << 8) | calib_raw[20]);
    dig_P9 = (int32_t)(int16_t)((calib_raw[23] << 8) | calib_raw[22]);

    /* ④ 配置测量模式: ctrl_meas(0xF4) + config(0xF5)
       修复点: 必须有这一行, BMP280 才进入 normal 连续测量模式;
       否则它停在睡眠模式, 0xF7 压力寄存器只回默认中间值 → 气压算不出。 */
    {
        uint8_t val = (0x05 << 5) | (0x03 << 2) | 0x03;      /* 温16x 压8x 正常模式 */
        HAL_I2C_Mem_Write(&hi2c1, bmp280_addr << 1, 0xF4, I2C_MEMADD_SIZE_8BIT, &val, 1, 100);
        val = (5 << 2);                                     /* config: IIR 滤波 16 */
        HAL_I2C_Mem_Write(&hi2c1, bmp280_addr << 1, 0xF5, I2C_MEMADD_SIZE_8BIT, &val, 1, 100);
    }
    return 1;

    /* ---- 版本A (最初错误写法, 仅对比勿用) ----
     * 错误1: 在校准前执行了软复位(写 0xE0=0xB6), 复位后校准区未就绪就立即读 0x88,
     *         导致 dig_P1=0。版本B 移除了软复位。
     * 错误2: 地址硬编码(调用方传入), 实际模块在 0x77。
     * 错误3: 缺少 chip_id(0xD0) 校验, 无法确认真实芯片。
     * 错误4: 缺少对 ctrl_meas(0xF4) 的关键配置 —— 这才是气压始终 0 的真根因。 */
    /*
    uint8_t val = 0xB6;
    // ① 软复位 (错误的: 复位后立即读校准会读到 0)
    if(HAL_I2C_Mem_Write(&hi2c1, bmp280_addr<<1, 0xE0, I2C_MEMADD_SIZE_8BIT, &val, 1, 100) != HAL_OK){
        return 0;
    }
    HAL_Delay(50);
    // ② 读校准
    for(uint8_t trial = 0; trial < 5; trial++){
        if(HAL_I2C_Mem_Read(&hi2c1, bmp280_addr<<1, 0x88, I2C_MEMADD_SIZE_8BIT, calib, 24, 100) != HAL_OK){
            return 0;
        }
        dig_P1 = (int32_t)((calib[6]) | (calib[7] << 8));
        if(dig_P1 != 0){ break; }
        HAL_Delay(50);
    }
    // ③ 解析校准
    dig_T1 = (int32_t)((calib[0]) | (calib[1] << 8));
    dig_T2 = (int32_t)((int16_t)(calib[2] | (calib[3] << 8)));
    dig_T3 = (int32_t)((int16_t)(calib[4] | (calib[5] << 8)));
    dig_P1 = (int32_t)((calib[6]) | (calib[7] << 8));
    dig_P2 = (int32_t)((int16_t)(calib[8]  | (calib[9]  << 8)));
    dig_P3 = (int32_t)((int16_t)(calib[10] | (calib[11] << 8)));
    dig_P4 = (int32_t)((int16_t)(calib[12] | (calib[13] << 8)));
    dig_P5 = (int32_t)((int16_t)(calib[14] | (calib[15] << 8)));
    dig_P6 = (int32_t)((int16_t)(calib[16] | (calib[17] << 8)));
    dig_P7 = (int32_t)((int16_t)(calib[18] | (calib[19] << 8)));
    dig_P8 = (int32_t)((int16_t)(calib[20] | (calib[21] << 8)));
    dig_P9 = (int32_t)((int16_t)(calib[22] | (calib[23] << 8)));
    // ④ 配置 (错误: 只写了 ctrl_meas, 且该写法未保证进入 normal 测量)
    val = 0x27;
    if(HAL_I2C_Mem_Write(&hi2c1, bmp280_addr<<1, 0xF4, I2C_MEMADD_SIZE_8BIT, &val, 1, 100) != HAL_OK){
        return 0;
    }
    return 1;
    */
}


/* =========================================================================
 *  二合一传感器联合初始化
 * ========================================================================= */
uint8_t WarmDamp_Init(void){
    if(!AHT20_Init()){    return 1; }
    if(!BMP280_Init()){   return 2; }
    return 0;
}


/* =========================================================================
 *  二合一传感器联合读取
 *  @param temp  输出温度 (°C)
 *  @param hum   输出湿度 (%RH)
 *  @param press 输出气压 (Pa)
 * ========================================================================= */
uint8_t WarmDamp_Read(float *temp, float *hum, float *press){
    *press = 0.0f;   /* 先清零, 失败时保持 0 */

    /* ==================== AHT20 温湿度读取 ==================== */
    {
        /* AHT20 触发测量命令 (版本B) */
        uint8_t cmd[3] = {0xAC, 0x33, 0x00};
        uint8_t buf[6] = {0};
        uint32_t t, h;

        if(HAL_I2C_Master_Transmit(&hi2c1, AHT20_ADDR << 1, cmd, 3, 100) == HAL_OK){
            HAL_Delay(80);   /* 等待测量完成 (典型 80ms) */

            if(HAL_I2C_Master_Receive(&hi2c1, AHT20_ADDR << 1, buf, 6, 100) == HAL_OK){
                /* 解析温湿度 (各 20bit) */
                h = ((uint32_t)buf[1] << 12) | ((uint32_t)buf[2] << 4) | (buf[3] >> 4);
                t = ((uint32_t)(buf[3] & 0x0F) << 16) | ((uint32_t)buf[4] << 8) | buf[5];

                *hum  = ((float)h * 100.0f) / 1048576.0f;
                *temp = ((float)t * 200.0f) / 1048576.0f - 50.0f;
            }
        }

        /* ---- 版本A (最初错误写法, 仅对比勿用) ----
         * 错误: 触发测量用错命令 0xBE, 传感器不开始测量 → 温湿度全 0。 */
        /*
        uint8_t cmd = 0xBE;
        uint8_t buf[6] = {0};
        if(HAL_I2C_Master_Transmit(&hi2c1, AHT20_ADDR<<1, &cmd, 1, 100) != HAL_OK){
            return 1;
        }
        HAL_Delay(80);
        HAL_I2C_Master_Receive(&hi2c1, AHT20_ADDR<<1, buf, 6, 100);
        *temp = ((buf[3]&0x0F)<<16) | (buf[4]<<8) | buf[5];
        *hum  = (buf[1]<<12) | (buf[2]<<4) | (buf[3]>>4);
        *temp = (*temp * 200.0f) / 1048576.0f - 50.0f;
        *hum  = (*hum * 100.0f) / 1048576.0f;
        */
    }

    /* ==================== BMP280 气压读取 ==================== */
    {
        uint8_t raw[6] = {0};   /* 0~2 压力, 3~5 温度 */
        int32_t adc_p, adc_t;
        float v1, v2, t_fine_f;
        float pv1, pv2, pp;

        /* ---- 配置自愈 ----
         * 双保险: 无论 BMP280_Init 是否真正执行过 ctrl_meas,
         * 这里都强制配置进 normal 测量模式(一次性)。
         * 修复点: 若缺失此行, BMP280 停在睡眠模式, adc 恒为 0x80000,
         * 气压表现为一个固定错误值(约 71507)。 */
        static uint8_t cfg_done = 0;
        if(!cfg_done){
            uint8_t v = (0x03 << 5) | (0x03 << 2) | 0x03;   /* 压8x + 温8x + normal */
            HAL_I2C_Mem_Write(&hi2c1, bmp280_addr << 1, 0xF4, I2C_MEMADD_SIZE_8BIT, &v, 1, 100);
            v = 0x00;                                       /* config: 关滤波 */
            HAL_I2C_Mem_Write(&hi2c1, bmp280_addr << 1, 0xF5, I2C_MEMADD_SIZE_8BIT, &v, 1, 100);
            cfg_done = 1;
            HAL_Delay(5);   /* 等待配置生效 */
        }

        /* ---- 校准自愈 ----
         * 若 Init 未成功写 dig_P1, 这里重新读校准补齐, 保证气压能换算。 */
        if(dig_P1 == 0){
            uint8_t calib_raw[24] = {0};
            if(HAL_I2C_Mem_Read(&hi2c1, bmp280_addr << 1, 0x88, I2C_MEMADD_SIZE_8BIT,
                                calib_raw, 24, 100) == HAL_OK){
                dig_T1 = (int32_t)(uint16_t)((calib_raw[1] << 8) | calib_raw[0]);
                dig_T2 = (int32_t)(int16_t)((calib_raw[3] << 8) | calib_raw[2]);
                dig_T3 = (int32_t)(int16_t)((calib_raw[5] << 8) | calib_raw[4]);
                dig_P1 = (int32_t)(uint16_t)((calib_raw[7] << 8) | calib_raw[6]);
                dig_P2 = (int32_t)(int16_t)((calib_raw[9] << 8) | calib_raw[8]);
                dig_P3 = (int32_t)(int16_t)((calib_raw[11] << 8) | calib_raw[10]);
                dig_P4 = (int32_t)(int16_t)((calib_raw[13] << 8) | calib_raw[12]);
                dig_P5 = (int32_t)(int16_t)((calib_raw[15] << 8) | calib_raw[14]);
                dig_P6 = (int32_t)(int16_t)((calib_raw[17] << 8) | calib_raw[16]);
                dig_P7 = (int32_t)(int16_t)((calib_raw[19] << 8) | calib_raw[18]);
                dig_P8 = (int32_t)(int16_t)((calib_raw[21] << 8) | calib_raw[20]);
                dig_P9 = (int32_t)(int16_t)((calib_raw[23] << 8) | calib_raw[22]);
            }
        }

        /* 读 0xF7 起 6 字节: 压力 + 温度原始数据 */
        HAL_I2C_Mem_Read(&hi2c1, bmp280_addr << 1, 0xF7, I2C_MEMADD_SIZE_8BIT, raw, 6, 100);

        adc_p = ((int32_t)(raw[0] << 12)) | ((int32_t)(raw[1] << 4)) | (raw[2] >> 4);
        adc_t = ((int32_t)(raw[3] << 12)) | ((int32_t)(raw[4] << 4)) | (raw[5] >> 4);

        /* ---- 温度补偿 (Bosch 官方浮点公式) ---- */
        v1 = (((float)adc_t / 16384.0f)    - ((float)dig_T1 / 1024.0f)) * ((float)dig_T2);
        v2 = ((((float)adc_t / 131072.0f)  - ((float)dig_T1 / 8192.0f)) *
              (((float)adc_t / 131072.0f)  - ((float)dig_T1 / 8192.0f))) * ((float)dig_T3);
        t_fine_f = v1 + v2;

        /* ---- 压力补偿 (Bosch 官方浮点公式) ---- */
        pv1 = (t_fine_f / 2.0f) - 64000.0f;
        pv2 = pv1 * pv1 * ((float)dig_P6) / 32768.0f;
        pv2 = pv2 + pv1 * ((float)dig_P5) * 2.0f;
        pv2 = (pv2 / 4.0f) + (((float)dig_P4) * 65536.0f);
        pv1 = ((((float)dig_P3) * pv1 * pv1) / 524288.0f + ((float)dig_P2) * pv1) / 524288.0f;
        pv1 = (1.0f + (pv1 / 32768.0f)) * ((float)dig_P1);

        if(pv1 == 0.0f){
            *press = 0.0f;
            return 1;
        }

        pp = 1048576.0f - (float)adc_p;
        pp = (pp - (pv2 / 4096.0f)) * 6250.0f / pv1;
        pv1 = (((float)dig_P9) * pp * pp) / 2147483648.0f;
        pv2 = (pp * ((float)dig_P8)) / 32768.0f;
        pp = pp + (pv1 + pv2 + ((float)dig_P7)) / 16.0f;

        *press = pp;    /* 单位 Pa */

        /* ---- 版本A (最初错误换算, 仅对比勿用) ----
         * 错误1: 读取用的是硬编码"未配置测量模式"的假设, 实际没写 ctrl_meas
         *         使 adc 恒为 0x80000, 气压结果错误。
         * 错误2: int64 定点公式在有符号/中间量扩展处溢出, 结果异常极小值。
         * 修复: 见上方 Bosch 官方浮点公式。 */
        /*
        int32_t var1, var2, t_fine;
        var1 = (((adc_t >> 3) - ((int32_t)dig_T1 << 1)) * ((int32_t)dig_T2)) >> 11;
        var2 = (((((adc_t >> 4) - ((int32_t)dig_T1)) * ((adc_t >> 4) - ((int32_t)dig_T1))) >> 12) * ((int32_t)dig_T3)) >> 14;
        t_fine = var1 + var2;

        int64_t p_var1, p_var2, p;
        p_var1 = ((int64_t)t_fine) - 128000;
        p_var2 = p_var1 * p_var1 * (int64_t)dig_P6;
        p_var2 = p_var2 + ((p_var1 * (int64_t)dig_P5) << 17);
        p_var2 = p_var2 + (((int64_t)dig_P4) << 35);
        p_var1 = ((p_var1 * p_var1 * (int64_t)dig_P3) >> 8) + ((p_var1 * (int64_t)dig_P2) << 12);
        p_var1 = (((((int64_t)1) << 47) + p_var1)) * ((int64_t)dig_P1) >> 33;

        if(p_var1 == 0){ *press = 0; return 1; }

        p = 1048576 - adc_p;
        p = (p - (p_var2 >> 12)) * 3125;
        if(p < 0){ p = (p * 2) / (p_var1 * -1); }
        else     { p = (p * 2) / p_var1; }
        p = p + ((p * p * (int64_t)dig_P9) >> 31);
        p = (p * ((int64_t)dig_P8) >> 12);
        p = p + (((((int64_t)0x80000000) + p)) * ((int64_t)dig_P7)) >> 40;

        *press = (float)p / 256.0f;
        */
    }

    return 0;
}