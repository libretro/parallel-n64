#ifndef M64P_PI_SUMMERCART_H
#define M64P_PI_SUMMERCART_H

#include <stddef.h>
#include <stdint.h>

struct summercart
{
    uint8_t buffer[8192];
    const char *sd_path;
    long sd_size;
    uint32_t status;
    uint32_t data0;
    uint32_t data1;
    uint32_t sd_sector;
    char cfg_rom_write;
    char sd_byteswap;
    char unlock;
    char lock_seq;
};

void init_summercart(struct summercart* summercart);
void poweron_summercart(struct summercart* summercart);
int read_summercart_regs(void* opaque, uint32_t address, uint32_t* value);
int write_summercart_regs(void* opaque, uint32_t address, uint32_t value, uint32_t mask);

#endif
