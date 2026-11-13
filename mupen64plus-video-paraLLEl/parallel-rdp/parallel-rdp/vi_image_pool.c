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

#include <string.h>
#include "vi_image_pool.h"

/* Lets go of what the entry holds */
static void entry_release(rdp_vi_image_entry_t *e)
{
	unsigned i;
	for (i = 0; i < 2; i++)
		if (e->layer_views[i])
			granite_image_view_unref(e->layer_views[i]);
	if (e->image)
		granite_image_unref(e->image);
	memset(e, 0, sizeof(*e));
}

static void pool_remove(rdp_vi_image_pool_t *pool, unsigned i)
{
	entry_release(&pool->entries[i]);
	pool->count--;
	if (i != pool->count)
	{
		pool->entries[i] = pool->entries[pool->count];
		memset(&pool->entries[pool->count], 0, sizeof(pool->entries[0]));
	}
}

/* An entry whose image failed to be created holds none. */
static bool unshared(const rdp_vi_image_entry_t *e)
{
	return !e->image || granite_refs_only(&e->image->refs);
}

static bool same(const granite_image_info_t *a, const granite_image_info_t *b)
{
	return a->width == b->width && a->height == b->height && a->depth == b->depth
		&& a->levels == b->levels && a->layers == b->layers && a->format == b->format
		&& a->type == b->type && a->usage == b->usage && a->samples == b->samples
		&& a->flags == b->flags && a->misc == b->misc;
}

void rdp_vi_image_pool_free(rdp_vi_image_pool_t *pool)
{
	while (pool->count)
		pool_remove(pool, pool->count - 1);
	entry_release(&pool->spare);
}

rdp_vi_image_entry_t *rdp_vi_image_pool_reuse(rdp_vi_image_pool_t *pool, const granite_image_info_t *info,
		bool layer_views, uint64_t serial, uint64_t contexts)
{
	unsigned i;

	for (i = 0; i < pool->count; i++)
	{
		rdp_vi_image_entry_t *e = &pool->entries[i];
		if (e->free_serial <= serial && same(&e->info, info) && (!layer_views || e->layer_views[0]) && unshared(e))
		{
			e->free_serial = serial + contexts;
			return e;
		}
	}
	return NULL;
}

rdp_vi_image_entry_t *rdp_vi_image_pool_add(rdp_vi_image_pool_t *pool, const granite_image_info_t *info,
		uint64_t serial, uint64_t contexts)
{
	rdp_vi_image_entry_t *e;
	unsigned i;

	/* When full, the free entry released longest ago makes room. */
	if (pool->count >= RDP_VI_IMAGE_POOL_ENTRIES)
	{
		unsigned oldest = pool->count;
		for (i = 0; i < pool->count; i++)
			if (pool->entries[i].free_serial <= serial && unshared(&pool->entries[i])
					&& (oldest == pool->count || pool->entries[i].free_serial < pool->entries[oldest].free_serial))
				oldest = i;
		if (oldest != pool->count)
			pool_remove(pool, oldest);
	}

	if (pool->count < RDP_VI_IMAGE_POOL_CAPACITY)
		e = &pool->entries[pool->count++];
	else
	{
		/* Every one of them in use: this one is not kept. */
		e = &pool->spare;
		entry_release(e);
	}
	e->info = *info;
	e->free_serial = serial + contexts;
	return e;
}

void rdp_vi_image_pool_trim(rdp_vi_image_pool_t *pool, uint64_t serial, uint64_t contexts)
{
	unsigned i = 0;

	while (i < pool->count)
	{
		if (pool->entries[i].free_serial + RDP_VI_IMAGE_POOL_IDLE_CONTEXTS * contexts <= serial)
			pool_remove(pool, i);
		else
			i++;
	}
}
