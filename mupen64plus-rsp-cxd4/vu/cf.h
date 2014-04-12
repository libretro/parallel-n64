/******************************************************************************\
* Authors:  Iconoclast                                                         *
* Release:  2013.12.04                                                         *
* License:  CC0 Public Domain Dedication                                       *
*                                                                              *
* To the extent possible under law, the author(s) have dedicated all copyright *
* and related and neighboring rights to this software to the public domain     *
* worldwide. This software is distributed without any warranty.                *
*                                                                              *
* You should have received a copy of the CC0 Public Domain Dedication along    *
* with this software.                                                          *
* If not, see <http://creativecommons.org/publicdomain/zero/1.0/>.             *
\******************************************************************************/
#ifndef _CF_H
#define _CF_H

/*
 * For a non-cycle-accurate RSP emulator using SSE2, the following
 * scalar definitions of the control registers are obsolete.
 */
#if (0)
/*
 * Many vector units have pairs of "vector condition flags" registers.
 * In SGI's vector unit implementation, these are denoted as the
 * "vector control registers" under coprocessor 2.
 *
 * VCF-0 is the carry-out flags register:  $vco.
 * VCF-1 is the compare code flags register:  $vcc.
 * VCF-2 is the compare extension flags register:  $vce.
 * There is no fourth RSP flags register.
 */

unsigned short VCO;
unsigned short VCC;
unsigned char VCE;
#endif

/*
 * These normally should have type `int` because they are Boolean T/F arrays.
 * However, since SSE2 uses 128-bit XMM's, and Win32 `int` storage is 32-bit,
 * we have the problem of 32*8 > 128 bits, so we use `short` to reduce packs.
 */
short ne[8]; /* $vco:  high byte "NOTEQUAL" */
short co[8]; /* $vco:  low byte "carry/borrow in/out" */
short clip[8]; /* $vcc:  high byte (clip tests:  VCL, VCH, VCR) */
short comp[8]; /* $vcc:  low byte (VEQ, VNE, VLT, VGE, VCL, VCH, VCR) */
short vce[8]; /* $vce:  vector compare extension register */

#ifndef ARCH_MIN_SSE2
unsigned short get_VCO(void)
{
    register unsigned short VCO;

    VCO = 0x0000
      | (ne[0xF % 8] << 0xF)
      | (ne[0xE % 8] << 0xE)
      | (ne[0xD % 8] << 0xD)
      | (ne[0xC % 8] << 0xC)
      | (ne[0xB % 8] << 0xB)
      | (ne[0xA % 8] << 0xA)
      | (ne[0x9 % 8] << 0x9)
      | (ne[0x8 % 8] << 0x8)
      | (co[0x7 % 8] << 0x7)
      | (co[0x6 % 8] << 0x6)
      | (co[0x5 % 8] << 0x5)
      | (co[0x4 % 8] << 0x4)
      | (co[0x3 % 8] << 0x3)
      | (co[0x2 % 8] << 0x2)
      | (co[0x1 % 8] << 0x1)
      | (co[0x0 % 8] << 0x0);
    return (VCO); /* Big endian becomes little. */
}
unsigned short get_VCC(void)
{
    register unsigned short VCC;

    VCC = 0x0000
      | (clip[0xF % 8] << 0xF)
      | (clip[0xE % 8] << 0xE)
      | (clip[0xD % 8] << 0xD)
      | (clip[0xC % 8] << 0xC)
      | (clip[0xB % 8] << 0xB)
      | (clip[0xA % 8] << 0xA)
      | (clip[0x9 % 8] << 0x9)
      | (clip[0x8 % 8] << 0x8)
      | (comp[0x7 % 8] << 0x7)
      | (comp[0x6 % 8] << 0x6)
      | (comp[0x5 % 8] << 0x5)
      | (comp[0x4 % 8] << 0x4)
      | (comp[0x3 % 8] << 0x3)
      | (comp[0x2 % 8] << 0x2)
      | (comp[0x1 % 8] << 0x1)
      | (comp[0x0 % 8] << 0x0);
    return (VCC); /* Big endian becomes little. */
}
unsigned char get_VCE(void)
{
    register unsigned char VCE;

    VCE = 0x00
      | (vce[07] << 0x7)
      | (vce[06] << 0x6)
      | (vce[05] << 0x5)
      | (vce[04] << 0x4)
      | (vce[03] << 0x3)
      | (vce[02] << 0x2)
      | (vce[01] << 0x1)
      | (vce[00] << 0x0);
    return (VCE); /* Big endian becomes little. */
}
#else
unsigned short get_VCO(void)
{
    __m128i xmm, hi, lo;
    register unsigned short VCO;

    hi = _mm_load_si128((__m128i *)ne);
    lo = _mm_load_si128((__m128i *)co);

/*
 * Rotate Boolean storage from LSB to MSB.
 */
    hi = _mm_slli_epi16(hi, 15);
    lo = _mm_slli_epi16(lo, 15);

    xmm = _mm_packs_epi16(lo, hi); /* Decompress INT16 Booleans to INT8 ones. */
    VCO = _mm_movemask_epi8(xmm) & 0x0000FFFF; /* PMOVMSKB combines each MSB. */
    return (VCO);
}
unsigned short get_VCC(void)
{
    __m128i xmm, hi, lo;
    register unsigned short VCC;

    hi = _mm_load_si128((__m128i *)clip);
    lo = _mm_load_si128((__m128i *)comp);

/*
 * Rotate Boolean storage from LSB to MSB.
 */
    hi = _mm_slli_epi16(hi, 15);
    lo = _mm_slli_epi16(lo, 15);

    xmm = _mm_packs_epi16(lo, hi); /* Decompress INT16 Booleans to INT8 ones. */
    VCC = _mm_movemask_epi8(xmm) & 0x0000FFFF; /* PMOVMSKB combines each MSB. */
    return (VCC);
}
unsigned char get_VCE(void)
{
    __m128i xmm, hi, lo;
    register unsigned char VCE;

    hi = _mm_setzero_si128();
    lo = _mm_load_si128((__m128i *)vce);

    lo = _mm_slli_epi16(lo, 15); /* Rotate Boolean storage from LSB to MSB. */

    xmm = _mm_packs_epi16(lo, hi); /* Decompress INT16 Booleans to INT8 ones. */
    VCE = _mm_movemask_epi8(xmm) & 0x000000FF; /* PMOVMSKB combines each MSB. */
    return (VCE);
}
#endif

/*
 * CTC2 resources
 * not sure how to vectorize going the other direction into SSE2
 */
void set_VCO(unsigned short VCO)
{
    register int i;

    for (i = 0; i < 8; i++)
        co[i] = (VCO >> (i + 0x0)) & 1;
    for (i = 0; i < 8; i++)
        ne[i] = (VCO >> (i + 0x8)) & 1;
    return; /* Little endian becomes big. */
}
void set_VCC(unsigned short VCC)
{
    register int i;

    for (i = 0; i < 8; i++)
        comp[i] = (VCC >> (i + 0x0)) & 1;
    for (i = 0; i < 8; i++)
        clip[i] = (VCC >> (i + 0x8)) & 1;
    return; /* Little endian becomes big. */
}
void set_VCE(unsigned char VCE)
{
    register int i;

    for (i = 0; i < 8; i++)
        vce[i] = (VCE >> i) & 1;
    return; /* Little endian becomes big. */
}
#endif
