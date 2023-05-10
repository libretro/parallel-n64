#include "pi_controller.h"

#define M64P_CORE_PROTOTYPES 1
#include "../main/util.h"

#include <stdlib.h>
#include <string.h>

static uint8_t* summercart_sd_addr(struct pi_controller* pi)
{
    uint32_t sector = pi->summercart.sd_sector;
    uint32_t addr = pi->summercart.data0 & 0x1fffffff;
    uint32_t count = pi->summercart.data1;
    int64_t size = (int64_t)512 * count;
    if ((int64_t)sector+count > pi->summercart.sd_size) return NULL;
    if (addr >= 0x1ffe0000 && addr+size <= 0x1ffe0000+8192)
    {
        return pi->summercart.buffer + (addr - 0x1ffe0000);
    }
    if (addr >= 0x10000000 && addr+size <= 0x10000000+0x4000000)
    {
        return pi->cart_rom.rom + (addr - 0x10000000);
    }
    return NULL;
}

static char summercart_sd_byteswap(struct pi_controller* pi)
{
    uint32_t addr = pi->summercart.data0 & 0x1fffffff;
    uint32_t count = pi->summercart.data1;
    int64_t size = (int64_t)512 * count;
    if (addr >= 0x10000000 && addr+size <= 0x10000000+0x4000000)
    {
        return pi->summercart.sd_byteswap;
    }
    return 0;
}

static void summercart_sd_init(struct summercart* summercart)
{
    if( summercart->file ) filestream_close( summercart->file );
    
    summercart->file = NULL;
    summercart->sd_size = 0;
    
    const char *const path = getenv("PL_SD_CARD_IMAGE");
    if( path && path[0] ) {
        summercart->file = filestream_open(
            path,
            RETRO_VFS_FILE_ACCESS_READ_WRITE | RETRO_VFS_FILE_ACCESS_UPDATE_EXISTING,
            RETRO_VFS_FILE_ACCESS_HINT_NONE
        );
        
        if( summercart->file ) {
            summercart->sd_size = filestream_get_size( summercart->file );
            summercart->status = 0;
        } else {
            summercart->status = 0x40000000;
        }
    }
}

static void summercart_sd_deinit(struct summercart* summercart)
{
    summercart->status = 0;
    if( summercart->file ) {
        filestream_close( summercart->file );
        summercart->file = NULL;
        summercart->sd_size = 0;
    }
}

static void summercart_sd_read(struct pi_controller* pi)
{
    RFILE* stream = pi->summercart.file;
    uint8_t* ptr = summercart_sd_addr(pi);
    uint32_t sector = pi->summercart.sd_sector;
    uint32_t count = pi->summercart.data1;
    int64_t offset = (int64_t)512 * sector;
    int64_t size = (int64_t)512 * count;
    if (ptr && stream)
    {
        filestream_seek(stream, offset, RETRO_VFS_SEEK_POSITION_START);
        if (!filestream_error(stream))
        {
#ifndef MSB_FIRST
            swap_buffer(ptr, 4, 512/4*count);
#endif
            if (filestream_read(stream, ptr, size) == size)
            {
                if (summercart_sd_byteswap(pi))
                {
                    swap_buffer(ptr, 2, 512/2*count);
                }
                pi->summercart.status = 0;
            }
#ifndef MSB_FIRST
            swap_buffer(ptr, 4, 512/4*count);
#endif
        }
        filestream_flush(stream);
    }
}

static void summercart_sd_write(struct pi_controller* pi)
{
    RFILE* stream = pi->summercart.file;
    uint8_t* ptr = summercart_sd_addr(pi);
    uint32_t sector = pi->summercart.sd_sector;
    uint32_t count = pi->summercart.data1;
    int64_t offset = (int64_t)512 * sector;
    int64_t size = (int64_t)512 * count;
    if (ptr && stream)
    {
        filestream_seek(stream, offset, RETRO_VFS_SEEK_POSITION_START);
        if (!filestream_error(stream))
        {
#ifndef MSB_FIRST
            swap_buffer(ptr, 4, 512/4*count);
#endif
            if (filestream_write(stream, ptr, size) == size)
            {
                pi->summercart.status = 0;
            }
#ifndef MSB_FIRST
            swap_buffer(ptr, 4, 512/4*count);
#endif
        }
        filestream_flush(stream);
    }
}

void init_summercart(struct summercart* summercart)
{
    memset(summercart, 0, sizeof(struct summercart));
}

int read_summercart_regs(void* opaque, uint32_t address, uint32_t* value)
{
    struct pi_controller* pi    = (struct pi_controller*)opaque;
    uint32_t addr               = address & 0xFFFF;

    *value = 0;

    if (!pi->summercart.unlock) return 0;

    switch (address & 0xFFFF)
    {
    case 0x00:  *value = pi->summercart.status; break;
    case 0x04:  *value = pi->summercart.data0;  break;
    case 0x08:  *value = pi->summercart.data1;  break;
    case 0x0C:  *value = 0x53437632;            break;
    }

    return 0;
}

int write_summercart_regs(void* opaque, uint32_t address, uint32_t value, uint32_t mask)
{
    struct pi_controller* pi    = (struct pi_controller*)opaque;
    uint32_t addr               = address & 0xFFFF;

    if (addr == 0x10)
    {
        switch (value & mask)
        {
        case 0xFFFFFFFF:
            pi->summercart.unlock = 0;
            break;
        case 0x5F554E4C:
            if (pi->summercart.lock_seq == 0)
            {
                pi->summercart.lock_seq = 2;
            }
            break;
        case 0x4F434B5F:
            if (pi->summercart.lock_seq == 2)
            {
                pi->summercart.unlock = 1;
                pi->summercart.lock_seq = 0;
            }
            break;
        default:
            pi->summercart.lock_seq = 0;
            break;
        }
        return 0;
    }

    if (!pi->summercart.unlock) return 0;

    switch (addr)
    {
    case 0x00:
        pi->summercart.status = 0x40000000;
        switch (value & mask)
        {
        case 'c':
            switch (pi->summercart.data0)
            {
            case 1:
                pi->summercart.data1 = pi->summercart.cfg_rom_write;
                pi->summercart.status = 0;
                break;
            case 3:
                pi->summercart.data1 = 0;
                pi->summercart.status = 0;
                break;
            case 6:
                pi->summercart.data1 = 0;
                pi->summercart.status = 0;
                break;
            }
            break;
        case 'C':
            switch (pi->summercart.data0)
            {
            case 1:
                if (pi->summercart.data1)
                {
                    pi->summercart.data1 = pi->summercart.cfg_rom_write;
                    pi->summercart.cfg_rom_write = 1;
                }
                else
                {
                    pi->summercart.data1 = pi->summercart.cfg_rom_write;
                    pi->summercart.cfg_rom_write = 0;
                }
                pi->summercart.status = 0;
                break;
            }
            break;
        case 'i':
            switch (pi->summercart.data1)
            {
            case 0:
                summercart_sd_deinit(&pi->summercart);
                break;
            case 1:
                summercart_sd_init(&pi->summercart);
                break;
            case 4:
                pi->summercart.sd_byteswap = 1;
                pi->summercart.status = 0;
                break;
            case 5:
                pi->summercart.sd_byteswap = 0;
                pi->summercart.status = 0;
                break;
            }
            break;
        case 'I':
            pi->summercart.sd_sector = pi->summercart.data0;
            pi->summercart.status = 0;
            break;
        case 's':
            summercart_sd_read(pi);
            break;
        case 'S':
            summercart_sd_write(pi);
            break;
        }
        break;
    case 0x04:
        pi->summercart.data0 = value & mask;
        break;
    case 0x08:
        pi->summercart.data1 = value & mask;
        break;
    }

    return 0;
}
