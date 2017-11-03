#pragma once

#include "core.h"

#include <stdint.h>
#include <stdbool.h>

#include <retro_inline.h>

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

#define RWRITEADDR8(in, val) rdram_write_idx8((in), (val))
#define RWRITEIDX16(in, val) rdram_write_idx16((in), (val))
#define RWRITEIDX32(in, val) rdram_write_idx32((in), (val))

#define PAIRREAD16(rdst, hdst, in) rdram_read_pair16(rdst, hdst, (in))

#define PAIRWRITE16(in, rval, hval) rdram_write_pair16((in), (rval), (hval))

#define PAIRWRITE32(in, rval, hval0, hval1) rdram_write_pair32((in), (rval), (hval0), (hval1))

#define PAIRWRITE8(in, rval, hval) rdram_write_pair8((in), (rval), (hval))

extern uint32_t idxlim8;
extern uint32_t idxlim16;
extern uint32_t idxlim32;

extern uint32_t *rdram32;
extern uint16_t *rdram16;

void rdram_init(void);

void rdram_write_idx8(uint32_t in, uint8_t val);
void rdram_write_idx16(uint32_t in, uint16_t val);
void rdram_write_idx32(uint32_t in, uint32_t val);

static INLINE void rdram_read_pair16(uint16_t* rdst, uint8_t* hdst, uint32_t in)
{
    in &= RDRAM_MASK >> 1;
    if (in <= idxlim16)
    {
        *rdst = rdram16[in ^ WORD_ADDR_XOR];
        *hdst = rdram_hidden_bits[in];
    }
}

void rdram_write_pair8(uint32_t in, uint8_t rval, uint8_t hval);
void rdram_write_pair16(uint32_t in, uint16_t rval, uint8_t hval);
void rdram_write_pair32(uint32_t in, uint32_t rval, uint8_t hval0, uint8_t hval1);
