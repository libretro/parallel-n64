#include "N64.h"

u8 *HEADER;

/*
    DMEM and IMEM conflict with CXD4 with GCC 10.x and later default
    -fno-common builds: both N64.cpp and cxd4's su.c historically held
    tentative definitions of the same names, and only -fcommon symbol
    merging hid the multiple-definition error. Make N64.cpp the single
    strong definition and give it C linkage so cxd4 (which is built as
    C and declares the same names with C linkage) sees the same symbol.
    The pointer type stays u8 *; cxd4's pu8 is a typedef of the same.
    GLideN64 and cxd4 are never active in the same render path, so
    sharing the storage is harmless.
*/
extern "C" {
    u8 *DMEM;
    u8 *IMEM;
}

u64 TMEM[512];
u8 *RDRAM;

u32 RDRAMSize = 0;

N64Regs REG;

bool ConfigOpen = false;
