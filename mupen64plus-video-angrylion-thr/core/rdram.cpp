//
// rdram.c: RDRAM memory interface
//

#include "m64p_plugin.h"
#include "rdram.h"

#ifdef __cplusplus
extern "C" {
#endif

#include "plugin.h"

#ifdef __cplusplus
}
#endif

#define RDRAM_MASK 0x00ffffff

// pointer indexing limits for aliasing RDRAM reads and writes
static uint32_t idxlim8;
static uint32_t idxlim16;
static uint32_t idxlim32;

void rdram_init(void)
{
    idxlim8      = plugin_get_rdram_size() - 1;
    idxlim16     = (idxlim8 >> 1) & 0xffffffu;
    idxlim32     = (idxlim8 >> 2) & 0xffffffu;
}

bool rdram_valid_idx8(uint32_t in)
{
   if (!idxlim8)
      return false;
   return in <= idxlim8;
}

bool rdram_valid_idx16(uint32_t in)
{
   if (!idxlim16)
      return false;
   return in <= idxlim16;
}

bool rdram_valid_idx32(uint32_t in)
{
    return in <= idxlim32;
}

uint8_t rdram_read_idx8(uint32_t in)
{
    in &= RDRAM_MASK;
    return rdram_valid_idx8(in) ? gfx_info.RDRAM[in ^ BYTE_ADDR_XOR] : 0;
}

uint8_t rdram_read_idx8_fast(uint32_t in)
{
    return gfx_info.RDRAM[in ^ BYTE_ADDR_XOR];
}

uint16_t rdram_read_idx16(uint32_t in)
{
    in &= RDRAM_MASK >> 1;
    return rdram_valid_idx16(in) 
       ? (uint16_t)gfx_info.RDRAM[in ^ WORD_ADDR_XOR] : 0;
}

uint16_t rdram_read_idx16_fast(uint32_t in)
{
    return (uint16_t)gfx_info.RDRAM[in ^ WORD_ADDR_XOR];
}

uint32_t rdram_read_idx32(uint32_t in)
{
    in &= RDRAM_MASK >> 2;
    return rdram_valid_idx32(in) ? (uint32_t)gfx_info.RDRAM[in] : 0;
}

uint32_t rdram_read_idx32_fast(uint32_t in)
{
    return (uint32_t)gfx_info.RDRAM[in];
}

void rdram_write_idx8(uint32_t in, uint8_t val)
{
    in &= RDRAM_MASK;
    if (in <= idxlim8)
        gfx_info.RDRAM[in ^ BYTE_ADDR_XOR] = val;
}

void rdram_write_idx16(uint32_t in, uint16_t val)
{
    in &= RDRAM_MASK >> 1;
    if (in <= idxlim16)
        gfx_info.RDRAM[in ^ WORD_ADDR_XOR] = val;
}

void rdram_write_idx32(uint32_t in, uint32_t val)
{
    in &= RDRAM_MASK >> 2;
    if (rdram_valid_idx32(in))
        gfx_info.RDRAM[in] = val;
}

void rdram_read_pair16(uint16_t* rdst, uint8_t* hdst, uint32_t in)
{
    in &= RDRAM_MASK >> 1;
    if (rdram_valid_idx16(in))
    {
        *rdst = (uint16_t)gfx_info.RDRAM[in ^ WORD_ADDR_XOR];
        *hdst = rdram_hidden_bits[in];
    }
    else
        *rdst = *hdst = 0;
}

void rdram_write_pair8(uint32_t in, uint8_t rval, uint8_t hval)
{
    in &= RDRAM_MASK;
    if (rdram_valid_idx8(in))
    {
        gfx_info.RDRAM[in ^ BYTE_ADDR_XOR] = rval;
        if (in & 1)
            rdram_hidden_bits[in >> 1] = hval;
    }
}

void rdram_write_pair16(uint32_t in, uint16_t rval, uint8_t hval)
{
    in &= RDRAM_MASK >> 1;
    if (rdram_valid_idx16(in))
    {
        gfx_info.RDRAM[in ^ WORD_ADDR_XOR] = rval;
        rdram_hidden_bits[in] = hval;
    }
}

void rdram_write_pair32(uint32_t in, uint32_t rval, uint8_t hval0, uint8_t hval1)
{
    in &= RDRAM_MASK >> 2;
    if (rdram_valid_idx32(in))
    {
        gfx_info.RDRAM[in]               = rval;
        rdram_hidden_bits[in << 1]       = hval0;
        rdram_hidden_bits[(in << 1) + 1] = hval1;
    }
}
