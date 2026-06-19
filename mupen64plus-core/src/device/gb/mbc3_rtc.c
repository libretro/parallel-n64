/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - mbc3_rtc.c                                              *
 *   Mupen64Plus homepage: https://mupen64plus.org/                        *
 *   Copyright (C) 2016 Bobby Smiles                                       *
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

#include "mbc3_rtc.h"

#include <string.h>
#include <time.h>

/* NOTE: this is a wall-clock backed RTC (host time()), matching the approach
 * used elsewhere in the libretro tree. The halt bit is not implemented. */

static void update_rtc_regs(struct mbc3_rtc* rtc)
{
    unsigned int days;

    /* compute elapsed time since last update */
    time_t now  = time(NULL);
    time_t diff = now - rtc->last_time;
    rtc->last_time = now;

    /* increment regs */
    if (diff > 0)
    {
        rtc->regs[MBC3_RTC_SECONDS] += (int)(diff % 60);
        if (rtc->regs[MBC3_RTC_SECONDS] >= 60)
        {
            rtc->regs[MBC3_RTC_SECONDS] -= 60;
            ++rtc->regs[MBC3_RTC_MINUTES];
        }
        diff /= 60;

        rtc->regs[MBC3_RTC_MINUTES] += (int)(diff % 60);
        if (rtc->regs[MBC3_RTC_MINUTES] >= 60)
        {
            rtc->regs[MBC3_RTC_MINUTES] -= 60;
            ++rtc->regs[MBC3_RTC_HOURS];
        }
        diff /= 60;

        rtc->regs[MBC3_RTC_HOURS] += (int)(diff % 24);
        if (rtc->regs[MBC3_RTC_HOURS] >= 24)
        {
            rtc->regs[MBC3_RTC_HOURS] -= 24;
            ++rtc->regs[MBC3_RTC_DAYS_L];
        }
        diff /= 24;

        /* update days counter */
        days = (((rtc->regs[MBC3_RTC_DAYS_H] & 0x01) << 8) | rtc->regs[MBC3_RTC_DAYS_L])
            + (int)diff;

        rtc->regs[MBC3_RTC_DAYS_L] = days & 0xff;
        rtc->regs[MBC3_RTC_DAYS_H] = (rtc->regs[MBC3_RTC_DAYS_H] & ~0x01) | ((days >> 8) & 0x01);

        /* set carry bit if days overflow */
        if (days > 511)
            rtc->regs[MBC3_RTC_DAYS_H] |= 0x80;
    }
}

void poweron_mbc3_rtc(struct mbc3_rtc* rtc)
{
    memset(rtc->regs, 0, MBC3_RTC_REGS_COUNT);
    memset(rtc->latched_regs, 0, MBC3_RTC_REGS_COUNT);
    rtc->latch     = 0;
    rtc->last_time = time(NULL);
}

uint8_t read_mbc3_rtc_regs(struct mbc3_rtc* rtc, unsigned int reg)
{
    if (reg >= MBC3_RTC_REGS_COUNT)
        return 0;

    if (rtc->latch)
        return rtc->latched_regs[reg];

    update_rtc_regs(rtc);
    return rtc->regs[reg];
}

void write_mbc3_rtc_regs(struct mbc3_rtc* rtc, unsigned int reg, uint8_t value)
{
    if (reg >= MBC3_RTC_REGS_COUNT)
        return;

    rtc->regs[reg] = value;
}

void latch_mbc3_rtc_regs(struct mbc3_rtc* rtc, uint8_t data)
{
    if (rtc->latch == 0 && data == 1)
    {
        update_rtc_regs(rtc);
        memcpy(rtc->latched_regs, rtc->regs, MBC3_RTC_REGS_COUNT);
    }

    rtc->latch = data & 0x1;
}
