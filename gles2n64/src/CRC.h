#include "Types.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __LIBRETRO__ // Prefix symbols
#define Reflect gln64Reflect
#define CRC_BuildTable gln64CRCBuildTable
#define CRCTable gln64CRCTable
#endif

void CRC_BuildTable();

u32 CRC_Calculate( u32 crc, void *buffer, u32 count );
u32 CRC_CalculatePalette( u32 crc, void *buffer, u32 count );

#ifdef __cplusplus
}
#endif
