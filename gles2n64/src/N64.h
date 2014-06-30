#ifndef N64_H
#define N64_H

#ifdef __cplusplus
extern "C" {
#endif

#include "Types.h"
#include "m64p_plugin.h"


#define MI_INTR_SP      0x1        // Bit 1: SP intr
#define MI_INTR_DP      0x20        // Bit 5: DP intr 

extern u64 TMEM[512];
extern u32 RDRAMSize;

extern GFX_INFO gfx_info;

#ifdef __cplusplus
}
#endif

#endif

