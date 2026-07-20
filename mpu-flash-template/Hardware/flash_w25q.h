#ifndef __FLASH_W25Q_H
#define __FLASH_W25Q_H

#include "stm32f1xx_hal.h"  // 添加HAL库头文件
#include <stdint.h>

/* USER CODE BEGIN Includes */
#include <string.h>
/* USER CODE END Includes */

// 【新增】声明SPI句柄
extern SPI_HandleTypeDef hspi1;

// Flash片选引脚定义（使用CubeMX中的定义）
#define FLASH_CS_GPIO_Port GPIOA
#define FLASH_CS_Pin GPIO_PIN_4

// 历史记录结构体定义
typedef struct {
    float max_angle;      // 最大角度
    float current_angle;  // 当前角度
    uint32_t save_count;  // 保存次数
    uint8_t reserved[8];  // 预留空间
} FlashHistory;

// 函数声明
uint16_t FLASH_ReadID(void);
void FLASH_WriteEnable(void);
void FLASH_Wait_Busy(void);
void FLASH_SectorErase(uint32_t dst_addr);
void FLASH_PageProgram(uint32_t dst_addr, uint8_t *src_data, uint16_t len);
void FLASH_Read(uint32_t src_addr, uint8_t *dst_data, uint16_t len);
uint8_t FLASH_WriteHistory(uint32_t addr, FlashHistory *history);
uint8_t FLASH_ReadHistory(uint32_t addr, FlashHistory *history);
uint16_t FLASH_Init(void);

/* USER CODE BEGIN 0 */
/* USER CODE END 0 */

#endif