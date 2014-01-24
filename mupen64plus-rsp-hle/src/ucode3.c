/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus-rsp-hle - ucode3.cpp                                      *
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

#include <string.h>
#include <stdint.h>

#include "m64p_types.h"
#include "hle.h"
#include "alist_internal.h"

/* alist naudio state */

static struct
{
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

/*
static void SPNOOP (uint32_t w1, uint32_t w2) {
    RSP_DEBUG_MESSAGE(M64MSG_ERROR, "Unknown/Unimplemented Audio Command %i in ABI 3", (int)(w1 >> 24));
}
*/

extern const uint16_t ResampleLUT [0x200];

extern uint8_t BufferSpace[0x10000];

static void SETVOL3 (uint32_t w1, uint32_t w2)
{
   uint8_t flags = (w1 >> 16);

   if (flags & 0x4)
   {
      if (flags & 0x2)
      {
         l_alist.vol[0] = w1;
         l_alist.dry = (w2 >> 16);
         l_alist.wet = w2;
      }
      else
      {
         l_alist.target[1] = w1;
         l_alist.rate[1] = w2;
      }
   }
   else
   {
      l_alist.target[0] = w1;
      l_alist.rate[0] = w2;
   }
}

static void ENVMIXER3 (uint32_t w1, uint32_t w2)
{
   uint8_t flags = (w1 >> 16);
   uint32_t address = (w2 & 0xffffff);

   l_alist.vol[1] = w1;

   alist_envmix_lin(
         flags & 0x1,
         0x9d0,
         0xb40,
         0xcb0,
         0xe20,
         0x4f0,
         0x170,
         l_alist.dry,
         l_alist.wet,
         l_alist.vol,
         l_alist.target,
         l_alist.rate,
         address);
}

static void CLEARBUFF3 (uint32_t w1, uint32_t w2)
{
   uint16_t dmem  = w1 + 0x4f0;
   uint16_t count = w2;
   alist_clear(dmem, count);
}

static void MIXER3 (uint32_t w1, uint32_t w2)
{
   int16_t  gain  = w1;
   uint16_t dmemi = (w2 >> 16) + 0x4f0;
   uint16_t dmemo = w2 + 0x4f0;

   alist_mix(dmemo, dmemi, 0x170, gain);
}

static void LOADBUFF3 (uint32_t w1, uint32_t w2)
{
   uint16_t count = (w1 >> 12) & 0xfff;
   uint16_t dmem = (w1 & 0xfff) + 0x4f0;
   uint32_t address = (w2 & 0xffffff);

   alist_load(dmem & ~3, address & ~3, (count + 3) & ~3);
}

static void SAVEBUFF3 (uint32_t w1, uint32_t w2)
{
   uint16_t count = (w1 >> 12) & 0xfff;
   uint16_t dmem = (w1 & 0xfff) + 0x4f0;
   uint32_t address = (w2 & 0xffffff);

   alist_save(dmem & ~3, address & ~3, (count + 3) & ~3);
}

static void LOADADPCM3 (uint32_t w1, uint32_t w2)
{
   uint16_t count = (w1 & 0xffff);
   uint32_t address = (w2 & 0xffffff);

   dram_load_u16((uint16_t*)l_alist.table, address, count >> 1);
}

static void DMEMMOVE3 (uint32_t w1, uint32_t w2)
{
   uint16_t dmemi = w1 + 0x4f0;
   uint16_t dmemo = (w2 >> 16) + 0x4f0;
   uint16_t count = w2;

   alist_move(dmemo, dmemi, (count + 3) & ~3);
}

static void SETLOOP3 (uint32_t w1, uint32_t w2) {
    l_alist.loop = (w2 & 0xffffff);
}

static void ADPCM3 (uint32_t w1, uint32_t w2)
{
   uint32_t address = (w1 & 0xffffff);
   uint8_t flags = (w2 >> 28);
   uint16_t count = (w2 >> 16) & 0xfff;
   uint16_t dmemi = ((w2 >> 12) & 0xf) + 0x4f0;
   uint16_t dmemo = (w2 & 0xfff) + 0x4f0;

   alist_adpcm(
         flags & 0x1,
         flags & 0x2,
         false, /* unsuported by this ucode */
         dmemo,
         dmemi,
         (count + 0x1f) & ~0x1f,
         l_alist.table,
         l_alist.loop,
         address);
}

static void RESAMPLE3 (uint32_t w1, uint32_t w2)
{
   uint32_t address = (w1 & 0xffffff);
   uint8_t flags = (w2 >> 30);
   uint16_t pitch = (w2 >> 14);
   uint16_t dmemi = ((w2 >> 2) & 0xfff) + 0x4f0;
   uint16_t dmemo = (w2 & 0x3) ? 0x660 : 0x4f0;

   alist_resample(
         flags & 0x1,
         dmemo,
         dmemi,
         0x170,
         pitch << 1,
         address);
}

static void INTERLEAVE3 (uint32_t w1, uint32_t w2)
{
   alist_interleave(0x4f0, 0x9d0, 0xb40, 0x170);
}

//static void UNKNOWN (uint32_t w1, uint32_t w2);
/*
typedef struct {
    uint8_t sync;

    uint8_t error_protection   : 1;    //  0=yes, 1=no
    uint8_t lay                : 2;    // 4-lay = layerI, II or III
    uint8_t version            : 1;    // 3=mpeg 1.0, 2=mpeg 2.5 0=mpeg 2.0
    uint8_t sync2              : 4;

    uint8_t extension          : 1;    // Unknown
    uint8_t padding            : 1;    // padding
    uint8_t sampling_freq      : 2;    // see table below
    uint8_t bitrate_index      : 4;    //     see table below

    uint8_t emphasis           : 2;    //see table below
    uint8_t original           : 1;    // 0=no 1=yes
    uint8_t copyright          : 1;    // 0=no 1=yes
    uint8_t mode_ext           : 2;    // used with "joint stereo" mode
    uint8_t mode               : 2;    // Channel Mode
} mp3struct;

mp3struct mp3;
FILE *mp3dat;
*/

static void WHATISTHIS (uint32_t w1, uint32_t w2) {
}

//static FILE *fp = fopen ("d:\\mp3info.txt", "wt");
static void MP3ADDY (uint32_t w1, uint32_t w2)
{
}

void rsp_run(void);
void mp3setup (uint32_t w1, uint32_t w2, uint32_t t8);

extern uint32_t base, dmembase;
extern int8_t *pDMEM;
void MP3 (uint32_t w1, uint32_t w2);
/*
 {
//  return;
    // Setup Registers...
    mp3setup (w1, w2, 0xFA0);
    
    // Setup Memory Locations...
    //uint32_t base = ((uint32_t*)dmem)[0xFD0/4]; // Should be 000291A0
    memcpy (BufferSpace, dmembase+rspInfo.RDRAM, 0x10);
    ((uint32_t*)BufferSpace)[0x0] = base;
    ((uint32_t*)BufferSpace)[0x008/4] += base;
    ((uint32_t*)BufferSpace)[0xFFC/4] = l_alist.loop;
    ((uint32_t*)BufferSpace)[0xFF8/4] = dmembase;

    memcpy (imem+0x238, rspInfo.RDRAM+((uint32_t*)BufferSpace)[0x008/4], 0x9C0);
    ((uint32_t*)BufferSpace)[0xFF4/4] = setaddr;
    pDMEM = (int8_t*)BufferSpace;
    rsp_run (void);
    dmembase = ((uint32_t*)BufferSpace)[0xFF8/4];
    l_alist.loop  = ((uint32_t*)BufferSpace)[0xFFC/4];
//0x1A98  SW       S1, 0x0FF4 (R0)
//0x1A9C  SW       S0, 0x0FF8 (R0)
//0x1AA0  SW       T7, 0x0FFC (R0)
//0x1AA4  SW       T3, 0x0FF0 (R0)
    //fprintf (fp, "mp3: w1: %08X, w2: %08X\n", w1, w2);
}*/
/*
FFT = Fast Fourier Transform
DCT = Discrete Cosine Transform
MPEG-1 Layer 3 retains Layer 2's 1152-sample window, as well as the FFT polyphase filter for
backward compatibility, but adds a modified DCT filter. DCT's advantages over DFTs (discrete
Fourier transforms) include half as many multiply-accumulate operations and half the 
generated coefficients because the sinusoidal portion of the calculation is absent, and DCT 
generally involves simpler math. The finite lengths of a conventional DCTs' bandpass impulse
responses, however, may result in block-boundary effects. MDCTs overlap the analysis blocks 
and lowpass-filter the decoded audio to remove aliases, eliminating these effects. MDCTs also 
have a higher transform coding gain than the standard DCT, and their basic functions 
correspond to better bandpass response. 

MPEG-1 Layer 3's DCT sub-bands are unequally sized, and correspond to the human auditory 
system's critical bands. In Layer 3 decoders must support both constant- and variable-bit-rate 
bit streams. (However, many Layer 1 and 2 decoders also handle variable bit rates). Finally, 
Layer 3 encoders Huffman-code the quantized coefficients before archiving or transmission for 
additional lossless compression. Bit streams range from 32 to 320 kbps, and 128-kbps rates 
achieve near-CD quality, an important specification to enable dual-channel ISDN 
(integrated-services-digital-network) to be the future high-bandwidth pipe to the home. 

*/
static void DISABLE (uint32_t w1, uint32_t w2) {
    //MessageBox (NULL, "Help", "ABI 3 Command 0", MB_OK);
    //ChangeABI (5);
}


static const acmd_callback_t ABI3[0x10] = {
    DISABLE , ADPCM3 , CLEARBUFF3,  ENVMIXER3  , LOADBUFF3, RESAMPLE3  , SAVEBUFF3, MP3,
    MP3ADDY, SETVOL3, DMEMMOVE3 , LOADADPCM3 , MIXER3   , INTERLEAVE3, WHATISTHIS   , SETLOOP3
};


void alist_process_naudio(void)
{
    alist_process(ABI3, 0x10);
}

void alist_process_naudio_bk(void)
{
    alist_process(ABI3, 0x10);
}

void alist_process_naudio_dk(void)
{
    alist_process(ABI3, 0x10);
}

void alist_process_naudio_mp3(void)
{
    alist_process(ABI3, 0x10);
}

void alist_process_naudio_cbfd(void)
{
    alist_process(ABI3, 0x10);
}
