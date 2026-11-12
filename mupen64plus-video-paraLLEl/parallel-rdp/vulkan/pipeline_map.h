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

#ifndef GRANITE_PIPELINE_MAP_H
#define GRANITE_PIPELINE_MAP_H

#include <stddef.h>
#include <stdint.h>
#include <boolean.h>
#include <retro_common_api.h>
#include <retro_atomic.h>
#include "volk.h"

RETRO_BEGIN_DECLS

#define GRANITE_PIPELINE_MAP_BUCKETS 64

/* A pipeline and the state it leaves dynamic */
typedef struct granite_pipeline
{
	VkPipeline pipeline;
	uint32_t dynamic_mask;
} granite_pipeline_t;

typedef struct granite_pipeline_node
{
	retro_atomic_ptr_t next;
	uint64_t hash;
	granite_pipeline_t pipeline;
} granite_pipeline_node_t;

/* The pipelines made of one program, by the hash of their state.
 *
 * Any thread may look one up or add one at any time, and none takes a
 * lock or waits: a bucket is a chain whose head is an atomic pointer,
 * pipelines are only ever added, and a new one is published at the head
 * with one compare and swap. Of two threads adding the same hash one
 * wins and the other is handed the winner's pipeline. Freeing is the
 * owner's, when no thread uses the map. */
typedef struct granite_pipeline_map
{
	retro_atomic_ptr_t buckets[GRANITE_PIPELINE_MAP_BUCKETS];
} granite_pipeline_map_t;

void granite_pipeline_map_init(granite_pipeline_map_t *map);
/* The pipeline for @hash; VK_NULL_HANDLE and no dynamic state when there
 * is none */
granite_pipeline_t granite_pipeline_map_find(const granite_pipeline_map_t *map, uint64_t hash);
/* Adds @pipeline for @hash and returns the map's pipeline for it: this
 * one, or one added for the hash before, in which case this one is the
 * caller's to destroy. Out of memory, VK_NULL_HANDLE: not added, and the
 * caller's to destroy. */
granite_pipeline_t granite_pipeline_map_add(granite_pipeline_map_t *map, uint64_t hash,
		const granite_pipeline_t *pipeline);
/* Empties the map, calling @destroy for each pipeline in it */
void granite_pipeline_map_free(granite_pipeline_map_t *map,
		void (*destroy)(void *userdata, const granite_pipeline_t *pipeline), void *userdata);

RETRO_END_DECLS

#endif
