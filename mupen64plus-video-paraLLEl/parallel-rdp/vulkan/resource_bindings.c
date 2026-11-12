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

#include <string.h>
#include "resource_bindings.h"

#define HASH_PRIME (((uint64_t)0x100 << 32) | 0x000001b3u)
#define HASH_SEED  (((uint64_t)0xcbf29ce4u << 32) | 0x84222325u)

static uint64_t hash_u32(uint64_t h, uint32_t value)
{
	return (h * HASH_PRIME) ^ value;
}

static uint64_t hash_u64(uint64_t h, uint64_t value)
{
	h = hash_u32(h, (uint32_t)(value & 0xffffffffu));
	return hash_u32(h, (uint32_t)(value >> 32));
}

/* The elements of the array at @binding that are bindings of the set */
static unsigned array_size_of(const granite_descriptor_set_layout_t *set_layout, unsigned binding)
{
	unsigned size = set_layout->array_size[binding];

	if (binding + size > GRANITE_NUM_BINDINGS)
		size = GRANITE_NUM_BINDINGS - binding;
	return size;
}

void granite_bindings_init(granite_resource_bindings_t *bindings)
{
	memset(bindings->push_constant_data, 0, sizeof(bindings->push_constant_data));
	memset(bindings->allocated_sets, 0, sizeof(bindings->allocated_sets));
	bindings->dirty_sets_dynamic = 0;
	granite_bindings_invalidate(bindings);
}

void granite_bindings_invalidate(granite_resource_bindings_t *bindings)
{
	memset(bindings->cookies, 0, sizeof(bindings->cookies));
	memset(bindings->secondary_cookies, 0, sizeof(bindings->secondary_cookies));
	bindings->dirty_sets = ~(uint32_t)0;
}

void granite_bindings_set_uniform_buffer(granite_resource_bindings_t *bindings, unsigned set, unsigned binding,
		VkBuffer buffer, uint64_t cookie, VkDeviceSize offset, VkDeviceSize range)
{
	granite_resource_binding_t *b = &bindings->bindings[set][binding];

	if (cookie == bindings->cookies[set][binding] && b->u.buffer.range == range)
	{
		if (b->dynamic_offset != offset)
		{
			bindings->dirty_sets_dynamic |= (uint32_t)1 << set;
			b->dynamic_offset = offset;
		}
		return;
	}

	b->u.buffer.buffer = buffer;
	b->u.buffer.offset = 0;
	b->u.buffer.range = range;
	b->dynamic_offset = offset;
	bindings->cookies[set][binding] = cookie;
	bindings->secondary_cookies[set][binding] = 0;
	bindings->dirty_sets |= (uint32_t)1 << set;
}

void granite_bindings_set_storage_buffer(granite_resource_bindings_t *bindings, unsigned set, unsigned binding,
		VkBuffer buffer, uint64_t cookie, VkDeviceSize offset, VkDeviceSize range)
{
	granite_resource_binding_t *b = &bindings->bindings[set][binding];

	if (cookie == bindings->cookies[set][binding] && b->u.buffer.offset == offset && b->u.buffer.range == range)
		return;

	b->u.buffer.buffer = buffer;
	b->u.buffer.offset = offset;
	b->u.buffer.range = range;
	b->dynamic_offset = 0;
	bindings->cookies[set][binding] = cookie;
	bindings->secondary_cookies[set][binding] = 0;
	bindings->dirty_sets |= (uint32_t)1 << set;
}

void granite_bindings_set_buffer_view(granite_resource_bindings_t *bindings, unsigned set, unsigned binding,
		VkBufferView view, uint64_t cookie)
{
	if (cookie == bindings->cookies[set][binding])
		return;

	bindings->bindings[set][binding].u.buffer_view = view;
	bindings->cookies[set][binding] = cookie;
	bindings->secondary_cookies[set][binding] = 0;
	bindings->dirty_sets |= (uint32_t)1 << set;
}

void granite_bindings_set_texture(granite_resource_bindings_t *bindings, unsigned set, unsigned binding,
		VkImageView float_view, VkImageView integer_view, VkImageLayout layout, uint64_t cookie)
{
	granite_resource_binding_t *b = &bindings->bindings[set][binding];

	if (cookie == bindings->cookies[set][binding] && b->u.image.fp.imageLayout == layout)
		return;

	b->u.image.fp.imageLayout = layout;
	b->u.image.fp.imageView = float_view;
	b->u.image.integer.imageLayout = layout;
	b->u.image.integer.imageView = integer_view;
	bindings->cookies[set][binding] = cookie;
	bindings->dirty_sets |= (uint32_t)1 << set;
}

void granite_bindings_set_sampler(granite_resource_bindings_t *bindings, unsigned set, unsigned binding,
		VkSampler sampler, uint64_t cookie)
{
	granite_resource_binding_t *b = &bindings->bindings[set][binding];

	if (cookie == bindings->secondary_cookies[set][binding])
		return;

	b->u.image.fp.sampler = sampler;
	b->u.image.integer.sampler = sampler;
	bindings->secondary_cookies[set][binding] = cookie;
	bindings->dirty_sets |= (uint32_t)1 << set;
}

bool granite_bindings_set_layout(granite_resource_bindings_t *bindings, const granite_bind_layout_t *old,
		const granite_bind_layout_t *layout)
{
	unsigned set;

	if (old && old->hash == layout->hash)
		return false;

	/* Sets stay bound across layouts that agree on the push constants
	 * and on the sets before them */
	if (!old || old->resource_layout->push_constant_layout_hash != layout->resource_layout->push_constant_layout_hash)
	{
		bindings->dirty_sets = ~(uint32_t)0;
		return true;
	}

	for (set = 0; set < GRANITE_NUM_DESCRIPTOR_SETS; set++)
	{
		if (old->allocators[set] != layout->allocators[set])
		{
			bindings->dirty_sets |= ~(((uint32_t)1 << set) - 1);
			break;
		}
	}
	return false;
}

uint64_t granite_bindings_hash_set(const granite_resource_bindings_t *bindings,
		const granite_descriptor_set_layout_t *set_layout, unsigned set)
{
	const granite_resource_binding_t *b = bindings->bindings[set];
	const uint64_t *cookies = bindings->cookies[set];
	const uint64_t *samplers = bindings->secondary_cookies[set];
	uint64_t h = HASH_SEED;
	unsigned binding, i, n;

	h = hash_u32(h, set_layout->fp_mask);

	/* uniform buffers: not their offsets, which are dynamic */
	for (binding = 0; binding < GRANITE_NUM_BINDINGS; binding++)
	{
		if (!(set_layout->uniform_buffer_mask & ((uint32_t)1 << binding)))
			continue;
		n = array_size_of(set_layout, binding);
		for (i = 0; i < n; i++)
		{
			h = hash_u64(h, cookies[binding + i]);
			h = hash_u32(h, (uint32_t)b[binding + i].u.buffer.range);
		}
	}

	/* storage buffers */
	for (binding = 0; binding < GRANITE_NUM_BINDINGS; binding++)
	{
		if (!(set_layout->storage_buffer_mask & ((uint32_t)1 << binding)))
			continue;
		n = array_size_of(set_layout, binding);
		for (i = 0; i < n; i++)
		{
			h = hash_u64(h, cookies[binding + i]);
			h = hash_u32(h, (uint32_t)b[binding + i].u.buffer.offset);
			h = hash_u32(h, (uint32_t)b[binding + i].u.buffer.range);
		}
	}

	/* texel buffers */
	for (binding = 0; binding < GRANITE_NUM_BINDINGS; binding++)
	{
		if (!((set_layout->sampled_texel_buffer_mask | set_layout->storage_texel_buffer_mask) & ((uint32_t)1 << binding)))
			continue;
		n = array_size_of(set_layout, binding);
		for (i = 0; i < n; i++)
			h = hash_u64(h, cookies[binding + i]);
	}

	/* sampled images, with their samplers unless those are the layout's */
	for (binding = 0; binding < GRANITE_NUM_BINDINGS; binding++)
	{
		if (!(set_layout->sampled_image_mask & ((uint32_t)1 << binding)))
			continue;
		n = array_size_of(set_layout, binding);
		for (i = 0; i < n; i++)
		{
			h = hash_u64(h, cookies[binding + i]);
			if (!(set_layout->immutable_sampler_mask & ((uint32_t)1 << (binding + i))))
				h = hash_u64(h, samplers[binding + i]);
			h = hash_u32(h, (uint32_t)b[binding + i].u.image.fp.imageLayout);
		}
	}

	/* separate images */
	for (binding = 0; binding < GRANITE_NUM_BINDINGS; binding++)
	{
		if (!(set_layout->separate_image_mask & ((uint32_t)1 << binding)))
			continue;
		n = array_size_of(set_layout, binding);
		for (i = 0; i < n; i++)
		{
			h = hash_u64(h, cookies[binding + i]);
			h = hash_u32(h, (uint32_t)b[binding + i].u.image.fp.imageLayout);
		}
	}

	/* separate samplers */
	for (binding = 0; binding < GRANITE_NUM_BINDINGS; binding++)
	{
		if (!(set_layout->sampler_mask & ~set_layout->immutable_sampler_mask & ((uint32_t)1 << binding)))
			continue;
		n = array_size_of(set_layout, binding);
		for (i = 0; i < n; i++)
			h = hash_u64(h, samplers[binding + i]);
	}

	/* storage images */
	for (binding = 0; binding < GRANITE_NUM_BINDINGS; binding++)
	{
		if (!(set_layout->storage_image_mask & ((uint32_t)1 << binding)))
			continue;
		n = array_size_of(set_layout, binding);
		for (i = 0; i < n; i++)
		{
			h = hash_u64(h, cookies[binding + i]);
			h = hash_u32(h, (uint32_t)b[binding + i].u.image.fp.imageLayout);
		}
	}

	/* input attachments */
	for (binding = 0; binding < GRANITE_NUM_BINDINGS; binding++)
	{
		if (!(set_layout->input_attachment_mask & ((uint32_t)1 << binding)))
			continue;
		n = array_size_of(set_layout, binding);
		for (i = 0; i < n; i++)
		{
			h = hash_u64(h, cookies[binding + i]);
			h = hash_u32(h, (uint32_t)b[binding + i].u.image.fp.imageLayout);
		}
	}

	return h;
}

/* The dynamic offsets of the set's uniform buffers, in binding order */
static uint32_t dynamic_offsets_of(const granite_resource_bindings_t *bindings,
		const granite_descriptor_set_layout_t *set_layout, unsigned set, uint32_t *offsets)
{
	uint32_t count = 0;
	unsigned binding, i, n;

	for (binding = 0; binding < GRANITE_NUM_BINDINGS; binding++)
	{
		if (!(set_layout->uniform_buffer_mask & ((uint32_t)1 << binding)))
			continue;
		n = array_size_of(set_layout, binding);
		for (i = 0; i < n && count < GRANITE_NUM_BINDINGS; i++)
			offsets[count++] = (uint32_t)bindings->bindings[set][binding + i].dynamic_offset;
	}
	return count;
}

/* A push set has plain uniform buffers: the dynamic offset goes into the
 * descriptor for the push. The stored offset stays zero, for layouts
 * that bind the same state as a dynamic uniform buffer. */
static void push_set(granite_resource_bindings_t *bindings, const granite_bind_layout_t *layout,
		const struct VolkDeviceTable *table, VkCommandBuffer cmd, unsigned set)
{
	const granite_descriptor_set_layout_t *set_layout = &layout->resource_layout->sets[set];
	granite_resource_binding_t *b = bindings->bindings[set];
	unsigned binding, i, n;

	for (binding = 0; binding < GRANITE_NUM_BINDINGS; binding++)
	{
		if (!(set_layout->uniform_buffer_mask & ((uint32_t)1 << binding)))
			continue;
		n = array_size_of(set_layout, binding);
		for (i = 0; i < n; i++)
			b[binding + i].u.buffer.offset = b[binding + i].dynamic_offset;
	}

	table->vkCmdPushDescriptorSetWithTemplateKHR(cmd, layout->update_templates[set], layout->layout, set, b);

	for (binding = 0; binding < GRANITE_NUM_BINDINGS; binding++)
	{
		if (!(set_layout->uniform_buffer_mask & ((uint32_t)1 << binding)))
			continue;
		n = array_size_of(set_layout, binding);
		for (i = 0; i < n; i++)
			b[binding + i].u.buffer.offset = 0;
	}
}

void granite_bindings_flush(granite_resource_bindings_t *bindings, const granite_bind_layout_t *layout,
		VkDevice device, const struct VolkDeviceTable *table, VkCommandBuffer cmd, VkPipelineBindPoint bind_point,
		unsigned thread_index)
{
	uint32_t used = layout->resource_layout->descriptor_set_mask;
	uint32_t update = used & bindings->dirty_sets;
	uint32_t rebind, failed = 0;
	uint32_t offsets[GRANITE_NUM_BINDINGS];
	unsigned set;

	/* a set that is updated is bound with its offsets as well */
	rebind = used & bindings->dirty_sets_dynamic & ~update;
	if (!(update | rebind))
		return;

	for (set = 0; set < GRANITE_NUM_DESCRIPTOR_SETS; set++)
	{
		const granite_descriptor_set_layout_t *set_layout = &layout->resource_layout->sets[set];
		uint32_t bit = (uint32_t)1 << set;
		uint32_t count;

		if (!((update | rebind) & bit))
			continue;

		if (layout->push_set_mask & bit)
		{
			push_set(bindings, layout, table, cmd, set);
			continue;
		}

		if (update & bit)
		{
			bool cached = false;
			VkDescriptorSet descriptor_set = granite_set_allocator_find(layout->allocators[set], thread_index,
					granite_bindings_hash_set(bindings, set_layout, set), &cached);

			if (descriptor_set == VK_NULL_HANDLE)
			{
				failed |= bit;
				continue;
			}

			if (!cached)
				table->vkUpdateDescriptorSetWithTemplate(device, descriptor_set, layout->update_templates[set],
						bindings->bindings[set]);
			bindings->allocated_sets[set] = descriptor_set;
		}
		else if (bindings->allocated_sets[set] == VK_NULL_HANDLE)
		{
			/* never had one: it is found when its contents are flushed */
			failed |= bit;
			continue;
		}

		/* only offsets changed: the set is bound again as it is, with no
		 * hashing or lookup */
		count = dynamic_offsets_of(bindings, set_layout, set, offsets);
		table->vkCmdBindDescriptorSets(cmd, bind_point, layout->layout, set, 1, &bindings->allocated_sets[set],
				count, offsets);
	}

	bindings->dirty_sets &= ~update | failed;
	bindings->dirty_sets_dynamic &= ~(update | rebind) | failed;
}
