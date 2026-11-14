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
#include "rdp_queue.h"

/* FBFormat */
enum
{
	FB_I4 = 0,
	FB_I8,
	FB_RGBA5551,
	FB_IA88,
	FB_RGBA8888
};

/* The tallest primitive: 1024 lines */
#define MAX_HEIGHT 1024

static int imin(int a, int b)
{
	return a < b ? a : b;
}

static int imax(int a, int b)
{
	return a > b ? a : b;
}

/* @element's index among @count of @size bytes at @elements, appended
 * when it is not there: the one found last first, then from the most
 * recent back */
static unsigned cache_add(void *elements, unsigned *count, int *last, size_t size, const void *element)
{
	uint8_t *base = (uint8_t *)elements;
	int i;

	if (*last >= 0 && memcmp(base + (size_t)*last * size, element, size) == 0)
		return (unsigned)*last;
	for (i = (int)*count - 1; i >= 0; i--)
		if (memcmp(base + (size_t)i * size, element, size) == 0)
		{
			*last = i;
			return (unsigned)i;
		}
	memcpy(base + (size_t)*count * size, element, size);
	*last = (int)*count;
	return (*count)++;
}

void rdp_queue_init(rdp_queue_t *q)
{
	memset(q, 0, sizeof(*q));
	rdp_queue_reset(q);
}

void rdp_queue_reset(rdp_queue_t *q)
{
	q->num_static_states = 0;
	q->last_static_state = -1;
	q->num_depth_blend_states = 0;
	q->last_depth_blend_state = -1;
	q->num_tile_infos = 0;
	q->last_tile_info = -1;
	q->num_primitives = 0;
	q->num_span_jobs = 0;
	q->num_uploads = 0;
	q->max_shaded_tiles = 0;
	q->height = 0;
	q->color_write_pending = false;
	q->depth_write_pending = false;
}

void rdp_queue_bind(rdp_queue_t *q, void *const mapped[RDP_STREAM_BUFFERS])
{
	q->triangles = (rdp_triangle_setup_t *)mapped[RDP_STREAM_TRIANGLE_SETUP];
	q->attributes = (rdp_attribute_setup_t *)mapped[RDP_STREAM_ATTRIBUTE_SETUP];
	q->derived = (rdp_derived_setup_t *)mapped[RDP_STREAM_DERIVED_SETUP];
	q->scissors = (rdp_scissor_state_t *)mapped[RDP_STREAM_SCISSOR_SETUP];
	q->indices = (rdp_instance_indices_t *)mapped[RDP_STREAM_STATE_INDICES];
	q->span_offsets = (rdp_span_info_offsets_t *)mapped[RDP_STREAM_SPAN_INFO_OFFSETS];
	q->span_jobs = (rdp_span_job_t *)mapped[RDP_STREAM_SPAN_INFO_JOBS];
}

/* The span interpolation jobs of @setup's lines inside the scissor */
static rdp_span_info_offsets_t add_span_jobs(rdp_queue_t *q, const rdp_triangle_setup_t *setup)
{
	const int min_line = imax(setup->yh, (int)q->scissor.ylo) >> 2;
	const int max_line = imin(setup->yl - 1, (int)q->scissor.yhi - 1) >> 2;
	rdp_span_info_offsets_t offsets;
	int height, jobs, i;

	memset(&offsets, 0, sizeof(offsets));
	if (max_line < min_line)
	{
		offsets.yhi = -1;
		return offsets;
	}

	/* the next line too: some workarounds look at it */
	height = imin(imax(max_line - min_line + 2, 0), MAX_HEIGHT);
	jobs = (height + (int)RDP_QUEUE_SPAN_JOB_LINES - 1) / (int)RDP_QUEUE_SPAN_JOB_LINES;

	offsets.offset = (int32_t)(q->num_span_jobs * RDP_QUEUE_SPAN_JOB_LINES);
	offsets.ylo = min_line;
	offsets.yhi = max_line;
	for (i = 0; i < jobs; i++)
	{
		rdp_span_job_t *job = &q->span_jobs[q->num_span_jobs++];
		job->primitive_index = (uint16_t)q->num_primitives;
		job->base_y = (uint16_t)(min_line + (int)RDP_QUEUE_SPAN_JOB_LINES * i);
		job->max_y = (uint16_t)(max_line + 1);
		job->padding = 0;
	}
	return offsets;
}

void rdp_queue_add_primitive(rdp_queue_t *q, const rdp_renderer_caps_t *caps, const rdp_constants_t *constants,
		const rdp_tile_info_t *tiles, const rdp_triangle_setup_t *setup_in, const rdp_attribute_setup_t *attr)
{
	static const rdp_attribute_setup_t flat;
	rdp_triangle_setup_t setup = *setup_in;
	rdp_attribute_setup_t *attributes;
	rdp_instance_indices_t indices;
	rdp_static_state_t normalized;
	const unsigned n = q->num_primitives;
	unsigned i;
	int lowest;

	if (!attr)
		attr = &flat;

	rdp_fixup_triangle_setup(&setup, q->static_state.flags);
	/* (no early out for a primitive with no tiles: it would throw off
	 * the seeding of the noise) */
	if (!caps->ubershader)
		q->max_shaded_tiles += rdp_conservative_tile_count(&setup, &q->scissor, caps->upscaling);

	lowest = imax((imin(setup.yl - 1, (int)q->scissor.yhi - 1) >> 2) + 1, 0);
	if ((uint32_t)lowest > q->height)
		q->height = (uint32_t)lowest;

	q->span_offsets[n] = add_span_jobs(q, &setup);
	q->triangles[n] = setup;

	attributes = &q->attributes[n];
	*attributes = *attr;
	if (constants->use_prim_depth)
	{
		attributes->z = constants->prim_depth;
		attributes->dzdx = 0;
		attributes->dzde = 0;
		attributes->dzdy = 0;
	}

	rdp_derived_setup(constants, &q->static_state, attr, &q->derived[n]);
	q->scissors[n] = q->scissor;

	rdp_deduce_static_texture_state(&q->static_state, tiles, setup.tile & 7, setup.tile >> 3);
	rdp_deduce_noise_state(&q->static_state);

	memset(&indices, 0, sizeof(indices));
	normalized = q->static_state;
	rdp_normalize_static_state(&normalized);
	indices.static_index = (uint8_t)cache_add(q->static_states, &q->num_static_states, &q->last_static_state,
			sizeof(normalized), &normalized);
	indices.depth_blend_index = (uint8_t)cache_add(q->depth_blend_states, &q->num_depth_blend_states,
			&q->last_depth_blend_state, sizeof(q->depth_blend), &q->depth_blend);
	indices.tile_instance_index = (uint8_t)q->num_uploads;
	for (i = 0; i < 8; i++)
		indices.tile_indices[i] = (uint8_t)cache_add(q->tile_infos, &q->num_tile_infos, &q->last_tile_info,
				sizeof(tiles[i]), &tiles[i]);
	q->indices[n] = indices;

	q->num_primitives = n + 1;
	q->color_write_pending = true;
	if (q->depth_blend.flags & DEPTH_BLEND_DEPTH_UPDATE_BIT)
		q->depth_write_pending = true;
}

bool rdp_queue_full(const rdp_queue_t *q, const rdp_renderer_caps_t *caps)
{
	return q->num_static_states >= caps->max_static_raster_states ||
			q->num_depth_blend_states == RDP_QUEUE_MAX_DEPTH_BLEND_STATES ||
			q->num_tile_infos + 8 > RDP_QUEUE_MAX_TILE_INFOS ||
			q->num_primitives == RDP_QUEUE_MAX_PRIMITIVES ||
			/* the tallest primitive's jobs */
			q->num_span_jobs * RDP_QUEUE_SPAN_JOB_LINES + MAX_HEIGHT > RDP_QUEUE_MAX_SPAN_SETUPS ||
			/* a primitive over the whole framebuffer */
			q->max_shaded_tiles + caps->max_tiles_x * caps->max_tiles_y > caps->max_num_tile_instances;
}

bool rdp_queue_add_upload(rdp_queue_t *q, const rdp_upload_info_t *upload)
{
	q->uploads[q->num_uploads++] = *upload;
	return q->num_uploads + 1 >= RDP_QUEUE_MAX_UPLOADS;
}

static void set_tile_size(rdp_tile_info_t *tile, const rdp_load_tile_info_t *info)
{
	tile->size.slo = info->slo;
	tile->size.shi = info->shi;
	tile->size.tlo = info->tlo;
	tile->size.thi = info->thi;
}

/* One part: false when it would crash the RDP */
static bool load_part(rdp_queue_t *q, rdp_tile_info_t *tile, const rdp_load_tile_info_t *info,
		uint32_t tmem_offset, rdp_queue_flush_fn flush, void *ctx)
{
	rdp_upload_info_t upload;

	set_tile_size(tile, info);
	switch (rdp_load_upload_info(info, &tile->meta, tmem_offset, &upload))
	{
		case RDP_LOAD_OK:
			break;
		case RDP_LOAD_CRASHES:
			return false;
		default:
			return true;
	}
	if (rdp_queue_add_upload(q, &upload))
		flush(ctx);
	return true;
}

unsigned rdp_queue_load_tile(rdp_queue_t *q, rdp_tile_info_t *tiles, unsigned tile,
		const rdp_load_tile_info_t *info, rdp_queue_flush_fn flush, void *ctx)
{
	rdp_tile_info_t *t = &tiles[tile];
	const unsigned lines_per_part = rdp_load_lines_per_part(info, &t->meta);
	unsigned crashes = 0, lines, line;

	if (lines_per_part == RDP_LOAD_TOO_WIDE)
		return 0;
	if (lines_per_part == RDP_LOAD_WHOLE)
		return load_part(q, t, info, 0, flush, ctx) ? 0 : 1;

	lines = ((info->thi >> 2) - (info->tlo >> 2)) + 1u;
	for (line = 0; line < lines; line += lines_per_part)
	{
		const unsigned part_lines = lines - line < lines_per_part ? lines - line : lines_per_part;
		rdp_load_tile_info_t part = *info;
		part.tlo = (uint16_t)(info->tlo + (line << 2));
		part.thi = (uint16_t)(part.tlo + ((part_lines - 1) << 2));
		if (!load_part(q, t, &part, line * t->meta.stride, flush, ctx))
			crashes++;
	}
	/* the tile's size is the whole load's */
	set_tile_size(t, info);
	return crashes;
}

bool rdp_queue_upload_reads_pending(const rdp_queue_t *q, const rdp_queue_fb_t *fb, uint32_t rdram_size,
		uint32_t addr)
{
	/* Not exact: a load can slice into a framebuffer. */
	const uint32_t pixels = q->height * fb->width;

	if (q->color_write_pending)
	{
		uint32_t offset = (addr - fb->addr) & (rdram_size - 1);
		/* the pixel there (I4: conservatively, the byte) */
		switch (fb->fmt)
		{
			case FB_RGBA5551:
			case FB_IA88:
				offset >>= 1;
				break;
			case FB_RGBA8888:
				offset >>= 2;
				break;
			default:
				break;
		}
		if (offset < pixels)
			return true;
	}

	if (q->depth_write_pending && (((addr - fb->depth_addr) & (rdram_size - 1)) >> 1) < pixels)
		return true;
	return false;
}

void rdp_queue_upload_data(const rdp_queue_t *q, const void *data[RDP_STREAM_BUFFERS],
		VkDeviceSize bytes[RDP_STREAM_BUFFERS])
{
	data[RDP_STREAM_TRIANGLE_SETUP] = q->triangles;
	bytes[RDP_STREAM_TRIANGLE_SETUP] = q->num_primitives * sizeof(*q->triangles);
	data[RDP_STREAM_ATTRIBUTE_SETUP] = q->attributes;
	bytes[RDP_STREAM_ATTRIBUTE_SETUP] = q->num_primitives * sizeof(*q->attributes);
	data[RDP_STREAM_DERIVED_SETUP] = q->derived;
	bytes[RDP_STREAM_DERIVED_SETUP] = q->num_primitives * sizeof(*q->derived);
	data[RDP_STREAM_SCISSOR_SETUP] = q->scissors;
	bytes[RDP_STREAM_SCISSOR_SETUP] = q->num_primitives * sizeof(*q->scissors);
	data[RDP_STREAM_STATIC_RASTER_STATE] = q->static_states;
	bytes[RDP_STREAM_STATIC_RASTER_STATE] = q->num_static_states * sizeof(q->static_states[0]);
	data[RDP_STREAM_DEPTH_BLEND_STATE] = q->depth_blend_states;
	bytes[RDP_STREAM_DEPTH_BLEND_STATE] = q->num_depth_blend_states * sizeof(q->depth_blend_states[0]);
	data[RDP_STREAM_TILE_INFO_STATE] = q->tile_infos;
	bytes[RDP_STREAM_TILE_INFO_STATE] = q->num_tile_infos * sizeof(q->tile_infos[0]);
	data[RDP_STREAM_STATE_INDICES] = q->indices;
	bytes[RDP_STREAM_STATE_INDICES] = q->num_primitives * sizeof(*q->indices);
	data[RDP_STREAM_SPAN_INFO_OFFSETS] = q->span_offsets;
	bytes[RDP_STREAM_SPAN_INFO_OFFSETS] = q->num_primitives * sizeof(*q->span_offsets);
	data[RDP_STREAM_SPAN_INFO_JOBS] = q->span_jobs;
	bytes[RDP_STREAM_SPAN_INFO_JOBS] = q->num_span_jobs * sizeof(*q->span_jobs);
}
