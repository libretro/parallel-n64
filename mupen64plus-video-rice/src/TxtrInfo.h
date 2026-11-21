/*
Copyright (C) 2003 Rice1964

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

/* A texture as Rice loads it: where from, its size, its format and how it
   wraps. C89, for C and C++ alike. */
#ifndef RICE_TXTRINFO_H
#define RICE_TXTRINFO_H

#include <stdint.h>
#include <boolean.h>
#include <retro_inline.h>

#include "../../Graphics/GBI.h"
#include "../../Graphics/RDP/gDP_state.h"

/* a texture's palette format (RSP_SETOTHERMODE_H gSetTextureLUT) */
#define TLUT_FMT_NONE           (0 << G_MDSFT_TEXTLUT)
#define TLUT_FMT_UNKNOWN        (1 << G_MDSFT_TEXTLUT)
#define TLUT_FMT_RGBA16         (2 << G_MDSFT_TEXTLUT)
#define TLUT_FMT_IA16           (3 << G_MDSFT_TEXTLUT)

typedef struct TxtrInfo
{
    uint32_t WidthToCreate;
    uint32_t HeightToCreate;

    uint32_t Address;
    void  *pPhysicalAddress;

    uint32_t Format;
    uint32_t Size;

    int  LeftToLoad;
    int  TopToLoad;
    uint32_t WidthToLoad;
    uint32_t HeightToLoad;
    uint32_t Pitch;

    uint8_t *PalAddress;
    uint32_t TLutFmt;
    uint32_t Palette;

    bool  bSwapped;

    uint32_t maskS;
    uint32_t maskT;

    bool  clampS;
    bool  clampT;
    bool  mirrorS;
    bool  mirrorT;

    int   tileNo;
} TxtrInfo;

/* the tile's format, palette and how it wraps */
static INLINE void TxtrInfo_SetTile(TxtrInfo *ti, const struct gDPTile *tile)
{
    ti->Format  = tile->format;
    ti->Size    = tile->size;
    ti->Palette = tile->palette;

    ti->maskS   = tile->masks;
    ti->maskT   = tile->maskt;
    ti->mirrorS = tile->mirrors;
    ti->mirrorT = tile->mirrort;
    ti->clampS  = tile->clamps;
    ti->clampT  = tile->clampt;
}

/* the same texture: all but its physical address and tile */
static INLINE bool TxtrInfo_Equal(const TxtrInfo *a, const TxtrInfo *b)
{
    return (
        a->Address == b->Address &&
        a->WidthToLoad == b->WidthToLoad &&
        a->HeightToLoad == b->HeightToLoad &&
        a->WidthToCreate == b->WidthToCreate &&
        a->HeightToCreate == b->HeightToCreate &&
        a->maskS == b->maskS &&
        a->maskT == b->maskT &&
        a->TLutFmt == b->TLutFmt &&
        a->PalAddress == b->PalAddress &&
        a->Palette == b->Palette &&
        a->LeftToLoad == b->LeftToLoad &&
        a->TopToLoad == b->TopToLoad &&
        a->Format == b->Format &&
        a->Size == b->Size &&
        a->Pitch == b->Pitch &&
        a->bSwapped == b->bSwapped &&
        a->mirrorS == b->mirrorS &&
        a->mirrorT == b->mirrorT &&
        a->clampS == b->clampS &&
        a->clampT == b->clampT
        );
}

#endif
