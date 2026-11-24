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
/* Rice's GL context state: clears, frame hand-off and the state set up
 * when the context is (re)made. C89. */
#ifndef GFXCONTEXT_H
#define GFXCONTEXT_H

#include <stdint.h>
#include <boolean.h>

enum ClearFlag
{
    CLEAR_COLOR_BUFFER           = 0x01,
    CLEAR_DEPTH_BUFFER           = 0x02,
    CLEAR_COLOR_AND_DEPTH_BUFFER = 0x03
};

#ifdef __cplusplus
extern "C" {
#endif

/* set when a frame ended with bForceScreenClear on: the next display list
 * clears the colour buffer first (RSP_Parser.cpp) */
extern bool rice_gc_need_clean_scene;

/* the context came up (for a game, or anew at another size): the
 * frame-buffer manager, the GL state, two cleared frames */
void RiceGC_Initialize(void);
/* the context is gone (the next one gets a vertex array object anew) */
void RiceGC_Release(void);

void RiceGC_Clear(unsigned flags, uint32_t color, float depth);
/* a frame is done: the render callback, the hand-off to the frontend */
void RiceGC_UpdateFrame(void);

#ifdef __cplusplus
}
#endif

#endif
