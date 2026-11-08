#ifndef __SGP30_H
#define __SGP30_H

#include "main.h"

#define SGP30_ADDR  0x58

uint8_t SGP30_Detect(void);

uint8_t SGP30_Init(void);

void SGP30_Read(uint16_t *eco2,uint16_t *tvoc);

#endif