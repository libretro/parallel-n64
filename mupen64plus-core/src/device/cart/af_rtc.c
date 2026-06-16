/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - af_rtc.c                                                *
 *   Mupen64Plus homepage: http://code.google.com/p/mupen64plus/           *
 *   Copyright (C) 2014 Bobby Smiles                                       *
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

#include "af_rtc.h"

#include "../../api/m64p_types.h"
#include "../../api/callbacks.h"
#include "../../backends/api/clock_backend.h"

#include <time.h>
#include <stdlib.h>
#include <limits.h>
#include <errno.h>
#include <string.h>

static struct tm s_time;
static int s_rtcOffset = 0;

static uint8_t byte2bcd(int n)
{
   n %= 100;
   return ((n / 10) << 4) | (n % 10);
}

/* parallel-n64 reads wall-clock time through next's clock_backend (which
 * returns a time_t), converts it to a broken-down struct tm, and applies the
 * PL_RTC_OFFSET-derived s_rtcOffset. This keeps pn64's struct tm-based RTC
 * model and the PL_RTC_OFFSET feature while sourcing the base time through the
 * same backend interface the other cart chips use. */
const struct tm* af_rtc_get_time(struct af_rtc* rtc) {
   time_t now = rtc->iclock->get_time(rtc->clock);
   const struct tm *bt = localtime(&now);
   memcpy( &s_time, bt, sizeof(struct tm) );

   s_time.tm_sec += s_rtcOffset;
   mktime( &s_time );

   return &s_time;
}

void af_rtc_set_time(struct af_rtc* rtc, struct tm *timestamp) {
   af_rtc_get_time( rtc );
   const time_t now = mktime( &s_time );
   const time_t then = mktime( timestamp );

   if( now == -1 || then == -1 ) return;
   const double offset = difftime( then, now );
   if( offset < (double)INT_MIN || offset > (double)(INT_MAX - 61) ) return;

   s_rtcOffset = (int)offset;
}

void af_rtc_status_command(struct af_rtc *rtc, uint8_t *cmd)
{
   /* AF-RTC status query */
   cmd[3] = 0x00;
   cmd[4] = 0x10;
   cmd[5] = 0x00;
}

void af_rtc_read_command(struct af_rtc *rtc, uint8_t *cmd)
{
   const struct tm *rtc_time;

   /* read RTC block (cmd[3]: block number) */
   switch (cmd[3])
   {
      case 0:
         /* block 0: report the control register (little-endian) */
         cmd[4] = (uint8_t)(rtc->control >> 0);
         cmd[5] = (uint8_t)(rtc->control >> 8);
         cmd[12] = 0x00;
         break;
      case 1:
         DebugMessage(M64MSG_ERROR, "AF-RTC read command: cannot read block 1");
         break;
      case 2:
         rtc_time = af_rtc_get_time(rtc);
         cmd[4] = byte2bcd(rtc_time->tm_sec);
         cmd[5] = byte2bcd(rtc_time->tm_min);
         cmd[6] = 0x80 + byte2bcd(rtc_time->tm_hour);
         cmd[7] = byte2bcd(rtc_time->tm_mday);
         cmd[8] = byte2bcd(rtc_time->tm_wday);
         cmd[9] = byte2bcd(rtc_time->tm_mon + 1);
         cmd[10] = byte2bcd(rtc_time->tm_year);
         cmd[11] = byte2bcd(rtc_time->tm_year / 100);
         cmd[12] = 0x00;	/* status */
         break;
   }
}

void af_rtc_write_command(struct af_rtc *rtc, uint8_t* cmd)
{
   /* write RTC block (cmd[3]: block number, data follows from cmd[4]) */
   switch (cmd[3])
   {
      case 0:
         /* block 0 sets the control register (little-endian) */
         rtc->control = (uint16_t)((cmd[5] << 8) | cmd[4]);
         break;
      case 1:
         /* block 1 is write-protected while control bit 0 is set */
         if (rtc->control & 0x01)
            break;
         DebugMessage(M64MSG_ERROR, "AF-RTC write command: block 1 not yet implemented");
         break;
      case 2:
         /* block 2 (the clock) is write-protected while control bit 1 is set */
         if (rtc->control & 0x02)
            break;
         DebugMessage(M64MSG_ERROR, "AF-RTC write command: block 2 not yet implemented");
         break;
   }
}

/* mupen64plus-next-style block accessors (used by the joybus cart device). These
 * present next's (block, data, status) interface, but read/write through
 * parallel-n64's struct tm + control-register model rather than next's
 * now/last_update_rtc accumulator, so the time source and savestate format are
 * unchanged. */
void af_rtc_read_block(struct af_rtc* rtc, uint8_t block, uint8_t* data, uint8_t* status)
{
   const struct tm *rtc_time;

   switch (block)
   {
      case 0:
         data[0] = (uint8_t)(rtc->control >> 0);
         data[1] = (uint8_t)(rtc->control >> 8);
         *status = 0x00;
         break;
      case 1:
         DebugMessage(M64MSG_ERROR, "AF-RTC reading block 1 is not implemented !");
         break;
      case 2:
         rtc_time = af_rtc_get_time(rtc);
         data[0] = byte2bcd(rtc_time->tm_sec);
         data[1] = byte2bcd(rtc_time->tm_min);
         data[2] = 0x80 + byte2bcd(rtc_time->tm_hour);
         data[3] = byte2bcd(rtc_time->tm_mday);
         data[4] = byte2bcd(rtc_time->tm_wday);
         data[5] = byte2bcd(rtc_time->tm_mon + 1);
         data[6] = byte2bcd(rtc_time->tm_year);
         data[7] = byte2bcd(rtc_time->tm_year / 100);
         *status = 0x00;
         break;
      default:
         DebugMessage(M64MSG_ERROR, "AF-RTC read invalid block: %u", block);
   }
}

void af_rtc_write_block(struct af_rtc* rtc, uint8_t block, const uint8_t* data, uint8_t* status)
{
   switch (block)
   {
      case 0:
         rtc->control = (uint16_t)((data[1] << 8) | data[0]);
         *status = 0x00;
         break;
      case 1:
         if (rtc->control & 0x01)
            break;
         DebugMessage(M64MSG_ERROR, "AF-RTC writing block 1 is not implemented !");
         break;
      case 2:
         if (rtc->control & 0x02)
            break;
         DebugMessage(M64MSG_ERROR, "AF-RTC writing block 2 is not implemented !");
         break;
      default:
         DebugMessage(M64MSG_ERROR, "AF-RTC write invalid block: %u", block);
   }
}

void poweron_af_rtc(struct af_rtc* rtc)
{
   /* power-on default: blocks 1 & 2 read/write, timer active (matches
    * mupen64plus-next). Time state itself lives in the external clock backend
    * plus the PL_RTC_OFFSET applied in af_rtc_get_time. */
   rtc->control = 0x0200;
}

void init_af_rtc(struct af_rtc* rtc,
      void* clock,
      const struct clock_backend_interface* iclock)
{
   rtc->clock  = clock;
   rtc->iclock = iclock;
   rtc->control = 0x0200;

   errno = 0;
   const char *offsetStr = getenv( "PL_RTC_OFFSET" );
   if( offsetStr && offsetStr[0] != '\0' ) {
      const long offset = strtol( offsetStr, NULL, 10 );
      s_rtcOffset = (errno == 0 && offset >= (long)INT_MIN && offset <= (long)INT_MAX - 61l) ? (int)offset : 0;
   } else {
      s_rtcOffset = 0;
   }
}
