/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __W25QXX_H
#define __W25QXX_H

#ifdef __cplusplus
 extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Exported types ------------------------------------------------------------*/
/* Exported constants --------------------------------------------------------*/
/* W25Q64 JEDEC ID */
#define W25Q64_ID               0xEF4017

/* Instruction definitions */
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

/* Timeout value */
#define W25QXX_TIMEOUT_VALUE    1000

/* Exported macro ------------------------------------------------------------*/
/* CS control macros - PA4 */
#define W25QXX_CS_LOW()   HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_RESET)
#define W25QXX_CS_HIGH()  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_SET)

/* Exported functions ------------------------------------------------------- */
uint8_t W25QXX_Init(void);
uint32_t W25QXX_ReadID(void);
void W25QXX_WriteEnable(void);
void W25QXX_WaitForWriteEnd(void);
void W25QXX_SectorErase(uint32_t SectorAddr);
void W25QXX_BufferWrite(uint8_t* pBuffer, uint32_t WriteAddr, uint16_t NumByteToWrite);
void W25QXX_BufferRead(uint8_t* pBuffer, uint32_t ReadAddr, uint16_t NumByteToRead);
void W25QXX_PowerDown(void);
void W25QXX_WAKEUP(void);
void W25QXX_ChipErase(void);

#ifdef __cplusplus
}
#endif
#endif /* __W25QXX_H */
