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

#ifndef GRANITE_SHADER_H
#define GRANITE_SHADER_H

#include <stddef.h>
#include <stdint.h>
#include <boolean.h>
#include <retro_common_api.h>
#include "volk.h"
#include "descriptor_set.h"

RETRO_BEGIN_DECLS

#define GRANITE_NUM_DESCRIPTOR_SETS     4
#define GRANITE_RESOURCE_LAYOUT_VERSION 3

/* The bit of each in VkShaderStageFlags is 1 << stage */
enum granite_shader_stage
{
	GRANITE_SHADER_STAGE_VERTEX = 0,
	GRANITE_SHADER_STAGE_TESS_CONTROL,
	GRANITE_SHADER_STAGE_TESS_EVALUATION,
	GRANITE_SHADER_STAGE_GEOMETRY,
	GRANITE_SHADER_STAGE_FRAGMENT,
	GRANITE_SHADER_STAGE_COMPUTE,
	GRANITE_SHADER_STAGE_COUNT
};

/* What one shader takes and gives. The shader bank stores these as they
 * lie, after a magic: no holes, and no member may move. */
typedef struct granite_resource_layout
{
	granite_descriptor_set_layout_t sets[GRANITE_NUM_DESCRIPTOR_SETS];
	uint32_t input_mask;
	uint32_t output_mask;
	uint32_t push_constant_size;
	uint32_t spec_constant_mask;
	/* in the stored form; never set */
	uint32_t bindless_set_mask;
} granite_resource_layout_t;

/* The size of a stored layout: the magic and the struct */
#define GRANITE_RESOURCE_LAYOUT_STORED_SIZE (4 * sizeof(uint16_t) + sizeof(granite_resource_layout_t))

/* What the shaders of a program take and give between them */
typedef struct granite_combined_layout
{
	uint32_t attribute_mask;
	uint32_t render_target_mask;
	granite_descriptor_set_layout_t sets[GRANITE_NUM_DESCRIPTOR_SETS];
	uint32_t stages_for_bindings[GRANITE_NUM_DESCRIPTOR_SETS][GRANITE_NUM_BINDINGS];
	uint32_t stages_for_sets[GRANITE_NUM_DESCRIPTOR_SETS];
	VkPushConstantRange push_constant_range;
	uint32_t descriptor_set_mask;
	uint32_t spec_constant_mask[GRANITE_SHADER_STAGE_COUNT];
	uint32_t combined_spec_constant_mask;
	uint64_t push_constant_layout_hash;
} granite_combined_layout_t;

/* What is bound to one binding. A descriptor update template reads an
 * array of these, GRANITE_NUM_BINDINGS to a set. */
typedef struct granite_resource_binding
{
	union
	{
		VkDescriptorBufferInfo buffer;
		struct
		{
			/* the view to sample as float, and as integer */
			VkDescriptorImageInfo fp;
			VkDescriptorImageInfo integer;
		} image;
		VkBufferView buffer_view;
	} u;
	VkDeviceSize dynamic_offset;
} granite_resource_binding_t;

/* A pipeline layout and the update template of each set in it */
typedef struct granite_pipeline_layout
{
	VkDevice device;
	const struct VolkDeviceTable *table;
	VkPipelineLayout layout;
	VkDescriptorUpdateTemplate update_template[GRANITE_NUM_DESCRIPTOR_SETS];
} granite_pipeline_layout_t;

/* Reads a stored layout: @size is GRANITE_RESOURCE_LAYOUT_STORED_SIZE.
 * False, and @layout as it was, for one of another size, version or
 * byte order. */
bool granite_resource_layout_unserialize(granite_resource_layout_t *layout, const uint8_t *data, size_t size);

/* Combining: init, add each shader with its stage, then bake */
void granite_combined_layout_init(granite_combined_layout_t *combined);
/* False when @layout has another array size for a binding than a shader
 * added before it: the first one stays */
bool granite_combined_layout_add(granite_combined_layout_t *combined, unsigned stage,
		const granite_resource_layout_t *layout);
/* False when a binding lies inside the array of another */
bool granite_combined_layout_bake(granite_combined_layout_t *combined);
/* Two baked layouts with one hash make the same pipeline layout */
uint64_t granite_combined_layout_hash(const granite_combined_layout_t *combined);
/* The sets to push into the command buffer instead of allocating: set 0,
 * when the device pushes descriptors and takes it whole. Vulkan allows
 * one push set a layout. It depends on the set alone, so layouts that
 * share set 0 choose alike and stay compatible. */
uint32_t granite_combined_layout_push_set_mask(const granite_combined_layout_t *combined,
		bool supports_push_descriptor, uint32_t max_push_descriptors);

/* @set_layouts[GRANITE_NUM_DESCRIPTOR_SETS]: the layout of each set of
 * @combined, baked; @push_set_mask: those of them that are push layouts.
 * False when the driver refuses the layout or a template: what it did
 * make is kept, the rest VK_NULL_HANDLE. */
bool granite_pipeline_layout_init(granite_pipeline_layout_t *pipeline_layout, VkDevice device,
		const struct VolkDeviceTable *table, const granite_combined_layout_t *combined,
		const VkDescriptorSetLayout *set_layouts, uint32_t push_set_mask, uint32_t max_bound_sets);
void granite_pipeline_layout_free(granite_pipeline_layout_t *pipeline_layout);

RETRO_END_DECLS

#endif
