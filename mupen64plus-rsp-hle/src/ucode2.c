/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus-rsp-hle - ucode2.cpp                                      *
 *   Mupen64Plus homepage: http://code.google.com/p/mupen64plus/           *
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
#include <stdbool.h>
#include <stdint.h>

#include "hle.h"
#include "alist_internal.h"

/* alist state */

static struct
{
    /* main buffers */
    uint16_t in;
    uint16_t out;
    uint16_t count;
    /* ADPCM loop point address */
    uint32_t loop;
    /* storage for ADPCM table and polef coefficients */
    uint16_t table[16 * 8];
} l_alist;

static void SPNOOP (uint32_t w1, uint32_t w2) {
    RSP_DEBUG_MESSAGE(M64MSG_ERROR, "Unknown/Unimplemented Audio Command %i in ABI 2", (int32_t)(w1 >> 24));
}

extern const uint16_t ResampleLUT [0x200];

bool isMKABI = false;
bool isZeldaABI = false;

void init_ucode2()
{
   isMKABI = isZeldaABI = false;
}

static void LOADADPCM2 (uint32_t w1, uint32_t w2)
{ // Loads an ADPCM table - Works 100% Now 03-13-01
   uint32_t x;
    uint32_t v0;
    v0 = (w2 & 0xffffff);// + SEGMENTS[(w2>>24)&0xf];
    uint16_t *table = (uint16_t *)(rspInfo.RDRAM+v0); // Zelda2 Specific...

    for (x = 0; x < ((w1&0xffff)>>0x4); x++) {
        l_alist.table[(0x0+(x<<3))^S] = table[0];
        l_alist.table[(0x1+(x<<3))^S] = table[1];

        l_alist.table[(0x2+(x<<3))^S] = table[2];
        l_alist.table[(0x3+(x<<3))^S] = table[3];

        l_alist.table[(0x4+(x<<3))^S] = table[4];
        l_alist.table[(0x5+(x<<3))^S] = table[5];

        l_alist.table[(0x6+(x<<3))^S] = table[6];
        l_alist.table[(0x7+(x<<3))^S] = table[7];
        table += 8;
    }
}

static void SETLOOP2 (uint32_t w1, uint32_t w2) {
    l_alist.loop = w2 & 0xffffff; // No segment?
}

static void SETBUFF2 (uint32_t w1, uint32_t w2) {
    l_alist.in   = (uint16_t)(w1);            // 0x00
    l_alist.out  = (uint16_t)((w2 >> 0x10)); // 0x02
    l_alist.count      = (uint16_t)(w2);            // 0x04
}

static void ADPCM2 (uint32_t w1, uint32_t w2) { // Verified to be 100% Accurate...
    uint8_t Flags=(uint8_t)(w1>>16)&0xff;
    //uint16_t Gain=(uint16_t)(w1&0xffff);
    uint32_t Address=(w2 & 0xffffff);// + SEGMENTS[(w2>>24)&0xf];
    uint16_t inPtr=0;
    //int16_t *out=(int16_t *)(testbuff+(l_alist.out>>2));
    int16_t *out=(int16_t*)(BufferSpace+l_alist.out);
    //uint8_t *in=(uint8_t*)(BufferSpace+l_alist.in);
    int16_t count=(int16_t)l_alist.count;
    uint8_t icode;
    uint8_t code;
    int vscale;
    uint16_t index;
    uint16_t j;
    int32_t a[8];
    int16_t *book1,*book2;

    uint8_t srange;
    uint8_t mask1;
    uint8_t mask2;
    uint8_t shifter;

    memset(out,0,32);

    if (Flags & 0x4) { // Tricky lil Zelda MM and ABI2!!! hahaha I know your secrets! :DDD
        srange = 0xE;
        mask1 = 0xC0;
        mask2 = 0x30;
        shifter = 10;
    } else {
        srange = 0xC;
        mask1 = 0xf0;
        mask2 = 0x0f;
        shifter = 12;
    }

    if(!(Flags&0x1))
    {
        if(Flags&0x2)
        {/*
            for(int i=0;i<16;i++)
            {
                out[i]=*(int16_t*)&rspInfo.RDRAM[(l_alist.loop+i*2)^2];
            }*/
            memcpy(out,&rspInfo.RDRAM[l_alist.loop],32);
        }
        else
        {/*
            for(int i=0;i<16;i++)
            {
                out[i]=*(int16_t*)&rspInfo.RDRAM[(Address+i*2)^2];
            }*/
            memcpy(out,&rspInfo.RDRAM[Address],32);
        }
    }

    int32_t l1=out[14^S];
    int32_t l2=out[15^S];
    int32_t inp1[8];
    int32_t inp2[8];
    out+=16;
    while(count>0) {
        code=BufferSpace[(l_alist.in+inPtr)^S8];
        index=code&0xf;
        index<<=4;
        book1=(int16_t*)&l_alist.table[index];
        book2=book1+8;
        code>>=4;
        vscale=(0x8000>>((srange-code)-1));
        
        inPtr++;
        j=0;

        while(j<8) {
            icode=BufferSpace[(l_alist.in+inPtr)^S8];
            inPtr++;

            inp1[j]=(int16_t)((icode&mask1) << 8);          // this will in effect be signed
            if(code<srange) inp1[j]=((int32_t)((int32_t)inp1[j]*(int32_t)vscale)>>16);
            //else int catchme=1;
            j++;

            inp1[j]=(int16_t)((icode&mask2)<<shifter);
            if(code<srange) inp1[j]=((int32_t)((int32_t)inp1[j]*(int32_t)vscale)>>16);
            //else int catchme=1;
            j++;

            if (Flags & 4) {
                inp1[j]=(int16_t)((icode&0xC) << 12);           // this will in effect be signed
                if(code < 0xE) inp1[j]=((int32_t)((int32_t)inp1[j]*(int32_t)vscale)>>16);
                //else int catchme=1;
                j++;

                inp1[j]=(int16_t)((icode&0x3) << 14);
                if(code < 0xE) inp1[j]=((int32_t)((int32_t)inp1[j]*(int32_t)vscale)>>16);
                //else int catchme=1;
                j++;
            } // end flags
        } // end while



        j=0;
        while(j<8) {
            icode=BufferSpace[(l_alist.in+inPtr)^S8];
            inPtr++;

            inp2[j]=(int16_t)((icode&mask1) << 8);
            if(code<srange) inp2[j]=((int32_t)((int32_t)inp2[j]*(int32_t)vscale)>>16);
            //else int catchme=1;
            j++;

            inp2[j]=(int16_t)((icode&mask2)<<shifter);
            if(code<srange) inp2[j]=((int32_t)((int32_t)inp2[j]*(int32_t)vscale)>>16);
            //else int catchme=1;
            j++;

            if (Flags & 4) {
                inp2[j]=(int16_t)((icode&0xC) << 12);
                if(code < 0xE) inp2[j]=((int32_t)((int32_t)inp2[j]*(int32_t)vscale)>>16);
                //else int catchme=1;
                j++;

                inp2[j]=(int16_t)((icode&0x3) << 14);
                if(code < 0xE) inp2[j]=((int32_t)((int32_t)inp2[j]*(int32_t)vscale)>>16);
                //else int catchme=1;
                j++;
            } // end flags
        }

        a[0]= (int32_t)book1[0] * (int32_t)l1;
        a[0]+=(int32_t)book2[0] * (int32_t)l2;
        a[0]+=(int32_t)inp1[0]  * (int32_t)2048;

        a[1] =(int32_t)book1[1]*(int32_t)l1;
        a[1]+=(int32_t)book2[1]*(int32_t)l2;
        a[1]+=(int32_t)book2[0]*inp1[0];
        a[1]+=(int32_t)inp1[1]*(int32_t)2048;

        a[2] =(int32_t)book1[2]*(int32_t)l1;
        a[2]+=(int32_t)book2[2]*(int32_t)l2;
        a[2]+=(int32_t)book2[1]*inp1[0];
        a[2]+=(int32_t)book2[0]*inp1[1];
        a[2]+=(int32_t)inp1[2]*(int32_t)2048;

        a[3] =(int32_t)book1[3]*(int32_t)l1;
        a[3]+=(int32_t)book2[3]*(int32_t)l2;
        a[3]+=(int32_t)book2[2]*inp1[0];
        a[3]+=(int32_t)book2[1]*inp1[1];
        a[3]+=(int32_t)book2[0]*inp1[2];
        a[3]+=(int32_t)inp1[3]*(int32_t)2048;

        a[4] =(int32_t)book1[4]*(int32_t)l1;
        a[4]+=(int32_t)book2[4]*(int32_t)l2;
        a[4]+=(int32_t)book2[3]*inp1[0];
        a[4]+=(int32_t)book2[2]*inp1[1];
        a[4]+=(int32_t)book2[1]*inp1[2];
        a[4]+=(int32_t)book2[0]*inp1[3];
        a[4]+=(int32_t)inp1[4]*(int32_t)2048;

        a[5] =(int32_t)book1[5]*(int32_t)l1;
        a[5]+=(int32_t)book2[5]*(int32_t)l2;
        a[5]+=(int32_t)book2[4]*inp1[0];
        a[5]+=(int32_t)book2[3]*inp1[1];
        a[5]+=(int32_t)book2[2]*inp1[2];
        a[5]+=(int32_t)book2[1]*inp1[3];
        a[5]+=(int32_t)book2[0]*inp1[4];
        a[5]+=(int32_t)inp1[5]*(int32_t)2048;

        a[6] =(int32_t)book1[6]*(int32_t)l1;
        a[6]+=(int32_t)book2[6]*(int32_t)l2;
        a[6]+=(int32_t)book2[5]*inp1[0];
        a[6]+=(int32_t)book2[4]*inp1[1];
        a[6]+=(int32_t)book2[3]*inp1[2];
        a[6]+=(int32_t)book2[2]*inp1[3];
        a[6]+=(int32_t)book2[1]*inp1[4];
        a[6]+=(int32_t)book2[0]*inp1[5];
        a[6]+=(int32_t)inp1[6]*(int32_t)2048;

        a[7] =(int32_t)book1[7]*(int32_t)l1;
        a[7]+=(int32_t)book2[7]*(int32_t)l2;
        a[7]+=(int32_t)book2[6]*inp1[0];
        a[7]+=(int32_t)book2[5]*inp1[1];
        a[7]+=(int32_t)book2[4]*inp1[2];
        a[7]+=(int32_t)book2[3]*inp1[3];
        a[7]+=(int32_t)book2[2]*inp1[4];
        a[7]+=(int32_t)book2[1]*inp1[5];
        a[7]+=(int32_t)book2[0]*inp1[6];
        a[7]+=(int32_t)inp1[7]*(int32_t)2048;

        for(j=0;j<8;j++)
        {
            a[j^S]>>=11;
            BLARGG_CLAMP16(a[j^S]);
            *(out++)=a[j^S];
        }
        l1=a[6];
        l2=a[7];

        a[0]= (int32_t)book1[0]*(int32_t)l1;
        a[0]+=(int32_t)book2[0]*(int32_t)l2;
        a[0]+=(int32_t)inp2[0]*(int32_t)2048;

        a[1] =(int32_t)book1[1]*(int32_t)l1;
        a[1]+=(int32_t)book2[1]*(int32_t)l2;
        a[1]+=(int32_t)book2[0]*inp2[0];
        a[1]+=(int32_t)inp2[1]*(int32_t)2048;

        a[2] =(int32_t)book1[2]*(int32_t)l1;
        a[2]+=(int32_t)book2[2]*(int32_t)l2;
        a[2]+=(int32_t)book2[1]*inp2[0];
        a[2]+=(int32_t)book2[0]*inp2[1];
        a[2]+=(int32_t)inp2[2]*(int32_t)2048;

        a[3] =(int32_t)book1[3]*(int32_t)l1;
        a[3]+=(int32_t)book2[3]*(int32_t)l2;
        a[3]+=(int32_t)book2[2]*inp2[0];
        a[3]+=(int32_t)book2[1]*inp2[1];
        a[3]+=(int32_t)book2[0]*inp2[2];
        a[3]+=(int32_t)inp2[3]*(int32_t)2048;

        a[4] =(int32_t)book1[4]*(int32_t)l1;
        a[4]+=(int32_t)book2[4]*(int32_t)l2;
        a[4]+=(int32_t)book2[3]*inp2[0];
        a[4]+=(int32_t)book2[2]*inp2[1];
        a[4]+=(int32_t)book2[1]*inp2[2];
        a[4]+=(int32_t)book2[0]*inp2[3];
        a[4]+=(int32_t)inp2[4]*(int32_t)2048;

        a[5] =(int32_t)book1[5]*(int32_t)l1;
        a[5]+=(int32_t)book2[5]*(int32_t)l2;
        a[5]+=(int32_t)book2[4]*inp2[0];
        a[5]+=(int32_t)book2[3]*inp2[1];
        a[5]+=(int32_t)book2[2]*inp2[2];
        a[5]+=(int32_t)book2[1]*inp2[3];
        a[5]+=(int32_t)book2[0]*inp2[4];
        a[5]+=(int32_t)inp2[5]*(int32_t)2048;

        a[6] =(int32_t)book1[6]*(int32_t)l1;
        a[6]+=(int32_t)book2[6]*(int32_t)l2;
        a[6]+=(int32_t)book2[5]*inp2[0];
        a[6]+=(int32_t)book2[4]*inp2[1];
        a[6]+=(int32_t)book2[3]*inp2[2];
        a[6]+=(int32_t)book2[2]*inp2[3];
        a[6]+=(int32_t)book2[1]*inp2[4];
        a[6]+=(int32_t)book2[0]*inp2[5];
        a[6]+=(int32_t)inp2[6]*(int32_t)2048;

        a[7] =(int32_t)book1[7]*(int32_t)l1;
        a[7]+=(int32_t)book2[7]*(int32_t)l2;
        a[7]+=(int32_t)book2[6]*inp2[0];
        a[7]+=(int32_t)book2[5]*inp2[1];
        a[7]+=(int32_t)book2[4]*inp2[2];
        a[7]+=(int32_t)book2[3]*inp2[3];
        a[7]+=(int32_t)book2[2]*inp2[4];
        a[7]+=(int32_t)book2[1]*inp2[5];
        a[7]+=(int32_t)book2[0]*inp2[6];
        a[7]+=(int32_t)inp2[7]*(int32_t)2048;

        for(j=0;j<8;j++)
        {
            a[j^S]>>=11;
            BLARGG_CLAMP16(a[j^S]);
            *(out++)=a[j^S];
        }
        l1=a[6];
        l2=a[7];

        count-=32;
    }
    out-=16;
    memcpy(&rspInfo.RDRAM[Address],out,32);
}

static void CLEARBUFF2 (uint32_t w1, uint32_t w2)
{
   uint16_t dmem  = w1;
   uint16_t count = w2;

   if (count == 0)
      return;
   
   alist_clear(dmem, count);
}

static void LOADBUFF2 (uint32_t w1, uint32_t w2)
{
   uint16_t count = (w1 >> 12) & 0xfff;
   uint16_t dmem = (w1 & 0xfff);
   uint32_t address = (w2 & 0xffffff);

   alist_load(dmem & ~3, address & ~3, (count + 3) & ~3);
}

static void SAVEBUFF2 (uint32_t w1, uint32_t w2)
{
   uint16_t count = (w1 >> 12) & 0xfff;
   uint16_t dmem = (w1 & 0xfff);
   uint32_t address = (w2 & 0xffffff);

   alist_save(dmem & ~3, address & ~3, (count + 3) & ~3);
}


static void MIXER2 (uint32_t w1, uint32_t w2)
{
   uint16_t count = (w1 >> 12) & 0xff0;
   int16_t  gain  = w1;
   uint16_t dmemi = (w2 >> 16);
   uint16_t dmemo = w2;

   alist_mix(dmemo, dmemi, count, gain);
}


static void RESAMPLE2 (uint32_t w1, uint32_t w2)
{
   uint8_t flags = (w1 >> 16);
   uint16_t pitch = w1;
   uint32_t address = (w2 & 0xffffff);

   alist_resample(
         flags & 0x1,
         l_alist.out,
         l_alist.in,
         (l_alist.count + 0xf) & ~0xf,
         pitch << 1,
         address);
}

static void DMEMMOVE2 (uint32_t w1, uint32_t w2)
{
   uint16_t dmemi = w1;
   uint16_t dmemo = (w2 >> 16);
   uint16_t count = w2;

   if (count == 0)
      return;

   alist_move(dmemo, dmemi, (count + 3) & ~3);
}

static uint32_t t3, s5, s6;
static uint16_t env[8];

static void ENVSETUP1 (uint32_t w1, uint32_t w2) {
    uint32_t tmp;

    //fprintf (dfile, "ENVSETUP1: w1 = %08X, w2 = %08X\n", w1, w2);
    t3 = w1 & 0xFFFF;
    tmp = (w1 >> 0x8) & 0xFF00;
    env[4] = (uint16_t)tmp;
    tmp += t3;
    env[5] = (uint16_t)tmp;
    s5 = w2 >> 0x10;
    s6 = w2 & 0xFFFF;
    //fprintf (dfile, " t3 = %X / s5 = %X / s6 = %X / env[4] = %X / env[5] = %X\n", t3, s5, s6, env[4], env[5]);
}

static void ENVSETUP2 (uint32_t w1, uint32_t w2) {
    uint32_t tmp;

    //fprintf (dfile, "ENVSETUP2: w1 = %08X, w2 = %08X\n", w1, w2);
    tmp = (w2 >> 0x10);
    env[0] = (uint16_t)tmp;
    tmp += s5;
    env[1] = (uint16_t)tmp;
    tmp = w2 & 0xffff;
    env[2] = (uint16_t)tmp;
    tmp += s6;
    env[3] = (uint16_t)tmp;
    //fprintf (dfile, " env[0] = %X / env[1] = %X / env[2] = %X / env[3] = %X\n", env[0], env[1], env[2], env[3]);
}

static void ENVMIXER2 (uint32_t w1, uint32_t w2) {
    //fprintf (dfile, "ENVMIXER: w1 = %08X, w2 = %08X\n", w1, w2);

    int16_t *bufft6, *bufft7, *buffs0, *buffs1;
    int16_t *buffs3;
    int32_t count;
    uint32_t adder;

    int16_t vec9, vec10;

    int16_t v2[8];

    buffs3 = (int16_t *)(BufferSpace + ((w1 >> 0x0c)&0x0ff0));
    bufft6 = (int16_t *)(BufferSpace + ((w2 >> 0x14)&0x0ff0));
    bufft7 = (int16_t *)(BufferSpace + ((w2 >> 0x0c)&0x0ff0));
    buffs0 = (int16_t *)(BufferSpace + ((w2 >> 0x04)&0x0ff0));
    buffs1 = (int16_t *)(BufferSpace + ((w2 << 0x04)&0x0ff0));


    v2[0] = 0 - (int16_t)((w1 & 0x2) >> 1);
    v2[1] = 0 - (int16_t)((w1 & 0x1));
    v2[2] = 0 - (int16_t)((w1 & 0x8) >> 1);
    v2[3] = 0 - (int16_t)((w1 & 0x4) >> 1);

    count = (w1 >> 8) & 0xff;

    if (!isMKABI) {
        s5 *= 2; s6 *= 2; t3 *= 2;
        adder = 0x10;
    } else {
        w1 = 0;
        adder = 0x8;
        t3 = 0;
    }


    while (count > 0) {
        int temp, x;
        for (x=0; x < 0x8; x++) {
            vec9  = (int16_t)(((int32_t)buffs3[x^S] * (uint32_t)env[0]) >> 0x10) ^ v2[0];
            vec10 = (int16_t)(((int32_t)buffs3[x^S] * (uint32_t)env[2]) >> 0x10) ^ v2[1];
            temp = bufft6[x^S] + vec9;
            BLARGG_CLAMP16(temp);
            bufft6[x^S] = temp;
            temp = bufft7[x^S] + vec10;
            BLARGG_CLAMP16(temp);
            bufft7[x^S] = temp;
            vec9  = (int16_t)(((int32_t)vec9  * (uint32_t)env[4]) >> 0x10) ^ v2[2];
            vec10 = (int16_t)(((int32_t)vec10 * (uint32_t)env[4]) >> 0x10) ^ v2[3];
            if (w1 & 0x10) {
                temp = buffs0[x^S] + vec10;
                BLARGG_CLAMP16(temp);
                buffs0[x^S] = temp;
                temp = buffs1[x^S] + vec9;
                BLARGG_CLAMP16(temp);
                buffs1[x^S] = temp;
            } else {
                temp = buffs0[x^S] + vec9;
                BLARGG_CLAMP16(temp);
                buffs0[x^S] = temp;
                temp = buffs1[x^S] + vec10;
                BLARGG_CLAMP16(temp);
                buffs1[x^S] = temp;
            }
        }

        if (!isMKABI)
        for (x=0x8; x < 0x10; x++) {
            vec9  = (int16_t)(((int32_t)buffs3[x^S] * (uint32_t)env[1]) >> 0x10) ^ v2[0];
            vec10 = (int16_t)(((int32_t)buffs3[x^S] * (uint32_t)env[3]) >> 0x10) ^ v2[1];
            temp = bufft6[x^S] + vec9;
            BLARGG_CLAMP16(temp);
            bufft6[x^S] = temp;
           temp = bufft7[x^S] + vec10;
            BLARGG_CLAMP16(temp);
            bufft7[x^S] = temp;
            vec9  = (int16_t)(((int32_t)vec9  * (uint32_t)env[5]) >> 0x10) ^ v2[2];
            vec10 = (int16_t)(((int32_t)vec10 * (uint32_t)env[5]) >> 0x10) ^ v2[3];
            if (w1 & 0x10) {
                temp = buffs0[x^S] + vec10;
                BLARGG_CLAMP16(temp);
                buffs0[x^S] = temp;
                temp = buffs1[x^S] + vec9;
                BLARGG_CLAMP16(temp);
                buffs1[x^S] = temp;
            } else {
                temp = buffs0[x^S] + vec9;
                BLARGG_CLAMP16(temp);
                buffs0[x^S] = temp;
                temp = buffs1[x^S] + vec10;
                BLARGG_CLAMP16(temp);
                buffs1[x^S] = temp;
            }
        }
        bufft6 += adder; bufft7 += adder;
        buffs0 += adder; buffs1 += adder;
        buffs3 += adder; count  -= adder;
        env[0] += (uint16_t)s5; env[1] += (uint16_t)s5;
        env[2] += (uint16_t)s6; env[3] += (uint16_t)s6;
        env[4] += (uint16_t)t3; env[5] += (uint16_t)t3;
    }
}

static void DUPLICATE2(uint32_t w1, uint32_t w2) {
    uint16_t Count = (w1 >> 16) & 0xff;
    uint16_t In  = w1&0xffff;
    uint16_t Out = (w2>>16);

    uint16_t buff[64];
    
    memcpy(buff,BufferSpace+In,128);

    while(Count) {
        memcpy(BufferSpace+Out,buff,128);
        Out+=128;
        Count--;
    }
}
/*
static void INTERL2 (uint32_t w1, uint32_t w2) { // Make your own...
    short Count = w1 & 0xffff;
    unsigned short  Out   = w2 & 0xffff;
    unsigned short In     = (w2 >> 16);

    short *src,*dst,tmp;
    src=(short *)&BufferSpace[In];
    dst=(short *)&BufferSpace[Out];
    while(Count)
    {
        *(dst++)=*(src++);
        src++;
        *(dst++)=*(src++);
        src++;
        *(dst++)=*(src++);
        src++;
        *(dst++)=*(src++);
        src++;
        *(dst++)=*(src++);
        src++;
        *(dst++)=*(src++);
        src++;
        *(dst++)=*(src++);
        src++;
        *(dst++)=*(src++);
        src++;
        Count-=8;
    }
}
*/

static void INTERL2 (uint32_t w1, uint32_t w2) {
    int16_t Count = w1 & 0xffff;
    uint16_t  Out   = w2 & 0xffff;
    uint16_t In     = (w2 >> 16);

    uint8_t *src,*dst/*,tmp*/;
    src=(uint8_t*)(BufferSpace);//[In];
    dst=(uint8_t*)(BufferSpace);//[Out];
    while(Count) {
        *(int16_t*)(dst+(Out^S8)) = *(int16_t*)(src+(In^S8));
        Out += 2;
        In  += 4;
        Count--;
    }
}

static void INTERLEAVE2 (uint32_t w1, uint32_t w2)
{
   uint16_t dmemo;
   uint16_t count = ((w1 >> 12) & 0xff0);
   uint16_t left = (w2 >> 16);
   uint16_t right = w2;

   /* FIXME: needs ABI splitting */
   if (count == 0) {
      count = l_alist.count;
      dmemo = l_alist.out;
   }
   else
      dmemo = w1;

   alist_interleave(dmemo, left, right, count);
}

static void ADDMIXER (uint32_t w1, uint32_t w2)
{
   int cntr;
    int16_t Count   = (w1 >> 12) & 0x00ff0;
    uint16_t InBuffer  = (w2 >> 16);
    uint16_t OutBuffer = w2 & 0xffff;

    int16_t *inp, *outp;
    int32_t temp;
    inp  = (int16_t *)(BufferSpace + InBuffer);
    outp = (int16_t *)(BufferSpace + OutBuffer);
    for (cntr = 0; cntr < Count; cntr+=2) {
        temp = *outp + *inp;
        BLARGG_CLAMP16(temp);
        *(outp++) = temp;
        inp++;
    }
}

static void HILOGAIN (uint32_t w1, uint32_t w2) {
    uint16_t cnt = w1 & 0xffff;
    uint16_t out = (w2 >> 16) & 0xffff;
    int16_t hi  = (int16_t)((w1 >> 4) & 0xf000);
    uint16_t lo  = (w1 >> 20) & 0xf;
    int16_t *src;

    src = (int16_t *)(BufferSpace+out);
    int32_t tmp, val;

    while(cnt) {
        val = (int32_t)*src;
        //tmp = ((val * (int32_t)hi) + ((u64)(val * lo) << 16) >> 16);
        tmp = ((val * (int32_t)hi) >> 16) + (uint32_t)(val * lo);
        BLARGG_CLAMP16(tmp);
        *src = tmp;
        src++;
        cnt -= 2;
    }
}

static void FILTER2 (uint32_t w1, uint32_t w2)
{
   int16_t outbuff[0x3c0], *outp, *inp1, *inp2;
   static int16_t *lutt6;
   static int16_t *lutt5;
   int32_t x, a;
   int32_t out1[8];
   uint32_t inPtr;
   uint8_t *save, t4;
   static int cnt = 0;

   save = (rspInfo.RDRAM+(w2&0xFFFFFF));
   t4 = (uint8_t)((w1 >> 0x10) & 0xFF);

   if (t4 > 1)
   { /* Then set the cnt variable */
      cnt = (w1 & 0xFFFF);
      lutt6 = (int16_t *)save;
      return;
   }

   if (t4 == 0)
      lutt5 = (int16_t*)(save+0x10);

   lutt5 = (int16_t*)(save+0x10);

   //          lutt5 = (int16_t*)(dmem + 0xFC0);
   //          lutt6 = (int16_t*)(dmem + 0xFE0);
   for (x = 0; x < 8; x++)
   {
      a = (lutt5[x] + lutt6[x]) >> 1;
      lutt5[x] = lutt6[x] = (int16_t)a;
   }

   inPtr = (uint32_t)(w1&0xffff);
   inp1 = (int16_t *)(save);
   outp = outbuff;
   inp2 = (int16_t*)(BufferSpace+inPtr);

   for (x = 0; x < cnt; x+=0x10)
   {
      out1[1] =  inp1[0]*lutt6[6];
      out1[1] += inp1[3]*lutt6[7];
      out1[1] += inp1[2]*lutt6[4];
      out1[1] += inp1[5]*lutt6[5];
      out1[1] += inp1[4]*lutt6[2];
      out1[1] += inp1[7]*lutt6[3];
      out1[1] += inp1[6]*lutt6[0];
      out1[1] += inp2[1]*lutt6[1]; /* 1 */

      out1[0] =  inp1[3]*lutt6[6];
      out1[0] += inp1[2]*lutt6[7];
      out1[0] += inp1[5]*lutt6[4];
      out1[0] += inp1[4]*lutt6[5];
      out1[0] += inp1[7]*lutt6[2];
      out1[0] += inp1[6]*lutt6[3];
      out1[0] += inp2[1]*lutt6[0];
      out1[0] += inp2[0]*lutt6[1];

      out1[3] =  inp1[2]*lutt6[6];
      out1[3] += inp1[5]*lutt6[7];
      out1[3] += inp1[4]*lutt6[4];
      out1[3] += inp1[7]*lutt6[5];
      out1[3] += inp1[6]*lutt6[2];
      out1[3] += inp2[1]*lutt6[3];
      out1[3] += inp2[0]*lutt6[0];
      out1[3] += inp2[3]*lutt6[1];

      out1[2] =  inp1[5]*lutt6[6];
      out1[2] += inp1[4]*lutt6[7];
      out1[2] += inp1[7]*lutt6[4];
      out1[2] += inp1[6]*lutt6[5];
      out1[2] += inp2[1]*lutt6[2];
      out1[2] += inp2[0]*lutt6[3];
      out1[2] += inp2[3]*lutt6[0];
      out1[2] += inp2[2]*lutt6[1];

      out1[5] =  inp1[4]*lutt6[6];
      out1[5] += inp1[7]*lutt6[7];
      out1[5] += inp1[6]*lutt6[4];
      out1[5] += inp2[1]*lutt6[5];
      out1[5] += inp2[0]*lutt6[2];
      out1[5] += inp2[3]*lutt6[3];
      out1[5] += inp2[2]*lutt6[0];
      out1[5] += inp2[5]*lutt6[1];

      out1[4] =  inp1[7]*lutt6[6];
      out1[4] += inp1[6]*lutt6[7];
      out1[4] += inp2[1]*lutt6[4];
      out1[4] += inp2[0]*lutt6[5];
      out1[4] += inp2[3]*lutt6[2];
      out1[4] += inp2[2]*lutt6[3];
      out1[4] += inp2[5]*lutt6[0];
      out1[4] += inp2[4]*lutt6[1];

      out1[7] =  inp1[6]*lutt6[6];
      out1[7] += inp2[1]*lutt6[7];
      out1[7] += inp2[0]*lutt6[4];
      out1[7] += inp2[3]*lutt6[5];
      out1[7] += inp2[2]*lutt6[2];
      out1[7] += inp2[5]*lutt6[3];
      out1[7] += inp2[4]*lutt6[0];
      out1[7] += inp2[7]*lutt6[1];

      out1[6] =  inp2[1]*lutt6[6];
      out1[6] += inp2[0]*lutt6[7];
      out1[6] += inp2[3]*lutt6[4];
      out1[6] += inp2[2]*lutt6[5];
      out1[6] += inp2[5]*lutt6[2];
      out1[6] += inp2[4]*lutt6[3];
      out1[6] += inp2[7]*lutt6[0];
      out1[6] += inp2[6]*lutt6[1];
      outp[1] = /*CLAMP*/((out1[1]+0x4000) >> 0xF);
      outp[0] = /*CLAMP*/((out1[0]+0x4000) >> 0xF);
      outp[3] = /*CLAMP*/((out1[3]+0x4000) >> 0xF);
      outp[2] = /*CLAMP*/((out1[2]+0x4000) >> 0xF);
      outp[5] = /*CLAMP*/((out1[5]+0x4000) >> 0xF);
      outp[4] = /*CLAMP*/((out1[4]+0x4000) >> 0xF);
      outp[7] = /*CLAMP*/((out1[7]+0x4000) >> 0xF);
      outp[6] = /*CLAMP*/((out1[6]+0x4000) >> 0xF);
      inp1 = inp2;
      inp2 += 8;
      outp += 8;
   }
   memcpy (save, inp2-8, 0x10);
   memcpy (BufferSpace+(w1&0xffff), outbuff, cnt);
}

static void SEGMENT2 (uint32_t w1, uint32_t w2)
{
   if (isZeldaABI)
   {
      FILTER2 (w1, w2);
      return;
   }
   if ((w1 & 0xffffff) == 0)
      isMKABI = true;
   else
   {
      isMKABI = false;
      isZeldaABI = true;
      FILTER2 (w1, w2);
   }
}

static void UNKNOWN (uint32_t w1, uint32_t w2) {
}
/*
void (*ABI2[0x20])(void) = {
    SPNOOP, ADPCM2, CLEARBUFF2, SPNOOP, SPNOOP, RESAMPLE2, SPNOOP, SEGMENT2,
    SETBUFF2, SPNOOP, DMEMMOVE2, LOADADPCM2, MIXER2, INTERLEAVE2, HILOGAIN, SETLOOP2,
    SPNOOP, INTERL2, ENVSETUP1, ENVMIXER2, LOADBUFF2, SAVEBUFF2, ENVSETUP2, SPNOOP,
    SPNOOP, SPNOOP, SPNOOP, SPNOOP, SPNOOP, SPNOOP, SPNOOP, SPNOOP
};*/

static const acmd_callback_t ABI2[0x20] = {
    SPNOOP , ADPCM2, CLEARBUFF2, UNKNOWN, ADDMIXER, RESAMPLE2, UNKNOWN, SEGMENT2,
    SETBUFF2 , DUPLICATE2, DMEMMOVE2, LOADADPCM2, MIXER2, INTERLEAVE2, HILOGAIN, SETLOOP2,
    SPNOOP, INTERL2 , ENVSETUP1, ENVMIXER2, LOADBUFF2, SAVEBUFF2, ENVSETUP2, SPNOOP,
    HILOGAIN , SPNOOP, DUPLICATE2 , UNKNOWN    , SPNOOP  , SPNOOP    , SPNOOP  , SPNOOP
};
/*
void (*ABI2[0x20])(void) = {
    SPNOOP , ADPCM2, CLEARBUFF2, SPNOOP, SPNOOP, RESAMPLE2  , SPNOOP  , SEGMENT2,
    SETBUFF2 , DUPLICATE2, DMEMMOVE2, LOADADPCM2, MIXER2, INTERLEAVE2, SPNOOP, SETLOOP2,
    SPNOOP, INTERL2 , ENVSETUP1, ENVMIXER2, LOADBUFF2, SAVEBUFF2, ENVSETUP2, SPNOOP,
    SPNOOP , SPNOOP, SPNOOP , SPNOOP    , SPNOOP  , SPNOOP    , SPNOOP  , SPNOOP
};*/
/* NOTES:

  FILTER/SEGMENT - Still needs to be finished up... add FILTER?
  UNKNOWWN #27   - Is this worth doing?  Looks like a pain in the ass just for WaveRace64
*/

void alist_process_mk(void)
{
    alist_process(ABI2, 0x20);
}

void alist_process_sfj(void)
{
    alist_process(ABI2, 0x20);
}

void alist_process_wrjb(void)
{
    alist_process(ABI2, 0x20);
}

void alist_process_sf(void)
{
    alist_process(ABI2, 0x20);
}

void alist_process_fz(void)
{
    alist_process(ABI2, 0x20);
}

void alist_process_ys(void)
{
    alist_process(ABI2, 0x20);
}

void alist_process_1080(void)
{
    alist_process(ABI2, 0x20);
}

void alist_process_oot(void)
{
    alist_process(ABI2, 0x20);
}

void alist_process_mm(void)
{
    alist_process(ABI2, 0x20);
}

void alist_process_mmb(void)
{
    alist_process(ABI2, 0x20);
}

void alist_process_ac(void)
{
    alist_process(ABI2, 0x20);
}
