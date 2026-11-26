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
/* Rice's colour combiner: a GL program each decoded mux, fog and alpha
 * test, built from the journal at load. C89. */
#ifndef _RICE_COMBINER_H_
#define _RICE_COMBINER_H_

#include <stdint.h>
#include <boolean.h>

#include "osal_opengl.h"

#include "DecodedMux.h"

#ifdef __cplusplus
extern "C" {
#endif

/* programs kept; past this the oldest is replaced */
#define RICE_COMBINER_MAX_PROGRAMS 2048

typedef struct RiceCombinerProgram
{
    uint32_t dwMux0;
    uint32_t dwMux1;

    bool     fogIsUsed;
    bool     alphaTest;
    GLuint   fragmentShaderID;
    GLuint   vertexShaderID;
    GLuint   programID;

    GLint    PrimColorLocation;
    GLint    EnvColorLocation;
    GLint    PrimFracLocation;
    GLint    EnvFracLocation;
    GLint    AlphaRefLocation;
    GLint    FogColorLocation;
    GLint    FogMinMaxLocation;

    /* the values last set, so an unchanged one is not set again */
    float    PrimColors[4];
    float    EnvColors[4];
    float    PrimLODFrac;
    float    EnvLODFrac;
    float    AlphaRef;
    float    FogColors[4];
    float    FogMin;
    float    FogMax;
} RiceCombinerProgram;

struct combiner_journal;

typedef struct RiceCombiner
{
    DecodedMux      mux;
    DecodedMuxCache muxCache;

    /* the mux samples texel 0 / texel 1 */
    bool     tex0Enabled;
    bool     tex1Enabled;
    /* the last mode set was COPY or FILL */
    bool     cycleChanged;

    /* fog and alpha test the next program is for */
    bool     fog;
    bool     alphaTest;
    bool     fogPrevious;
    bool     alphaTestPrevious;
    float    alphaRef;

    int      lastIndex;
    uint32_t lastMux0;
    uint32_t lastMux1;
    GLuint   currentProgram;

    RiceCombinerProgram *programs;
    int      numPrograms;
    /* the slot replaced next once full */
    int      nextReplaced;

    struct combiner_journal *journal;
} RiceCombiner;

extern RiceCombiner rice_combiner;

/* the copy and fill programs, the tables (the renderer's construction) */
void RiceCombiner_Create(void);
/* every program and the journal gone */
void RiceCombiner_Destroy(void);
/* each key an earlier session of this game met, built */
void RiceCombiner_BuildKnownKeys(void);

/* @dwMux0 / @dwMux1 decoded, the texel flags with them */
void RiceCombiner_Update(uint32_t dwMux0, uint32_t dwMux1);
/* the program for the cycle type, the mux, fog and alpha test, with
 * @dwAlpha the alpha reference (0-255) */
void RiceCombiner_InitMode(uint32_t dwAlpha);
/* the copy program, nothing discarded, @tile's texture linear and
 * clamped, for a CPU-written frame */
void RiceCombiner_InitSimpleTextureDraw(uint32_t tile);
/* the wrap modes of the tiles the mux samples */
void RiceCombiner_SetTexelRepeat(void);

#ifdef __cplusplus
}
#endif

#endif
