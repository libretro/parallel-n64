/*
Copyright (C) 2002 Rice1964

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*/
/* Rice's decoded combiner mux (DecodedMux.h). C89.
 *
 * The equations are 16 bytes, four stages of four (a, b, c, d); a whole
 * stage is compared and copied as its four bytes. */
#include <stdint.h>
#include <string.h>

#include <retro_miscellaneous.h>

#include "Config.h"
#include "DecodedMux.h"
#include "RenderBase.h"

#include "../../Graphics/image_convert.h"

static const uint8_t sc_Mux32[32] = 
{
    MUX_COMBINED, MUX_TEXEL0,   MUX_TEXEL1, MUX_PRIM,
    MUX_SHADE,    MUX_ENV,      MUX_1,      MUX_COMBINED|MUX_ALPHAREPLICATE,
    MUX_TEXEL0|MUX_ALPHAREPLICATE, MUX_TEXEL1|MUX_ALPHAREPLICATE, MUX_PRIM|MUX_ALPHAREPLICATE, MUX_SHADE|MUX_ALPHAREPLICATE,
    MUX_ENV|MUX_ALPHAREPLICATE, MUX_LODFRAC, MUX_PRIMLODFRAC, MUX_K5,       /* actually k5 */
    MUX_UNK, MUX_UNK, MUX_UNK, MUX_UNK,
    MUX_UNK, MUX_UNK, MUX_UNK, MUX_UNK,
    MUX_UNK, MUX_UNK, MUX_UNK, MUX_UNK,
    MUX_UNK, MUX_UNK, MUX_UNK, MUX_0
};

static const uint8_t sc_Mux16[16] = 
{
    MUX_COMBINED, MUX_TEXEL0,   MUX_TEXEL1, MUX_PRIM,
    MUX_SHADE,    MUX_ENV,      MUX_1,      MUX_COMBALPHA,
    MUX_TEXEL0|MUX_ALPHAREPLICATE, MUX_TEXEL1|MUX_ALPHAREPLICATE, MUX_PRIM|MUX_ALPHAREPLICATE, MUX_SHADE|MUX_ALPHAREPLICATE,
    MUX_ENV|MUX_ALPHAREPLICATE, MUX_LODFRAC, MUX_PRIMLODFRAC, MUX_0 
};

static const uint8_t sc_Mux8[8] = 
{
    MUX_COMBINED, MUX_TEXEL0,   MUX_TEXEL1, MUX_PRIM,
    MUX_SHADE,    MUX_ENV,      MUX_1,      MUX_0
};

#define MUX_BYTES(mux) ((uint8_t*)(mux)->m_n64Combiners)

/* stage @s's (a-b)*c+d */
static void set_stage(DecodedMux *mux, int s, uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
    N64CombinerType *m = &mux->m_n64Combiners[s];
    m->a = a;
    m->b = b;
    m->c = c;
    m->d = d;
}

static bool same_stage(const DecodedMux *mux, int s1, int s2)
{
    return memcmp(&mux->m_n64Combiners[s1], &mux->m_n64Combiners[s2], sizeof(N64CombinerType)) == 0;
}

void DecodedMux_Init(DecodedMux *mux)
{
    int i;
    memset(mux->m_n64Combiners, 0, sizeof(mux->m_n64Combiners));
    mux->mType = CM_FMT_TYPE_NOT_CHECKED;
    for (i = 0; i < 4; i++)
        mux->splitType[i] = CM_FMT_TYPE_NOT_CHECKED;
}

void DecodedMux_Decode(DecodedMux *mux, uint32_t dwMux0, uint32_t dwMux1)
{
    mux->m_dwMux0 = dwMux0;
    mux->m_dwMux1 = dwMux1;

    /* the unified MUX_ values of each field */
    set_stage(mux, N64Cycle0RGB,
          sc_Mux16[(dwMux0>>20)&0x0F],      /* a0 */
          sc_Mux16[(dwMux1>>28)&0x0F],      /* b0 */
          sc_Mux32[(dwMux0>>15)&0x1F],      /* c0 */
          sc_Mux8 [(dwMux1>>15)&0x07]);     /* d0 */
    set_stage(mux, N64Cycle0Alpha,
          sc_Mux8[(dwMux0>>12)&0x07],       /* Aa0 */
          sc_Mux8[(dwMux1>>12)&0x07],       /* Ab0 */
          sc_Mux8[(dwMux0>>9 )&0x07],       /* Ac0 */
          sc_Mux8[(dwMux1>>9 )&0x07]);      /* Ad0 */
    set_stage(mux, N64Cycle1RGB,
          sc_Mux16[(dwMux0>>5 )&0x0F],      /* a1 */
          sc_Mux16[(dwMux1>>24)&0x0F],      /* b1 */
          sc_Mux32[(dwMux0    )&0x1F],      /* c1 */
          sc_Mux8 [(dwMux1>>6 )&0x07]);     /* d1 */
    set_stage(mux, N64Cycle1Alpha,
          sc_Mux8[(dwMux1>>21)&0x07],       /* Aa1 */
          sc_Mux8[(dwMux1>>3 )&0x07],       /* Ab1 */
          sc_Mux8[(dwMux1>>18)&0x07],       /* Ac1 */
          sc_Mux8[(dwMux1    )&0x07]);      /* Ad1 */

    mux->m_bTexel0IsUsed = DecodedMux_IsUsed(mux, MUX_TEXEL0, MUX_MASK);
    mux->m_bTexel1IsUsed = DecodedMux_IsUsed(mux, MUX_TEXEL1, MUX_MASK);
}

bool DecodedMux_IsUsed(const DecodedMux *mux, uint8_t val, uint8_t mask)
{
    const uint8_t *pmux = (const uint8_t*)mux->m_n64Combiners;
    int i;
    for (i = 0; i < 16; i++)
    {
        if ((pmux[i]&mask) == (val&mask))
            return true;
    }
    return false;
}

/* @val in cycle @cycle's colour or alpha equation */
static bool used_in_cycle(const DecodedMux *mux, uint8_t val, int cycle, CombineChannel channel, uint8_t mask)
{
    const uint8_t *pmux = (const uint8_t*)mux->m_n64Combiners;
    int i;

    cycle *= 2;
    if (channel == ALPHA_CHANNEL)
        cycle++;

    for (i = 0; i < 4; i++)
    {
        if ((pmux[i+cycle*4]&mask) == (val&mask))
            return true;
    }
    return false;
}

/* @val1 (under @mask) replaced by @val2 in stage @cycle, or all with -1 */
static void replace_val(DecodedMux *mux, uint8_t val1, uint8_t val2, int cycle, uint8_t mask)
{
    uint8_t *pmux = MUX_BYTES(mux);
    int start = 0;
    int end = 16;
    int i;

    if (cycle >= 0)
    {
        start = cycle*4;
        end = start+4;
    }

    for (i = start; i < end; i++)
    {
        if ((pmux[i]&mask) == (val1&mask))
        {
            pmux[i] &= (~mask);
            pmux[i] |= val2;
        }
    }
}

static void check_combine_in_cycle1(DecodedMux *mux)
{
    N64CombinerType *alpha0 = &mux->m_n64Combiners[N64Cycle0Alpha];
    N64CombinerType *rgb0   = &mux->m_n64Combiners[N64Cycle0RGB];

    if (used_in_cycle(mux, MUX_COMBINED, 0, COLOR_CHANNEL, MUX_MASK))
        replace_val(mux, MUX_COMBINED, MUX_SHADE, 0, MUX_MASK);

    if (used_in_cycle(mux, MUX_COMBALPHA, 0, COLOR_CHANNEL, MUX_MASK))
        replace_val(mux, MUX_COMBALPHA, MUX_SHADE|MUX_ALPHAREPLICATE, 0, MUX_MASK);

    if (used_in_cycle(mux, MUX_COMBINED, 0, ALPHA_CHANNEL, MUX_MASK))
    {
        if (alpha0->c == MUX_COMBINED && rgb0->c == MUX_LODFRAC && rgb0->b == rgb0->d && alpha0->b == alpha0->d)
            alpha0->c = MUX_LODFRAC;
        else
            replace_val(mux, MUX_COMBINED, MUX_SHADE, 1, MUX_MASK);
    }

    /* (the C++ also replaced a combined alpha in cycle 0's alpha with the
     * shade, but an alpha equation's inputs come from sc_Mux8, which has
     * none, and no hack puts one there) */
}

void DecodedMux_Simplify(DecodedMux *mux)
{
    check_combine_in_cycle1(mux);

    if (g_curRomInfo.bTexture1Hack)
    {
        replace_val(mux, MUX_TEXEL1, MUX_TEXEL0, 2, MUX_MASK);
        replace_val(mux, MUX_TEXEL1, MUX_TEXEL0, 3, MUX_MASK);
    }
    mux->splitType[0] = CM_FMT_TYPE_NOT_USED;
    mux->splitType[1] = CM_FMT_TYPE_NOT_USED;
    mux->splitType[2] = CM_FMT_TYPE_NOT_USED;
    mux->splitType[3] = CM_FMT_TYPE_NOT_USED;
    mux->mType = CM_FMT_TYPE_NOT_USED;

    mux->m_bTexel0IsUsed = DecodedMux_IsUsed(mux, MUX_TEXEL0, MUX_MASK);
    mux->m_bTexel1IsUsed = DecodedMux_IsUsed(mux, MUX_TEXEL1, MUX_MASK);
}

void DecodedMux_Reformat(DecodedMux *mux, bool do_complement)
{
    CombinerFormatType *splitType = mux->splitType;
    uint8_t *bytes = MUX_BYTES(mux);
    int i;
    signed cond2, cond3;

    if (same_stage(mux, N64Cycle0RGB, N64Cycle1RGB))
    {
        set_stage(mux, N64Cycle1RGB, MUX_0, MUX_0, MUX_0, MUX_COMBINED);
        splitType[N64Cycle1RGB] = CM_FMT_TYPE_NOT_USED;
    }

    if (same_stage(mux, N64Cycle0Alpha, N64Cycle1Alpha))
    {
        set_stage(mux, N64Cycle1Alpha, MUX_0, MUX_0, MUX_0, MUX_COMBINED);
        splitType[N64Cycle1Alpha] = CM_FMT_TYPE_NOT_USED;
    }

    for (i = 0; i < 4; i++)
    {
        N64CombinerType *m;

        if (splitType[i] == CM_FMT_TYPE_NOT_USED)
            continue;   /* skip this, it is not used */

        m = &mux->m_n64Combiners[i];
        if (m->c == MUX_0 || m->a == m->b)   m->a = m->b = m->c = MUX_0;
        if (do_complement && (m->b == MUX_1 || m->d == MUX_1))  m->a = m->b = m->c = MUX_0;
        if (m->a == MUX_0 && m->b == m->d)
        {
            m->a = m->b;
            m->b = m->d = 0;
            /* hack for Mario Tennis */
            if (options.enableHackForGames == HACK_FOR_MARIO_TENNIS && m->c == MUX_TEXEL1)
            {
                if (do_complement)
                    m->c = MUX_TEXEL0|MUX_COMPLEMENT;
                else
                {
                    m->a = m->c;
                    m->c = m->b;
                    m->b = m->a;
                    m->a = MUX_1;
                }
            }
        }

        /* Type 1 == D:
         *   D = 1             = D(=1)
         *   C=0               = D
         *   A==B              = D
         *   B=0, C=1, D=0     = A
         *   C=1, B==D         = A
         *   A=1, B=0, D=0     = C
         *   A=1, C=1, D=0     = 1-B */
        splitType[i] = CM_FMT_TYPE_NOT_CHECKED;
        if (m->c == MUX_0 || m->a == m->b || (do_complement && (m->d == MUX_1 || m->b == MUX_1)))
        {
            splitType[i] = CM_FMT_TYPE_D;
            m->a = m->b = m->c = MUX_0;
            if (m->d == MUX_COMBINED && i >= N64Cycle1RGB)    splitType[i] = CM_FMT_TYPE_NOT_USED;
        }
        else if ((m->b == MUX_0 && m->c == MUX_1 && m->d == MUX_0) || (m->c == MUX_1 && m->b == m->d))
        {
            splitType[i] = CM_FMT_TYPE_D;
            m->d = m->a;
            m->a = m->b = m->c = MUX_0;
            if (m->d == MUX_COMBINED && i >= N64Cycle1RGB)    splitType[i] = CM_FMT_TYPE_NOT_USED;
        }
        else if (m->a == MUX_1 && m->b == MUX_0 && m->d == MUX_0)
        {
            splitType[i] = CM_FMT_TYPE_D;
            m->d = m->c;
            m->a = m->b = m->c = MUX_0;
            if (m->d == MUX_COMBINED && i >= N64Cycle1RGB)    splitType[i] = CM_FMT_TYPE_NOT_USED;
        }
        else if (m->a == MUX_1 && m->c == MUX_1 && m->d == MUX_0 && do_complement)
        {
            splitType[i] = CM_FMT_TYPE_D;
            m->d = m->b^MUX_COMPLEMENT;
            m->a = m->b = m->c = MUX_0;
            if (m->d == MUX_COMBINED && i >= N64Cycle1RGB)    splitType[i] = CM_FMT_TYPE_NOT_USED;
        }

        if (splitType[i] == CM_FMT_TYPE_NOT_USED)
            continue;

        if (splitType[i] == CM_FMT_TYPE_D)
        {
            if ((i == N64Cycle0RGB || i == N64Cycle0Alpha) && splitType[i+2] != CM_FMT_TYPE_NOT_USED)
            {
                /* cycle 1's combined input replaced with cycle 0's D,
                 * cycle 0's result thrown away and cycle 1 put in its
                 * place, and the stage done again */
                static const uint32_t passthrough = 0x02000000; /* d = MUX_COMBINED in memory order */
                uint8_t saveD = m->d;
                int j;
                for (j = 0; j < 4; j++)
                {
                    if ((bytes[j+i*4+8]&MUX_MASK) == MUX_COMBINED)
                        bytes[j+i*4+8] = saveD|(bytes[j+i*4+8]&0xC0);
                }
                mux->m_n64Combiners[i] = mux->m_n64Combiners[i+2];
                splitType[i+2] = CM_FMT_TYPE_NOT_USED;
                memcpy(&mux->m_n64Combiners[i+2], &passthrough, sizeof(passthrough));
                i = i-1;
                continue;
            }

            if ((i == 2 || i == 3) && (m->d&MUX_MASK) == MUX_COMBINED)
                splitType[i] = CM_FMT_TYPE_NOT_USED;
            continue;
        }

        /* Type 2: A+D
         *   B=0, C=1          = A+D
         *   A=1, B=0          = C+D */
        splitType[i] = CM_FMT_TYPE_A_ADD_D;
        if (m->b == MUX_0 && m->c == MUX_1)
        {
            if (m->d == MUX_TEXEL0 || m->d == MUX_TEXEL1)    swapbyte(&m->a, &m->d);
            if (m->a == MUX_COMBINED) swapbyte(&m->a, &m->d);
            continue;
        }

        if (m->a == MUX_1 && m->b == MUX_0)
        {
            m->a = m->c;          /* to A+D */
            m->c = MUX_1;
            if (m->d == MUX_TEXEL0 || m->d == MUX_TEXEL1)    swapbyte(&m->a, &m->d);
            continue;
        }

        /* Type 3: A*C
         *   B=0, D=0          = A*C
         *   A=1, D=0          = (1-A)*C */
        splitType[i] = CM_FMT_TYPE_A_MOD_C;
        if (m->b == MUX_0 && m->d == MUX_0)
        {
            if (m->c == MUX_TEXEL0 || m->c == MUX_TEXEL1)    swapbyte(&m->a, &m->c);
            if (m->a == MUX_COMBINED) swapbyte(&m->a, &m->c);
            continue;
        }

        if (m->a == MUX_1 && m->d == MUX_0 && do_complement)
        {
            m->a = m->b^MUX_COMPLEMENT;
            m->b = MUX_0;
            if (m->c == MUX_TEXEL0 || m->c == MUX_TEXEL1)    swapbyte(&m->a, &m->c);
            if (m->a == MUX_COMBINED) swapbyte(&m->a, &m->c);
            continue;
        }

        /* Type 4: A-B
         *   C=1, D=0          = A-B */
        splitType[i] = CM_FMT_TYPE_A_SUB_B;
        if (m->c == MUX_1 && m->d == MUX_0)
            continue;

        /* Type 5: A*C+D
         *   B=0               = A*C+D
         *   A=1               = (1-B) * C + D */
        splitType[i] = CM_FMT_TYPE_A_MOD_C_ADD_D;
        if (m->b == MUX_0)
        {
            if (m->c == MUX_TEXEL0 || m->c == MUX_TEXEL1)    swapbyte(&m->a, &m->c);
            if (m->a == MUX_COMBINED) swapbyte(&m->a, &m->c);
            continue;
        }

        if (m->a == MUX_1 && m->b != m->d && do_complement)
        {
            m->a = m->b^MUX_COMPLEMENT;
            m->b = MUX_0;
            if (m->c == MUX_TEXEL0 || m->c == MUX_TEXEL1)    swapbyte(&m->a, &m->c);
            if (m->a == MUX_COMBINED) swapbyte(&m->a, &m->c);
            continue;
        }

        /* Type 6: (A-B)*C+B, a LERP: D==B */
        splitType[i] = CM_FMT_TYPE_A_LERP_B_C;
        if (m->b == m->d)
            continue;

        /* Type 7: A-B+D
         *   C=1               = A-B+D
         * (the C++ swapped a texel C into A here, but C is 1) */
        splitType[i] = CM_FMT_TYPE_A_SUB_B_ADD_D;
        if (m->c == MUX_1)
            continue;

        /* Type 8: (A-B)*C */
        splitType[i] = CM_FMT_TYPE_A_SUB_B_MOD_C;
        if (m->d == MUX_0)
            continue;

        if (m->c == m->d && do_complement)   /* (A-B)*C+C ==> (A + B|C ) * C */
        {
            m->d = MUX_0;
            m->b |= MUX_COMPLEMENT;
            continue;
        }

        if (m->a == m->d)
        {
            splitType[i] = CM_FMT_TYPE_A_B_C_A;
            continue;
        }

        /* Type 9: (A-B)*C+D */
        splitType[i] = CM_FMT_TYPE_A_B_C_D;
    }

    if ((splitType[0] == CM_FMT_TYPE_D && splitType[2] != CM_FMT_TYPE_NOT_USED) ||  /* cycle 1 colour */
        (!used_in_cycle(mux, MUX_COMBINED, 1, COLOR_CHANNEL, MUX_MASK) && !used_in_cycle(mux, MUX_COMBINED, 1, ALPHA_CHANNEL, MUX_MASK) && splitType[2] != CM_FMT_TYPE_NOT_USED))
    {
        /* cycle 0's colour replaced with cycle 1's, whose combined
         * input was replaced already */
        mux->m_n64Combiners[N64Cycle0RGB] = mux->m_n64Combiners[N64Cycle1RGB];
        set_stage(mux, N64Cycle1RGB, MUX_0, MUX_0, MUX_0, MUX_COMBINED);
        splitType[0] = splitType[2];
        splitType[2] = CM_FMT_TYPE_NOT_USED;
    }

    if ((splitType[1] == CM_FMT_TYPE_D && splitType[3] != CM_FMT_TYPE_NOT_USED) ||  /* cycle 1 alpha */
        (!used_in_cycle(mux, MUX_COMBINED, 1, ALPHA_CHANNEL, MUX_MASK) && !used_in_cycle(mux, MUX_COMBINED|MUX_ALPHAREPLICATE, 1, COLOR_CHANNEL, MUX_MASK_WITH_ALPHA) && splitType[3] != CM_FMT_TYPE_NOT_USED))
    {
        /* cycle 0's alpha replaced with cycle 1's, likewise */
        mux->m_n64Combiners[N64Cycle0Alpha] = mux->m_n64Combiners[N64Cycle1Alpha];
        set_stage(mux, N64Cycle1Alpha, MUX_0, MUX_0, MUX_0, MUX_COMBINED);
        splitType[1] = splitType[3];
        splitType[3] = CM_FMT_TYPE_NOT_USED;
    }

    for (i = 0; i < 2; i++)
    {
        /* cycle 0's A*C and cycle 1's A+D as one A*C+D */
        N64CombinerType *c0 = &mux->m_n64Combiners[i];
        N64CombinerType *c1 = &mux->m_n64Combiners[i+2];
        if (splitType[i] == CM_FMT_TYPE_A_MOD_C && splitType[i+2] == CM_FMT_TYPE_A_ADD_D)
        {
            c0->d = (c1->a & MUX_MASK) == MUX_COMBINED ? c1->d : c1->a;
            splitType[i] = CM_FMT_TYPE_A_MOD_C_ADD_D;
            splitType[i+2] = CM_FMT_TYPE_NOT_USED;
            c1->a = MUX_0;
            c1->c = MUX_0;
            c1->d = MUX_COMBINED;
        }
    }

    cond3 = MAX(splitType[0], splitType[1]);
    cond2 = MAX(cond3, splitType[2]);
    mux->mType = (CombinerFormatType)MAX(cond2, splitType[3]);
}

void DecodedMux_Hack(DecodedMux *mux)
{
    if( options.enableHackForGames == HACK_FOR_TONYHAWK )
    {
        if( gRSP.curTile == 1 )
            replace_val(mux, MUX_TEXEL1, MUX_TEXEL0, -1, MUX_MASK);
    }
    else if( options.enableHackForGames == HACK_FOR_ZELDA || options.enableHackForGames == HACK_FOR_ZELDA_MM)
    {
        if( mux->m_dwMux1 == 0xfffd9238 && mux->m_dwMux0 == 0x00ffadff )
            replace_val(mux, MUX_TEXEL1, MUX_TEXEL0, -1, MUX_MASK);
        else if( mux->m_dwMux1 == 0xff5bfff8 && mux->m_dwMux0 == 0x00121603 )
        {
            /* the Zelda road trace */
            replace_val(mux, MUX_TEXEL1, MUX_0, -1, MUX_MASK);
        }
    }
    else if( options.enableHackForGames == HACK_FOR_MARIO_TENNIS )
    {
        if( mux->m_dwMux1 == 0xffebdbc0 && mux->m_dwMux0 == 0x00ffb9ff )
        {
            /* player shadow */
            mux->m_n64Combiners[N64Cycle1Alpha].c = MUX_TEXEL0;
        }
    }
    else if( options.enableHackForGames == HACK_FOR_MARIO_GOLF )
    {
        /* hack for Mario Golf */
        if( mux->m_dwMux1 == 0xf1ffca7e || mux->m_dwMux0 == 0x00115407 )
        {
            /* the grass */
            replace_val(mux, MUX_TEXEL0, MUX_TEXEL1, -1, MUX_MASK);
        }
    }
    else if( options.enableHackForGames == HACK_FOR_TOPGEARRALLY )
    {
        /* Mux=0x00317e025ffef3fa    used in TOP GEAR RALLY
         * Color0: (PRIM - ENV) * TEXEL1 + ENV
         * Color1: (COMBINED - 0) * TEXEL1 + 0
         * Alpha0: (0 - 0) * 0 + TEXEL0
         * Alpha1: (0 - 0) * 0 + TEXEL1 */
        if( mux->m_dwMux1 == 0x5ffef3fa || mux->m_dwMux0 == 0x00317e02 )
        {
            /* the grass */
            mux->m_n64Combiners[N64Cycle1Alpha].d = MUX_COMBINED;
            mux->m_n64Combiners[N64Cycle1RGB].c = MUX_TEXEL0;
        }
    }
}
