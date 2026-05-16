#ifndef M64P_PI_SUMMERCART_H
#define M64P_PI_SUMMERCART_H

#include <stddef.h>
#include <stdint.h>
#include <boolean.h>
#include <streams/file_stream.h>

struct summercart
{
    uint8_t buffer[8192];
    int64_t sd_size;
    RFILE *file;
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
bool load_sdcard(struct summercart* summercart, const char *path);
int read_summercart_regs(void* opaque, uint32_t address, uint32_t* value);
int write_summercart_regs(void* opaque, uint32_t address, uint32_t value, uint32_t mask);

#endif
