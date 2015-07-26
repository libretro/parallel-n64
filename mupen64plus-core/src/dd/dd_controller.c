/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
*   Mupen64plus - dd_controller.c                                         *
*   Mupen64Plus homepage: http://code.google.com/p/mupen64plus/           *
*   Copyright (C) 2015 LuigiBlood                                         *
*                                                                         *
*   This program is free software; you can redistribute it and/or modify  *
*   it under the terms of the GNU General Public License as published by  *
*   the Free Software Foundation; either version 2 of the License, or     *
*   (at your option) any later version.                                   *
*                                                                         *
*   This program is distributed in the hope that it will be useful,       *
*   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
*   GNU General Public License for more details.                          *
*                                                                         *
*   You should have received a copy of the GNU General Public License     *
*   along with this program; if not, write to the                         *
*   Free Software Foundation, Inc.,                                       *
*   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.          *
* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include "dd_controller.h"
#include "dd_rom.h"
#include "dd_disk.h"

#include <string.h>
#include <time.h>

#define M64P_CORE_PROTOTYPES 1
#include "api/m64p_config.h"
#include "api/m64p_types.h"
#include "main/main.h"
#include "memory/memory.h"
#include "r4300/r4300_core.h"
#include "si/pif.h"
#include "si/si_controller.h"

int dd_bm_mode_read;
int CUR_BLOCK;
int dd_sector55;
struct tm* timeinfo;

static unsigned char byte2bcd(int n)
{
    n %= 100;
    return ((n / 10) << 4) | (n % 10);
}

void connect_dd(struct dd_controller* dd,
                struct r4300_core* r4300,
                uint8_t* dd_disk,
                size_t dd_disk_size)
{
    dd->r4300 = r4300;

    connect_dd_disk(&dd->disk, dd_disk, dd_disk_size);
}

void init_dd(struct dd_controller* dd)
{
    memset(dd->regs, 0, ASIC_REGS_COUNT*sizeof(uint32_t));
    memset(dd->c2_buf, 0, 0x400);
    memset(dd->sec_buf, 0, 0x100);
    memset(dd->mseq_buf, 0, 0x40);

    dd->regs[ASIC_CMD_STATUS] =
        (ConfigGetParamBool(g_CoreConfig, "64DD") == 1) ? 0x01000000 : 0xffffffff;
    dd->regs[ASIC_ID_REG] = 0x00030000;
}

int read_dd_regs(void* opaque, uint32_t address, uint32_t* value)
{
    struct dd_controller* dd = (struct dd_controller*)opaque;
    uint32_t reg = dd_reg(address);

    uint32_t offset = address & 0x00000fff;

    *value = 0x00000000;

    if (reg < ASIC_REGS_COUNT)
        *value = dd->regs[reg];

    int Cur_Sector = dd->regs[ASIC_CUR_SECTOR];
    if (Cur_Sector >= 0x5A)
        Cur_Sector -= 0x5A;

    if ((dd->regs[ASIC_CMD_STATUS] & 0x04000000) && (85 < dd->regs[ASIC_CUR_SECTOR]))
    {
        dd->regs[ASIC_CMD_STATUS] &= ~0x04000000;
        dd_update_bm(dd);
    }

    /*
    //BUFFERS
    switch (address & 0x00000c00)
    {
        case 0x000:
            //C2 BUFFER
            *value = dd->c2_buf[offset/4];
            break;
        case 0x400:
            //SECTOR BUFFER
            offset -= 0x400;
            *value = dd->sec_buf[offset/4];
            break;
    }

    if ((address & 0x00000f00) == 0x500)
    {
        //REGS
        if (reg < ASIC_REGS_COUNT)
            *value = dd->regs[reg];

        if (((address & 0x00000FFF) >= 0x580) || ((address & 0x00000FFF) < 0x5C0))
        {
            //MSEQ
            offset -= 0x580;
            *value = dd->mseq_buf[offset/4];
        }
    }
    */
    return 0;
}

int write_dd_regs(void* opaque, uint32_t address, uint32_t value, uint32_t mask)
{
    struct dd_controller* dd = (struct dd_controller*)opaque;
    uint32_t reg = dd_reg(address);

    value &= 0xffff0000;

    if (!ConfigGetParamBool(g_CoreConfig, "64DD"))
        return 0;

    switch (reg)
    {
        case ASIC_DATA:
            dd->regs[ASIC_DATA] = value;
            break;

        case ASIC_BM_STATUS_CTL:
            if (value & 0x01000000)
            {
                //MECHA INT RESET
                dd->regs[ASIC_CMD_STATUS] &= ~0x02000000;
            }

            if (value & 0x02000000)
            {
                //BLOCK TRANSFER
                dd->regs[ASIC_BM_STATUS_CTL] |= 0x01000000;
            }
            else
                dd->regs[ASIC_BM_STATUS_CTL] &= ~0x01000000;

            if (value & 0x10000000)
            {
                //BM RESET
                dd->regs[ASIC_CMD_STATUS] &= ~0x5C000000;
                dd->regs[ASIC_BM_STATUS_CTL] = 0x00000000;
            }

            //SET SECTOR
            dd->regs[ASIC_CUR_SECTOR] = value & 0x00FF0000;
            CUR_BLOCK = ((dd->regs[ASIC_CUR_SECTOR] >> 16) < 0x5A) ? 0 : 1;

            if (value & 0x80000000)
            {
                //BM START
                dd->regs[ASIC_BM_STATUS_CTL] |= 0x80000000;
                dd_sector55 = 0;
                dd_update_bm(dd);
            }

            break;

        case ASIC_CMD_STATUS:
            //ASIC Commands
            timeinfo = (struct tm*)af_rtc_get_time(&g_si.pif.af_rtc);
            uint8_t year, month, hour, day, min, sec;

            switch (value >> 16)
            {
                case 0x01:
                    //SEEK READ TRACK
                    dd->regs[ASIC_CUR_TK] = dd->regs[ASIC_DATA] | 0x60000000;
                    dd->regs[ASIC_CMD_STATUS] &= ~0x00180000;
                    dd_bm_mode_read = 1;
                    dd_set_zone_and_track_offset(dd);
                    break;

                case 0x02:
                    //SEEK WRITE TRACK
                    dd->regs[ASIC_CUR_TK] = dd->regs[ASIC_DATA] | 0x60000000;
                    dd->regs[ASIC_CMD_STATUS] &= ~0x00180000;
                    dd_bm_mode_read = 0;
                    dd_set_zone_and_track_offset(dd);
                    break;

                case 0x08:
                    //CLEAR DISK CHANGE FLAG
                    dd->regs[ASIC_CMD_STATUS] &= ~0x00010000;
                    break;

                case 0x09:
                    //CLEAR RESET FLAG
                    dd->regs[ASIC_CMD_STATUS] &= ~0x00400000;
                    break;

                case 0x12:
                    //Get Year/Month

                    //Put time in DATA as BCD
                    year = (uint8_t)byte2bcd(timeinfo->tm_year);
                    month = (uint8_t)byte2bcd((timeinfo->tm_mon + 1));

                    dd->regs[ASIC_DATA] = (year << 24) | (month << 16);
                    break;

                case 0x13:
                    //Get Day/Hour

                    //Put time in DATA as BCD
                    hour = (uint8_t)byte2bcd(timeinfo->tm_hour);
                    day = (uint8_t)byte2bcd(timeinfo->tm_mday);

                    dd->regs[ASIC_DATA] = (day << 24) | (hour << 16);
                    break;
                case 0x14:
                    //Get Min/Sec

                    //Put time in DATA as BCD
                    min = (uint8_t)byte2bcd(timeinfo->tm_min);
                    sec = (uint8_t)byte2bcd(timeinfo->tm_sec);

                    dd->regs[ASIC_DATA] = (min << 24) | (sec << 16);
                    break;

                case 0x1b:
                    //Feature Inquiry
                    dd->regs[ASIC_DATA] = 0x00010000;
                    break;
            }

            dd->regs[ASIC_CMD_STATUS] |= 0x02000000;
            update_count();
            add_interupt_event(CART_INT, 100);
            break;

        case ASIC_HARD_RESET:
            dd->regs[ASIC_CMD_STATUS] |= 0x00400000;
            break;
    }

    return 0;
}

int dd_end_of_dma_event(struct dd_controller* dd)
{
    //Insert clear CART INT here or something
    dd_update_bm(dd);

    if ((dd->regs[ASIC_CMD_STATUS] & 0x06000000) == 0)
        return 1;

    return 0;
}
