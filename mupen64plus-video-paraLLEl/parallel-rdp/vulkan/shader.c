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
#include "shader.h"
#include "logging.h"

#define HASH_PRIME (((uint64_t)0x100 << 32) | 0x000001b3u)
#define HASH_SEED  (((uint64_t)0xcbf29ce4u << 32) | 0x84222325u)

static uint64_t hash_u32(uint64_t h, uint32_t value)
{
	return (h * HASH_PRIME) ^ value;
}

/* @size bytes as 32-bit words */
static uint64_t hash_words(uint64_t h, const void *data, size_t size)
{
	const uint8_t *bytes = (const uint8_t *)data;
	size_t i;

	for (i = 0; i + sizeof(uint32_t) <= size; i += sizeof(uint32_t))
	{
		uint32_t word;
		memcpy(&word, bytes + i, sizeof(word));
		h = hash_u32(h, word);
	}
	return h;
}

static uint32_t bindings_of(const granite_descriptor_set_layout_t *set)
{
	return set->sampled_image_mask | set->storage_image_mask | set->uniform_buffer_mask |
		set->storage_buffer_mask | set->sampled_texel_buffer_mask | set->storage_texel_buffer_mask |
		set->input_attachment_mask | set->sampler_mask | set->separate_image_mask;
}

bool granite_resource_layout_unserialize(granite_resource_layout_t *layout, const uint8_t *data, size_t size)
{
	/* in the reader's byte order: a layout stored in the other one does
	 * not match */
	uint16_t magic[4];

	magic[0] = 'G';
	magic[1] = 'R';
	magic[2] = 'A';
	magic[3] = GRANITE_RESOURCE_LAYOUT_VERSION;

	if (size != GRANITE_RESOURCE_LAYOUT_STORED_SIZE)
	{
		LOGE("Reflection size mismatch.\n");
		return false;
	}

	if (memcmp(data, magic, sizeof(magic)) != 0)
	{
		LOGE("Magic mismatch.\n");
		return false;
	}

	memcpy(layout, data + sizeof(magic), sizeof(*layout));
	return true;
}

void granite_combined_layout_init(granite_combined_layout_t *combined)
{
	memset(combined, 0, sizeof(*combined));
}

bool granite_combined_layout_add(granite_combined_layout_t *combined, unsigned stage,
		const granite_resource_layout_t *layout)
{
	uint32_t stage_mask = (uint32_t)1 << stage;
	bool ret = true;
	unsigned set, bit;

	if (stage == GRANITE_SHADER_STAGE_VERTEX)
		combined->attribute_mask = layout->input_mask;
	else if (stage == GRANITE_SHADER_STAGE_FRAGMENT)
		combined->render_target_mask = layout->output_mask;

	for (set = 0; set < GRANITE_NUM_DESCRIPTOR_SETS; set++)
	{
		const granite_descriptor_set_layout_t *from = &layout->sets[set];
		granite_descriptor_set_layout_t *to = &combined->sets[set];
		uint32_t active = bindings_of(from);

		to->sampled_image_mask |= from->sampled_image_mask;
		to->storage_image_mask |= from->storage_image_mask;
		to->uniform_buffer_mask |= from->uniform_buffer_mask;
		to->storage_buffer_mask |= from->storage_buffer_mask;
		to->sampled_texel_buffer_mask |= from->sampled_texel_buffer_mask;
		to->storage_texel_buffer_mask |= from->storage_texel_buffer_mask;
		to->input_attachment_mask |= from->input_attachment_mask;
		to->sampler_mask |= from->sampler_mask;
		to->separate_image_mask |= from->separate_image_mask;
		to->fp_mask |= from->fp_mask;
		to->immutable_sampler_mask |= from->immutable_sampler_mask;

		if (active)
			combined->stages_for_sets[set] |= stage_mask;

		for (bit = 0; bit < GRANITE_NUM_BINDINGS; bit++)
		{
			if (!(active & ((uint32_t)1 << bit)))
				continue;

			combined->stages_for_bindings[set][bit] |= stage_mask;
			if (to->array_size[bit] && to->array_size[bit] != from->array_size[bit])
			{
				LOGE("Mismatch between array sizes in different shaders.\n");
				ret = false;
			}
			else
				to->array_size[bit] = from->array_size[bit];
		}
	}

	/* One range for all the stages: splitting it gains nothing */
	if (layout->push_constant_size != 0)
	{
		combined->push_constant_range.stageFlags |= stage_mask;
		if (combined->push_constant_range.size < layout->push_constant_size)
			combined->push_constant_range.size = layout->push_constant_size;
	}

	combined->spec_constant_mask[stage] = layout->spec_constant_mask;
	combined->combined_spec_constant_mask |= layout->spec_constant_mask;
	return ret;
}

bool granite_combined_layout_bake(granite_combined_layout_t *combined)
{
	bool ret = true;
	unsigned set, binding, i;
	uint64_t h;

	combined->descriptor_set_mask = 0;
	for (set = 0; set < GRANITE_NUM_DESCRIPTOR_SETS; set++)
	{
		if (combined->stages_for_sets[set] == 0)
			continue;

		combined->descriptor_set_mask |= (uint32_t)1 << set;

		for (binding = 0; binding < GRANITE_NUM_BINDINGS; binding++)
		{
			unsigned array_size = combined->sets[set].array_size[binding];

			if (array_size == 0)
			{
				combined->sets[set].array_size[binding] = 1;
				continue;
			}

			if (binding + array_size > GRANITE_NUM_BINDINGS)
			{
				LOGE("Binding array with %u elements starting at (%u, %u) runs past the last binding.\n",
						array_size, set, binding);
				ret = false;
			}

			for (i = 1; i < array_size && binding + i < GRANITE_NUM_BINDINGS; i++)
			{
				if (combined->stages_for_bindings[set][binding + i] != 0)
				{
					LOGE("Detected binding aliasing for (%u, %u). Binding array with %u elements starting at (%u, %u) overlaps.\n",
							set, binding + i, array_size, set, binding);
					ret = false;
				}
			}
		}
	}

	h = HASH_SEED;
	h = hash_u32(h, combined->push_constant_range.stageFlags);
	h = hash_u32(h, combined->push_constant_range.size);
	combined->push_constant_layout_hash = h;
	return ret;
}

uint64_t granite_combined_layout_hash(const granite_combined_layout_t *combined)
{
	uint64_t h = HASH_SEED;

	h = hash_words(h, combined->sets, sizeof(combined->sets));
	h = hash_words(h, combined->stages_for_bindings, sizeof(combined->stages_for_bindings));
	h = hash_u32(h, combined->push_constant_range.stageFlags);
	h = hash_u32(h, combined->push_constant_range.size);
	h = hash_words(h, combined->spec_constant_mask, sizeof(combined->spec_constant_mask));
	h = hash_u32(h, combined->attribute_mask);
	h = hash_u32(h, combined->render_target_mask);
	return h;
}

uint32_t granite_combined_layout_push_set_mask(const granite_combined_layout_t *combined,
		bool supports_push_descriptor, uint32_t max_push_descriptors)
{
	uint32_t count = 0;
	unsigned binding;

	if (!supports_push_descriptor || !(combined->descriptor_set_mask & 1u))
		return 0;

	for (binding = 0; binding < GRANITE_NUM_BINDINGS; binding++)
		if (combined->stages_for_bindings[0][binding])
			count += combined->sets[0].array_size[binding];

	return (count && count <= max_push_descriptors) ? 1u : 0u;
}

bool granite_pipeline_layout_init(granite_pipeline_layout_t *pipeline_layout, VkDevice device,
		const struct VolkDeviceTable *table, const granite_combined_layout_t *combined,
		const VkDescriptorSetLayout *set_layouts, uint32_t push_set_mask, uint32_t max_bound_sets)
{
	/* where a template finds each kind of descriptor in a
	 * granite_resource_binding_t */
	const size_t buffer_offset = offsetof(granite_resource_binding_t, u.buffer);
	const size_t view_offset = offsetof(granite_resource_binding_t, u.buffer_view);
	const size_t fp_offset = offsetof(granite_resource_binding_t, u.image.fp);
	const size_t integer_offset = offsetof(granite_resource_binding_t, u.image.integer);
	VkPipelineLayoutCreateInfo info;
	unsigned num_sets = 0;
	bool ret = true;
	unsigned set, kind, binding;

	memset(pipeline_layout, 0, sizeof(*pipeline_layout));
	pipeline_layout->device = device;
	pipeline_layout->table = table;

	for (set = 0; set < GRANITE_NUM_DESCRIPTOR_SETS; set++)
		if (combined->descriptor_set_mask & ((uint32_t)1 << set))
			num_sets = set + 1;

	if (num_sets > max_bound_sets)
		LOGE("Number of sets %u exceeds device limit of %u.\n", num_sets, (unsigned)max_bound_sets);

	memset(&info, 0, sizeof(info));
	info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	if (num_sets)
	{
		info.setLayoutCount = num_sets;
		info.pSetLayouts = set_layouts;
	}

	if (combined->push_constant_range.stageFlags != 0)
	{
		info.pushConstantRangeCount = 1;
		info.pPushConstantRanges = &combined->push_constant_range;
	}

	if (table->vkCreatePipelineLayout(device, &info, NULL, &pipeline_layout->layout) != VK_SUCCESS)
	{
		LOGE("Failed to create pipeline layout.\n");
		pipeline_layout->layout = VK_NULL_HANDLE;
		return false;
	}

	for (set = 0; set < GRANITE_NUM_DESCRIPTOR_SETS; set++)
	{
		const granite_descriptor_set_layout_t *set_layout = &combined->sets[set];
		bool is_push = (push_set_mask & ((uint32_t)1 << set)) != 0;
		VkDescriptorUpdateTemplateEntry entries[GRANITE_NUM_BINDINGS];
		VkDescriptorUpdateTemplateCreateInfo template_info;
		/* the kinds in the order the entries have always been listed */
		VkDescriptorType types[9];
		uint32_t masks[9];
		/* whether the kind is an image, with a float and an integer view */
		bool image[9];
		size_t offsets[9];
		uint32_t count = 0;

		if (!(combined->descriptor_set_mask & ((uint32_t)1 << set)))
			continue;

		types[0] = is_push ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
		masks[0] = set_layout->uniform_buffer_mask;
		offsets[0] = buffer_offset;
		image[0] = false;
		types[1] = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		masks[1] = set_layout->storage_buffer_mask;
		offsets[1] = buffer_offset;
		image[1] = false;
		types[2] = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
		masks[2] = set_layout->sampled_texel_buffer_mask;
		offsets[2] = view_offset;
		image[2] = false;
		types[3] = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
		masks[3] = set_layout->storage_texel_buffer_mask;
		offsets[3] = view_offset;
		image[3] = false;
		types[4] = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		masks[4] = set_layout->sampled_image_mask;
		offsets[4] = integer_offset;
		image[4] = true;
		types[5] = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
		masks[5] = set_layout->separate_image_mask;
		offsets[5] = integer_offset;
		image[5] = true;
		/* an immutable sampler is the layout's own: nothing to write */
		types[6] = VK_DESCRIPTOR_TYPE_SAMPLER;
		masks[6] = set_layout->sampler_mask & ~set_layout->immutable_sampler_mask;
		offsets[6] = fp_offset;
		image[6] = false;
		types[7] = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		masks[7] = set_layout->storage_image_mask;
		offsets[7] = integer_offset;
		image[7] = true;
		types[8] = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
		masks[8] = set_layout->input_attachment_mask;
		offsets[8] = integer_offset;
		image[8] = true;

		for (kind = 0; kind < 9; kind++)
		{
			for (binding = 0; binding < GRANITE_NUM_BINDINGS; binding++)
			{
				VkDescriptorUpdateTemplateEntry *entry;
				size_t offset = offsets[kind];

				if (!(masks[kind] & ((uint32_t)1 << binding)))
					continue;
				/* a binding of two kinds is no layout the driver takes */
				if (count == GRANITE_NUM_BINDINGS)
					break;

				if (image[kind] && (set_layout->fp_mask & ((uint32_t)1 << binding)))
					offset = fp_offset;

				entry = &entries[count++];
				entry->descriptorType = types[kind];
				entry->dstBinding = binding;
				entry->dstArrayElement = 0;
				entry->descriptorCount = set_layout->array_size[binding];
				entry->offset = offset + sizeof(granite_resource_binding_t) * binding;
				entry->stride = sizeof(granite_resource_binding_t);
			}
		}

		memset(&template_info, 0, sizeof(template_info));
		template_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_UPDATE_TEMPLATE_CREATE_INFO;
		template_info.pipelineLayout = pipeline_layout->layout;
		template_info.descriptorSetLayout = set_layouts[set];
		template_info.templateType = is_push ? VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_PUSH_DESCRIPTORS_KHR :
			VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_DESCRIPTOR_SET;
		template_info.set = set;
		template_info.descriptorUpdateEntryCount = count;
		template_info.pDescriptorUpdateEntries = entries;
		template_info.pipelineBindPoint = (combined->stages_for_sets[set] & VK_SHADER_STAGE_COMPUTE_BIT) ?
			VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_GRAPHICS;

		if (table->vkCreateDescriptorUpdateTemplate(device, &template_info, NULL,
					&pipeline_layout->update_template[set]) != VK_SUCCESS)
		{
			LOGE("Failed to create descriptor update template.\n");
			pipeline_layout->update_template[set] = VK_NULL_HANDLE;
			ret = false;
		}
	}

	return ret;
}

void granite_pipeline_layout_free(granite_pipeline_layout_t *pipeline_layout)
{
	unsigned set;

	if (!pipeline_layout->table)
		return;

	for (set = 0; set < GRANITE_NUM_DESCRIPTOR_SETS; set++)
		if (pipeline_layout->update_template[set] != VK_NULL_HANDLE)
			pipeline_layout->table->vkDestroyDescriptorUpdateTemplate(pipeline_layout->device,
					pipeline_layout->update_template[set], NULL);

	if (pipeline_layout->layout != VK_NULL_HANDLE)
		pipeline_layout->table->vkDestroyPipelineLayout(pipeline_layout->device, pipeline_layout->layout, NULL);

	memset(pipeline_layout, 0, sizeof(*pipeline_layout));
}
