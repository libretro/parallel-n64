/* Copyright (c) 2017-2022 Hans-Kristian Arntzen
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

#ifndef GRANITE_FRAMEBUFFER_H
#define GRANITE_FRAMEBUFFER_H

#include <stddef.h>
#include <stdint.h>
#include <boolean.h>
#include <retro_common_api.h>
#include "volk.h"

RETRO_BEGIN_DECLS

/* frames a framebuffer that is not asked for again is kept */
#define GRANITE_FRAMEBUFFER_RING_SIZE 8
#define GRANITE_FRAMEBUFFER_BUCKETS   64
/* framebuffers a block of nodes holds */
#define GRANITE_FRAMEBUFFER_BLOCK     16

typedef struct granite_framebuffer
{
	/* VK_NULL_HANDLE when the driver refused it */
	VkFramebuffer framebuffer;
	uint32_t width;
	uint32_t height;
	/* the caller's: the render pass it was made with */
	const void *render_pass;

	uint64_t hash;
	/* in its frame's list, or the vacant ones (next alone) */
	struct granite_framebuffer *prev;
	struct granite_framebuffer *next;
	/* in its bucket */
	struct granite_framebuffer *hash_next;
	/* the frame it was last asked for in */
	unsigned index;
} granite_framebuffer_t;

typedef struct granite_framebuffer_block
{
	struct granite_framebuffer_block *next;
	granite_framebuffer_t nodes[GRANITE_FRAMEBUFFER_BLOCK];
} granite_framebuffer_block_t;

/* The framebuffers of a device, by the hash of their render pass and
 * images. One asked for in the last GRANITE_FRAMEBUFFER_RING_SIZE frames
 * is handed out again; one that was not is given to @destroy, which is to
 * destroy it once the GPU is done with it.
 *
 * It is the device owner thread's alone and takes no lock. Nodes come a
 * block at a time and are used again: a frame that asks for no new
 * framebuffer allocates nothing. */
typedef struct granite_framebuffer_allocator
{
	VkDevice device;
	const struct VolkDeviceTable *table;
	void (*destroy)(void *userdata, VkFramebuffer framebuffer);
	void *userdata;
	/* those asked for in each of the last frames, the current at @index */
	granite_framebuffer_t *rings[GRANITE_FRAMEBUFFER_RING_SIZE];
	unsigned index;
	granite_framebuffer_t *buckets[GRANITE_FRAMEBUFFER_BUCKETS];
	granite_framebuffer_t *vacants;
	granite_framebuffer_block_t *blocks;
	/* handed out, never kept, when there is no memory for a node */
	granite_framebuffer_t fallback;
} granite_framebuffer_allocator_t;

void granite_framebuffer_allocator_init(granite_framebuffer_allocator_t *allocator, VkDevice device,
		const struct VolkDeviceTable *table, void (*destroy)(void *userdata, VkFramebuffer framebuffer),
		void *userdata);
/* Gives every framebuffer to destroy and frees the nodes */
void granite_framebuffer_allocator_free(granite_framebuffer_allocator_t *allocator);
/* A frame begins: the framebuffers last asked for
 * GRANITE_FRAMEBUFFER_RING_SIZE frames ago go */
void granite_framebuffer_allocator_begin_frame(granite_framebuffer_allocator_t *allocator);
/* The framebuffer for @hash, now one of this frame's; NULL when there is
 * none */
granite_framebuffer_t *granite_framebuffer_allocator_find(granite_framebuffer_allocator_t *allocator, uint64_t hash);
/* Makes the framebuffer for @hash, which has none: @num_views views of
 * @width by @height for @render_pass, one layer. @owner is kept as its
 * render_pass. Never NULL: one the driver refuses has no handle, and is
 * kept so that it is not asked of the driver again every pass; without
 * memory for a node it is the fallback, valid until the next one made. */
granite_framebuffer_t *granite_framebuffer_allocator_create(granite_framebuffer_allocator_t *allocator,
		uint64_t hash, VkRenderPass render_pass, const void *owner, const VkImageView *views,
		unsigned num_views, uint32_t width, uint32_t height);
/* Gives every framebuffer to destroy; the nodes are kept for the next */
void granite_framebuffer_allocator_clear(granite_framebuffer_allocator_t *allocator);

RETRO_END_DECLS

#endif
