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
/* Rice's GL render state: depth, culling, clears, scissors and the fog
 * colour, set straight on the GL context. C89. */
#ifndef _RICE_GL_STATE_H_
#define _RICE_GL_STATE_H_

#include <stdint.h>
#include <boolean.h>

#ifdef __cplusplus
extern "C" {
#endif

/* depth tested (LEQUAL) and written, or neither (the ROM can force it on) */
void RiceGL_ZBufferEnable(bool zbuffer);
/* depth tested, or ALWAYS passes */
void RiceGL_SetZCompare(bool compare);
/* depth written */
void RiceGL_SetZUpdate(bool update);
/* polygon offset on for a positive bias; nothing when the bias is the
 * last one set */
void RiceGL_ApplyZBias(int bias);

/* the colour and/or depth buffer cleared, the depth to the fill colour's */
void RiceGL_ClearBuffer(bool cbuffer, bool zbuffer);
void RiceGL_ClearZBuffer(float depth);

void RiceGL_SetCullMode(bool cullFront, bool cullBack);

/* Resident Evil 2's 512-wide frame buffer scissor, when it applies: true
 * when it was set */
bool RiceGL_RE2Scissor(void);
/* the RDP's scissor, unless it is the one set and @force is not */
void RiceGL_ApplyRDPScissor(bool force);
/* the RSP's clip-ratio scissor, likewise */
void RiceGL_ApplyScissorWithClipRatio(bool force);
/* the viewport and the clip-ratio scissor from the RSP's viewport, or the
   render texture's, then applied */
void RiceGL_UpdateClipRectangle(void);
/* the clip-ratio scissor from the RDP's scissor, then applied */
void RiceGL_UpdateScissorWithClipRatio(void);

/* gRDP's fog colour, packed and as floats */
void RiceGL_SetFogColor(uint32_t r, uint32_t g, uint32_t b, uint32_t a);

#ifdef __cplusplus
}
#endif

#endif
