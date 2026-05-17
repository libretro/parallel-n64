#include "N64.h"

u8 *HEADER;

/*
    DMEM and IMEM live in mupen64plus-rsp-cxd4/su.c. cxd4 is built on
    every libretro target; this TU (N64.cpp) is part of gliden64 and is
    excluded on HAVE_OPENGL=0 / HAVE_GLIDEN64=0 builds such as webOS,
    so it cannot be the canonical owner. Declare the symbols extern
    with C linkage so this C++ TU resolves to cxd4's C-linkage
    definitions.
*/
extern "C" {
    extern u8 *DMEM;
    extern u8 *IMEM;
}

u64 TMEM[512];
u8 *RDRAM;

u32 RDRAMSize = 0;

N64Regs REG;

bool ConfigOpen = false;
