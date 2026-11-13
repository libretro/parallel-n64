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
#include <string.h>
#include "program_cache.h"
#include "aligned_alloc.h"
#include "logging.h"

/* ---- objects by hash ---- */

void granite_object_cache_init(granite_object_cache_t *cache)
{
	unsigned i;
	for (i = 0; i < GRANITE_OBJECT_CACHE_BUCKETS; i++)
		retro_atomic_ptr_init(&cache->buckets[i], NULL);
}

static INLINE granite_cache_node_t *node_next(granite_cache_node_t *node)
{
	return (granite_cache_node_t*)retro_atomic_load_acquire_ptr(&node->next);
}

granite_cache_node_t *granite_object_cache_find(const granite_object_cache_t *cache, uint64_t hash)
{
	/* loading does not change the cache */
	retro_atomic_ptr_t *bucket = (retro_atomic_ptr_t*)&cache->buckets[hash & (GRANITE_OBJECT_CACHE_BUCKETS - 1)];
	granite_cache_node_t *node = (granite_cache_node_t*)retro_atomic_load_acquire_ptr(bucket);

	for (; node; node = node_next(node))
		if (node->hash == hash)
			return node;
	return NULL;
}

granite_cache_node_t *granite_object_cache_add(granite_object_cache_t *cache, granite_cache_node_t *node)
{
	retro_atomic_ptr_t *bucket = &cache->buckets[node->hash & (GRANITE_OBJECT_CACHE_BUCKETS - 1)];
	granite_cache_node_t *head = (granite_cache_node_t*)retro_atomic_load_acquire_ptr(bucket);
	granite_cache_node_t *scanned_to = NULL, *other;

	for (;;)
	{
		/* only the nodes added since the last look can have the hash */
		for (other = head; other != scanned_to; other = node_next(other))
			if (other->hash == node->hash)
				return other;
		scanned_to = head;

		retro_atomic_ptr_init(&node->next, head);
		if (retro_atomic_cas_ptr(bucket, head, node))
			return node;
		head = (granite_cache_node_t*)retro_atomic_load_acquire_ptr(bucket);
	}
}

void granite_object_cache_for_each(const granite_object_cache_t *cache,
		void (*visit)(void *userdata, granite_cache_node_t *node), void *userdata)
{
	unsigned i;

	for (i = 0; i < GRANITE_OBJECT_CACHE_BUCKETS; i++)
	{
		granite_cache_node_t *node = (granite_cache_node_t*)retro_atomic_load_acquire_ptr(
				(retro_atomic_ptr_t*)&cache->buckets[i]);
		for (; node; node = node_next(node))
			visit(userdata, node);
	}
}

void granite_object_cache_free(granite_object_cache_t *cache,
		void (*dispose)(void *userdata, granite_cache_node_t *node), void *userdata)
{
	unsigned i;

	for (i = 0; i < GRANITE_OBJECT_CACHE_BUCKETS; i++)
	{
		granite_cache_node_t *node = (granite_cache_node_t*)retro_atomic_load_acquire_ptr(&cache->buckets[i]);

		retro_atomic_ptr_init(&cache->buckets[i], NULL);
		while (node)
		{
			granite_cache_node_t *next = node_next(node);
			dispose(userdata, node);
			node = next;
		}
	}
}

uint64_t granite_hash_words(uint64_t h, const uint32_t *words, size_t size)
{
	size_t i;
	size /= sizeof(*words);
	for (i = 0; i < size; i++)
		h = granite_hash_u32(h, words[i]);
	return h;
}

/* ---- the device's caches ---- */

/* A zeroed object of a cache, aligned for anything in it */
static void *new_object(size_t size, uint64_t hash)
{
	granite_cache_node_t *node = (granite_cache_node_t*)granite_memalign_alloc(64, size);
	if (!node)
		return NULL;
	memset(node, 0, size);
	node->hash = hash;
	return node;
}

void granite_program_cache_init(granite_program_cache_t *cache, VkDevice device,
		const struct VolkDeviceTable *table, bool push_descriptors, uint32_t max_push_descriptors,
		uint32_t max_bound_sets, unsigned num_threads)
{
	cache->device = device;
	cache->table = table;
	cache->push_descriptors = push_descriptors;
	cache->max_push_descriptors = max_push_descriptors;
	cache->max_bound_sets = max_bound_sets;
	cache->num_threads = num_threads;
	granite_object_cache_init(&cache->shaders);
	granite_object_cache_init(&cache->programs);
	granite_object_cache_init(&cache->layouts);
	granite_object_cache_init(&cache->set_allocators);
	granite_object_cache_init(&cache->render_passes);
}

/* shaders */

static void shader_dispose(void *userdata, granite_cache_node_t *node)
{
	granite_program_cache_t *cache = (granite_program_cache_t*)userdata;
	granite_shader_t *shader = (granite_shader_t*)node;

	if (shader->module != VK_NULL_HANDLE)
		cache->table->vkDestroyShaderModule(cache->device, shader->module, NULL);
	granite_memalign_free(shader);
}

granite_shader_t *granite_program_cache_find_shader(const granite_program_cache_t *cache, uint64_t hash)
{
	return (granite_shader_t*)granite_object_cache_find(&cache->shaders, hash);
}

granite_shader_t *granite_program_cache_shader(granite_program_cache_t *cache, const uint32_t *code, size_t size,
		const granite_resource_layout_t *layout)
{
	uint64_t hash = granite_hash_words(GRANITE_HASH_INIT, code, size);
	granite_shader_t *shader = granite_program_cache_find_shader(cache, hash);
	granite_shader_t *kept;
	VkShaderModuleCreateInfo info;

	if (shader)
		return shader;
	shader = (granite_shader_t*)new_object(sizeof(*shader), hash);
	if (!shader)
		return NULL;

	memset(&info, 0, sizeof(info));
	info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	info.codeSize = size;
	info.pCode = code;
	if (cache->table->vkCreateShaderModule(cache->device, &info, NULL, &shader->module) != VK_SUCCESS)
	{
		LOGE("Failed to create shader module.\n");
		shader->module = VK_NULL_HANDLE;
	}
	if (layout)
		shader->layout = *layout;

	kept = (granite_shader_t*)granite_object_cache_add(&cache->shaders, &shader->node);
	if (kept != shader)
		shader_dispose(cache, &shader->node);
	return kept;
}

/* set allocators */

static void set_allocator_dispose(void *userdata, granite_cache_node_t *node)
{
	granite_cached_set_allocator_t *allocator = (granite_cached_set_allocator_t*)node;
	(void)userdata;
	granite_set_allocator_free(&allocator->impl);
	granite_memalign_free(allocator);
}

granite_cached_set_allocator_t *granite_program_cache_set_allocator(granite_program_cache_t *cache,
		const granite_descriptor_set_layout_t *layout, const uint32_t *stages_for_bindings, bool push)
{
	granite_cached_set_allocator_t *allocator, *kept;
	uint64_t hash = granite_hash_u32(GRANITE_HASH_INIT, push);

	hash = granite_hash_words(hash, (const uint32_t*)layout, sizeof(*layout));
	hash = granite_hash_words(hash, stages_for_bindings, sizeof(uint32_t) * GRANITE_NUM_BINDINGS);

	allocator = (granite_cached_set_allocator_t*)granite_object_cache_find(&cache->set_allocators, hash);
	if (allocator)
		return allocator;
	allocator = (granite_cached_set_allocator_t*)new_object(sizeof(*allocator), hash);
	if (!allocator)
		return NULL;
	granite_set_allocator_init(&allocator->impl, cache->device, cache->table, layout, stages_for_bindings,
			NULL, push, cache->num_threads);

	kept = (granite_cached_set_allocator_t*)granite_object_cache_add(&cache->set_allocators, &allocator->node);
	if (kept != allocator)
		set_allocator_dispose(cache, &allocator->node);
	return kept;
}

static void set_allocator_begin_frame(void *userdata, granite_cache_node_t *node)
{
	(void)userdata;
	granite_set_allocator_begin_frame(&((granite_cached_set_allocator_t*)node)->impl);
}

static void set_allocator_clear(void *userdata, granite_cache_node_t *node)
{
	(void)userdata;
	granite_set_allocator_clear(&((granite_cached_set_allocator_t*)node)->impl);
}

void granite_program_cache_begin_frame(granite_program_cache_t *cache)
{
	granite_object_cache_for_each(&cache->set_allocators, set_allocator_begin_frame, NULL);
}

void granite_program_cache_clear_sets(granite_program_cache_t *cache)
{
	granite_object_cache_for_each(&cache->set_allocators, set_allocator_clear, NULL);
}

/* pipeline layouts */

static void layout_dispose(void *userdata, granite_cache_node_t *node)
{
	granite_cached_pipeline_layout_t *layout = (granite_cached_pipeline_layout_t*)node;
	(void)userdata;
	granite_pipeline_layout_free(&layout->impl);
	granite_memalign_free(layout);
}

granite_cached_pipeline_layout_t *granite_program_cache_layout(granite_program_cache_t *cache,
		const granite_combined_layout_t *combined)
{
	VkDescriptorSetLayout set_layouts[GRANITE_NUM_DESCRIPTOR_SETS];
	granite_cached_pipeline_layout_t *layout, *kept;
	uint64_t hash = granite_combined_layout_hash(combined);
	uint32_t push_set_mask;
	unsigned i;

	layout = (granite_cached_pipeline_layout_t*)granite_object_cache_find(&cache->layouts, hash);
	if (layout)
		return layout;
	layout = (granite_cached_pipeline_layout_t*)new_object(sizeof(*layout), hash);
	if (!layout)
		return NULL;

	layout->layout = *combined;
	push_set_mask = granite_combined_layout_push_set_mask(&layout->layout, cache->push_descriptors,
			cache->max_push_descriptors);

	for (i = 0; i < GRANITE_NUM_DESCRIPTOR_SETS; i++)
	{
		granite_cached_set_allocator_t *allocator = granite_program_cache_set_allocator(cache,
				&layout->layout.sets[i], layout->layout.stages_for_bindings[i], (push_set_mask & (1u << i)) != 0);
		if (!allocator)
		{
			granite_memalign_free(layout);
			return NULL;
		}
		set_layouts[i] = allocator->impl.set_layout;
		layout->bind.allocators[i] = &allocator->impl;
	}

	granite_pipeline_layout_init(&layout->impl, cache->device, cache->table, &layout->layout, set_layouts,
			push_set_mask, cache->max_bound_sets);

	layout->bind.hash = hash;
	layout->bind.layout = layout->impl.layout;
	layout->bind.resource_layout = &layout->layout;
	layout->bind.update_templates = layout->impl.update_template;
	layout->bind.push_set_mask = push_set_mask;

	kept = (granite_cached_pipeline_layout_t*)granite_object_cache_add(&cache->layouts, &layout->node);
	if (kept != layout)
		layout_dispose(cache, &layout->node);
	return kept;
}

/* programs */

typedef struct program_dispose_args
{
	void (*destroy_pipeline)(void *userdata, const granite_pipeline_t *pipeline);
	void *userdata;
} program_dispose_args_t;

static void program_dispose(void *userdata, granite_cache_node_t *node)
{
	program_dispose_args_t *args = (program_dispose_args_t*)userdata;
	granite_program_t *program = (granite_program_t*)node;

	granite_pipeline_map_free(&program->pipelines, args ? args->destroy_pipeline : NULL, args ? args->userdata : NULL);
	granite_memalign_free(program);
}

granite_program_t *granite_program_cache_find_program(const granite_program_cache_t *cache, uint64_t hash)
{
	return (granite_program_t*)granite_object_cache_find(&cache->programs, hash);
}

/* The program of @program's shaders, which are set: looks it up by
 * @hash or makes it */
static granite_program_t *request_program(granite_program_cache_t *cache, uint64_t hash,
		const granite_shader_t **shaders)
{
	granite_program_t *program = granite_program_cache_find_program(cache, hash);
	granite_program_t *kept;
	granite_combined_layout_t combined;
	unsigned i;

	if (program)
		return program;
	program = (granite_program_t*)new_object(sizeof(*program), hash);
	if (!program)
		return NULL;
	granite_pipeline_map_init(&program->pipelines);

	granite_combined_layout_init(&combined);
	for (i = 0; i < GRANITE_SHADER_STAGE_COUNT; i++)
	{
		program->shaders[i] = shaders[i];
		if (shaders[i])
			granite_combined_layout_add(&combined, i, &shaders[i]->layout);
	}
	granite_combined_layout_bake(&combined);
	program->layout = granite_program_cache_layout(cache, &combined);

	kept = (granite_program_t*)granite_object_cache_add(&cache->programs, &program->node);
	if (kept != program)
		program_dispose(NULL, &program->node);
	return kept;
}

granite_program_t *granite_program_cache_compute(granite_program_cache_t *cache, const granite_shader_t *compute)
{
	const granite_shader_t *shaders[GRANITE_SHADER_STAGE_COUNT];

	if (!compute)
		return NULL;
	memset((void*)shaders, 0, sizeof(shaders));
	shaders[GRANITE_SHADER_STAGE_COMPUTE] = compute;
	return request_program(cache, granite_hash_u64(GRANITE_HASH_INIT, compute->node.hash), shaders);
}

granite_program_t *granite_program_cache_graphics(granite_program_cache_t *cache, const granite_shader_t *vertex,
		const granite_shader_t *fragment)
{
	const granite_shader_t *shaders[GRANITE_SHADER_STAGE_COUNT];
	uint64_t hash;

	if (!vertex || !fragment)
		return NULL;
	memset((void*)shaders, 0, sizeof(shaders));
	shaders[GRANITE_SHADER_STAGE_VERTEX] = vertex;
	shaders[GRANITE_SHADER_STAGE_FRAGMENT] = fragment;
	hash = granite_hash_u64(GRANITE_HASH_INIT, vertex->node.hash);
	hash = granite_hash_u64(hash, fragment->node.hash);
	return request_program(cache, hash, shaders);
}

/* render passes */

static void render_pass_dispose(void *userdata, granite_cache_node_t *node)
{
	granite_cached_render_pass_t *pass = (granite_cached_render_pass_t*)node;
	(void)userdata;
	granite_render_pass_free(&pass->impl);
	granite_memalign_free(pass);
}

granite_cached_render_pass_t *granite_program_cache_find_render_pass(const granite_program_cache_t *cache,
		uint64_t hash)
{
	return (granite_cached_render_pass_t*)granite_object_cache_find(&cache->render_passes, hash);
}

granite_cached_render_pass_t *granite_program_cache_render_pass(granite_program_cache_t *cache,
		const granite_render_pass_desc_t *desc, bool compatible)
{
	uint64_t hash = granite_render_pass_hash(desc, compatible);
	granite_cached_render_pass_t *pass = granite_program_cache_find_render_pass(cache, hash);
	granite_cached_render_pass_t *kept;

	if (pass)
		return pass;
	pass = (granite_cached_render_pass_t*)new_object(sizeof(*pass), hash);
	if (!pass)
		return NULL;
	granite_render_pass_init(&pass->impl, cache->device, cache->table, desc);

	kept = (granite_cached_render_pass_t*)granite_object_cache_add(&cache->render_passes, &pass->node);
	if (kept != pass)
		render_pass_dispose(cache, &pass->node);
	return kept;
}

void granite_program_cache_free(granite_program_cache_t *cache,
		void (*destroy_pipeline)(void *userdata, const granite_pipeline_t *pipeline), void *userdata)
{
	program_dispose_args_t args;

	args.destroy_pipeline = destroy_pipeline;
	args.userdata = userdata;
	granite_object_cache_free(&cache->programs, program_dispose, &args);
	granite_object_cache_free(&cache->layouts, layout_dispose, cache);
	granite_object_cache_free(&cache->set_allocators, set_allocator_dispose, cache);
	granite_object_cache_free(&cache->shaders, shader_dispose, cache);
	granite_object_cache_free(&cache->render_passes, render_pass_dispose, cache);
}
