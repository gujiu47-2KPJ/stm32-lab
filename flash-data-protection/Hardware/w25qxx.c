#include "w25qxx.h"
#include <string.h>

/* SPI句柄声明 - 从main.c获取 */
extern SPI_HandleTypeDef hspi1;

/* 指令定义 */
#define W25X_WriteEnable        0x06
#define W25X_WriteDisable       0x04
#define W25X_ReadStatusReg      0x05
#define W25X_WriteStatusReg     0x01
#define W25X_ReadData           0x03
#define W25X_FastReadData       0x0B
#define W25X_FastReadDual       0x3B
#define W25X_PageProgram        0x02
#define W25X_BlockErase         0xD8
#define W25X_SectorErase        0x20
#define W25X_ChipErase          0xC7
#define W25X_PowerDown          0xB9
#define W25X_ReleasePowerDown   0xAB
#define W25X_DeviceID           0xAB
#define W25X_ManufactDeviceID   0x90
#define W25X_JedecDeviceID      0x9F
#define WIP_Flag                0x01 /*!< Write In Progress (WIP) flag */

/* 超时定义 */
#define W25QXX_TIMEOUT_VALUE    1000

/**
  * @brief W25QXX初始化
  * @param None
  * @retval 0:成功 1:失败
  */
uint8_t W25QXX_Init(void)
{
    uint32_t flash_id = 0;
    
    /* 释放掉电模式 */
    W25QXX_WAKEUP();
    HAL_Delay(3);
    
    /* 读取Flash ID */
    flash_id = W25QXX_ReadID();
    
    /* 检查ID是否匹配 */
    if(flash_id == W25Q64_ID) {
        return 0;  // 成功
    } else {
        return 1;  // 失败
    }
}

/**
  * @brief 读取W25QXX的ID
  * @param None
  * @retval Flash ID
  */
uint32_t W25QXX_ReadID(void)
{
    uint8_t cmd[4] = {W25X_JedecDeviceID, 0x00, 0x00, 0x00};  // 命令+3个dummy字节
    uint8_t rx[4]  = {0, 0, 0, 0};                             // 4个接收字节
    
    W25QXX_CS_LOW();
    
    /* 一次性收/发：SPI全双工，发送的同时接收 */
    /* 时序: MOSI=0x9F,0x00,0x00,0x00   MISO=XX, ID2, ID1, ID0 */
    /* rx[0]=垃圾, rx[1]=0xEF, rx[2]=0x40, rx[3]=0x17 */
    HAL_SPI_TransmitReceive(&hspi1, cmd, rx, 4, W25QXX_TIMEOUT_VALUE);
    
    W25QXX_CS_HIGH();
    
    return (rx[1] << 16) | (rx[2] << 8) | rx[3];
}

/**
  * @brief 向W25QXX发送写使能命令
  * @param None
  * @retval None
  */
void W25QXX_WriteEnable(void)
{
    uint8_t cmd = W25X_WriteEnable;
    
    W25QXX_CS_LOW();
    HAL_SPI_Transmit(&hspi1, &cmd, 1, W25QXX_TIMEOUT_VALUE);
    W25QXX_CS_HIGH();
}

/**
  * @brief 等待写操作完成
  * @param None
  * @retval None
  */
void W25QXX_WaitForWriteEnd(void)
{
    uint8_t cmd = W25X_ReadStatusReg;
    uint8_t status = 0;
    
    W25QXX_CS_LOW();
    HAL_SPI_Transmit(&hspi1, &cmd, 1, W25QXX_TIMEOUT_VALUE);
    
    do {
        HAL_SPI_Receive(&hspi1, &status, 1, W25QXX_TIMEOUT_VALUE);
    } while((status & WIP_Flag) != 0);
    
    W25QXX_CS_HIGH();
}

/**
  * @brief 擦除扇区
  * @param SectorAddr: 扇区地址（4KB对齐）
  * @retval None
  */
void W25QXX_SectorErase(uint32_t SectorAddr)
{
    uint8_t cmd[4];
    
    /* 写使能 */
    W25QXX_WriteEnable();
    
    /* 构建擦除命令 */
    cmd[0] = W25X_SectorErase;
    cmd[1] = (SectorAddr & 0xFF0000) >> 16;
    cmd[2] = (SectorAddr & 0xFF00) >> 8;
    cmd[3] = SectorAddr & 0xFF;
    
    W25QXX_CS_LOW();
    HAL_SPI_Transmit(&hspi1, cmd, 4, W25QXX_TIMEOUT_VALUE);
    W25QXX_CS_HIGH();
    
    /* 等待擦除完成 */
    W25QXX_WaitForWriteEnd();
}

/**
  * @brief 写入一页数据
  * @param pBuffer: 要写入的数据缓冲区
  * @param WriteAddr: 写入地址（必须页对齐）
  * @param NumByteToWrite: 要写入的字节数（<=256）
  * @retval None
  */
static void W25QXX_PageWrite(uint8_t* pBuffer, uint32_t WriteAddr, uint16_t NumByteToWrite)
{
    uint8_t cmd[4];
    
    /* 写使能 */
    W25QXX_WriteEnable();
    
    /* 构建写入命令 */
    cmd[0] = W25X_PageProgram;
    cmd[1] = (WriteAddr & 0xFF0000) >> 16;
    cmd[2] = (WriteAddr & 0xFF00) >> 8;
    cmd[3] = WriteAddr & 0xFF;
    
    W25QXX_CS_LOW();
    HAL_SPI_Transmit(&hspi1, cmd, 4, W25QXX_TIMEOUT_VALUE);
    HAL_SPI_Transmit(&hspi1, pBuffer, NumByteToWrite, W25QXX_TIMEOUT_VALUE);
    W25QXX_CS_HIGH();
    
    /* 等待写入完成 */
    W25QXX_WaitForWriteEnd();
}

/**
  * @brief 写入多页数据
  * @param pBuffer: 要写入的数据缓冲区
  * @param WriteAddr: 写入地址
  * @param NumByteToWrite: 要写入的字节数
  * @retval None
  */
void W25QXX_BufferWrite(uint8_t* pBuffer, uint32_t WriteAddr, uint16_t NumByteToWrite)
{
    uint32_t page_remain = 0;
    uint32_t addr = WriteAddr;
    uint8_t* pbuf = pBuffer;
    uint16_t num = NumByteToWrite;
    
    /* 计算当前页剩余字节数 */
    page_remain = 256 - (addr % 256);
    
    if(num <= page_remain) {
        page_remain = num;  // 一次写入完成
    }
    
    while(num > 0) {
        /* 写入当前页 */
        W25QXX_PageWrite(pbuf, addr, (uint16_t)page_remain);
        
        /* 更新指针和计数 */
        pbuf += page_remain;
        addr += page_remain;
        num -= page_remain;
        
        /* 计算下一页写入大小 */
        if(num > 256) {
            page_remain = 256;
        } else {
            page_remain = num;
        }
    }
}

/**
  * @brief 读取数据
  * @param pBuffer: 存储读出数据的缓冲区
  * @param ReadAddr: 读取地址
  * @param NumByteToRead: 要读取的字节数
  * @retval None
  */
void W25QXX_BufferRead(uint8_t* pBuffer, uint32_t ReadAddr, uint16_t NumByteToRead)
{
    uint8_t cmd[4];
    
    cmd[0] = W25X_ReadData;
    cmd[1] = (ReadAddr & 0xFF0000) >> 16;
    cmd[2] = (ReadAddr & 0xFF00) >> 8;
    cmd[3] = ReadAddr & 0xFF;
    
    W25QXX_CS_LOW();
    
    /* 发送读命令 */
    HAL_SPI_Transmit(&hspi1, cmd, 4, W25QXX_TIMEOUT_VALUE);
    
    /* 读取数据 */
    HAL_SPI_Receive(&hspi1, pBuffer, NumByteToRead, W25QXX_TIMEOUT_VALUE);
    
    W25QXX_CS_HIGH();
}

/**
  * @brief 进入掉电模式
  * @param None
  * @retval None
  */
void W25QXX_PowerDown(void)
{
    uint8_t cmd = W25X_PowerDown;
    
    W25QXX_CS_LOW();
    HAL_SPI_Transmit(&hspi1, &cmd, 1, W25QXX_TIMEOUT_VALUE);
    W25QXX_CS_HIGH();
}

/**
  * @brief 退出掉电模式
  * @param None
  * @retval None
  */
void W25QXX_WAKEUP(void)
{
    uint8_t cmd = W25X_ReleasePowerDown;
    
    W25QXX_CS_LOW();
    HAL_SPI_Transmit(&hspi1, &cmd, 1, W25QXX_TIMEOUT_VALUE);
    W25QXX_CS_HIGH();
    
    /* 等待唤醒完成 */
    HAL_Delay(3);
}

/**
  * @brief 擦除整个芯片
  * @param None
  * @retval None
  */
void W25QXX_ChipErase(void)
{
    uint8_t cmd = W25X_ChipErase;
    
    /* 写使能 */
    W25QXX_WriteEnable();
    
    W25QXX_CS_LOW();
    HAL_SPI_Transmit(&hspi1, &cmd, 1, W25QXX_TIMEOUT_VALUE);
    W25QXX_CS_HIGH();
    
    /* 等待擦除完成 */
    W25QXX_WaitForWriteEnd();
}
