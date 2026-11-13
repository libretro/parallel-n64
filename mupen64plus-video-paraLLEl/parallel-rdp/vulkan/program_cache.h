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

#ifndef GRANITE_PROGRAM_CACHE_H
#define GRANITE_PROGRAM_CACHE_H

#include <stddef.h>
#include <stdint.h>
#include <boolean.h>
#include <retro_common_api.h>
#include <retro_atomic.h>
#include "volk.h"
#include "shader.h"
#include "pipeline_map.h"
#include "resource_bindings.h"
#include "render_pass.h"

RETRO_BEGIN_DECLS

#define GRANITE_OBJECT_CACHE_BUCKETS 64

/* The first member of whatever an object cache holds */
typedef struct granite_cache_node
{
	retro_atomic_ptr_t next;
	/* two objects of a cache with one hash are the same object */
	uint64_t hash;
} granite_cache_node_t;

/* Objects by hash, made once and kept until the cache is freed.
 *
 * Any thread may look one up or add one at any time, and none takes a
 * lock or waits: a bucket is a chain whose head is an atomic pointer,
 * objects are only ever added, and a new one is published at the head
 * with one compare and swap. Of two threads adding the same hash one
 * wins and the other is handed the winner's object. */
typedef struct granite_object_cache
{
	retro_atomic_ptr_t buckets[GRANITE_OBJECT_CACHE_BUCKETS];
} granite_object_cache_t;

void granite_object_cache_init(granite_object_cache_t *cache);
/* NULL when there is none for @hash */
granite_cache_node_t *granite_object_cache_find(const granite_object_cache_t *cache, uint64_t hash);
/* Adds @node, whose hash is set, and returns the cache's object for the
 * hash: this one, or one added before, in which case this one is the
 * caller's to dispose of. */
granite_cache_node_t *granite_object_cache_add(granite_object_cache_t *cache, granite_cache_node_t *node);
/* Every object there is at the time, in no order. Other threads may be
 * adding. */
void granite_object_cache_for_each(const granite_object_cache_t *cache,
		void (*visit)(void *userdata, granite_cache_node_t *node), void *userdata);
/* Empties the cache, handing each object to @dispose. No other thread
 * may be using it. */
void granite_object_cache_free(granite_object_cache_t *cache,
		void (*dispose)(void *userdata, granite_cache_node_t *node), void *userdata);

/* The same hash the saved pipeline records were made with */
#define GRANITE_HASH_INIT (((uint64_t)0xcbf29ce4u << 32) | 0x84222325u)
static INLINE uint64_t granite_hash_u32(uint64_t h, uint32_t value)
{
	return (h * (((uint64_t)0x100u << 32) | 0x000001b3u)) ^ value;
}

static INLINE uint64_t granite_hash_u64(uint64_t h, uint64_t value)
{
	h = granite_hash_u32(h, (uint32_t)(value & 0xffffffffu));
	return granite_hash_u32(h, (uint32_t)(value >> 32));
}

/* @size in bytes: whole words of it */
uint64_t granite_hash_words(uint64_t h, const uint32_t *words, size_t size);

/* A shader module and what it takes and gives */
typedef struct granite_shader
{
	granite_cache_node_t node;
	VkShaderModule module;
	granite_resource_layout_t layout;
} granite_shader_t;

/* The descriptor sets of one set layout */
typedef struct granite_cached_set_allocator
{
	granite_cache_node_t node;
	granite_set_allocator_t impl;
} granite_cached_set_allocator_t;

/* A pipeline layout and what binds against it */
typedef struct granite_cached_pipeline_layout
{
	granite_cache_node_t node;
	granite_pipeline_layout_t impl;
	granite_combined_layout_t layout;
	granite_bind_layout_t bind;
} granite_cached_pipeline_layout_t;

/* The shaders of a pipeline, their layout, and the pipelines made of
 * them so far */
typedef struct granite_program
{
	granite_cache_node_t node;
	const granite_shader_t *shaders[GRANITE_SHADER_STAGE_COUNT];
	granite_cached_pipeline_layout_t *layout;
	granite_pipeline_map_t pipelines;
} granite_program_t;

/* A render pass */
typedef struct granite_cached_render_pass
{
	granite_cache_node_t node;
	granite_render_pass_t impl;
} granite_cached_render_pass_t;

/* A device's shaders, programs, pipeline layouts, set allocators and
 * render passes.
 * Each is made the first time it is asked for, from any thread, and
 * lives as long as the cache. */
typedef struct granite_program_cache
{
	VkDevice device;
	const struct VolkDeviceTable *table;
	/* the device pushes descriptors, and how many */
	bool push_descriptors;
	uint32_t max_push_descriptors;
	uint32_t max_bound_sets;
	/* the threads that allocate descriptor sets */
	unsigned num_threads;
	granite_object_cache_t shaders;
	granite_object_cache_t programs;
	granite_object_cache_t layouts;
	granite_object_cache_t set_allocators;
	granite_object_cache_t render_passes;
} granite_program_cache_t;

void granite_program_cache_init(granite_program_cache_t *cache, VkDevice device,
		const struct VolkDeviceTable *table, bool push_descriptors, uint32_t max_push_descriptors,
		uint32_t max_bound_sets, unsigned num_threads);
/* Destroys everything in it; each pipeline goes to @destroy_pipeline.
 * No other thread may be using it. */
void granite_program_cache_free(granite_program_cache_t *cache,
		void (*destroy_pipeline)(void *userdata, const granite_pipeline_t *pipeline), void *userdata);

/* The shader for the SPIR-V in @code, @size bytes of it. @layout: what
 * it takes and gives; NULL for nothing. NULL when out of memory; a
 * module the driver refuses leaves a shader without one. */
granite_shader_t *granite_program_cache_shader(granite_program_cache_t *cache, const uint32_t *code, size_t size,
		const granite_resource_layout_t *layout);
granite_shader_t *granite_program_cache_find_shader(const granite_program_cache_t *cache, uint64_t hash);

/* The program of one compute shader, or of a vertex and a fragment
 * shader. NULL without the shaders or when out of memory. */
granite_program_t *granite_program_cache_compute(granite_program_cache_t *cache, const granite_shader_t *compute);
granite_program_t *granite_program_cache_graphics(granite_program_cache_t *cache, const granite_shader_t *vertex,
		const granite_shader_t *fragment);
granite_program_t *granite_program_cache_find_program(const granite_program_cache_t *cache, uint64_t hash);

/* The pipeline layout of a baked combined layout, with the set
 * allocators it binds against */
granite_cached_pipeline_layout_t *granite_program_cache_layout(granite_program_cache_t *cache,
		const granite_combined_layout_t *layout);
/* The allocator for sets of @layout used by @stages_for_bindings
 * [GRANITE_NUM_BINDINGS]; @push: the set is pushed, not allocated */
granite_cached_set_allocator_t *granite_program_cache_set_allocator(granite_program_cache_t *cache,
		const granite_descriptor_set_layout_t *layout, const uint32_t *stages_for_bindings, bool push);

/* The render pass of @desc; @compatible: one to make pipelines and
 * framebuffers for, whatever the pass loads, stores or clears. NULL
 * when out of memory. */
granite_cached_render_pass_t *granite_program_cache_render_pass(granite_program_cache_t *cache,
		const granite_render_pass_desc_t *desc, bool compatible);
granite_cached_render_pass_t *granite_program_cache_find_render_pass(const granite_program_cache_t *cache,
		uint64_t hash);

/* The owner thread, each frame: the set allocators move on a frame */
void granite_program_cache_begin_frame(granite_program_cache_t *cache);
/* The device is idle: the set allocators give up their sets */
void granite_program_cache_clear_sets(granite_program_cache_t *cache);

RETRO_END_DECLS

#endif
