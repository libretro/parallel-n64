/* Copyright (c) 2026 libretro team
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

#ifndef RDP_VI_IMAGE_POOL_H
#define RDP_VI_IMAGE_POOL_H

#include <stdint.h>
#include <boolean.h>
#include <retro_common_api.h>
#include "image.h"

RETRO_BEGIN_DECLS

/* Past this many, a new image takes the place of the free one released
 * longest ago */
#define RDP_VI_IMAGE_POOL_ENTRIES 24
/* How many there can be when that many are all in use */
#define RDP_VI_IMAGE_POOL_CAPACITY 160
/* An image nothing asked for in this many turns of the frame contexts
 * is dropped */
#define RDP_VI_IMAGE_POOL_IDLE_CONTEXTS 4

typedef struct rdp_vi_image_entry
{
	/* As requested: the image's own may have been adjusted. */
	granite_image_info_t info;
	/* The pool's references; NULL where one could not be made. */
	granite_image_t *image;
	granite_image_view_t *layer_views[2];
	uint64_t free_serial;
} rdp_vi_image_entry_t;

/* The VI's images, reused across frames instead of created every frame.
 * An image handed out at frame serial S is free again at S + contexts:
 * by then the device has begun the same frame context again, which
 * waits for all of that context's work, as it does before destroying
 * what was released in it. An image handed on (the scanout the frontend
 * shows, the previous frame kept to blend with) is free only once the
 * pool holds its last reference as well: whoever had it is done with
 * it. Nothing is allocated: the entries are in the pool. All zero is an
 * empty pool. */
typedef struct rdp_vi_image_pool
{
	rdp_vi_image_entry_t entries[RDP_VI_IMAGE_POOL_CAPACITY];
	unsigned count;
	/* handed out when the pool is full of images in use: kept for
	 * nobody, and let go when the next one is needed */
	rdp_vi_image_entry_t spare;
} rdp_vi_image_pool_t;

/* Lets go of every image and view */
void rdp_vi_image_pool_free(rdp_vi_image_pool_t *pool);

/* A free entry for @info, now busy for this frame, or NULL.
 * @layer_views: only one that has its layer views. */
rdp_vi_image_entry_t *rdp_vi_image_pool_reuse(rdp_vi_image_pool_t *pool, const granite_image_info_t *info,
		bool layer_views, uint64_t serial, uint64_t contexts);

/* Makes room, then adds a busy entry for @info, with no image: the
 * caller creates it and its views, and gives the entry their
 * references. Valid until the next reuse or add. */
rdp_vi_image_entry_t *rdp_vi_image_pool_add(rdp_vi_image_pool_t *pool, const granite_image_info_t *info,
		uint64_t serial, uint64_t contexts);

/* Once a frame: drops what nothing has asked for in a while, so a
 * resolution change does not keep the old sizes alive. */
void rdp_vi_image_pool_trim(rdp_vi_image_pool_t *pool, uint64_t serial, uint64_t contexts);

RETRO_END_DECLS

#endif
