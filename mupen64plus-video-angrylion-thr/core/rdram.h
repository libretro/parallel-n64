#pragma once

#include "core.h"

#include <stdint.h>
#include <stdbool.h>

#include <retro_inline.h>

#include "m64p_plugin.h"

#define RDRAM_MASK 0x00ffffff

/* endianness */
#ifdef MSB_FIRST
#define BYTE_ADDR_XOR       0
#define WORD_ADDR_XOR       0
#define BYTE4_XOR_BE(a)     (a)

#define BYTE_XOR_DWORD_SWAP 4
#define WORD_XOR_DWORD_SWAP 2
#else
#define BYTE_ADDR_XOR       3
#define WORD_ADDR_XOR       1
#define BYTE4_XOR_BE(a)     ((a) ^ BYTE_ADDR_XOR)

#define BYTE_XOR_DWORD_SWAP 7
#define WORD_XOR_DWORD_SWAP 3
#endif

#define DWORD_XOR_DWORD_SWAP 1

// macros used to interface with AL's code
#define RREADADDR8(rdst, in) {(rdst) = ((((in) & RDRAM_MASK) <= idxlim8) ? gfx_info.RDRAM[((in) & RDRAM_MASK) ^ BYTE_ADDR_XOR] : 0);}
#define RREADIDX16(rdst, in) {(rdst) = ((((in) & (RDRAM_MASK >> 1)) <= idxlim16) ? rdram16[((in) & (RDRAM_MASK >> 1)) ^ WORD_ADDR_XOR] : 0);}
#define RREADIDX32(rdst, in) {(rdst) = ((((in) & (RDRAM_MASK >> 2)) <= idxlim32) ? rdram32[((in) & (RDRAM_MASK >> 2))] : 0);}
#define RREADIDX16FAST(rdst, in) {(rdst) = rdram16[(in) ^WORD_ADDR_XOR];}

#define PAIRREAD16(rdst, hdst, in) \
{ \
    (in) &= RDRAM_MASK >> 1; \
    if ((in) <= idxlim16) \
    { \
        *(rdst) = rdram16[(in) ^ WORD_ADDR_XOR]; \
        *(hdst) = rdram_hidden_bits[(in)]; \
    } \
}
   
#define PAIRWRITE16(in, rval, hval) \
{ \
    (in) &= RDRAM_MASK >> 1; \
    if ((in) <= idxlim16) \
    { \
        rdram16[(in) ^ WORD_ADDR_XOR] = (rval); \
        rdram_hidden_bits[(in)]       = (hval); \
    } \
}

#define PAIRWRITE32(in, rval, hval0, hval1) \
{ \
    (in) &= RDRAM_MASK >> 2; \
    if ((in) <= idxlim32) \
    { \
        rdram32[(in)]                      = (rval); \
        rdram_hidden_bits[(in) << 1]       = (hval0); \
        rdram_hidden_bits[((in) << 1) + 1] = (hval1); \
    } \
}

#define PAIRWRITE8(in, rval, hval) \
{ \
    (in) &= RDRAM_MASK; \
    if ((in) <= idxlim8) \
    { \
        gfx_info.RDRAM[(in) ^ BYTE_ADDR_XOR] = rval; \
        if ((in) & 1) \
            rdram_hidden_bits[(in) >> 1] = hval; \
    } \
}

#define RWRITEADDR8(in, val) \
{ \
    (in) &= RDRAM_MASK; \
    if ((in) <= idxlim8) \
        gfx_info.RDRAM[(in) ^ BYTE_ADDR_XOR] = (val); \
}

#define RWRITEIDX16(in, val) \
{ \
    (in) &= RDRAM_MASK >> 1; \
    if ((in) <= idxlim16) \
        rdram16[(in) ^ WORD_ADDR_XOR] = (val); \
}

#define RWRITEIDX32(in, val) \
{ \
    (in) &= RDRAM_MASK >> 2; \
    if ((in) <= idxlim32) \
        rdram32[(in)] = (val); \
}

extern uint32_t idxlim8;
extern uint32_t idxlim16;
extern uint32_t idxlim32;

extern uint32_t *rdram32;
extern uint16_t *rdram16;
