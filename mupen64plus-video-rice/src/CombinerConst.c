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
/* The combiner's constant factors for the shade colour (CombinerDefs.h).
 * C89.
 *
 * A colour is worked on as its four bytes in memory order, the way the
 * IColor class this replaces did: channel 3 is the one AlphaReplicate
 * copies, whatever the host's byte order. */
#include <string.h>

#include "Config.h"
#include "CombinerDefs.h"
#include "RenderBase.h"

static COLOR alpha_replicate(COLOR c)
{
    uint8_t b[4];
    memcpy(b, &c, sizeof b);
    b[0] = b[1] = b[2] = b[3];
    memcpy(&c, b, sizeof b);
    return c;
}

/* one equation input as a colour */
static COLOR get_icolor(uint8_t flag, COLOR curCol)
{
    COLOR newc;
    switch (flag & MUX_MASK)
    {
        case MUX_0:
            newc = 0;
            break;
        case MUX_1:
        case MUX_K5:
            newc = 0xFFFFFFFF;
            break;
        case MUX_PRIM:
            newc = gRDP.primitiveColor;
            break;
        case MUX_ENV:
            newc = gRDP.envColor;
            break;
        case MUX_UNK:
            newc = curCol;
            if (options.enableHackForGames == HACK_FOR_CONKER)
                newc = 0xFFFFFFFF;
            break;
        default: /* MUX_COMBINED, MUX_SHADE and the rest */
            newc = curCol;
            break;
    }
    if (flag & MUX_COMPLEMENT)
        newc = ~newc;
    if (flag & MUX_ALPHAREPLICATE)
        newc = alpha_replicate(newc);
    return newc;
}

/* per byte, saturating, as IColor's -=, *= and += were */
static COLOR c_sub(COLOR x, COLOR y)
{
    uint8_t a[4], b[4];
    int i;
    memcpy(a, &x, sizeof a);
    memcpy(b, &y, sizeof b);
    for (i = 0; i < 4; i++)
        a[i] = (uint8_t)(a[i] > b[i] ? a[i] - b[i] : 0);
    memcpy(&x, a, sizeof a);
    return x;
}

static COLOR c_mul(COLOR x, COLOR y)
{
    uint8_t a[4], b[4];
    int i;
    memcpy(a, &x, sizeof a);
    memcpy(b, &y, sizeof b);
    for (i = 0; i < 4; i++)
        a[i] = (uint8_t)(((unsigned)a[i] * b[i]) >> 8);
    memcpy(&x, a, sizeof a);
    return x;
}

static COLOR c_add(COLOR x, COLOR y)
{
    uint8_t a[4], b[4];
    int i;
    memcpy(a, &x, sizeof a);
    memcpy(b, &y, sizeof b);
    for (i = 0; i < 4; i++)
    {
        unsigned s = (unsigned)a[i] + b[i];
        a[i] = (uint8_t)(s > 0xFF ? 0xFF : s);
    }
    memcpy(&x, a, sizeof a);
    return x;
}

/* (A-B)*C+D on one channel's equation, bytes in N64CombinerType order */
static COLOR equation(uint32_t op, COLOR curCol)
{
    N64CombinerType m;
    COLOR c = curCol;

    memcpy(&m, &op, sizeof m);
    if (m.c != MUX_0 && m.a != m.b)
    {
        if (m.a != MUX_0) c = get_icolor(m.a, curCol);
        if (m.b != MUX_0) c = c_sub(c, get_icolor(m.b, curCol));
        if (m.c != MUX_1) c = c_mul(c, get_icolor(m.c, curCol));
    }
    if (m.d != MUX_0) c = c_add(c, get_icolor(m.d, curCol));
    return c;
}

COLOR CalculateConstFactor(uint32_t colorOp, uint32_t alphaOp, uint32_t curCol)
{
    COLOR color = equation(colorOp, curCol);
    COLOR alpha = equation(alphaOp, curCol);
    return (color & 0x00FFFFFF) | (alpha & 0xFF000000);
}

/* the colour one constant source picks; is_alpha: the alpha channel's
 * list, which has no K5 and no Conker hack.  (MUX_FORCE_0, 0xFE, is past
 * MUX_MASK, so a masked source never equals it: such a flag gets the
 * default colour, as it did before.) */
static uint32_t const_source(uint32_t flag, uint32_t defaultColor, int is_alpha)
{
    switch (flag & MUX_MASK)
    {
        case MUX_0:
            return defaultColor;
        case MUX_1:
            return 0xFFFFFFFF;
        case MUX_PRIM:
            return gRDP.primitiveColor;
        case MUX_ENV:
            return gRDP.envColor;
        case MUX_LODFRAC:
            return COLOR_RGBA(gRDP.LODFrac, gRDP.LODFrac, gRDP.LODFrac, gRDP.LODFrac);
        case MUX_PRIMLODFRAC:
            return COLOR_RGBA(gRDP.primLODFrac, gRDP.primLODFrac, gRDP.primLODFrac, gRDP.primLODFrac);
        case MUX_PRIM_ALPHA:
            return alpha_replicate(gRDP.primitiveColor);
        case MUX_ENV_ALPHA:
            return alpha_replicate(gRDP.envColor);
        case MUX_K5:
            if (!is_alpha)
                return 0xFFFFFFFF;
            break;
        case MUX_UNK:
            if (!is_alpha && options.enableHackForGames == HACK_FOR_CONKER)
                return 0xFFFFFFFF;
            break;
        default:
            break;
    }
    return defaultColor;
}

COLOR Rice_GetConstFactor(uint32_t colorFlag, uint32_t alphaFlag, uint32_t defaultColor)
{
    uint32_t color = const_source(colorFlag, defaultColor, 0);
    uint32_t alpha = const_source(alphaFlag, defaultColor, 1);

    if (colorFlag & MUX_COMPLEMENT)
        color = 0xFFFFFFFF - color;
    if (colorFlag & MUX_ALPHAREPLICATE)
    {
        color = color >> 24;
        color = color | (color << 8) | (color << 16);
    }
    color &= 0x00FFFFFF;    /* the colour channel only, not the alpha one */

    if (alphaFlag & MUX_COMPLEMENT)
        alpha = 0xFFFFFFFF - alpha;
    alpha &= 0xFF000000;

    return color | alpha;
}
