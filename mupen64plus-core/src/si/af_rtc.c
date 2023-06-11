/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
* Mupen64plus - af_rtc.c *
* Mupen64Plus homepage: http://code.google.com/p/mupen64plus/ *
* Copyright (C) 2014 Bobby Smiles *
* *
* This program is free software; you can redistribute it and/or modify *
* it under the terms of the GNU General Public License as published by *
* the Free Software Foundation; either version 2 of the License, or *
* (at your option) any later version. *
* *
* This program is distributed in the hope that it will be useful, *
* but WITHOUT ANY WARRANTY; without even the implied warranty of *
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the *
* GNU General Public License for more details. *
* *
* You should have received a copy of the GNU General Public License *
* along with this program; if not, write to the *
* Free Software Foundation, Inc., *
* 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA. *
* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include "af_rtc.h"

#include "api/m64p_types.h"
#include "api/callbacks.h"

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

const struct tm* af_rtc_get_time(struct af_rtc* rtc) {
   const struct tm *now = rtc->get_time( rtc->user_data );
   memcpy( &s_time, now, sizeof(struct tm) );
   
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
         cmd[4] = 0x00;
         cmd[5] = 0x02;
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
   /* write RTC block */
   DebugMessage(M64MSG_ERROR, "AF-RTC write command: not yet implemented");
}

void init_af_rtc(struct af_rtc* rtc,
      void* user_data,
      const struct tm* (*get_time)(void*))
{
   rtc->user_data = user_data;
   rtc->get_time = get_time;
   
   errno = 0;
   const long offset = strtol( getenv( "PL_RTC_OFFSET" ), NULL, 10 );
   s_rtcOffset = (errno == 0 && offset >= (long)INT_MIN && offset <= (long)INT_MAX - 61l) ? (int)offset : 0;
}
