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

#include <stdlib.h>
#include "pipeline_map.h"

void granite_pipeline_map_init(granite_pipeline_map_t *map)
{
	unsigned i;

	for (i = 0; i < GRANITE_PIPELINE_MAP_BUCKETS; i++)
		retro_atomic_ptr_init(&map->buckets[i], NULL);
}

granite_pipeline_t granite_pipeline_map_find(const granite_pipeline_map_t *map, uint64_t hash)
{
	/* loading does not change the map */
	retro_atomic_ptr_t *bucket = (retro_atomic_ptr_t *)&map->buckets[hash & (GRANITE_PIPELINE_MAP_BUCKETS - 1)];
	granite_pipeline_node_t *node = (granite_pipeline_node_t *)retro_atomic_load_acquire_ptr(bucket);
	granite_pipeline_t none;

	for (; node; node = (granite_pipeline_node_t *)retro_atomic_load_acquire_ptr(&node->next))
		if (node->hash == hash)
			return node->pipeline;

	none.pipeline = VK_NULL_HANDLE;
	none.dynamic_mask = 0;
	return none;
}

granite_pipeline_t granite_pipeline_map_add(granite_pipeline_map_t *map, uint64_t hash,
		const granite_pipeline_t *pipeline)
{
	retro_atomic_ptr_t *bucket = &map->buckets[hash & (GRANITE_PIPELINE_MAP_BUCKETS - 1)];
	granite_pipeline_node_t *node = (granite_pipeline_node_t *)malloc(sizeof(*node));
	granite_pipeline_node_t *head, *scanned_to = NULL, *other;
	granite_pipeline_t ret;

	if (!node)
	{
		ret.pipeline = VK_NULL_HANDLE;
		ret.dynamic_mask = 0;
		return ret;
	}

	node->hash = hash;
	node->pipeline = *pipeline;
	head = (granite_pipeline_node_t *)retro_atomic_load_acquire_ptr(bucket);

	for (;;)
	{
		/* only the nodes added since the last look can have the hash */
		for (other = head; other != scanned_to;
				other = (granite_pipeline_node_t *)retro_atomic_load_acquire_ptr(&other->next))
		{
			if (other->hash == hash)
			{
				free(node);
				return other->pipeline;
			}
		}
		scanned_to = head;

		retro_atomic_ptr_init(&node->next, head);
		if (retro_atomic_cas_ptr(bucket, head, node))
			return *pipeline;
		head = (granite_pipeline_node_t *)retro_atomic_load_acquire_ptr(bucket);
	}
}

void granite_pipeline_map_free(granite_pipeline_map_t *map,
		void (*destroy)(void *userdata, const granite_pipeline_t *pipeline), void *userdata)
{
	unsigned i;

	for (i = 0; i < GRANITE_PIPELINE_MAP_BUCKETS; i++)
	{
		granite_pipeline_node_t *node = (granite_pipeline_node_t *)retro_atomic_load_acquire_ptr(&map->buckets[i]);

		retro_atomic_ptr_init(&map->buckets[i], NULL);
		while (node)
		{
			granite_pipeline_node_t *next = (granite_pipeline_node_t *)retro_atomic_load_acquire_ptr(&node->next);

			if (destroy)
				destroy(userdata, &node->pipeline);
			free(node);
			node = next;
		}
	}
}
