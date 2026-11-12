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

#ifndef GRANITE_RESOURCE_BINDINGS_H
#define GRANITE_RESOURCE_BINDINGS_H

#include <stddef.h>
#include <stdint.h>
#include <boolean.h>
#include <retro_common_api.h>
#include "volk.h"
#include "shader.h"
#include "descriptor_set.h"

RETRO_BEGIN_DECLS

#define GRANITE_PUSH_CONSTANT_SIZE 128

/* The pipeline layout descriptor sets are bound against */
typedef struct granite_bind_layout
{
	/* two layouts with one hash are the same layout */
	uint64_t hash;
	VkPipelineLayout layout;
	const granite_combined_layout_t *resource_layout;
	/* [GRANITE_NUM_DESCRIPTOR_SETS] */
	const VkDescriptorUpdateTemplate *update_templates;
	granite_set_allocator_t *allocators[GRANITE_NUM_DESCRIPTOR_SETS];
	/* the sets pushed into the command buffer instead of allocated */
	uint32_t push_set_mask;
} granite_bind_layout_t;

/* What a command buffer has bound, and which of its descriptor sets are
 * behind it.
 *
 * A resource is known by its cookie: binding the one that is bound
 * already changes nothing and dirties nothing. A zero cookie is no
 * resource, so the bindings themselves need no clearing. */
typedef struct granite_resource_bindings
{
	granite_resource_binding_t bindings[GRANITE_NUM_DESCRIPTOR_SETS][GRANITE_NUM_BINDINGS];
	/* the buffer, buffer view or image */
	uint64_t cookies[GRANITE_NUM_DESCRIPTOR_SETS][GRANITE_NUM_BINDINGS];
	/* the sampler */
	uint64_t secondary_cookies[GRANITE_NUM_DESCRIPTOR_SETS][GRANITE_NUM_BINDINGS];
	uint8_t push_constant_data[GRANITE_PUSH_CONSTANT_SIZE];
	/* a bit a set: its contents changed, a set with them has to be found
	 * or written and bound */
	uint32_t dirty_sets;
	/* only uniform buffer offsets changed: the set bound is bound again
	 * with them */
	uint32_t dirty_sets_dynamic;
	VkDescriptorSet allocated_sets[GRANITE_NUM_DESCRIPTOR_SETS];
} granite_resource_bindings_t;

/* Nothing bound, every set dirty, no push constants */
void granite_bindings_init(granite_resource_bindings_t *bindings);
/* A new command buffer context: what was bound is forgotten and every
 * set is dirty. The push constants stay. */
void granite_bindings_invalidate(granite_resource_bindings_t *bindings);

/* A uniform buffer is bound with a dynamic offset: another offset into
 * the same buffer and range only rebinds the set */
void granite_bindings_set_uniform_buffer(granite_resource_bindings_t *bindings, unsigned set, unsigned binding,
		VkBuffer buffer, uint64_t cookie, VkDeviceSize offset, VkDeviceSize range);
void granite_bindings_set_storage_buffer(granite_resource_bindings_t *bindings, unsigned set, unsigned binding,
		VkBuffer buffer, uint64_t cookie, VkDeviceSize offset, VkDeviceSize range);
void granite_bindings_set_buffer_view(granite_resource_bindings_t *bindings, unsigned set, unsigned binding,
		VkBufferView view, uint64_t cookie);
/* The views of an image to sample as float and as integer */
void granite_bindings_set_texture(granite_resource_bindings_t *bindings, unsigned set, unsigned binding,
		VkImageView float_view, VkImageView integer_view, VkImageLayout layout, uint64_t cookie);
/* For a combined image sampler too: the texture is set apart */
void granite_bindings_set_sampler(granite_resource_bindings_t *bindings, unsigned set, unsigned binding,
		VkSampler sampler, uint64_t cookie);

/* The program changed from one of layout @old (NULL: none yet) to one of
 * @layout: dirties the sets the two do not share. True when the push
 * constants have to be pushed again. */
bool granite_bindings_set_layout(granite_resource_bindings_t *bindings, const granite_bind_layout_t *old,
		const granite_bind_layout_t *layout);

/* The hash of what set @set of @set_layout holds: the key its descriptor
 * set is cached under */
uint64_t granite_bindings_hash_set(const granite_resource_bindings_t *bindings,
		const granite_descriptor_set_layout_t *set_layout, unsigned set);

/* Binds the dirty sets @layout uses: a cached set with these contents, or
 * one written now; a push set is pushed. A set that cannot be had stays
 * dirty. */
void granite_bindings_flush(granite_resource_bindings_t *bindings, const granite_bind_layout_t *layout,
		VkDevice device, const struct VolkDeviceTable *table, VkCommandBuffer cmd, VkPipelineBindPoint bind_point,
		unsigned thread_index);

RETRO_END_DECLS

#endif
