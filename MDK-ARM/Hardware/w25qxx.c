/**
 ******************************************************************************
 * @file    w25qxx.c
 * @brief   W25QXX 系列 SPI Flash 驱动实现
 * @note    SPI 命令定义:
 *          0x03: 读数据
 *          0x02: 页写入
 *          0x20: 扇区擦除 (4KB)
 *          0x05: 读状态寄存器
 *          0x06: 写使能
 *          0x9F: 读 JEDEC ID
 *          CS 引脚: GPIOA PIN10
 ******************************************************************************
 */

#include "w25qxx.h"

extern SPI_HandleTypeDef hspi1;

/**
 * @brief  初始化 Flash, 读取 JEDEC ID 确认通信正常
 */
void W25QXX_Init(void)
{
    uint8_t cmd = 0x9F;                     /* 读 JEDEC ID 命令 */
    uint8_t buf[3] = {0};

    HAL_GPIO_WritePin(CS_GPIO_Port, CS_Pin, GPIO_PIN_RESET);
    HAL_SPI_Transmit(&hspi1, &cmd, 1, 100);
    HAL_SPI_Receive(&hspi1, buf, 3, 100);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_10, GPIO_PIN_SET);
}

/**
 * @brief  从 Flash 读取 1 字节
 * @param  addr 24 位地址
 * @param  data 输出指针
 * @note   SPI 帧: [0x03][AddrH][AddrM][AddrL] ... 接收 1 字节
 */
void W25QXX_ReadByte(uint32_t addr, uint8_t *data)
{
    uint8_t cmd = 0x03;
    uint8_t addr_buf[3];

    /* 拆解 24 位地址为 3 字节 (大端) */
    addr_buf[0] = (addr >> 16) & 0xFF;
    addr_buf[1] = (addr >> 8) & 0xFF;
    addr_buf[2] = addr & 0xFF;

    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_10, GPIO_PIN_RESET);
    HAL_SPI_Transmit(&hspi1, &cmd, 1, 100);         /* 发送读命令 */
    HAL_SPI_Transmit(&hspi1, addr_buf, 3, 100);     /* 发送 24 位地址 */
    HAL_SPI_Receive(&hspi1, data, 1, 100);           /* 接收 1 字节 */
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_10, GPIO_PIN_SET);
}

/**
 * @brief  向 Flash 写入 1 字节 (自动擦除扇区)
 * @param  addr 24 位地址
 * @param  data 要写入的数据
 * @note   流程: 写使能 -> 擦除扇区 -> 等待就绪 -> 写使能 -> 写入 -> 等待就绪
 *         擦除粒度: 1 个扇区 (4KB)
 */
void W25QXX_WriteByte(uint32_t addr, uint8_t data)
{
    uint8_t cmd;
    uint8_t addr_buf[3];
    uint8_t state = 0;

    /* 拆解地址 */
    addr_buf[0] = (addr >> 16) & 0xFF;
    addr_buf[1] = (addr >> 8) & 0xFF;
    addr_buf[2] = addr & 0xFF;

    /* 步骤 1: 写使能 */
    cmd = 0x06;
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_10, GPIO_PIN_RESET);
    HAL_SPI_Transmit(&hspi1, &cmd, 1, 100);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_10, GPIO_PIN_SET);

    /* 步骤 2: 扇区擦除 (0x20) */
    cmd = 0x20;
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_10, GPIO_PIN_RESET);
    HAL_SPI_Transmit(&hspi1, &cmd, 1, 100);
    HAL_SPI_Transmit(&hspi1, addr_buf, 3, 100);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_10, GPIO_PIN_SET);

    /* 步骤 3: 等待擦除完成 (轮询状态寄存器 BUSY 位) */
    do {
        cmd = 0x05;                             /* 读状态寄存器 */
        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_10, GPIO_PIN_RESET);
        HAL_SPI_Transmit(&hspi1, &cmd, 1, 100);
        HAL_SPI_Receive(&hspi1, &state, 1, 100);
        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_10, GPIO_PIN_SET);
    } while (state & 0x01);                     /* BIT0=1 表示忙 */

    /* 步骤 4: 再次写使能 */
    cmd = 0x06;
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_10, GPIO_PIN_RESET);
    HAL_SPI_Transmit(&hspi1, &cmd, 1, 100);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_10, GPIO_PIN_SET);

    /* 步骤 5: 页写入 (0x02) */
    cmd = 0x02;
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_10, GPIO_PIN_RESET);
    HAL_SPI_Transmit(&hspi1, &cmd, 1, 100);
    HAL_SPI_Transmit(&hspi1, addr_buf, 3, 100);
    HAL_SPI_Transmit(&hspi1, &data, 1, 100);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_10, GPIO_PIN_SET);

    /* 步骤 6: 等待写入完成 */
    do {
        cmd = 0x05;
        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_10, GPIO_PIN_RESET);
        HAL_SPI_Transmit(&hspi1, &cmd, 1, 100);
        HAL_SPI_Receive(&hspi1, &state, 1, 100);
        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_10, GPIO_PIN_SET);
    } while (state & 0x01);
}