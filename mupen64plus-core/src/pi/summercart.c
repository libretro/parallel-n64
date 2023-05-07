#include "pi_controller.h"

#define M64P_CORE_PROTOTYPES 1
#include "../memory/memory.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static uint8_t* summercart_sd_addr(struct pi_controller* pi, size_t size)
{
    uint32_t addr = pi->summercart.data0 & 0x1fffffff;
    if (addr >= 0x1ffe0000 && addr+size < 0x1ffe0000+8192)
    {
        return pi->summercart.buffer + (addr - 0x1ffe0000);
    }
    if (addr >= 0x10000000 && addr+size < 0x10000000+0x4000000)
    {
        return pi->cart_rom.rom + (addr - 0x10000000);
    }
    return NULL;
}

static uint32_t summercart_sd_init(struct pi_controller* pi)
{
    FILE* fp;
    if (!pi->summercart.sd_path) return 0x40000000;
    if (!(fp = fopen(pi->summercart.sd_path, "rb"))) return 0x40000000;
    fseek(fp, 0, SEEK_END);
    pi->summercart.sd_size = ftell(fp);
    fclose(fp);
    return 0;
}

static uint32_t summercart_sd_read(struct pi_controller* pi)
{
    size_t i;
    FILE* fp;
    uint8_t* ptr;
    long offset = 512 * pi->summercart.sd_sector;
    size_t size = 512 * pi->summercart.data1;
    if (offset+size > pi->summercart.sd_size) return 0x40000000;
    if (!(ptr = summercart_sd_addr(pi, size))) return 0x40000000;
    if (!(fp = fopen(pi->summercart.sd_path, "rb"))) return 0x40000000;
    fseek(fp, offset, SEEK_SET);
    for (i = 0; i < size; ++i)
    {
        int c = fgetc(fp);
        if (c < 0)
        {
            fclose(fp);
            return 0x40000000;
        }
        ptr[i^pi->summercart.sd_byteswap^S8] = c;
    }
    fclose(fp);
    return 0;
}

static uint32_t summercart_sd_write(struct pi_controller* pi)
{
    size_t i;
    FILE* fp;
    uint8_t* ptr;
    long offset = 512 * pi->summercart.sd_sector;
    size_t size = 512 * pi->summercart.data1;
    if (offset+size > pi->summercart.sd_size) return 0x40000000;
    if (!(ptr = summercart_sd_addr(pi, size))) return 0x40000000;
    if (!(fp = fopen(pi->summercart.sd_path, "r+b"))) return 0x40000000;
    fseek(fp, offset, SEEK_SET);
    for (i = 0; i < size; ++i)
    {
        int c = fputc(ptr[i^S8], fp);
        if (c < 0)
        {
            fclose(fp);
            return 0x40000000;
        }
    }
    fclose(fp);
    return 0;
}

void init_summercart(struct summercart* summercart)
{
    summercart->sd_path = getenv("PL_SD_CARD_IMAGE");
}

void poweron_summercart(struct summercart* summercart)
{
    memset(summercart->buffer, 0, 8192);
    summercart->sd_size = -1;
    summercart->status = 0;
    summercart->data0 = 0;
    summercart->data1 = 0;
    summercart->sd_sector = 0;
    summercart->cfg_rom_write = 0;
    summercart->sd_byteswap = 0;
    summercart->unlock = 0;
    summercart->lock_seq = 0;
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
        pi->summercart.status = 0;
        switch (value & mask)
        {
        case 'c':
            switch (pi->summercart.data0)
            {
            case 1:
                pi->summercart.data1 = pi->summercart.cfg_rom_write;
                break;
            case 3:
                pi->summercart.data1 = 0;
                break;
            case 6:
                pi->summercart.data1 = 0;
                break;
            default:
                pi->summercart.status = 0x40000000;
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
                break;
            default:
                pi->summercart.status = 0x40000000;
                break;
            }
            break;
        case 'i':
            switch (pi->summercart.data1)
            {
            case 0:
                break;
            case 1:
                pi->summercart.status = summercart_sd_init(pi);
                break;
            case 4:
                pi->summercart.sd_byteswap = 1;
                break;
            case 5:
                pi->summercart.sd_byteswap = 0;
                break;
            default:
                pi->summercart.status = 0x40000000;
                break;
            }
            break;
        case 'I':
            pi->summercart.sd_sector = pi->summercart.data0;
            break;
        case 's':
            pi->summercart.status = summercart_sd_read(pi);
            break;
        case 'S':
            pi->summercart.status = summercart_sd_write(pi);
            break;
        default:
            pi->summercart.status = 0x40000000;
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
