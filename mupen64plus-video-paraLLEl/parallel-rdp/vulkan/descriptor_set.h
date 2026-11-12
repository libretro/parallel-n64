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

#ifndef GRANITE_DESCRIPTOR_SET_H
#define GRANITE_DESCRIPTOR_SET_H

#include <stddef.h>
#include <stdint.h>
#include <boolean.h>
#include <retro_common_api.h>
#include "volk.h"

RETRO_BEGIN_DECLS

#define GRANITE_NUM_BINDINGS         32
#define GRANITE_NUM_SETS_PER_POOL    16
/* frames a set that is not asked for again is kept */
#define GRANITE_DESCRIPTOR_RING_SIZE 8

/* What a shader has in one descriptor set: a bit a binding in the mask
 * of its kind. Hashed and stored as it lies: a multiple of 8 bytes, no
 * holes. */
typedef struct granite_descriptor_set_layout
{
	uint32_t sampled_image_mask;
	uint32_t storage_image_mask;
	uint32_t uniform_buffer_mask;
	uint32_t storage_buffer_mask;
	uint32_t sampled_texel_buffer_mask;
	uint32_t storage_texel_buffer_mask;
	uint32_t input_attachment_mask;
	uint32_t sampler_mask;
	uint32_t separate_image_mask;
	uint32_t fp_mask;
	uint32_t immutable_sampler_mask;
	uint8_t array_size[GRANITE_NUM_BINDINGS];
	uint32_t padding;
} granite_descriptor_set_layout_t;

/* A descriptor set and the contents it was written for, by their hash */
typedef struct granite_set_node
{
	uint64_t hash;
	VkDescriptorSet set;
	/* in its frame's list, or the vacant ones (next alone) */
	struct granite_set_node *prev;
	struct granite_set_node *next;
	/* in its bucket */
	struct granite_set_node *hash_next;
	/* the frame it was last asked for in */
	unsigned index;
} granite_set_node_t;

/* A pool and the sets allocated from it */
typedef struct granite_set_pool
{
	struct granite_set_pool *next;
	VkDescriptorPool pool;
	granite_set_node_t nodes[GRANITE_NUM_SETS_PER_POOL];
} granite_set_pool_t;

/* One thread's sets of a layout */
typedef struct granite_set_thread
{
	/* the sets asked for in each of the last frames, the current one at
	 * @index */
	granite_set_node_t *rings[GRANITE_DESCRIPTOR_RING_SIZE];
	unsigned index;
	/* written for nothing now */
	granite_set_node_t *vacants;
	/* by hash: those in the rings */
	granite_set_node_t **buckets;
	size_t bucket_mask;
	size_t num_nodes;
	granite_set_pool_t *pools;
	bool should_begin;
} granite_set_thread_t;

/* The descriptor sets of one layout.
 *
 * A set is asked for by the hash of what it is to hold: one written for
 * that in the last frames is handed out again as it is, and otherwise
 * one nothing has asked for in GRANITE_DESCRIPTOR_RING_SIZE frames, or a
 * new one, for the caller to write. Sets are allocated a pool of
 * GRANITE_NUM_SETS_PER_POOL at a time and never freed one by one.
 *
 * Each thread index has its own and takes no lock; begin_frame and clear
 * are the device owner's, while no thread asks. A push layout has no
 * sets: its descriptors are pushed into the command buffer. */
typedef struct granite_set_allocator
{
	VkDevice device;
	const struct VolkDeviceTable *table;
	VkDescriptorSetLayout set_layout;
	granite_set_thread_t *per_thread;
	unsigned num_per_thread;
	bool push;
	VkDescriptorPoolSize pool_size[GRANITE_NUM_BINDINGS];
	unsigned num_pool_sizes;
} granite_set_allocator_t;

/* @stages_for_bindings[GRANITE_NUM_BINDINGS]: the shader stages that use
 * each binding, 0 for one unused. @immutable_samplers[GRANITE_NUM_BINDINGS],
 * or NULL: the sampler of a binding in @layout's immutable_sampler_mask.
 * @num_threads: the thread indices that will ask. False when out of
 * memory; a layout the driver refuses leaves set_layout VK_NULL_HANDLE. */
bool granite_set_allocator_init(granite_set_allocator_t *allocator, VkDevice device,
		const struct VolkDeviceTable *table, const granite_descriptor_set_layout_t *layout,
		const uint32_t *stages_for_bindings, const VkSampler *immutable_samplers, bool push,
		unsigned num_threads);
/* Destroys the layout, the pools and with them the sets */
void granite_set_allocator_free(granite_set_allocator_t *allocator);
/* A frame begins: each thread's ring moves on when it next asks */
void granite_set_allocator_begin_frame(granite_set_allocator_t *allocator);
/* The set for @hash, and in @cached whether it holds that already.
 * VK_NULL_HANDLE when one cannot be made. */
VkDescriptorSet granite_set_allocator_find(granite_set_allocator_t *allocator, unsigned thread_index,
		uint64_t hash, bool *cached);
/* Destroys the pools and with them the sets */
void granite_set_allocator_clear(granite_set_allocator_t *allocator);

RETRO_END_DECLS

#endif
