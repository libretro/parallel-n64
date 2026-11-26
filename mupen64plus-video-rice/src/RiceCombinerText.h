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
/* Rice's combiner programs as text: what a decoded mux computes, in
 * GLSL. C89. */
#ifndef _RICE_COMBINER_TEXT_H_
#define _RICE_COMBINER_TEXT_H_

#include <boolean.h>

#include "DecodedMux.h"

#ifdef __cplusplus
extern "C" {
#endif

/* the fragment program text for @mux (reformatted here), and the
 * texels it samples (either may be NULL); @out holds
 * RICE_COMBINER_TEXT_SIZE. The #version, FOG and ALPHA_TEST lines go
 * before it. */
#define RICE_COMBINER_TEXT_SIZE 4092
void RiceCombiner_ProgramText(DecodedMux *mux, char *out, bool *needT0, bool *needT1);

#ifdef __cplusplus
}
#endif

#endif
