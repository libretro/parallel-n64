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
#include "pipeline_compile.h"
#include "logging.h"

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

static bool needs_blend_constant(unsigned factor)
{
	return factor == VK_BLEND_FACTOR_CONSTANT_COLOR || factor == VK_BLEND_FACTOR_CONSTANT_ALPHA;
}

/* The specialization constants set, the internal ones after the user's */
static uint32_t spec_constants_set(const granite_pipeline_compile_t *compile)
{
	return compile->potential_static_state.spec_constant_mask |
		((uint32_t)compile->potential_static_state.internal_spec_constant_mask << GRANITE_NUM_USER_SPEC_CONSTANTS);
}

static uint64_t hash_spec_constants(uint64_t h, const granite_pipeline_compile_t *compile,
		const granite_pipeline_program_t *program)
{
	uint32_t mask = program->resource_layout->combined_spec_constant_mask & spec_constants_set(compile);
	unsigned bit;

	h = hash_u32(h, mask);
	for (bit = 0; bit < GRANITE_NUM_TOTAL_SPEC_CONSTANTS; bit++)
		if (mask & ((uint32_t)1 << bit))
			h = hash_u32(h, compile->potential_static_state.spec_constants[bit]);
	return h;
}

uint64_t granite_pipeline_hash_compute(const granite_pipeline_compile_t *compile,
		const granite_pipeline_program_t *program)
{
	uint64_t h = HASH_SEED;

	h = hash_u64(h, program->hash);
	h = hash_spec_constants(h, compile, program);

	if (compile->static_state.state.subgroup_control_size)
	{
		h = hash_u32(h, 1);
		h = hash_u32(h, compile->static_state.state.subgroup_minimum_size_log2);
		h = hash_u32(h, compile->static_state.state.subgroup_maximum_size_log2);
		h = hash_u32(h, compile->static_state.state.subgroup_full_group);
		/* what the sizes come to depends on the device */
		h = hash_u32(h, compile->subgroup_size_tag);
	}
	else
		h = hash_u32(h, 0);

	return h;
}

uint64_t granite_pipeline_hash_graphics(const granite_pipeline_compile_t *compile,
		const granite_pipeline_program_t *program, uint64_t render_pass_hash, uint32_t *active_vbos)
{
	uint64_t h = HASH_SEED;
	uint32_t vbos = 0;
	unsigned bit, i;

	for (bit = 0; bit < GRANITE_NUM_VERTEX_ATTRIBS; bit++)
	{
		if (!(program->resource_layout->attribute_mask & ((uint32_t)1 << bit)))
			continue;

		h = hash_u32(h, bit);
		vbos |= (uint32_t)1 << compile->attribs[bit].binding;
		h = hash_u32(h, compile->attribs[bit].binding);
		h = hash_u32(h, (uint32_t)compile->attribs[bit].format);
		h = hash_u32(h, compile->attribs[bit].offset);
	}

	for (bit = 0; bit < GRANITE_NUM_VERTEX_BUFFERS; bit++)
	{
		if (!(vbos & ((uint32_t)1 << bit)))
			continue;

		h = hash_u32(h, (uint32_t)compile->input_rates[bit]);
		h = hash_u32(h, (uint32_t)compile->strides[bit]);
	}

	h = hash_u64(h, render_pass_hash);
	h = hash_u32(h, compile->subpass_index);
	h = hash_u64(h, program->hash);
	for (i = 0; i < 4; i++)
		h = hash_u32(h, compile->static_state.words[i]);

	if (compile->static_state.state.blend_enable &&
			(needs_blend_constant(compile->static_state.state.src_color_blend) ||
			 needs_blend_constant(compile->static_state.state.src_alpha_blend) ||
			 needs_blend_constant(compile->static_state.state.dst_color_blend) ||
			 needs_blend_constant(compile->static_state.state.dst_alpha_blend)))
	{
		uint32_t words[4];

		memcpy(words, compile->potential_static_state.blend_constants, sizeof(words));
		for (i = 0; i < 4; i++)
			h = hash_u32(h, words[i]);
	}

	h = hash_spec_constants(h, compile, program);

	*active_vbos = vbos;
	return h;
}

/* The specialization of one stage: the constants of @stage_mask that are
 * set. NULL when there are none. */
static const VkSpecializationInfo *fill_specialization(VkSpecializationInfo *info, VkSpecializationMapEntry *entries,
		uint32_t *data, const granite_pipeline_compile_t *compile, uint32_t stage_mask)
{
	uint32_t mask = stage_mask & spec_constants_set(compile);
	unsigned bit;

	if (!mask)
		return NULL;

	memset(info, 0, sizeof(*info));
	info->pData = data;
	info->pMapEntries = entries;
	for (bit = 0; bit < GRANITE_NUM_TOTAL_SPEC_CONSTANTS; bit++)
	{
		VkSpecializationMapEntry *entry;

		if (!(mask & ((uint32_t)1 << bit)))
			continue;

		entry = &entries[info->mapEntryCount];
		entry->offset = sizeof(uint32_t) * info->mapEntryCount;
		entry->size = sizeof(uint32_t);
		entry->constantID = bit;
		data[info->mapEntryCount] = compile->potential_static_state.spec_constants[bit];
		info->mapEntryCount++;
	}
	info->dataSize = info->mapEntryCount * sizeof(uint32_t);
	return info;
}

VkPipeline granite_pipeline_build_compute(const granite_pipeline_device_t *device,
		const granite_pipeline_compile_t *compile, const granite_pipeline_program_t *program,
		bool cached_only, VkResult *result)
{
	VkComputePipelineCreateInfo info;
	VkSpecializationInfo spec_info;
	VkSpecializationMapEntry spec_entries[GRANITE_NUM_TOTAL_SPEC_CONSTANTS];
	uint32_t spec_constants[GRANITE_NUM_TOTAL_SPEC_CONSTANTS];
	VkPipelineShaderStageRequiredSubgroupSizeCreateInfoEXT subgroup_size_info;
	VkPipeline pipeline = VK_NULL_HANDLE;
	VkResult vr;

	if (result)
		*result = VK_SUCCESS;

	if (cached_only && !device->cache_control)
		return VK_NULL_HANDLE;

	memset(&info, 0, sizeof(info));
	info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
	info.layout = program->layout;
	info.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	info.stage.module = program->modules[GRANITE_SHADER_STAGE_COMPUTE];
	info.stage.pName = "main";
	info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	info.stage.pSpecializationInfo = fill_specialization(&spec_info, spec_entries, spec_constants, compile,
			program->resource_layout->combined_spec_constant_mask);

	if (compile->static_state.state.subgroup_control_size)
	{
		uint32_t min_subgroups = (uint32_t)1 << compile->static_state.state.subgroup_minimum_size_log2;
		uint32_t max_subgroups = (uint32_t)1 << compile->static_state.state.subgroup_maximum_size_log2;

		if (compile->static_state.state.subgroup_full_group)
			info.stage.flags |= VK_PIPELINE_SHADER_STAGE_CREATE_REQUIRE_FULL_SUBGROUPS_BIT_EXT;

		if (min_subgroups <= device->min_subgroup_size && max_subgroups >= device->max_subgroup_size)
			info.stage.flags |= VK_PIPELINE_SHADER_STAGE_CREATE_ALLOW_VARYING_SUBGROUP_SIZE_BIT_EXT;
		else
		{
			/* a fixed size: the smallest the device has that is asked for */
			memset(&subgroup_size_info, 0, sizeof(subgroup_size_info));
			subgroup_size_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_REQUIRED_SUBGROUP_SIZE_CREATE_INFO_EXT;
			subgroup_size_info.requiredSubgroupSize = min_subgroups < device->min_subgroup_size ?
				device->min_subgroup_size : min_subgroups;
			info.stage.pNext = &subgroup_size_info;
		}
	}

	if (cached_only)
		info.flags |= VK_PIPELINE_CREATE_FAIL_ON_PIPELINE_COMPILE_REQUIRED_BIT_EXT;

	vr = device->table->vkCreateComputePipelines(device->device, compile->cache, 1, &info, NULL, &pipeline);
	if (result)
		*result = vr;

	if (vr != VK_SUCCESS || pipeline == VK_NULL_HANDLE)
	{
		if (vr < 0)
			LOGE("Failed to create compute pipeline!\n");
		return VK_NULL_HANDLE;
	}
	return pipeline;
}

VkPipeline granite_pipeline_build_graphics(const granite_pipeline_device_t *device,
		const granite_pipeline_compile_t *compile, const granite_pipeline_program_t *program,
		const granite_render_pass_t *render_pass, bool cached_only, uint32_t *dynamic_mask, VkResult *result)
{
	const granite_combined_layout_t *layout = program->resource_layout;
	VkPipelineViewportStateCreateInfo vp;
	VkPipelineDynamicStateCreateInfo dyn;
	VkDynamicState states[7];
	VkPipelineColorBlendAttachmentState blend_attachments[GRANITE_NUM_ATTACHMENTS];
	VkPipelineColorBlendStateCreateInfo blend;
	VkPipelineDepthStencilStateCreateInfo ds;
	VkPipelineVertexInputStateCreateInfo vi;
	VkVertexInputAttributeDescription vi_attribs[GRANITE_NUM_VERTEX_ATTRIBS];
	VkVertexInputBindingDescription vi_bindings[GRANITE_NUM_VERTEX_BUFFERS];
	VkPipelineInputAssemblyStateCreateInfo ia;
	VkPipelineMultisampleStateCreateInfo ms;
	VkPipelineRasterizationStateCreateInfo raster;
	VkPipelineRasterizationConservativeStateCreateInfoEXT conservative_raster;
	VkPipelineShaderStageCreateInfo stages[GRANITE_SHADER_STAGE_COUNT];
	VkSpecializationInfo spec_info[GRANITE_SHADER_STAGE_COUNT];
	VkSpecializationMapEntry spec_entries[GRANITE_SHADER_STAGE_COUNT][GRANITE_NUM_TOTAL_SPEC_CONSTANTS];
	uint32_t spec_constants[GRANITE_SHADER_STAGE_COUNT][GRANITE_NUM_TOTAL_SPEC_CONSTANTS];
	VkGraphicsPipelineCreateInfo pipe;
	VkPipeline pipeline = VK_NULL_HANDLE;
	uint32_t mask = GRANITE_COMMAND_BUFFER_DIRTY_VIEWPORT_BIT | GRANITE_COMMAND_BUFFER_DIRTY_SCISSOR_BIT;
	uint32_t binding_mask = 0;
	unsigned num_stages = 0;
	bool has_depth, has_stencil;
	VkResult vr;
	unsigned i;

	if (result)
		*result = VK_SUCCESS;
	*dynamic_mask = 0;

	if (cached_only && !device->cache_control)
		return VK_NULL_HANDLE;

	/* Viewport */
	memset(&vp, 0, sizeof(vp));
	vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	vp.viewportCount = 1;
	vp.scissorCount = 1;

	/* Dynamic state */
	memset(&dyn, 0, sizeof(dyn));
	dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dyn.pDynamicStates = states;
	states[dyn.dynamicStateCount++] = VK_DYNAMIC_STATE_SCISSOR;
	states[dyn.dynamicStateCount++] = VK_DYNAMIC_STATE_VIEWPORT;

	if (compile->static_state.state.depth_bias_enable)
	{
		states[dyn.dynamicStateCount++] = VK_DYNAMIC_STATE_DEPTH_BIAS;
		mask |= GRANITE_COMMAND_BUFFER_DIRTY_DEPTH_BIAS_BIT;
	}

	if (compile->static_state.state.stencil_test)
	{
		states[dyn.dynamicStateCount++] = VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK;
		states[dyn.dynamicStateCount++] = VK_DYNAMIC_STATE_STENCIL_REFERENCE;
		states[dyn.dynamicStateCount++] = VK_DYNAMIC_STATE_STENCIL_WRITE_MASK;
		mask |= GRANITE_COMMAND_BUFFER_DIRTY_STENCIL_REFERENCE_BIT;
	}

	/* Blend */
	memset(&blend, 0, sizeof(blend));
	memset(blend_attachments, 0, sizeof(blend_attachments));
	blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	blend.attachmentCount = render_pass->num_color;
	blend.pAttachments = blend_attachments;
	for (i = 0; i < blend.attachmentCount; i++)
	{
		VkPipelineColorBlendAttachmentState *att = &blend_attachments[i];

		/* one the fragment shader does not write is masked off */
		if (!(layout->render_target_mask & ((uint32_t)1 << i)))
			continue;

		att->colorWriteMask = (compile->static_state.state.write_mask >> (4 * i)) & 0xf;
		att->blendEnable = compile->static_state.state.blend_enable;
		if (att->blendEnable)
		{
			att->alphaBlendOp = (VkBlendOp)compile->static_state.state.alpha_blend_op;
			att->colorBlendOp = (VkBlendOp)compile->static_state.state.color_blend_op;
			att->dstAlphaBlendFactor = (VkBlendFactor)compile->static_state.state.dst_alpha_blend;
			att->srcAlphaBlendFactor = (VkBlendFactor)compile->static_state.state.src_alpha_blend;
			att->dstColorBlendFactor = (VkBlendFactor)compile->static_state.state.dst_color_blend;
			att->srcColorBlendFactor = (VkBlendFactor)compile->static_state.state.src_color_blend;
		}
	}
	memcpy(blend.blendConstants, compile->potential_static_state.blend_constants, sizeof(blend.blendConstants));

	/* Depth and stencil */
	has_depth = granite_render_pass_has_depth(render_pass);
	has_stencil = granite_render_pass_has_stencil(render_pass);
	memset(&ds, 0, sizeof(ds));
	ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	ds.stencilTestEnable = has_stencil && compile->static_state.state.stencil_test;
	ds.depthTestEnable = has_depth && compile->static_state.state.depth_test;
	ds.depthWriteEnable = has_depth && compile->static_state.state.depth_write;

	if (ds.depthTestEnable)
		ds.depthCompareOp = (VkCompareOp)compile->static_state.state.depth_compare;

	if (ds.stencilTestEnable)
	{
		ds.front.compareOp = (VkCompareOp)compile->static_state.state.stencil_front_compare_op;
		ds.front.passOp = (VkStencilOp)compile->static_state.state.stencil_front_pass;
		ds.front.failOp = (VkStencilOp)compile->static_state.state.stencil_front_fail;
		ds.front.depthFailOp = (VkStencilOp)compile->static_state.state.stencil_front_depth_fail;
		ds.back.compareOp = (VkCompareOp)compile->static_state.state.stencil_back_compare_op;
		ds.back.passOp = (VkStencilOp)compile->static_state.state.stencil_back_pass;
		ds.back.failOp = (VkStencilOp)compile->static_state.state.stencil_back_fail;
		ds.back.depthFailOp = (VkStencilOp)compile->static_state.state.stencil_back_depth_fail;
	}

	/* Vertex input: the attributes the program reads and their buffers */
	memset(&vi, 0, sizeof(vi));
	vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vi.pVertexAttributeDescriptions = vi_attribs;
	vi.pVertexBindingDescriptions = vi_bindings;
	for (i = 0; i < GRANITE_NUM_VERTEX_ATTRIBS; i++)
	{
		VkVertexInputAttributeDescription *attr;

		if (!(layout->attribute_mask & ((uint32_t)1 << i)))
			continue;

		attr = &vi_attribs[vi.vertexAttributeDescriptionCount++];
		attr->location = i;
		attr->binding = compile->attribs[i].binding;
		attr->format = compile->attribs[i].format;
		attr->offset = compile->attribs[i].offset;
		binding_mask |= (uint32_t)1 << attr->binding;
	}

	for (i = 0; i < GRANITE_NUM_VERTEX_BUFFERS; i++)
	{
		VkVertexInputBindingDescription *bind;

		if (!(binding_mask & ((uint32_t)1 << i)))
			continue;

		bind = &vi_bindings[vi.vertexBindingDescriptionCount++];
		bind->binding = i;
		bind->inputRate = compile->input_rates[i];
		bind->stride = (uint32_t)compile->strides[i];
	}

	/* Input assembly */
	memset(&ia, 0, sizeof(ia));
	ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	ia.primitiveRestartEnable = compile->static_state.state.primitive_restart;
	ia.topology = (VkPrimitiveTopology)compile->static_state.state.topology;

	/* Multisample */
	memset(&ms, 0, sizeof(ms));
	ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	ms.rasterizationSamples = (VkSampleCountFlagBits)render_pass->samples;
	if (render_pass->samples > 1)
	{
		ms.alphaToCoverageEnable = compile->static_state.state.alpha_to_coverage;
		ms.alphaToOneEnable = compile->static_state.state.alpha_to_one;
		ms.sampleShadingEnable = compile->static_state.state.sample_shading;
		ms.minSampleShading = 1.0f;
	}

	/* Raster */
	memset(&raster, 0, sizeof(raster));
	raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	raster.cullMode = (VkCullModeFlags)compile->static_state.state.cull_mode;
	raster.frontFace = (VkFrontFace)compile->static_state.state.front_face;
	raster.lineWidth = 1.0f;
	raster.polygonMode = compile->static_state.state.wireframe ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
	raster.depthBiasEnable = compile->static_state.state.depth_bias_enable != 0;

	if (compile->static_state.state.conservative_raster)
	{
		if (!device->conservative_rasterization)
		{
			LOGE("Conservative rasterization is not supported on this device.\n");
			return VK_NULL_HANDLE;
		}

		memset(&conservative_raster, 0, sizeof(conservative_raster));
		conservative_raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_CONSERVATIVE_STATE_CREATE_INFO_EXT;
		conservative_raster.conservativeRasterizationMode = VK_CONSERVATIVE_RASTERIZATION_MODE_OVERESTIMATE_EXT;
		raster.pNext = &conservative_raster;
	}

	/* Stages */
	for (i = 0; i < GRANITE_SHADER_STAGE_COUNT; i++)
	{
		VkPipelineShaderStageCreateInfo *s;

		if (program->modules[i] == VK_NULL_HANDLE)
			continue;

		s = &stages[num_stages++];
		memset(s, 0, sizeof(*s));
		s->sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		s->module = program->modules[i];
		s->pName = "main";
		s->stage = (VkShaderStageFlagBits)((uint32_t)1 << i);
		s->pSpecializationInfo = fill_specialization(&spec_info[i], spec_entries[i], spec_constants[i], compile,
				layout->spec_constant_mask[i]);
	}

	memset(&pipe, 0, sizeof(pipe));
	pipe.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipe.layout = program->layout;
	pipe.renderPass = render_pass->render_pass;
	pipe.subpass = compile->subpass_index;
	pipe.pViewportState = &vp;
	pipe.pDynamicState = &dyn;
	pipe.pColorBlendState = &blend;
	pipe.pDepthStencilState = &ds;
	pipe.pVertexInputState = &vi;
	pipe.pInputAssemblyState = &ia;
	pipe.pMultisampleState = &ms;
	pipe.pRasterizationState = &raster;
	pipe.pStages = stages;
	pipe.stageCount = num_stages;

	if (cached_only)
		pipe.flags |= VK_PIPELINE_CREATE_FAIL_ON_PIPELINE_COMPILE_REQUIRED_BIT_EXT;

	vr = device->table->vkCreateGraphicsPipelines(device->device, compile->cache, 1, &pipe, NULL, &pipeline);
	if (result)
		*result = vr;

	if (vr != VK_SUCCESS || pipeline == VK_NULL_HANDLE)
	{
		if (vr < 0)
			LOGE("Failed to create graphics pipeline!\n");
		return VK_NULL_HANDLE;
	}

	*dynamic_mask = mask;
	return pipeline;
}
