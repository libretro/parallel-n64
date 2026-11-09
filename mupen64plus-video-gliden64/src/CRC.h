#pragma once
#include "Types.h"

#ifdef __cplusplus
extern "C" {
#endif

void CRC_Init(void);

u32 CRC_Calculate_Strict( u32 crc, const void *buffer, u32 count );
u64 CRC_Calculate( u64 crc, const void *buffer, u32 count );
u64 CRC_CalculatePalette( u64 crc, const void *buffer, u32 count );

#ifdef __cplusplus
}
#endif
