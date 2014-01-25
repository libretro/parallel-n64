/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus-rsp-hle - ucode1.cpp                                      *
 *   Mupen64Plus homepage: http://code.google.com/p/mupen64plus/           *
 *   Copyright (C) 2014 Bobby Smiles                                       *
 *   Copyright (C) 2009 Richard Goedeken                                   *
 *   Copyright (C) 2002 Hacktarux                                          *
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

#include <stdint.h>
#include <string.h>

#include "hle.h"
#include "alist_internal.h"

/* alist state */

static struct {
    /* main buffers */
    uint16_t in;
    uint16_t out;
    uint16_t count;

    /* auxiliary buffers */
    uint16_t dry_right;
    uint16_t wet_left;
    uint16_t wet_right;

    /* gains */
    int16_t dry;
    int16_t wet;

    /* envelopes (0:left, 1:right) */
    int16_t vol[2];
    int16_t target[2];
    int32_t rate[2];

    /* ADPCM loop point address */
    uint32_t loop;

    /* storage for ADPCM table and polef coefficients */
    int16_t table[16 * 8];
} l_alist;

//#include "rsp.h"
//#define SAFE_MEMORY
/*
#ifndef SAFE_MEMORY
#   define wr8 (src , address);
#   define rd8 (dest, address);
#   define wr16 (src, address);
#   define rd16 (dest, address);
#   define wr32 (src, address);
#   define rd32 (dest, address);
#   define wr64 (src, address);
#   define rd64 (dest, address);
#   define dmamem (dest, src, size) memcpy (dest, src, size);
#   define clrmem (dest, size)      memset (dest, 0, size);
#else
    void wr8 (uint8_t src, void *address);
    void rd8 (uint8_t dest, void *address);
    void wr16 (uint16_t src, void *address);
    void rd16 (uint16_t dest, void *address);
    void wr32 (uint16_t src, void *address);
    void rd32 (uint16_t dest, void *address);
    void wr64 (uint16_t src, void *address);
    void rd64 (uint16_t dest, void *address);
    void dmamem (void *dest, void *src, int32_t size);
    void clrmem (void *dest, int32_t size);
#endif
*/
/******** DMEM Memory Map for ABI 1 ***************
Address/Range       Description
-------------       -------------------------------
0x000..0x2BF        UCodeData
    0x000-0x00F     Constants  - 0000 0001 0002 FFFF 0020 0800 7FFF 4000
    0x010-0x02F     Function Jump Table (16 Functions * 2 bytes each = 32) 0x20
    0x030-0x03F     Constants  - F000 0F00 00F0 000F 0001 0010 0100 1000
    0x040-0x03F     Used by the Envelope Mixer (But what for?)
    0x070-0x07F     Used by the Envelope Mixer (But what for?)
0x2C0..0x31F        <Unknown>
0x320..0x35F        Segments
0x360               Audio In Buffer (Location)
0x362               Audio Out Buffer (Location)
0x364               Audio Buffer Size (Location)
0x366               Initial Volume for Left Channel
0x368               Initial Volume for Right Channel
0x36A               Auxillary Buffer #1 (Location)
0x36C               Auxillary Buffer #2 (Location)
0x36E               Auxillary Buffer #3 (Location)
0x370               Loop Value (shared location)
0x370               Target Volume (Left)
0x372               Ramp?? (Left)
0x374               Rate?? (Left)
0x376               Target Volume (Right)
0x378               Ramp?? (Right)
0x37A               Rate?? (Right)
0x37C               Dry??
0x37E               Wet??
0x380..0x4BF        Alist data
0x4C0..0x4FF        ADPCM CodeBook
0x500..0x5BF        <Unknown>
0x5C0..0xF7F        Buffers...
0xF80..0xFFF        <Unknown>
***************************************************/

/* audio commands definition */
static void SPNOOP (uint32_t w1, uint32_t w2)
{
}

static void CLEARBUFF (uint32_t w1, uint32_t w2)
{
   uint16_t dmem  = w1;
   uint16_t count = w2;

   alist_clear(dmem & ~3, (count + 3) & ~3);
}

//FILE *dfile = fopen ("d:\\envmix.txt", "wt");

static void ENVMIXER (uint32_t w1, uint32_t w2)
{
   uint8_t flags = (w1 >> 16);
   uint32_t address = (w2 & 0xffffff);

   alist_envmix_exp(
         flags & A_INIT,
         flags & A_AUX,
         l_alist.out, l_alist.dry_right,
         l_alist.wet_left, l_alist.wet_right,
         l_alist.in, l_alist.count,
         l_alist.dry, l_alist.wet,
         l_alist.vol,
         l_alist.target,
         l_alist.rate,
         address);
}

static void RESAMPLE (uint32_t w1, uint32_t w2)
{
   uint8_t  flags   = (w1 >> 16);
   uint16_t pitch   = w1;
   uint32_t address = (w2 & 0xffffff);

   alist_resample(
         flags & 0x1,
         l_alist.out,
         l_alist.in,
         (l_alist.count + 0xf) & ~0xf,
         pitch << 1,
         address);
}

static void SETVOL (uint32_t w1, uint32_t w2)
{
   uint8_t flags = (w1 >> 16);

   if (flags & A_AUX)
   {
      l_alist.dry = w1;
      l_alist.wet = w2;
   }
   else {
      unsigned lr = (flags & A_LEFT) ? 0 : 1;

      if (flags & A_VOL)
         l_alist.vol[lr] = w1;
      else
      {
         l_alist.target[lr] = w1;
         l_alist.rate[lr] = w2;
      }
   }
}

static void SETLOOP (uint32_t w1, uint32_t w2)
{
    l_alist.loop = (w2 & 0xffffff);// + SEGMENTS[(w2>>24)&0xf];
    //l_alist.target[0]  = (int16_t)(l_alist.loop >> 16);        // m_LeftVol
    //l_alist.rate[0] = (int16_t)(l_alist.loop);    // m_LeftVolTarget
}

static void ADPCM (uint32_t w1, uint32_t w2)
{
   uint8_t flags = (w1 >> 16);
   uint32_t address = (w2 & 0xffffff);

   alist_adpcm(
         flags & 0x1,
         flags & 0x2,
         false, /* unsupported in this ucode */
         l_alist.out,
         l_alist.in,
         (l_alist.count + 0x1f) & ~0x1f,
         l_alist.table,
         l_alist.loop,
         address);
}

static void LOADBUFF (uint32_t w1, uint32_t w2)
{
       uint32_t address = (w2 & 0xffffff);

    if (l_alist.count == 0)
        return;

    alist_load(l_alist.in & ~3, address & ~3, (l_alist.count + 3) & ~3);
}

static void SAVEBUFF (uint32_t w1, uint32_t w2)
{
   uint32_t address = (w2 & 0xffffff);

   if (l_alist.count == 0)
      return;

   alist_save(l_alist.out & ~3, address & ~3, (l_alist.count + 3) & ~3);
}

static void SETBUFF (uint32_t w1, uint32_t w2)
{
   uint8_t flags = (w1 >> 16);

   if (flags & A_AUX)
   {
      l_alist.dry_right = w1;
      l_alist.wet_left = (w2 >> 16);
      l_alist.wet_right = w2;
   }
   else
   {
      l_alist.in = w1;
      l_alist.out = (w2 >> 16);
      l_alist.count = w2;
   }
}

static void DMEMMOVE (uint32_t w1, uint32_t w2)
{
   uint16_t dmemi = w1;
    uint16_t dmemo = (w2 >> 16);
    uint16_t count = w2;

    if (count == 0)
        return;

    alist_move(dmemo, dmemi, (count + 3) & ~3);
}

static void LOADADPCM (uint32_t w1, uint32_t w2)
{
   uint16_t count   = (w1 & 0xffff);
   uint32_t address = (w2 & 0xffffff);

   dram_load_u16((uint16_t*)l_alist.table, address, count >> 1);
}


static void INTERLEAVE (uint32_t w1, uint32_t w2)
{ 
   uint16_t left = (w2 >> 16);
   uint16_t right = w2;

   if (l_alist.count == 0)
      return;

   alist_interleave(l_alist.out, left, right, l_alist.count);
}


static void MIXER (uint32_t w1, uint32_t w2)
{
   int16_t gain = w1;
   uint16_t dmemi = (w2 >> 16);
   uint16_t dmemo = w2;

   if (l_alist.count == 0)
      return;

   alist_mix(dmemo, dmemi, l_alist.count, gain);
}

// TOP Performance Hogs:
//Command: ADPCM    - Calls:  48 - Total Time: 331226 - Avg Time:  6900.54 - Percent: 31.53%
//Command: ENVMIXER - Calls:  48 - Total Time: 408563 - Avg Time:  8511.73 - Percent: 38.90%
//Command: LOADBUFF - Calls:  56 - Total Time:  21551 - Avg Time:   384.84 - Percent:  2.05%
//Command: RESAMPLE - Calls:  48 - Total Time: 225922 - Avg Time:  4706.71 - Percent: 21.51%

//Command: ADPCM    - Calls:  48 - Total Time: 391600 - Avg Time:  8158.33 - Percent: 32.52%
//Command: ENVMIXER - Calls:  48 - Total Time: 444091 - Avg Time:  9251.90 - Percent: 36.88%
//Command: LOADBUFF - Calls:  58 - Total Time:  29945 - Avg Time:   516.29 - Percent:  2.49%
//Command: RESAMPLE - Calls:  48 - Total Time: 276354 - Avg Time:  5757.38 - Percent: 22.95%


static void SEGMENT(uint32_t w1, uint32_t w2)
{
   /* TODO */
}

static void POLEF(uint32_t w1, uint32_t w2)
{
   /* TODO */
}


/* global functions */
void alist_process_audio(void)
{
   static const acmd_callback_t ABI[0x10] = { // TOP Performace Hogs: MIXER, RESAMPLE, ENVMIXER
      SPNOOP,     ADPCM,      CLEARBUFF,  ENVMIXER, 
      LOADBUFF,   RESAMPLE,   SAVEBUFF,   SEGMENT,
      SETBUFF,    SETVOL,     DMEMMOVE,   LOADADPCM,
      MIXER,      INTERLEAVE, POLEF,      SETLOOP
   };
   alist_process(ABI, 0x10);
}

void alist_process_audio_ge(void)
{
    /* TODO: see what differs from alist_process_audio */
    static const acmd_callback_t ABI[0x10] = {
        SPNOOP,         ADPCM ,         CLEARBUFF,      ENVMIXER,
        LOADBUFF,       RESAMPLE,       SAVEBUFF,       SEGMENT,
        SETBUFF,        SETVOL,         DMEMMOVE,       LOADADPCM,
        MIXER,          INTERLEAVE,     POLEF,          SETLOOP
    };
    alist_process(ABI, 0x10);
}

void alist_process_audio_bc(void)
{
   /* TODO: see what differs from alist_process_audio */
   static const acmd_callback_t ABI[0x10] = {
      SPNOOP,         ADPCM ,         CLEARBUFF,      ENVMIXER,
      LOADBUFF,       RESAMPLE,       SAVEBUFF,       SEGMENT,
      SETBUFF,        SETVOL,         DMEMMOVE,       LOADADPCM,
      MIXER,          INTERLEAVE,     POLEF,          SETLOOP
   };
   alist_process(ABI, 0x10);
}

/*  BACKUPS
void MIXER (uint32_t w1, uint32_t w2) { // Fixed a sign issue... 03-14-01
    uint16_t dmemin  = (uint16_t)(w2 >> 0x10);
    uint16_t dmemout = (uint16_t)(w2 & 0xFFFF);
    uint16_t gain    = (uint16_t)(w1 & 0xFFFF);
    uint8_t  flags   = (uint8_t)((w1 >> 16) & 0xff);
    uint64_t temp;

    if (l_alist.count == 0)
        return;

    for (int32_t x=0; x < l_alist.count; x+=2) { // I think I can do this a lot easier
        temp = (int64_t)(*(int16_t *)(BufferSpace+dmemout+x)) * (int64_t)((int16_t)(0x7FFF)*2);

        if (temp & 0x8000)
            temp = (temp^0x8000) + 0x10000;
        else
            temp = (temp^0x8000);

        temp = (temp & 0xFFFFFFFFFFFF);

        temp += ((*(int16_t *)(BufferSpace+dmemin+x) * (int64_t)((int16_t)gain*2))) & 0xFFFFFFFFFFFF;
            
        temp = (int32_t)(temp >> 16);
        if ((int32_t)temp > 32767) 
            temp = 32767;
        if ((int32_t)temp < -32768) 
            temp = -32768;

        *(uint16_t *)(BufferSpace+dmemout+x) = (uint16_t)(temp & 0xFFFF);
    }
}
*/


