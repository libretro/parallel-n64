#ifndef MEMORY_H
#define MEMORY_H

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include "hle_plugin.h"

#ifdef M64P_BIG_ENDIAN
#define S 0
#define S16 0
#define S8 0
#else
#define S 1
#define S16 2
#define S8 3
#endif

enum {
   TASK_TYPE               = 0xfc0,
   TASK_FLAGS              = 0xfc4,
   TASK_UCODE_BOOT         = 0xfc8,
   TASK_UCODE_BOOT_SIZE    = 0xfcc,
   TASK_UCODE              = 0xfd0,
   TASK_UCODE_SIZE         = 0xfd4,
   TASK_UCODE_DATA         = 0xfd8,
   TASK_UCODE_DATA_SIZE    = 0xfdc,
   TASK_DRAM_STACK         = 0xfe0,
   TASK_DRAM_STACK_SIZE    = 0xfe4,
   TASK_OUTPUT_BUFF        = 0xfe8,
   TASK_OUTPUT_BUFF_SIZE   = 0xfec,
   TASK_DATA_PTR           = 0xff0,
   TASK_DATA_SIZE          = 0xff4,
   TASK_YIELD_DATA_PTR     = 0xff8,
   TASK_YIELD_DATA_SIZE    = 0xffc
};

static inline unsigned int align(unsigned int x, unsigned amount)
{
   --amount;
   return (x + amount) & ~amount;
}

static inline uint8_t* const dmem_u8(uint16_t address)
{
    return (uint8_t*)(&q_RspInfo.DMEM[(address & 0xfff) ^ S8]);
}

static inline uint16_t* const dmem_u16(uint16_t address)
{
    assert((address & 1) == 0);
    return (uint16_t*)(&q_RspInfo.DMEM[(address & 0xfff) ^ S16]);
}

static inline uint32_t* const dmem_u32(uint16_t address)
{
    assert((address & 3) == 0);
    return (uint32_t*)(&q_RspInfo.DMEM[(address & 0xfff)]);
}

static inline uint8_t* const dram_u8(uint32_t address)
{
    return (uint8_t*)&q_RspInfo.RDRAM[(address & 0xffffff) ^ S8];
}

static inline uint16_t* const dram_u16(uint32_t address)
{
    assert((address & 1) == 0);
    return (uint16_t*)&q_RspInfo.RDRAM[(address & 0xffffff) ^ S16];
}

static inline uint32_t* const dram_u32(uint32_t address)
{
    assert((address & 3) == 0);
    return (uint32_t*)&q_RspInfo.RDRAM[address & 0xffffff];
}

void dmem_load_u8 (uint8_t* dst, uint16_t address, size_t count);
void dmem_load_u16(uint16_t* dst, uint16_t address, size_t count);
void dmem_load_u32(uint32_t* dst, uint16_t address, size_t count);
void dmem_store_u8 (const uint8_t* src, uint16_t address, size_t count);
void dmem_store_u16(const uint16_t* src, uint16_t address, size_t count);
void dmem_store_u32(const uint32_t* src, uint16_t address, size_t count);

void dram_load_u8 (uint8_t* dst, uint32_t address, size_t count);
void dram_load_u16(uint16_t* dst, uint32_t address, size_t count);
void dram_load_u32(uint32_t* dst, uint32_t address, size_t count);
void dram_store_u8 (const uint8_t* src, uint32_t address, size_t count);
void dram_store_u16(const uint16_t* src, uint32_t address, size_t count);
void dram_store_u32(const uint32_t* src, uint32_t address, size_t count);

#endif
