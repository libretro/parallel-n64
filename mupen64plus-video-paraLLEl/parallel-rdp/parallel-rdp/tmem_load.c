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
#include "tmem_load.h"

void rdp_decode_load(const uint32_t *words, uint32_t tex_addr, uint32_t tex_width, unsigned fmt, unsigned size,
		enum rdp_upload_mode mode, rdp_load_tile_info_t *info)
{
	memset(info, 0, sizeof(*info));
	info->tex_addr = tex_addr;
	info->tex_width = tex_width;
	info->fmt = (uint8_t)fmt;
	info->size = (uint8_t)size;
	info->slo = (uint16_t)((words[0] >> 12) & 0xfff);
	info->shi = (uint16_t)((words[1] >> 12) & 0xfff);
	info->tlo = (uint16_t)((words[0] >> 0) & 0xfff);
	info->thi = (uint16_t)((words[1] >> 0) & 0xfff);
	info->mode = mode;
}

bool rdp_load_is_empty(const rdp_load_tile_info_t *info)
{
	unsigned pixel_count;
	if (info->mode == RDP_UPLOAD_BLOCK)
	{
		pixel_count = ((unsigned)info->shi - info->slo + 1) & 0xfff;
		return !pixel_count || pixel_count > 2048;
	}
	if ((info->thi >> 2) < (info->tlo >> 2))
		return true;
	pixel_count = (((unsigned)(info->shi >> 2) - (info->slo >> 2)) + 1) & 0xfff;
	return !pixel_count;
}

void rdp_load_rdram_range(const rdp_load_tile_info_t *info, uint32_t *addr, uint32_t *size)
{
	unsigned pixel_count, offset_pixels;

	if (info->mode == RDP_UPLOAD_BLOCK)
	{
		pixel_count = ((unsigned)info->shi - info->slo + 1) & 0xfff;
		offset_pixels = info->slo + info->tex_width * info->tlo;
	}
	else
	{
		const unsigned max_x = ((unsigned)(info->shi >> 2) - (info->slo >> 2)) & 0xfff;
		const unsigned max_y = (unsigned)(info->thi >> 2) - (info->tlo >> 2);
		pixel_count = max_y * info->tex_width + max_x + 1;
		offset_pixels = (info->slo >> 2) + info->tex_width * (info->tlo >> 2);
	}

	/* (bytes: a 4-bit pixel is half of one) */
	*size = (((pixel_count << info->size) >> 1) + 7) & ~7u;
	*addr = info->tex_addr + ((offset_pixels << info->size) >> 1);
}

unsigned rdp_load_lines_per_part(const rdp_load_tile_info_t *info, const rdp_tile_meta_t *meta)
{
	const unsigned pixels_per_line = (((unsigned)(info->shi >> 2) - (info->slo >> 2)) + 1) & 0xfff;
	/* Technically, 32-bpp TMEM upload and YUV upload will work like 16bpp,
	 * just split into two halves, but that also means we get 2kB
	 * wraparound instead of 4kB wraparound, so this works out just fine
	 * for our purposes. */
	unsigned quad_words_per_line = ((pixels_per_line << meta->size) + 15) >> 4;
	unsigned bytes_per_line, max_bytes, lines, lines_per_part;

	if (info->mode != RDP_UPLOAD_TILE)
		return RDP_LOAD_WHOLE;

	/* Deal with mismatch in state, there is no reasonable scenarios where
	 * this should even matter, but you never know ... */
	if (meta->size > info->size)
		quad_words_per_line <<= meta->size - info->size;
	else if (meta->size < info->size)
		quad_words_per_line >>= info->size - meta->size;

	/* A conservative estimate for how many bytes we're going to splat
	 * down into TMEM. */
	bytes_per_line = quad_words_per_line * 8;
	if (bytes_per_line < meta->stride)
		bytes_per_line = meta->stride;

	/* We need to write lower and upper halves at once, so we need to wrap
	 * around at 2k boundary. */
	max_bytes = meta->fmt == RDP_TEXTURE_FORMAT_YUV ? 0x800 : 0x1000;
	lines = (unsigned)(info->thi >> 2) - (info->tlo >> 2) + 1;
	if (bytes_per_line * lines <= max_bytes)
		return RDP_LOAD_WHOLE;

	/* The game wants to write more than 4k of texture data to TMEM in one
	 * go: split it up, keeping the T state aligned. One line of more than
	 * that could be split horizontally, but no content does it. */
	lines_per_part = (max_bytes / bytes_per_line) & ~1u;
	return lines_per_part ? lines_per_part : RDP_LOAD_TOO_WIDE;
}

/* LoadBlock is kinda awkward. Rather than specifying width and height, we
 * get width and dTdx. dTdx will increment and generate a T coordinate
 * based on S coordinate (T = (S_64bpp_word * dTdx) >> 11). The stride is
 * added on top of this, so effective stride is stride(T) + stride(tile).
 * Usually it makes sense for stride(tile) to be 0, but it doesn't have to
 * be ... The only reasonable solution is to try to decompose this mess
 * into a normal width/height/stride. In the general dTdx case, we don't
 * have to deduce a stable value for stride. If dTdx is very weird, we
 * might get variable stride, which is near-impossible to deal with.
 * However, it makes zero sense for content to actually rely on this
 * behavior. Even if there are inaccuracies in the fraction, we always
 * floor it to get T, and thus we'll have to run for quite some time to
 * observe the fractional error accumulate. */
static enum rdp_load_result block_upload(const rdp_load_tile_info_t *info, const rdp_tile_meta_t *meta,
		rdp_upload_info_t *upload, unsigned *mode)
{
	const unsigned pixel_count = ((unsigned)info->shi - info->slo + 1) & 0xfff;
	const unsigned dt = info->thi;
	const unsigned max_tmem_iteration = (pixel_count - 1) >> (4u - info->size);
	const unsigned max_t = (max_tmem_iteration * dt) >> 11;
	unsigned max_elements, min_elements, max_line_stride, min_line_stride;
	bool uneven_dt;

	upload->width = (int32_t)pixel_count;
	upload->height = 1;

	if (max_t == 0)
	{
		/* We never trigger a case where T is non-zero, so this is
		 * equivalent to a Tile upload. */
		upload->tmem_stride_words = 0;
		*mode = RDP_UPLOAD_TILE;
		return RDP_LOAD_OK;
	}

	/* dT is an inverse which is not necessarily accurate, we can end up
	 * with an uneven amount of texels per "line". If we have stride == 0,
	 * this is fairly easy to deal with, but for the case where stride !=
	 * 0, it is very difficult to implement it correctly. We will need to
	 * solve this kind of equation for X:
	 *   TMEM word = floor((x * dt) / 2048) * stride + x
	 * This equation has no solutions for cases where we stride over TMEM
	 * words. The only way I can think of is to test all candidates for the
	 * floor() expression, and see if that is a valid solution. We can find
	 * an conservative estimate for floor() by:
	 *   t_min = TMEM word / (max_num_64bpp_elements + stride)
	 *   t_max = TMEM word / (min_num_64bpp_elements + stride) */
	max_elements = ((1u << 11u) + dt - 1u) / dt;
	min_elements = (1u << 11u) / dt;
	uneven_dt = max_elements != min_elements;

	if (uneven_dt)
	{
		/* If we never get rounding errors, we can handwave this issue away
		 * and pretend that min == max iterations. This is by far the common
		 * case. Each overflow into next T adds a certain amount of error;
		 * multiplied by the maximum value of T we can observe, a
		 * conservative estimate for our T error. If this error is less than
		 * 1 step of dt, we can be certain that we will get max_num
		 * iterations every time, and we can ignore the worst edge cases. */
		if ((dt * max_elements - (1u << 11)) * max_t < dt)
		{
			min_elements = max_elements;
			uneven_dt = false;
		}
	}

	/* Add more precision bits to DXT. We might have to shift it down if we
	 * have a meta.size fixup down below. Also makes the right shift nicer
	 * (16 vs 11). */
	upload->dxt = (int32_t)(dt << 5);

	if (meta->size == RDP_TEXTURE_SIZE_32 || meta->fmt == RDP_TEXTURE_FORMAT_YUV)
	{
		/* We iterate twice for Bpp32 and YUV to complete a 64bpp word. */
		upload->tmem_stride_words <<= 1;

		/* Pure, utter insanity, but no content should *ever* hit this ... */
		if (uneven_dt && meta->size != info->size)
			return RDP_LOAD_SKIPPED;
	}

	/* If TMEM and VRAM bpp misalign, we need to fixup this since we step
	 * too fast or slow. */
	if (meta->size > info->size)
	{
		const unsigned shamt = meta->size - info->size;
		max_elements <<= shamt;
		min_elements <<= shamt;
		/* Need to step slower so we can handle the added striding. */
		upload->dxt >>= shamt;
	}
	else if (info->size > meta->size)
	{
		/* Here we step multiple times over the same pixel, but potentially
		 * with different T state, since dTdx applies between the
		 * iterations. Unsupported. */
		return RDP_LOAD_SKIPPED;
	}

	/* Multiplying 64bpp TMEM word by these gives us lower and upper bounds
	 * for T. These serve as candidate expressions for floor(). */
	max_line_stride = max_elements + ((unsigned)upload->tmem_stride_words >> 2);
	min_line_stride = min_elements + ((unsigned)upload->tmem_stride_words >> 2);
	upload->min_t_mod = 1.0f / (float)max_line_stride;
	upload->max_t_mod = 1.0f / (float)min_line_stride;

	/* Stride in 64bpp instead of 16bpp. */
	upload->tmem_stride_words >>= 2;
	return RDP_LOAD_OK;
}

enum rdp_load_result rdp_load_upload_info(const rdp_load_tile_info_t *info, const rdp_tile_meta_t *meta,
		uint32_t tmem_offset, rdp_upload_info_t *upload)
{
	unsigned mode = info->mode;
	uint32_t upload_x, upload_y;

	memset(upload, 0, sizeof(*upload));

	/* Only 16bpp is supported for YUV uploads. */
	if (meta->fmt == RDP_TEXTURE_FORMAT_YUV && (meta->size != RDP_TEXTURE_SIZE_16 || info->size != RDP_TEXTURE_SIZE_16))
		return RDP_LOAD_SKIPPED;

	/* A 4-bit VRAM pointer crashes the RDP. */
	if (info->size == RDP_TEXTURE_SIZE_4)
		return RDP_LOAD_CRASHES;

	/* 32bpp tile uploads must use the RGBA texture format, unsupported
	 * otherwise; nor TLUT uploads to 32bpp tiles. */
	if (meta->size == RDP_TEXTURE_SIZE_32 && meta->fmt != RDP_TEXTURE_FORMAT_RGBA)
		return RDP_LOAD_SKIPPED;
	if (mode == RDP_UPLOAD_TLUT && meta->size == RDP_TEXTURE_SIZE_32)
		return RDP_LOAD_SKIPPED;

	/* A 32-bit texture into an 8 or 4-bit tile, or a 16-bit one into a
	 * 4-bit tile, creates insane results: unsupported. */
	if (mode != RDP_UPLOAD_TLUT && ((info->size == RDP_TEXTURE_SIZE_32 && meta->size <= RDP_TEXTURE_SIZE_8) ||
			(info->size == RDP_TEXTURE_SIZE_16 && meta->size == RDP_TEXTURE_SIZE_4)))
		return RDP_LOAD_SKIPPED;

	upload->tmem_stride_words = (int32_t)(meta->stride >> 1);

	if (mode == RDP_UPLOAD_BLOCK)
	{
		enum rdp_load_result result;
		upload_x = info->slo;
		upload_y = info->tlo;
		result = block_upload(info, meta, upload, &mode);
		if (result != RDP_LOAD_OK)
			return result;
	}
	else
	{
		upload_x = info->slo >> 2;
		upload_y = info->tlo >> 2;
		upload->width = (int32_t)((((unsigned)(info->shi >> 2) - (info->slo >> 2)) + 1) & 0xfff);
		upload->height = (int32_t)((unsigned)(info->thi >> 2) - (info->tlo >> 2) + 1);
	}

	if (!upload->width)
		return RDP_LOAD_SKIPPED;

	switch (info->size)
	{
	case RDP_TEXTURE_SIZE_8:
		upload->vram_effective_width = (upload->width + 7) & ~7;
		break;

	case RDP_TEXTURE_SIZE_16:
		/* In 16-bit VRAM pointer with TLUT, we iterate one texel at a time, not 4. */
		if (mode == RDP_UPLOAD_TLUT)
			upload->vram_effective_width = upload->width;
		else
			upload->vram_effective_width = (upload->width + 3) & ~3;
		break;

	default:
		upload->vram_effective_width = (upload->width + 1) & ~1;
		break;
	}

	/* Uploads happen in chunks of 8 bytes in groups of 4x16-bits. */
	switch (meta->size)
	{
	case RDP_TEXTURE_SIZE_4:
		upload->width = ((upload->width + 15) & ~15) >> 2;
		break;

	case RDP_TEXTURE_SIZE_8:
		upload->width = ((upload->width + 7) & ~7) >> 1;
		break;

	case RDP_TEXTURE_SIZE_16:
		upload->width = (upload->width + 3) & ~3;
		/* Consider YUV uploads to be 32bpp since that's kinda what they are. */
		if (meta->fmt == RDP_TEXTURE_FORMAT_YUV)
			upload->width >>= 1;
		break;

	default:
		upload->width = (upload->width + 1) & ~1;
		break;
	}

	/* Load TLUT with height > 1 is not supported. */
	if (upload->height > 1 && mode == RDP_UPLOAD_TLUT)
		return RDP_LOAD_SKIPPED;

	upload->vram_addr = (int32_t)(info->tex_addr + ((info->tex_width * upload_y + upload_x) << (info->size - 1)));
	upload->vram_width = mode == RDP_UPLOAD_BLOCK ? upload->vram_effective_width : (int32_t)info->tex_width;
	upload->vram_size = info->size;

	upload->tmem_offset = (int32_t)((meta->offset + tmem_offset) & 0xfff);
	upload->tmem_size = meta->size;
	upload->tmem_fmt = meta->fmt;
	upload->mode = (int32_t)mode;

	upload->inv_tmem_stride_words = 1.0f / (float)upload->tmem_stride_words;
	return RDP_LOAD_OK;
}
