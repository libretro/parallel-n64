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

#ifndef RDP_TMEM_LOAD_H
#define RDP_TMEM_LOAD_H

#include <stdint.h>
#include <boolean.h>
#include <retro_common_api.h>
#include "rdp_state.h"

RETRO_BEGIN_DECLS

/* The three loads into TMEM, as the shaders number them */
enum rdp_upload_mode
{
	RDP_UPLOAD_TILE = 0,
	RDP_UPLOAD_TLUT = 1,
	RDP_UPLOAD_BLOCK = 2
};

/* A load command with the texture image it reads from */
typedef struct rdp_load_tile_info
{
	uint32_t tex_addr;
	uint32_t tex_width;
	/* 10.2 for tile and TLUT loads; for a block, texels and dT/dS */
	uint16_t slo, tlo, shi, thi;
	/* enum rdp_texture_format, rdp_texture_size */
	uint8_t fmt;
	uint8_t size;
	/* enum rdp_upload_mode */
	uint32_t mode;
} rdp_load_tile_info_t;

/* One load as the TMEM update shader runs it: 64 bytes */
typedef struct rdp_upload_info
{
	int32_t width, height;
	float min_t_mod, max_t_mod;

	int32_t vram_addr;
	int32_t vram_width;
	int32_t vram_size;
	int32_t vram_effective_width;

	int32_t tmem_offset;
	int32_t tmem_stride_words;
	int32_t tmem_size;
	int32_t tmem_fmt;

	int32_t mode;
	float inv_tmem_stride_words;
	int32_t dxt;
	int32_t padding;
} rdp_upload_info_t;

/* Load tile (0x34), load TLUT (0x30) and load block (0x33): @words with
 * the texture image set before */
void rdp_decode_load(const uint32_t *words, uint32_t tex_addr, uint32_t tex_width, unsigned fmt, unsigned size,
		enum rdp_upload_mode mode, rdp_load_tile_info_t *info);

/* The load loads nothing */
bool rdp_load_is_empty(const rdp_load_tile_info_t *info);

/* The bytes of RDRAM a load that is not empty reads, from @addr on: a
 * multiple of 8 */
void rdp_load_rdram_range(const rdp_load_tile_info_t *info, uint32_t *addr, uint32_t *size);

#define RDP_LOAD_WHOLE 0u
#define RDP_LOAD_TOO_WIDE (~0u)

/* A tile load writes at most 4 KiB of TMEM in one go (2 KiB for YUV): how
 * many lines each part of it is, RDP_LOAD_WHOLE when it fits, or
 * RDP_LOAD_TOO_WIDE when one line does not */
unsigned rdp_load_lines_per_part(const rdp_load_tile_info_t *info, const rdp_tile_meta_t *meta);

enum rdp_load_result
{
	RDP_LOAD_OK = 0,
	/* not something this implementation loads, or nothing to load */
	RDP_LOAD_SKIPPED,
	/* a 4-bit texture image: the RDP crashes */
	RDP_LOAD_CRASHES
};

/* @info into the tile described by @meta, @tmem_offset bytes past its
 * start, as the shader runs it */
enum rdp_load_result rdp_load_upload_info(const rdp_load_tile_info_t *info, const rdp_tile_meta_t *meta,
		uint32_t tmem_offset, rdp_upload_info_t *upload);

RETRO_END_DECLS

#endif
