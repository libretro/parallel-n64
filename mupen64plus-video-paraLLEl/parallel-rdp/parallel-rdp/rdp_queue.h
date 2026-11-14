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


#ifndef RDP_QUEUE_H
#define RDP_QUEUE_H

/* The primitives and tile loads of a flush as the renderer queues them:
 * each primitive's setup, attributes, derived setup, scissor and state
 * indices written straight into the buffers the GPU reads them from,
 * each different state kept once, and when the flush has to go. C89. */

#include <stdint.h>
#include <boolean.h>
#include <retro_common_api.h>
#include "rdp_state.h"
#include "tmem_load.h"
#include "renderer_buffers.h"
#include "renderer_caps.h"

RETRO_BEGIN_DECLS

#define RDP_QUEUE_MAX_PRIMITIVES 256u
#define RDP_QUEUE_MAX_STATIC_STATES 64u
#define RDP_QUEUE_MAX_DEPTH_BLEND_STATES 64u
#define RDP_QUEUE_MAX_TILE_INFOS 256u
#define RDP_QUEUE_MAX_UPLOADS 256u
#define RDP_QUEUE_MAX_SPAN_SETUPS (32u * 1024u)
/* lines to a span interpolation job */
#define RDP_QUEUE_SPAN_JOB_LINES 64u

/* Where a primitive's span setups start, and its lines */
typedef struct rdp_span_info_offsets
{
	int32_t offset, ylo, yhi, padding;
} rdp_span_info_offsets_t;

/* A job of RDP_QUEUE_SPAN_JOB_LINES lines of a primitive */
typedef struct rdp_span_job
{
	uint16_t primitive_index, base_y, max_y, padding;
} rdp_span_job_t;

/* A primitive's states, as indices into the flush's */
typedef struct rdp_instance_indices
{
	uint8_t static_index;
	uint8_t depth_blend_index;
	uint8_t tile_instance_index;
	uint8_t padding[5];
	uint8_t tile_indices[8];
} rdp_instance_indices_t;

typedef struct rdp_queue
{
	/* what the next primitive is drawn with */
	rdp_scissor_state_t scissor;
	rdp_static_state_t static_state;
	rdp_depth_blend_state_t depth_blend;

	/* each different state once, in the order met; @last_*: the one
	 * found last, tried first */
	rdp_static_state_t static_states[RDP_QUEUE_MAX_STATIC_STATES];
	unsigned num_static_states;
	int last_static_state;
	rdp_depth_blend_state_t depth_blend_states[RDP_QUEUE_MAX_DEPTH_BLEND_STATES];
	unsigned num_depth_blend_states;
	int last_depth_blend_state;
	rdp_tile_info_t tile_infos[RDP_QUEUE_MAX_TILE_INFOS];
	unsigned num_tile_infos;
	int last_tile_info;

	/* the streams, where the GPU reads them (rdp_queue_bind) */
	rdp_triangle_setup_t *triangles;
	rdp_attribute_setup_t *attributes;
	rdp_derived_setup_t *derived;
	rdp_scissor_state_t *scissors;
	rdp_instance_indices_t *indices;
	rdp_span_info_offsets_t *span_offsets;
	rdp_span_job_t *span_jobs;
	unsigned num_primitives;
	unsigned num_span_jobs;

	rdp_upload_info_t uploads[RDP_QUEUE_MAX_UPLOADS];
	unsigned num_uploads;

	/* the tiles the primitives may shade, conservatively */
	unsigned max_shaded_tiles;
	/* how far down the primitives reach; whether they write colour and
	 * depth */
	uint32_t height;
	bool color_write_pending;
	bool depth_write_pending;
} rdp_queue_t;

/* The framebuffer a flush draws */
typedef struct rdp_queue_fb
{
	uint32_t addr;
	uint32_t depth_addr;
	uint32_t width;
	uint32_t fmt; /* FBFormat */
} rdp_queue_fb_t;

/* Empty, nothing bound, no state */
void rdp_queue_init(rdp_queue_t *q);

/* Nothing queued; the state the next primitive is drawn with stays */
void rdp_queue_reset(rdp_queue_t *q);

/* The streams are written into @mapped (an instance's CPU buffers,
 * mapped), by enum rdp_stream_buffer */
void rdp_queue_bind(rdp_queue_t *q, void *const mapped[RDP_STREAM_BUFFERS]);

/* @setup with @attr (NULL: flat), drawn with the queue's state, tiles
 * @tiles and @constants, at @caps' upscaling */
void rdp_queue_add_primitive(rdp_queue_t *q, const rdp_renderer_caps_t *caps, const rdp_constants_t *constants,
		const rdp_tile_info_t *tiles, const rdp_triangle_setup_t *setup, const rdp_attribute_setup_t *attr);

/* Whether the flush has to go before another primitive: a state cache,
 * the primitives, the span setups or the tiles that may be shaded would
 * not hold one more */
bool rdp_queue_full(const rdp_queue_t *q, const rdp_renderer_caps_t *caps);

/* A tile load. Whether the flush has to go before another. */
bool rdp_queue_add_upload(rdp_queue_t *q, const rdp_upload_info_t *upload);

/* Whether a tile load from RDRAM @addr may read pixels the queued
 * primitives write to @fb, in an RDRAM of @rdram_size bytes (a power of
 * two): the flush has to go first */
bool rdp_queue_upload_reads_pending(const rdp_queue_t *q, const rdp_queue_fb_t *fb, uint32_t rdram_size,
		uint32_t addr);

/* The streams and state caches to upload, by enum rdp_stream_buffer */
void rdp_queue_upload_data(const rdp_queue_t *q, const void *data[RDP_STREAM_BUFFERS],
		VkDeviceSize bytes[RDP_STREAM_BUFFERS]);

RETRO_END_DECLS

#endif
