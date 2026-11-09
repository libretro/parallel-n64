#include "N64.h"

u8 *HEADER;

u64 TMEM[512];
u8 *RDRAM;

u32 RDRAMSize = 0;

N64Regs REG;

int ConfigOpen = 0;
