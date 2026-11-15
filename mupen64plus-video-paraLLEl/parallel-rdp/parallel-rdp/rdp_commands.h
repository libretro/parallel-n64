/* Copyright (c) 2020 Themaister
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */


#ifndef RDP_COMMANDS_H
#define RDP_COMMANDS_H

/* The RDP's commands, 0x08 to 0x3f, run: each decoded and handed to the
 * renderer, with the state the next ones decode by and what the temporal
 * upscaler keeps of it. C89. */

#include <stdint.h>
#include <boolean.h>
#include <retro_common_api.h>
#include "rdp_state.h"
#include "rdp_command_decode.h"
#include "rdp_renderer.h"
#include "rdp_upscaler_view.h"
#include "temporal_upscaler.h"

RETRO_BEGIN_DECLS

/* The upscaling quirks, as rdp_decode_* flags: only these two */
#define RDP_COMMANDS_QUIRKS (RDP_DECODE_NATIVE_TEXTURE_LOD | RDP_DECODE_NATIVE_TEX_RECT)

typedef struct rdp_commands
{
	rdp_renderer_t *renderer;
	/* or NULL */
	rdp_upscaler_t *upscaler;
	rdp_upscaler_view_t upscaler_view;

	rdp_scissor_state_t scissor;
	rdp_static_state_t static_state;
	rdp_depth_blend_state_t depth_blend;
	rdp_texture_image_t texture_image;
	/* RDP_COMMANDS_QUIRKS */
	unsigned quirks;
} rdp_commands_t;

/* No state set, the quirks as RDP::Quirks starts them (texture
 * rectangles at native resolution) */
void rdp_commands_init(rdp_commands_t *c, rdp_renderer_t *renderer, rdp_upscaler_t *upscaler);

/* @quirks: RDP_DECODE_NATIVE_TEXTURE_LOD and RDP_DECODE_NATIVE_TEX_RECT */
void rdp_commands_set_quirks(rdp_commands_t *c, unsigned quirks);

/* Runs the command @words starts. Opcodes 0 to 7 (the processor's own)
 * and those the RDP does not have run nothing. */
void rdp_commands_run(rdp_commands_t *c, const uint32_t *words);

RETRO_END_DECLS

#endif
