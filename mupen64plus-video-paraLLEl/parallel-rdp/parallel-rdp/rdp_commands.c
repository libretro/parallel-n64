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


#include <string.h>
#include "rdp_commands.h"
#include "primitive_decode.h"
#include "tmem_load.h"

void rdp_commands_init(rdp_commands_t *c, rdp_renderer_t *renderer, rdp_upscaler_t *upscaler)
{
	memset(c, 0, sizeof(*c));
	c->renderer = renderer;
	c->upscaler = upscaler;
	c->quirks = RDP_DECODE_NATIVE_TEX_RECT;
}

void rdp_commands_set_quirks(rdp_commands_t *c, unsigned quirks)
{
	c->quirks = quirks & RDP_COMMANDS_QUIRKS;
}

/* What of the state a primitive's decoding depends on */
static unsigned decode_flags(const rdp_commands_t *c)
{
	unsigned flags = c->quirks;
	if (c->static_state.flags & RASTERIZATION_COPY_BIT)
		flags |= RDP_DECODE_COPY_CYCLE;
	if (c->static_state.flags & RASTERIZATION_FILL_BIT)
		flags |= RDP_DECODE_FILL_CYCLE;
	return flags;
}

typedef void (*rdp_command_fn)(rdp_commands_t *c, const uint32_t *words);

/* The eight triangle commands, 0x08 to 0x0f */
static void op_triangle(rdp_commands_t *c, const uint32_t *words)
{
	rdp_triangle_setup_t setup;
	rdp_attribute_setup_t attr;
	rdp_decode_triangle(words, decode_flags(c), &setup, &attr);
	rdp_upscaler_view_triangle(c->upscaler, &c->upscaler_view, (words[0] >> 24) & 63, &c->static_state,
			&c->depth_blend, rdp_renderer_scaling_factor(c->renderer), &setup, &attr);
	rdp_renderer_draw(c->renderer, &setup, &attr);
}

static void rectangle(rdp_commands_t *c, const rdp_triangle_setup_t *setup)
{
	rdp_upscaler_rectangle(c->upscaler, (uint32_t)setup->xh >> 13, (uint32_t)setup->yh, (uint32_t)setup->xl >> 13,
			(uint32_t)setup->yl);
}

static void texture_rectangle(rdp_commands_t *c, const uint32_t *words, bool flip)
{
	rdp_triangle_setup_t setup;
	rdp_attribute_setup_t attr;
	rdp_decode_texture_rectangle(words, flip, decode_flags(c), &setup, &attr);
	rectangle(c, &setup);
	rdp_renderer_draw(c->renderer, &setup, &attr);
}

static void op_texture_rectangle(rdp_commands_t *c, const uint32_t *words)
{
	texture_rectangle(c, words, false);
}

static void op_texture_rectangle_flip(rdp_commands_t *c, const uint32_t *words)
{
	texture_rectangle(c, words, true);
}

static void op_fill_rectangle(rdp_commands_t *c, const uint32_t *words)
{
	rdp_triangle_setup_t setup;
	rdp_decode_fill_rectangle(words, decode_flags(c), &setup);
	rectangle(c, &setup);
	rdp_renderer_draw(c->renderer, &setup, NULL);
}

static void op_sync_full(rdp_commands_t *c, const uint32_t *words)
{
	(void)words;
	rdp_renderer_flush_and_signal(c->renderer);
}

static void op_set_key_gb(rdp_commands_t *c, const uint32_t *words)
{
	rdp_color_key_t g, b;
	rdp_decode_key_gb(words, &g, &b);
	rdp_renderer_set_color_key(c->renderer, 1, g.width, g.center, g.scale);
	rdp_renderer_set_color_key(c->renderer, 2, b.width, b.center, b.scale);
}

static void op_set_key_r(rdp_commands_t *c, const uint32_t *words)
{
	rdp_color_key_t r;
	rdp_decode_key_r(words, &r);
	rdp_renderer_set_color_key(c->renderer, 0, r.width, r.center, r.scale);
}

static void op_set_convert(rdp_commands_t *c, const uint32_t *words)
{
	uint16_t k[6];
	rdp_decode_convert(words, k);
	rdp_renderer_set_convert(c->renderer, k[0], k[1], k[2], k[3], k[4], k[5]);
}

static void op_set_scissor(rdp_commands_t *c, const uint32_t *words)
{
	rdp_decode_scissor(words, &c->scissor, &c->static_state.flags);
	rdp_renderer_set_scissor_state(c->renderer, &c->scissor);
	rdp_renderer_set_static_state(c->renderer, &c->static_state);
}

static void op_set_prim_depth(rdp_commands_t *c, const uint32_t *words)
{
	rdp_renderer_set_primitive_depth(c->renderer, (uint16_t)(words[1] >> 16), (uint16_t)words[1]);
	c->upscaler_view.prim_z = (int32_t)(((words[1] >> 16) & 0x7fff) << 16);
}

static void op_set_other_modes(rdp_commands_t *c, const uint32_t *words)
{
	c->upscaler_view.prim_depth = rdp_decode_other_modes(words, &c->static_state, &c->depth_blend);
	rdp_renderer_set_static_state(c->renderer, &c->static_state);
	rdp_renderer_set_depth_blend_state(c->renderer, &c->depth_blend);
	rdp_renderer_set_enable_primitive_depth(c->renderer, c->upscaler_view.prim_depth);
	c->upscaler_view.other_modes[0] = words[0];
	c->upscaler_view.other_modes[1] = words[1];
}

static void load(rdp_commands_t *c, const uint32_t *words, enum rdp_upload_mode mode)
{
	rdp_load_tile_info_t info;
	rdp_decode_load(words, c->texture_image.addr, c->texture_image.width, c->texture_image.fmt,
			c->texture_image.size, mode, &info);
	c->upscaler_view.load_addr = c->texture_image.addr;
	rdp_renderer_load_tile(c->renderer, (words[1] >> 24) & 7, &info);
}

static void op_load_tlut(rdp_commands_t *c, const uint32_t *words)
{
	load(c, words, RDP_UPLOAD_TLUT);
}

static void op_load_block(rdp_commands_t *c, const uint32_t *words)
{
	load(c, words, RDP_UPLOAD_BLOCK);
}

static void op_load_tile(rdp_commands_t *c, const uint32_t *words)
{
	load(c, words, RDP_UPLOAD_TILE);
}

static void op_set_tile_size(rdp_commands_t *c, const uint32_t *words)
{
	rdp_tile_size_t size;
	unsigned tile = rdp_decode_tile_size(words, &size);
	rdp_renderer_set_tile_size(c->renderer, tile, size.slo, size.shi, size.tlo, size.thi);
}

static void op_set_tile(rdp_commands_t *c, const uint32_t *words)
{
	rdp_tile_meta_t meta;
	unsigned tile = rdp_decode_tile(words, &meta);
	c->upscaler_view.tile[tile & 7][0] = words[0];
	c->upscaler_view.tile[tile & 7][1] = words[1];
	rdp_renderer_set_tile(c->renderer, tile, &meta);
}

static void op_set_fill_color(rdp_commands_t *c, const uint32_t *words)
{
	rdp_renderer_set_fill_color(c->renderer, words[1]);
}

static void op_set_fog_color(rdp_commands_t *c, const uint32_t *words)
{
	rdp_renderer_set_fog_color(c->renderer, words[1]);
}

static void op_set_blend_color(rdp_commands_t *c, const uint32_t *words)
{
	rdp_renderer_set_blend_color(c->renderer, words[1]);
}

static void op_set_prim_color(rdp_commands_t *c, const uint32_t *words)
{
	uint8_t min_level, lod_frac;
	uint32_t color;
	rdp_decode_prim_color(words, &min_level, &lod_frac, &color);
	rdp_renderer_set_primitive_color(c->renderer, min_level, lod_frac, color);
}

static void op_set_env_color(rdp_commands_t *c, const uint32_t *words)
{
	rdp_renderer_set_env_color(c->renderer, words[1]);
}

static void op_set_combine(rdp_commands_t *c, const uint32_t *words)
{
	rdp_decode_combine(words, &c->static_state);
	c->upscaler_view.combine[0] = words[0];
	c->upscaler_view.combine[1] = words[1];
	rdp_renderer_set_static_state(c->renderer, &c->static_state);
}

static void op_set_texture_image(rdp_commands_t *c, const uint32_t *words)
{
	rdp_decode_texture_image(words, &c->texture_image);
}

static void op_set_mask_image(rdp_commands_t *c, const uint32_t *words)
{
	rdp_renderer_set_depth_framebuffer(c->renderer, words[1] & 0xffffff);
}

static void op_set_color_image(rdp_commands_t *c, const uint32_t *words)
{
	uint32_t addr, width, fmt;
	unsigned size;
	rdp_decode_color_image(words, &addr, &width, &fmt, &size);
	rdp_upscaler_set_color_image(c->upscaler, addr, width, size == 3 ? 2 : size == 2 ? 1 : -1);
	rdp_renderer_set_color_framebuffer(c->renderer, addr, width, fmt);
}

/* (sync load, pipe and tile: the renderer orders what they order itself) */
static const rdp_command_fn commands[64] = {
	/* 0x00 */ NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	/* 0x08 */ op_triangle, op_triangle, op_triangle, op_triangle,
	/* 0x0c */ op_triangle, op_triangle, op_triangle, op_triangle,
	/* 0x10 */ NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	/* 0x18 */ NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	/* 0x20 */ NULL, NULL, NULL, NULL,
	/* 0x24 */ op_texture_rectangle, op_texture_rectangle_flip, NULL, NULL,
	/* 0x28 */ NULL, op_sync_full, op_set_key_gb, op_set_key_r,
	/* 0x2c */ op_set_convert, op_set_scissor, op_set_prim_depth, op_set_other_modes,
	/* 0x30 */ op_load_tlut, NULL, op_set_tile_size, op_load_block,
	/* 0x34 */ op_load_tile, op_set_tile, op_fill_rectangle, op_set_fill_color,
	/* 0x38 */ op_set_fog_color, op_set_blend_color, op_set_prim_color, op_set_env_color,
	/* 0x3c */ op_set_combine, op_set_texture_image, op_set_mask_image, op_set_color_image
};

void rdp_commands_run(rdp_commands_t *c, const uint32_t *words)
{
	rdp_command_fn fn = commands[(words[0] >> 24) & 63];
	if (fn)
		fn(c, words);
}
