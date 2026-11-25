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

/* A combiner mux decoded: the two cycles' colour and alpha equations,
 * (A-B)*C+D each, as Rice's MUX_ values, and what kind of equation each
 * is once simplified. C89. */
#ifndef _DECODEDMUX_H_
#define _DECODEDMUX_H_

#include <stdint.h>
#include <boolean.h>

#include "typedefs.h"
#include "CombinerDefs.h"

#define CM_IGNORE 0
#define CM_IGNORE_BYTE 0xFF

typedef enum 
{
    N64Cycle0RGB   = 0,
    N64Cycle0Alpha = 1,
    N64Cycle1RGB   = 2,
    N64Cycle1Alpha = 3
} N64StageNumberType;

typedef struct DecodedMux
{
    /* by N64StageNumberType: cycle 0 colour, cycle 0 alpha, cycle 1
     * colour, cycle 1 alpha; 16 bytes, a stage's four in a row */
    N64CombinerType m_n64Combiners[4];

    uint32_t m_dwMux0;
    uint32_t m_dwMux1;

    CombinerFormatType splitType[4];
    CombinerFormatType mType;

    bool m_bTexel0IsUsed;
    bool m_bTexel1IsUsed;
} DecodedMux;

#ifdef __cplusplus
extern "C" {
#endif

/* nothing in the equations, no stage checked */
void DecodedMux_Init(DecodedMux *mux);
/* the two mux words to equations of MUX_ values */
void DecodedMux_Decode(DecodedMux *mux, uint32_t dwMux0, uint32_t dwMux1);
/* the per-game mux hacks (Tony Hawk, Zelda, Mario Tennis and Golf, Top
 * Gear Rally) */
void DecodedMux_Hack(DecodedMux *mux);
/* cycle 0's combined inputs taken from the shade, Texture 1 hack, the
 * texels used */
void DecodedMux_Simplify(DecodedMux *mux);
/* each stage's equation in its simplest form and kind (splitType, mType) */
void DecodedMux_Reformat(DecodedMux *mux, bool do_complement);
/* @fac (under @mask) anywhere in the four equations */
bool DecodedMux_IsUsed(const DecodedMux *mux, uint8_t fac, uint8_t mask);

/* The muxes decoded so far, kept by key in a sorted table made once:
 * a game's mux is decoded, hacked and simplified the first time it is
 * seen and copied from here after. */
#define DECODED_MUX_CACHE_SIZE 4096
typedef struct DecodedMuxCache
{
    uint64_t   *keys;
    DecodedMux *muxes;
    int         count;
    int         capacity;
} DecodedMuxCache;

/* the table for @capacity muxes; false if it could not be had */
bool DecodedMuxCache_Init(DecodedMuxCache *cache, int capacity);
void DecodedMuxCache_Free(DecodedMuxCache *cache);
/* @key's index, or -1 */
int  DecodedMuxCache_Find(const DecodedMuxCache *cache, uint64_t key);
/* @mux under @key, replacing what is there; nothing when full */
void DecodedMuxCache_Add(DecodedMuxCache *cache, uint64_t key, const DecodedMux *mux);

/* set by DecodedMux_Update: Conker's shadow mux is current (Render.cpp
 * hides that shadow) */
extern bool bConkerHideShadow;

/* @mux made the one for (@dwMux0, @dwMux1): nothing when it is already,
 * else from @cache or decoded, hacked, simplified and kept there (with
 * Dr. Mario's and Conker's mux hacks). True when it changed. */
bool DecodedMux_Update(DecodedMux *mux, DecodedMuxCache *cache, uint32_t dwMux0, uint32_t dwMux1);

#ifdef __cplusplus
}
#endif

#endif
