//
// rdram.c: RDRAM memory interface
//

#include "rdram.h"

#ifdef __cplusplus
extern "C" {
#endif

#include "m64p_plugin.h"
#include "plugin.h"

#ifdef __cplusplus
}
#endif


// pointer indexing limits for aliasing RDRAM reads and writes
uint32_t idxlim8;
uint32_t idxlim16;
uint32_t idxlim32;

uint32_t* rdram32;
uint16_t* rdram16;

void rdram_init(void)
{
    idxlim8      = plugin_get_rdram_size() - 1;
    idxlim16     = (idxlim8 >> 1) & 0xffffffu;
    idxlim32     = (idxlim8 >> 2) & 0xffffffu;

    rdram32      = (uint32_t*)gfx_info.RDRAM;
    rdram16      = (uint16_t*)gfx_info.RDRAM;
}

uint16_t rdram_read_idx16(uint32_t in)
{
    in &= RDRAM_MASK >> 1;
    return (in <= idxlim16) ? rdram16[in ^ WORD_ADDR_XOR] : 0;
}

uint16_t rdram_read_idx16_fast(uint32_t in)
{
    return rdram16[in ^ WORD_ADDR_XOR];
}

uint32_t rdram_read_idx32(uint32_t in)
{
    in &= RDRAM_MASK >> 2;
    return (in <= idxlim32) ? rdram32[in] : 0;
}

uint32_t rdram_read_idx32_fast(uint32_t in)
{
    return rdram32[in];
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
        rdram16[in ^ WORD_ADDR_XOR] = val;
}

void rdram_write_idx32(uint32_t in, uint32_t val)
{
    in &= RDRAM_MASK >> 2;
    if (in <= idxlim32)
        rdram32[in] = val;
}

void rdram_read_pair16(uint16_t* rdst, uint8_t* hdst, uint32_t in)
{
    in &= RDRAM_MASK >> 1;
    if (in <= idxlim16)
    {
        *rdst = rdram16[in ^ WORD_ADDR_XOR];
        *hdst = rdram_hidden_bits[in];
    }
    else
        *rdst = *hdst = 0;
}

void rdram_write_pair8(uint32_t in, uint8_t rval, uint8_t hval)
{
    in &= RDRAM_MASK;
    if (in <= idxlim8)
    {
        gfx_info.RDRAM[in ^ BYTE_ADDR_XOR] = rval;
        if (in & 1)
            rdram_hidden_bits[in >> 1] = hval;
    }
}

void rdram_write_pair16(uint32_t in, uint16_t rval, uint8_t hval)
{
    in &= RDRAM_MASK >> 1;
    if (in <= idxlim16)
    {
        rdram16[in ^ WORD_ADDR_XOR] = rval;
        rdram_hidden_bits[in] = hval;
    }
}

void rdram_write_pair32(uint32_t in, uint32_t rval, uint8_t hval0, uint8_t hval1)
{
    in &= RDRAM_MASK >> 2;
    if (in <= idxlim32)
    {
        rdram32[in] = rval;
        rdram_hidden_bits[in << 1] = hval0;
        rdram_hidden_bits[(in << 1) + 1] = hval1;
    }
}
