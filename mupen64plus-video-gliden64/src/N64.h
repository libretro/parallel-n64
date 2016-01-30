#ifndef N64_H
#define N64_H

#include "Types.h"

#define MI_INTR_DP		0x20		// Bit 5: DP intr

extern u8 *HEADER;
extern u8 *DMEM;
extern u8 *IMEM;
extern u8 *RDRAM;
extern u64 TMEM[512];
extern u32 RDRAMSize;
extern bool ConfigOpen;

#endif

