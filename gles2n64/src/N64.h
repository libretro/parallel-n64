#ifndef N64_H
#define N64_H

#ifdef __cplusplus
extern "C" {
#endif

#include "Types.h"
#include "m64p_plugin.h"

#define MI_INTR_SP      0x1        // Bit 1: SP intr
#define MI_INTR_DP      0x20        // Bit 5: DP intr 

// Actual TMEM size is 512 QWORDS. However, some load operations load more data that TMEM can take.
// We can either cut the surplus data, or increase the buffer and load everything.
// The second option is more simple and safe. Actual texture load will use correct TMEM size.
#define TMEM_SIZE 1024
#define TMEM_SIZE_BYTES 8192

extern u64 TMEM[512];
extern u32 RDRAMSize;

extern GFX_INFO gfx_info;

#ifdef __cplusplus
}
#endif

#endif

