#ifndef __WARM_DAMP_H
#define __WARM_DAMP_H

#include "main.h"

#define AHT20_ADDR   0x38    /* AHT20 温湿度 */
#define BMP280_ADDR  0x76    /* BMP280 气压 */

uint8_t WarmDamp_Detect(void);
uint8_t WarmDamp_Init(void);
uint8_t WarmDamp_Read(float *temp,float *hum,float *press);

#endif