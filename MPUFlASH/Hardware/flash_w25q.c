#include "flash_w25q.h"
#include "stm32f1xx_hal.h"
#include "main.h" // CubeMX生成的SPI句柄

/* USER CODE BEGIN Includes */
#include "string.h"
#include "stdio.h"
/* USER CODE END Includes */

// W25Qxx Flash相关定义
#define W25QXX_SPI_PORT &hspi1 // CubeMX生成的SPI1句柄

// Flash指令定义
#define W25X_WriteEnable		0x06
#define W25X_WriteDisable		0x04
#define W25X_ReadStatusReg		0x05
#define W25X_WriteStatusReg		0x01
#define W25X_ReadData			0x03
#define W25X_FastReadData		0x0B
#define W25X_FastReadDual		0x3B
#define W25X_PageProgram		0x02
#define W25X_BlockErase			0xD8
#define W25X_SectorErase		0x20
#define W25X_ChipErase			0xC7
#define W25X_PowerDown			0xB9
#define W25X_ReleasePowerDown	0xAB
#define W25X_DeviceID			0xAB
#define W25X_ManufactDeviceID	0x90
#define W25X_JedecDeviceID		0x9F

// Flash容量定义
#define W25Q80 	0XEF13
#define W25Q16 	0XEF14
#define W25Q32 	0XEF15
#define W25Q64 	0XEF16
#define W25Q128	0XEF17

// 状态寄存器位定义
#define W25Q_BUSY_FLAG 0x01

static uint16_t W25QXX_TYPE = W25Q16; // 默认W25Q16

/**
 * @brief  Flash发送命令+地址+数据
 * @param  cmd: 命令
 * @param  addr: 地址
 * @param  addr_en: 是否发送地址
 * @param  data: 数据指针
 * @param  len: 数据长度
 * @retval None
 */
static void SPI_FLASH_SendCommand(uint8_t cmd, uint32_t addr, uint8_t addr_en, uint8_t *data, uint16_t len) {
    uint8_t send_data[4];
    
    // 拉低片选
    HAL_GPIO_WritePin(FLASH_CS_GPIO_Port, FLASH_CS_Pin, GPIO_PIN_RESET);
    
    // 发送命令
    HAL_SPI_Transmit(W25QXX_SPI_PORT, &cmd, 1, 100);
    
    // 如果需要发送地址
    if(addr_en) {
        send_data[0] = (uint8_t)((addr) >> 16);
        send_data[1] = (uint8_t)((addr) >> 8);
        send_data[2] = (uint8_t)(addr);
        HAL_SPI_Transmit(W25QXX_SPI_PORT, send_data, 3, 100);
    }
    
    // 发送或接收数据
    if(data != NULL && len != 0) {
        if(cmd == W25X_ReadData || cmd == W25X_FastReadData) {
            // 读取数据
            HAL_SPI_TransmitReceive(W25QXX_SPI_PORT, data, data, len, 1000);
        } else {
            // 写入数据
            HAL_SPI_Transmit(W25QXX_SPI_PORT, data, len, 1000);
        }
    }
    
    // 拉高片选
    HAL_GPIO_WritePin(FLASH_CS_GPIO_Port, FLASH_CS_Pin, GPIO_PIN_SET);
}

/**
 * @brief  读取Flash状态寄存器
 * @param  None
 * @retval 状态寄存器值
 */
uint8_t FLASH_ReadSR(void) {
    uint8_t byte = 0, temp = 0;
    
    // 拉低片选
    HAL_GPIO_WritePin(FLASH_CS_GPIO_Port, FLASH_CS_Pin, GPIO_PIN_RESET);
    
    // 发送读状态寄存器命令
    temp = W25X_ReadStatusReg;
    HAL_SPI_Transmit(W25QXX_SPI_PORT, &temp, 1, 100);
    
    // 接收数据
    HAL_SPI_Receive(W25QXX_SPI_PORT, &byte, 1, 100);
    
    // 拉高片选
    HAL_GPIO_WritePin(FLASH_CS_GPIO_Port, FLASH_CS_Pin, GPIO_PIN_SET);
    
    return byte;
}

/**
 * @brief  等待Flash空闲
 * @param  None
 * @retval None
 */
void FLASH_Wait_Busy(void) {
    while((FLASH_ReadSR() & W25Q_BUSY_FLAG) == SET); // 等待忙标志清零
}

/**
 * @brief  写使能
 * @param  None
 * @retval None
 */
void FLASH_WriteEnable(void) {
    SPI_FLASH_SendCommand(W25X_WriteEnable, 0, 0, NULL, 0);
}

/**
 * @brief  读取Flash ID
 * @param  None
 * @retval Flash ID
 */
uint16_t FLASH_ReadID(void) {
    uint8_t temp[2];
    uint16_t id = 0;
    
    SPI_FLASH_SendCommand(W25X_ManufactDeviceID, 0x000000, 1, temp, 2);
    id = (temp[0] << 8) | temp[1];
    
    return id;
}

/**
 * @brief  擦除扇区（4KB）
 * @param  dst_addr: 扇区地址（必须是4096的倍数）
 * @retval None
 */
void FLASH_SectorErase(uint32_t dst_addr) {
    // 保证地址对齐
    dst_addr &= ~(4096 - 1);
    
    // 写使能
    FLASH_WriteEnable();
    
    // 擦除扇区
    SPI_FLASH_SendCommand(W25X_SectorErase, dst_addr, 1, NULL, 0);
    
    // 等待擦除完成（约400ms）
    FLASH_Wait_Busy();
    
    // 【无电容优化】：擦除后延时释放电流
    HAL_Delay(50); // 释放擦除电流，防止电压波动
}

/**
 * @brief  页编程（无电容优化版）
 * @param  dst_addr: 目标地址
 * @param  src_data: 源数据
 * @param  len: 数据长度（≤256字节）
 * @retval None
 */
void FLASH_PageProgram(uint32_t dst_addr, uint8_t *src_data, uint16_t len) {
    uint16_t write_len;
    
    // 【无电容优化】：写入前延时稳定电压
    HAL_Delay(2); // 稳定电源电压，避免写入瞬间跌落
    
    // 分批写入（每页256字节）
    while(len > 0) {
        write_len = (len > 256) ? 256 : len;
        
        // 写使能
        FLASH_WriteEnable();
        
        // 页编程
        SPI_FLASH_SendCommand(W25X_PageProgram, dst_addr, 1, src_data, write_len);
        
        // 等待写入完成（约3ms）
        FLASH_Wait_Busy();
        
        // 【无电容优化】：写入后延时释放电流
        HAL_Delay(5); // 释放写入电流，防止电压波动
        
        // 更新地址和数据指针
        dst_addr += write_len;
        src_data += write_len;
        len -= write_len;
    }
}

/**
 * @brief  读取数据（无电容优化版）
 * @param  src_addr: 源地址
 * @param  dst_data: 目标数据
 * @param  len: 数据长度
 * @retval None
 */
void FLASH_Read(uint32_t src_addr, uint8_t *dst_data, uint16_t len) {
    // 【无电容优化】：读取前短暂延时
    HAL_Delay(1); // 避免刚写入后立即读取
    
    SPI_FLASH_SendCommand(W25X_ReadData, src_addr, 1, dst_data, len);
}

/**
 * @brief  写入历史记录（带重试机制）
 * @param  addr: 存储地址
 * @param  history: 历史记录结构体
 * @retval 0=成功, 1=失败
 */
uint8_t FLASH_WriteHistory(uint32_t addr, FlashHistory *history) {
    uint8_t retry = 3;
    uint8_t verify_buffer[sizeof(FlashHistory)];
    
    while(retry--) {
        // 擦除扇区（如果不在同一扇区内）
        if((addr & 0xFFF) == 0) { // 扇区首地址
            FLASH_SectorErase(addr);
        }
        
        // 写入数据
        FLASH_PageProgram(addr, (uint8_t*)history, sizeof(FlashHistory));
        
        // 验证写入（重试机制核心）
        FLASH_Read(addr, verify_buffer, sizeof(FlashHistory));
        
        if(memcmp(history, verify_buffer, sizeof(FlashHistory)) == 0) {
            // 验证成功
            return 0; // 成功
        }
        
        // 验证失败，准备重试
        if(retry > 0) {
            HAL_Delay(100); // 重试前等待
        }
    }
    
    return 1; // 重试失败
}

/**
 * @brief  读取历史记录
 * @param  addr: 存储地址
 * @param  history: 历史记录结构体
 * @retval 0=成功, 1=失败
 */
uint8_t FLASH_ReadHistory(uint32_t addr, FlashHistory *history) {
    FLASH_Read(addr, (uint8_t*)history, sizeof(FlashHistory));
    return 0; // 读取操作一般不会失败
}

/**
 * @brief  初始化Flash
 * @param  None
 * @retval Flash型号
 */
uint16_t FLASH_Init(void) {
    uint16_t device_id = FLASH_ReadID();
    
    // 检查设备ID
    switch(device_id) {
        case W25Q80:
        case W25Q16:
        case W25Q32:
        case W25Q64:
        case W25Q128:
            W25QXX_TYPE = device_id;
            break;
        default:
            W25QXX_TYPE = 0; // 无效ID
            break;
    }
    
    return W25QXX_TYPE;
}

/* USER CODE BEGIN 0 */
// 用户可在此添加自定义函数
/* USER CODE END 0 */