#pragma once

#include "core.h"

#include <stdint.h>
#include <stdbool.h>

// endianness
#define LSB_FIRST 1 
#ifdef LSB_FIRST
    #define BYTE_ADDR_XOR       3
    #define WORD_ADDR_XOR       1
    #define BYTE4_XOR_BE(a)     ((a) ^ BYTE_ADDR_XOR)
#else
    #define BYTE_ADDR_XOR       0
    #define WORD_ADDR_XOR       0
    #define BYTE4_XOR_BE(a)     (a)
#endif

#ifdef LSB_FIRST
    #define BYTE_XOR_DWORD_SWAP 7
    #define WORD_XOR_DWORD_SWAP 3
#else
    #define BYTE_XOR_DWORD_SWAP 4
    #define WORD_XOR_DWORD_SWAP 2
#endif

#define DWORD_XOR_DWORD_SWAP 1

// macros used to interface with AL's code
#define RREADADDR8(rdst, in) {(rdst) = rdram_read_idx8((in));}
#define RREADIDX16(rdst, in) {(rdst) = rdram_read_idx16((in));}
#define RREADIDX32(rdst, in) {(rdst) = rdram_read_idx32((in));}

#define RWRITEADDR8(in, val) rdram_write_idx8((in), (val))
#define RWRITEIDX16(in, val) rdram_write_idx16((in), (val))
#define RWRITEIDX32(in, val) rdram_write_idx32((in), (val))

#define PAIRREAD16(rdst, hdst, in) rdram_read_pair16(&rdst, &hdst, (in))

#define PAIRWRITE16(in, rval, hval) rdram_write_pair16((in), (rval), (hval))

#define PAIRWRITE32(in, rval, hval0, hval1) rdram_write_pair32((in), (rval), (hval0), (hval1))

#define PAIRWRITE8(in, rval, hval) rdram_write_pair8((in), (rval), (hval))

void rdram_init(void);

bool rdram_valid_idx8(uint32_t in);
bool rdram_valid_idx16(uint32_t in);
bool rdram_valid_idx32(uint32_t in);

uint8_t rdram_read_idx8(uint32_t in);
uint8_t rdram_read_idx8_fast(uint32_t in);
uint16_t rdram_read_idx16(uint32_t in);
uint16_t rdram_read_idx16_fast(uint32_t in);
uint32_t rdram_read_idx32(uint32_t in);
uint32_t rdram_read_idx32_fast(uint32_t in);

void rdram_write_idx8(uint32_t in, uint8_t val);
void rdram_write_idx16(uint32_t in, uint16_t val);
void rdram_write_idx32(uint32_t in, uint32_t val);

void rdram_read_pair16(uint16_t* rdst, uint8_t* hdst, uint32_t in);

void rdram_write_pair8(uint32_t in, uint8_t rval, uint8_t hval);
void rdram_write_pair16(uint32_t in, uint16_t rval, uint8_t hval);
void rdram_write_pair32(uint32_t in, uint32_t rval, uint8_t hval0, uint8_t hval1);
