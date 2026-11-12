/* parallel-rdp's pipeline compile (vulkan/pipeline_compile.c), through a
 * stand-in device table.
 *
 * The baked state is four words, which the pipeline records on disk keep.
 *
 * The hash a pipeline is kept under has everything it is compiled from
 * and nothing it is not: the attributes the program reads and their
 * buffers but no others, the blend constants only when a blend factor
 * uses them, the specialization constants only when the program has them
 * and they are set, the subgroup sizes only when asked for.
 *
 * A graphics pipeline gets viewport and scissor dynamic, and depth bias
 * and the stencil masks when it uses them, which the dynamic mask says;
 * a blend attachment a pass colour, masked off when the fragment shader
 * does not write it; depth and stencil tests only when the pass has the
 * aspect; the attributes of the program with their buffers; the pass's
 * sample count; a stage a shader with the specialization constants of
 * that stage. A compute pipeline gets its shader, its constants and the
 * subgroup size asked for.
 *
 * Asked for from the cache alone, a pipeline is made with the flag that
 * fails instead of compiling, and not at all on a device that cannot be
 * asked. A refused pipeline gives no handle. */
#include <stdio.h>
#include <string.h>
#include <boolean.h>

#include "pipeline_compile.h"

static unsigned failures;

#define CHECK(cond, what) \
	do \
	{ \
		if (!(cond)) \
		{ \
			fprintf(stderr, "test_vk_pipeline_compile: FAIL (line %d): %s\n", __LINE__, what); \
			failures++; \
		} \
	} while (0)

/* what the driver was last asked for, copied out of the caller's stack */
static VkGraphicsPipelineCreateInfo g_info;
static VkPipelineDynamicStateCreateInfo g_dyn;
static VkDynamicState g_states[8];
static VkPipelineColorBlendStateCreateInfo g_blend;
static VkPipelineColorBlendAttachmentState g_blend_att[GRANITE_NUM_ATTACHMENTS];
static VkPipelineDepthStencilStateCreateInfo g_ds;
static VkPipelineVertexInputStateCreateInfo g_vi;
static VkVertexInputAttributeDescription g_attr[GRANITE_NUM_VERTEX_ATTRIBS];
static VkVertexInputBindingDescription g_bind[GRANITE_NUM_VERTEX_BUFFERS];
static VkPipelineInputAssemblyStateCreateInfo g_ia;
static VkPipelineMultisampleStateCreateInfo g_ms;
static VkPipelineRasterizationStateCreateInfo g_raster;
static VkPipelineViewportStateCreateInfo g_vp;
static bool g_conservative;
static VkPipelineShaderStageCreateInfo g_stages[GRANITE_SHADER_STAGE_COUNT];
static VkSpecializationInfo g_spec[GRANITE_SHADER_STAGE_COUNT];
static VkSpecializationMapEntry g_spec_entries[GRANITE_SHADER_STAGE_COUNT][GRANITE_NUM_TOTAL_SPEC_CONSTANTS];
static uint32_t g_spec_data[GRANITE_SHADER_STAGE_COUNT][GRANITE_NUM_TOTAL_SPEC_CONSTANTS];
static VkPipelineCache g_cache;

static VkComputePipelineCreateInfo c_info;
static uint32_t c_required_subgroup;

static unsigned created;
static VkResult create_result = VK_SUCCESS;

static void copy_stage(unsigned i, const VkPipelineShaderStageCreateInfo *stage)
{
	uint32_t j;

	g_stages[i] = *stage;
	memset(&g_spec[i], 0, sizeof(g_spec[i]));
	if (!stage->pSpecializationInfo)
		return;
	g_spec[i] = *stage->pSpecializationInfo;
	for (j = 0; j < g_spec[i].mapEntryCount; j++)
		g_spec_entries[i][j] = g_spec[i].pMapEntries[j];
	memcpy(g_spec_data[i], g_spec[i].pData, g_spec[i].dataSize);
}

static VkResult VKAPI_CALL fake_create_graphics(VkDevice d, VkPipelineCache cache, uint32_t count,
		const VkGraphicsPipelineCreateInfo *info, const VkAllocationCallbacks *a, VkPipeline *pipeline)
{
	uint32_t i;
	(void)d; (void)a;
	CHECK(count == 1, "one pipeline a call");
	g_info = *info;
	g_cache = cache;
	g_dyn = *info->pDynamicState;
	for (i = 0; i < g_dyn.dynamicStateCount && i < 8; i++)
		g_states[i] = g_dyn.pDynamicStates[i];
	g_blend = *info->pColorBlendState;
	for (i = 0; i < g_blend.attachmentCount; i++)
		g_blend_att[i] = g_blend.pAttachments[i];
	g_ds = *info->pDepthStencilState;
	g_vi = *info->pVertexInputState;
	for (i = 0; i < g_vi.vertexAttributeDescriptionCount; i++)
		g_attr[i] = g_vi.pVertexAttributeDescriptions[i];
	for (i = 0; i < g_vi.vertexBindingDescriptionCount; i++)
		g_bind[i] = g_vi.pVertexBindingDescriptions[i];
	g_ia = *info->pInputAssemblyState;
	g_ms = *info->pMultisampleState;
	g_raster = *info->pRasterizationState;
	g_conservative = g_raster.pNext != NULL &&
		((const VkPipelineRasterizationConservativeStateCreateInfoEXT *)g_raster.pNext)->conservativeRasterizationMode ==
		VK_CONSERVATIVE_RASTERIZATION_MODE_OVERESTIMATE_EXT;
	g_vp = *info->pViewportState;
	for (i = 0; i < info->stageCount; i++)
		copy_stage(i, &info->pStages[i]);
	created++;
	if (create_result != VK_SUCCESS)
	{
		*pipeline = VK_NULL_HANDLE;
		return create_result;
	}
	*pipeline = (VkPipeline)(size_t)(0x700 + created);
	return VK_SUCCESS;
}

static VkResult VKAPI_CALL fake_create_compute(VkDevice d, VkPipelineCache cache, uint32_t count,
		const VkComputePipelineCreateInfo *info, const VkAllocationCallbacks *a, VkPipeline *pipeline)
{
	(void)d; (void)a;
	CHECK(count == 1, "one pipeline a call");
	c_info = *info;
	g_cache = cache;
	copy_stage(0, &info->stage);
	c_required_subgroup = info->stage.pNext ?
		((const VkPipelineShaderStageRequiredSubgroupSizeCreateInfoEXT *)info->stage.pNext)->requiredSubgroupSize : 0;
	created++;
	if (create_result != VK_SUCCESS)
	{
		*pipeline = VK_NULL_HANDLE;
		return create_result;
	}
	*pipeline = (VkPipeline)(size_t)(0x700 + created);
	return VK_SUCCESS;
}

static struct VolkDeviceTable table;
static granite_combined_layout_t layout;
static granite_pipeline_program_t program;
static granite_pipeline_device_t device;
static granite_render_pass_t pass;

static bool has_state(VkDynamicState state)
{
	uint32_t i;
	for (i = 0; i < g_dyn.dynamicStateCount; i++)
		if (g_states[i] == state)
			return true;
	return false;
}

static void setup(granite_pipeline_compile_t *c)
{
	unsigned i;

	memset(&layout, 0, sizeof(layout));
	layout.attribute_mask = (1u << 0) | (1u << 2);
	layout.render_target_mask = 1u << 0;
	layout.spec_constant_mask[GRANITE_SHADER_STAGE_VERTEX] = 1u << 1;
	layout.spec_constant_mask[GRANITE_SHADER_STAGE_FRAGMENT] = (1u << 0) | (1u << 9);
	layout.combined_spec_constant_mask = (1u << 0) | (1u << 1) | (1u << 9);

	memset(&program, 0, sizeof(program));
	program.hash = ((uint64_t)0x12345678u << 32) | 0x9abcdef0u;
	program.layout = (VkPipelineLayout)(size_t)0x40;
	program.resource_layout = &layout;
	program.modules[GRANITE_SHADER_STAGE_VERTEX] = (VkShaderModule)(size_t)0x41;
	program.modules[GRANITE_SHADER_STAGE_FRAGMENT] = (VkShaderModule)(size_t)0x42;

	memset(&device, 0, sizeof(device));
	device.device = (VkDevice)(size_t)1;
	device.table = &table;
	device.cache_control = true;
	device.min_subgroup_size = 4;
	device.max_subgroup_size = 32;

	memset(&pass, 0, sizeof(pass));
	pass.render_pass = (VkRenderPass)(size_t)0x50;
	pass.num_color = 2;
	pass.samples = 1;
	pass.depth_stencil_format = VK_FORMAT_D24_UNORM_S8_UINT;

	memset(c, 0, sizeof(*c));
	c->static_state.state.depth_test = 1;
	c->static_state.state.depth_write = 1;
	c->static_state.state.depth_compare = VK_COMPARE_OP_LESS_OR_EQUAL;
	c->static_state.state.cull_mode = VK_CULL_MODE_BACK_BIT;
	c->static_state.state.front_face = VK_FRONT_FACE_CLOCKWISE;
	c->static_state.state.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
	c->static_state.state.write_mask = 0xff;
	for (i = 0; i < GRANITE_NUM_VERTEX_ATTRIBS; i++)
	{
		c->attribs[i].binding = i == 2 ? 1 : 0;
		c->attribs[i].format = (VkFormat)(VK_FORMAT_R32_SFLOAT + i);
		c->attribs[i].offset = 4 * i;
	}
	c->strides[0] = 16;
	c->strides[1] = 24;
	c->strides[2] = 99;
	c->input_rates[1] = VK_VERTEX_INPUT_RATE_INSTANCE;
	for (i = 0; i < GRANITE_NUM_TOTAL_SPEC_CONSTANTS; i++)
		c->potential_static_state.spec_constants[i] = 1000 + i;
	c->potential_static_state.spec_constant_mask = 0x3;
	c->potential_static_state.internal_spec_constant_mask = 0x2;
	c->cache = (VkPipelineCache)(size_t)0x60;
}

static void test_state_words(void)
{
	granite_pipeline_state_t s;

	CHECK(sizeof(granite_pipeline_state_t) == 16, "the baked state is four words");
	memset(&s, 0, sizeof(s));
	s.state.write_mask = 0xa5a5a5a5u;
	CHECK(s.words[3] == 0xa5a5a5a5u && s.words[0] == 0 && s.words[1] == 0 && s.words[2] == 0,
			"the write mask is the fourth");
	memset(&s, 0, sizeof(s));
	s.state.depth_write = 1;
	s.state.conservative_raster = 1;
	CHECK(s.words[0] != 0 && s.words[2] != 0 && s.words[3] == 0, "the fields fill the first three");
}

static void test_hash(void)
{
	granite_pipeline_compile_t c, d;
	uint32_t vbos = 0, vbos2 = 0;
	uint64_t h, hc;

	setup(&c);
	h = granite_pipeline_hash_graphics(&c, &program, 77, &vbos);
	CHECK(vbos == 0x3, "attributes 0 and 2 read buffers 0 and 1");
	CHECK(h == granite_pipeline_hash_graphics(&c, &program, 77, &vbos2) && vbos2 == 0x3, "the hash is stable");

	d = c; d.attribs[2].offset++;
	CHECK(h != granite_pipeline_hash_graphics(&d, &program, 77, &vbos2), "an attribute read changes the hash");
	d = c; d.attribs[1].offset++; d.attribs[1].binding = 3;
	CHECK(h == granite_pipeline_hash_graphics(&d, &program, 77, &vbos2) && vbos2 == 0x3,
			"one the program does not read changes nothing");
	d = c; d.strides[1]++;
	CHECK(h != granite_pipeline_hash_graphics(&d, &program, 77, &vbos2), "a stride read changes the hash");
	d = c; d.input_rates[0] = VK_VERTEX_INPUT_RATE_INSTANCE;
	CHECK(h != granite_pipeline_hash_graphics(&d, &program, 77, &vbos2), "so does an input rate");
	d = c; d.strides[2]++; d.input_rates[3] = VK_VERTEX_INPUT_RATE_INSTANCE;
	CHECK(h == granite_pipeline_hash_graphics(&d, &program, 77, &vbos2), "a buffer not read changes nothing");
	CHECK(h != granite_pipeline_hash_graphics(&c, &program, 78, &vbos2), "the render pass changes the hash");
	d = c; d.subpass_index = 1;
	CHECK(h != granite_pipeline_hash_graphics(&d, &program, 77, &vbos2), "so does the subpass");
	d = c; d.static_state.state.depth_compare = VK_COMPARE_OP_ALWAYS;
	CHECK(h != granite_pipeline_hash_graphics(&d, &program, 77, &vbos2), "so does the baked state");
	d = c; d.static_state.state.write_mask = 0xf;
	CHECK(h != granite_pipeline_hash_graphics(&d, &program, 77, &vbos2), "and its write mask");

	/* blend constants only when a factor uses them */
	d = c; d.potential_static_state.blend_constants[2] = 0.5f;
	CHECK(h == granite_pipeline_hash_graphics(&d, &program, 77, &vbos2), "unused blend constants change nothing");
	c.static_state.state.blend_enable = 1;
	c.static_state.state.src_color_blend = VK_BLEND_FACTOR_SRC_ALPHA;
	h = granite_pipeline_hash_graphics(&c, &program, 77, &vbos);
	d = c; d.potential_static_state.blend_constants[2] = 0.5f;
	CHECK(h == granite_pipeline_hash_graphics(&d, &program, 77, &vbos2), "nor when blending without them");
	c.static_state.state.dst_alpha_blend = VK_BLEND_FACTOR_CONSTANT_ALPHA;
	h = granite_pipeline_hash_graphics(&c, &program, 77, &vbos);
	d = c; d.potential_static_state.blend_constants[2] = 0.5f;
	CHECK(h != granite_pipeline_hash_graphics(&d, &program, 77, &vbos2), "a factor that uses them puts them in the hash");

	/* specialization constants: the program's, when set */
	setup(&c);
	h = granite_pipeline_hash_graphics(&c, &program, 77, &vbos);
	d = c; d.potential_static_state.spec_constants[0]++;
	CHECK(h != granite_pipeline_hash_graphics(&d, &program, 77, &vbos2), "a constant set and used changes the hash");
	d = c; d.potential_static_state.spec_constants[9]++;
	CHECK(h != granite_pipeline_hash_graphics(&d, &program, 77, &vbos2), "an internal one too");
	d = c; d.potential_static_state.spec_constants[2]++;
	CHECK(h == granite_pipeline_hash_graphics(&d, &program, 77, &vbos2), "one not set changes nothing");
	d = c; d.potential_static_state.spec_constants[8]++;
	CHECK(h == granite_pipeline_hash_graphics(&d, &program, 77, &vbos2), "nor an internal one not set");
	d = c; d.potential_static_state.spec_constant_mask = 0x1;
	CHECK(h != granite_pipeline_hash_graphics(&d, &program, 77, &vbos2), "unsetting one changes the hash");
	d = c; d.potential_static_state.spec_constant_mask |= 0x80;
	CHECK(h == granite_pipeline_hash_graphics(&d, &program, 77, &vbos2), "setting one the program lacks changes nothing");

	/* compute */
	hc = granite_pipeline_hash_compute(&c, &program);
	CHECK(hc != h, "a compute hash is not the graphics one");
	d = c; d.attribs[0].offset++; d.static_state.state.depth_compare = VK_COMPARE_OP_ALWAYS;
	CHECK(hc == granite_pipeline_hash_compute(&d, &program), "graphics state is not in it");
	d = c; d.potential_static_state.spec_constants[1]++;
	CHECK(hc != granite_pipeline_hash_compute(&d, &program), "specialization constants are");
	d = c; d.subgroup_size_tag = 5;
	CHECK(hc == granite_pipeline_hash_compute(&d, &program), "the subgroup tag is not, unasked");
	d.static_state.state.subgroup_control_size = 1;
	CHECK(hc != granite_pipeline_hash_compute(&d, &program), "asking for subgroup sizes changes the hash");
	hc = granite_pipeline_hash_compute(&d, &program);
	d.subgroup_size_tag = 6;
	CHECK(hc != granite_pipeline_hash_compute(&d, &program), "and then the tag does");
	d.subgroup_size_tag = 5;
	d.static_state.state.subgroup_maximum_size_log2 = 3;
	CHECK(hc != granite_pipeline_hash_compute(&d, &program), "and the sizes");
	hc = granite_pipeline_hash_compute(&c, &program);
	program.hash += (uint64_t)1 << 32;
	CHECK(hc != granite_pipeline_hash_compute(&c, &program) &&
			h != granite_pipeline_hash_graphics(&c, &program, 77, &vbos2), "the program changes both");
}

static void test_graphics(void)
{
	granite_pipeline_compile_t c;
	uint32_t dynamic = 0xffff;
	VkResult vr = VK_ERROR_UNKNOWN;
	VkPipeline p;

	setup(&c);
	p = granite_pipeline_build_graphics(&device, &c, &program, &pass, false, &dynamic, &vr);
	CHECK(p != VK_NULL_HANDLE && vr == VK_SUCCESS && created == 1, "a graphics pipeline is made");
	CHECK(g_info.sType == VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO && g_info.layout == program.layout &&
			g_info.renderPass == pass.render_pass && g_info.subpass == 0 && g_info.flags == 0 &&
			g_cache == c.cache, "for the program's layout and the pass, with the cache");
	CHECK(g_vp.viewportCount == 1 && g_vp.scissorCount == 1, "one viewport and scissor");
	CHECK(g_dyn.dynamicStateCount == 2 && has_state(VK_DYNAMIC_STATE_VIEWPORT) && has_state(VK_DYNAMIC_STATE_SCISSOR) &&
			dynamic == (GRANITE_COMMAND_BUFFER_DIRTY_VIEWPORT_BIT | GRANITE_COMMAND_BUFFER_DIRTY_SCISSOR_BIT),
			"both dynamic, and nothing else");

	CHECK(g_blend.attachmentCount == 2 && g_blend_att[0].colorWriteMask == 0xf && !g_blend_att[0].blendEnable,
			"a blend attachment a pass colour, written as the mask says");
	CHECK(g_blend_att[1].colorWriteMask == 0 && !g_blend_att[1].blendEnable,
			"one the fragment shader does not write is masked off");

	CHECK(g_ds.depthTestEnable && g_ds.depthWriteEnable && g_ds.depthCompareOp == VK_COMPARE_OP_LESS_OR_EQUAL &&
			!g_ds.stencilTestEnable, "the depth test as asked, no stencil test");

	CHECK(g_vi.vertexAttributeDescriptionCount == 2 && g_attr[0].location == 0 && g_attr[0].binding == 0 &&
			g_attr[0].format == VK_FORMAT_R32_SFLOAT && g_attr[0].offset == 0 && g_attr[1].location == 2 &&
			g_attr[1].binding == 1 && g_attr[1].offset == 8, "the two attributes the program reads");
	CHECK(g_vi.vertexBindingDescriptionCount == 2 && g_bind[0].binding == 0 && g_bind[0].stride == 16 &&
			g_bind[0].inputRate == VK_VERTEX_INPUT_RATE_VERTEX && g_bind[1].binding == 1 && g_bind[1].stride == 24 &&
			g_bind[1].inputRate == VK_VERTEX_INPUT_RATE_INSTANCE, "and their two buffers");

	CHECK(g_ia.topology == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP && !g_ia.primitiveRestartEnable, "the topology");
	CHECK(g_ms.rasterizationSamples == VK_SAMPLE_COUNT_1_BIT && !g_ms.alphaToCoverageEnable && !g_ms.sampleShadingEnable,
			"one sample");
	CHECK(g_raster.cullMode == VK_CULL_MODE_BACK_BIT && g_raster.frontFace == VK_FRONT_FACE_CLOCKWISE &&
			g_raster.polygonMode == VK_POLYGON_MODE_FILL && g_raster.lineWidth == 1.0f && !g_raster.depthBiasEnable &&
			g_raster.pNext == NULL, "the raster state");

	CHECK(g_info.stageCount == 2 && g_stages[0].stage == VK_SHADER_STAGE_VERTEX_BIT &&
			g_stages[0].module == program.modules[GRANITE_SHADER_STAGE_VERTEX] && strcmp(g_stages[0].pName, "main") == 0 &&
			g_stages[1].stage == VK_SHADER_STAGE_FRAGMENT_BIT &&
			g_stages[1].module == program.modules[GRANITE_SHADER_STAGE_FRAGMENT], "a stage a shader");
	CHECK(g_spec[0].mapEntryCount == 1 && g_spec_entries[0][0].constantID == 1 && g_spec_entries[0][0].offset == 0 &&
			g_spec_entries[0][0].size == 4 && g_spec[0].dataSize == 4 && g_spec_data[0][0] == 1001,
			"the vertex stage gets its one constant");
	CHECK(g_spec[1].mapEntryCount == 2 && g_spec_entries[1][0].constantID == 0 && g_spec_data[1][0] == 1000 &&
			g_spec_entries[1][1].constantID == 9 && g_spec_entries[1][1].offset == 4 && g_spec_data[1][1] == 1009 &&
			g_spec[1].dataSize == 8, "the fragment stage a user and an internal one");

	/* nothing set: no specialization */
	c.potential_static_state.spec_constant_mask = 0;
	c.potential_static_state.internal_spec_constant_mask = 0;
	granite_pipeline_build_graphics(&device, &c, &program, &pass, false, &dynamic, &vr);
	CHECK(g_stages[0].pSpecializationInfo == NULL && g_stages[1].pSpecializationInfo == NULL,
			"no constants set, no specialization");

	/* depth bias, stencil, blending, wireframe */
	setup(&c);
	c.static_state.state.depth_bias_enable = 1;
	c.static_state.state.stencil_test = 1;
	c.static_state.state.stencil_front_compare_op = VK_COMPARE_OP_EQUAL;
	c.static_state.state.stencil_front_pass = VK_STENCIL_OP_REPLACE;
	c.static_state.state.stencil_back_fail = VK_STENCIL_OP_INVERT;
	c.static_state.state.stencil_back_depth_fail = VK_STENCIL_OP_ZERO;
	c.static_state.state.blend_enable = 1;
	c.static_state.state.src_color_blend = VK_BLEND_FACTOR_SRC_ALPHA;
	c.static_state.state.dst_color_blend = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	c.static_state.state.src_alpha_blend = VK_BLEND_FACTOR_ONE;
	c.static_state.state.dst_alpha_blend = VK_BLEND_FACTOR_CONSTANT_ALPHA;
	c.static_state.state.color_blend_op = VK_BLEND_OP_SUBTRACT;
	c.static_state.state.alpha_blend_op = VK_BLEND_OP_MAX;
	c.static_state.state.wireframe = 1;
	c.static_state.state.primitive_restart = 1;
	c.static_state.state.write_mask = 0x3;
	c.potential_static_state.blend_constants[3] = 0.25f;
	p = granite_pipeline_build_graphics(&device, &c, &program, &pass, false, &dynamic, &vr);
	CHECK(p != VK_NULL_HANDLE, "a pipeline with more state");
	CHECK(g_dyn.dynamicStateCount == 6 && has_state(VK_DYNAMIC_STATE_DEPTH_BIAS) &&
			has_state(VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK) && has_state(VK_DYNAMIC_STATE_STENCIL_REFERENCE) &&
			has_state(VK_DYNAMIC_STATE_STENCIL_WRITE_MASK), "leaves depth bias and the stencil masks dynamic");
	CHECK(dynamic == (GRANITE_COMMAND_BUFFER_DIRTY_VIEWPORT_BIT | GRANITE_COMMAND_BUFFER_DIRTY_SCISSOR_BIT |
				GRANITE_COMMAND_BUFFER_DIRTY_DEPTH_BIAS_BIT | GRANITE_COMMAND_BUFFER_DIRTY_STENCIL_REFERENCE_BIT),
			"which the mask says");
	CHECK(g_raster.depthBiasEnable && g_raster.polygonMode == VK_POLYGON_MODE_LINE && g_ia.primitiveRestartEnable,
			"has depth bias, wireframe and primitive restart");
	CHECK(g_ds.stencilTestEnable && g_ds.front.compareOp == VK_COMPARE_OP_EQUAL && g_ds.front.passOp == VK_STENCIL_OP_REPLACE &&
			g_ds.back.failOp == VK_STENCIL_OP_INVERT && g_ds.back.depthFailOp == VK_STENCIL_OP_ZERO, "the stencil ops");
	CHECK(g_blend_att[0].blendEnable && g_blend_att[0].colorWriteMask == 0x3 &&
			g_blend_att[0].srcColorBlendFactor == VK_BLEND_FACTOR_SRC_ALPHA &&
			g_blend_att[0].dstColorBlendFactor == VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA &&
			g_blend_att[0].srcAlphaBlendFactor == VK_BLEND_FACTOR_ONE &&
			g_blend_att[0].dstAlphaBlendFactor == VK_BLEND_FACTOR_CONSTANT_ALPHA &&
			g_blend_att[0].colorBlendOp == VK_BLEND_OP_SUBTRACT && g_blend_att[0].alphaBlendOp == VK_BLEND_OP_MAX &&
			g_blend.blendConstants[3] == 0.25f, "the blend state and constants");

	/* a pass without stencil, then without depth */
	pass.depth_stencil_format = VK_FORMAT_D32_SFLOAT;
	granite_pipeline_build_graphics(&device, &c, &program, &pass, false, &dynamic, &vr);
	CHECK(g_ds.depthTestEnable && !g_ds.stencilTestEnable, "no stencil in the pass, no stencil test");
	pass.depth_stencil_format = VK_FORMAT_UNDEFINED;
	granite_pipeline_build_graphics(&device, &c, &program, &pass, false, &dynamic, &vr);
	CHECK(!g_ds.depthTestEnable && !g_ds.depthWriteEnable && !g_ds.stencilTestEnable, "no depth in the pass, no tests");

	/* multisampled */
	setup(&c);
	pass.samples = 4;
	c.static_state.state.alpha_to_coverage = 1;
	c.static_state.state.sample_shading = 1;
	granite_pipeline_build_graphics(&device, &c, &program, &pass, false, &dynamic, &vr);
	CHECK(g_ms.rasterizationSamples == VK_SAMPLE_COUNT_4_BIT && g_ms.alphaToCoverageEnable && g_ms.sampleShadingEnable &&
			!g_ms.alphaToOneEnable && g_ms.minSampleShading == 1.0f, "a multisampled pass gets the sample state");

	/* conservative rasterization */
	setup(&c);
	c.static_state.state.conservative_raster = 1;
	created = 0;
	p = granite_pipeline_build_graphics(&device, &c, &program, &pass, false, &dynamic, &vr);
	CHECK(p == VK_NULL_HANDLE && created == 0 && dynamic == 0, "conservative rasterization the device lacks: nothing made");
	device.conservative_rasterization = true;
	p = granite_pipeline_build_graphics(&device, &c, &program, &pass, false, &dynamic, &vr);
	CHECK(p != VK_NULL_HANDLE && g_conservative, "with it, it is asked for");

	/* from the cache alone */
	setup(&c);
	p = granite_pipeline_build_graphics(&device, &c, &program, &pass, true, &dynamic, &vr);
	CHECK(p != VK_NULL_HANDLE && (g_info.flags & VK_PIPELINE_CREATE_FAIL_ON_PIPELINE_COMPILE_REQUIRED_BIT_EXT),
			"from the cache alone: asked not to compile");
	create_result = VK_PIPELINE_COMPILE_REQUIRED_EXT;
	p = granite_pipeline_build_graphics(&device, &c, &program, &pass, true, &dynamic, &vr);
	CHECK(p == VK_NULL_HANDLE && vr == VK_PIPELINE_COMPILE_REQUIRED_EXT && dynamic == 0, "not in the cache: none");
	create_result = VK_ERROR_OUT_OF_DEVICE_MEMORY;
	p = granite_pipeline_build_graphics(&device, &c, &program, &pass, false, &dynamic, &vr);
	CHECK(p == VK_NULL_HANDLE && vr == VK_ERROR_OUT_OF_DEVICE_MEMORY, "a refused pipeline gives none");
	create_result = VK_SUCCESS;
	device.cache_control = false;
	created = 0;
	vr = VK_ERROR_UNKNOWN;
	p = granite_pipeline_build_graphics(&device, &c, &program, &pass, true, &dynamic, &vr);
	CHECK(p == VK_NULL_HANDLE && created == 0 && vr == VK_SUCCESS, "a device that cannot be asked is not asked");
	p = granite_pipeline_build_graphics(&device, &c, &program, &pass, false, &dynamic, &vr);
	CHECK(p != VK_NULL_HANDLE && g_info.flags == 0, "but compiles when that is allowed");
}

static void test_compute(void)
{
	granite_pipeline_compile_t c;
	VkResult vr = VK_ERROR_UNKNOWN;
	VkPipeline p;

	setup(&c);
	memset(program.modules, 0, sizeof(program.modules));
	program.modules[GRANITE_SHADER_STAGE_COMPUTE] = (VkShaderModule)(size_t)0x43;

	p = granite_pipeline_build_compute(&device, &c, &program, false, &vr);
	CHECK(p != VK_NULL_HANDLE && vr == VK_SUCCESS, "a compute pipeline is made");
	CHECK(c_info.sType == VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO && c_info.layout == program.layout &&
			c_info.flags == 0 && c_info.stage.stage == VK_SHADER_STAGE_COMPUTE_BIT &&
			c_info.stage.module == program.modules[GRANITE_SHADER_STAGE_COMPUTE] &&
			strcmp(c_info.stage.pName, "main") == 0 && c_info.stage.flags == 0 && c_info.stage.pNext == NULL &&
			g_cache == c.cache, "of the compute shader");
	CHECK(g_spec[0].mapEntryCount == 3 && g_spec_entries[0][0].constantID == 0 && g_spec_entries[0][1].constantID == 1 &&
			g_spec_entries[0][2].constantID == 9 && g_spec_data[0][2] == 1009 && g_spec[0].dataSize == 12,
			"with all the program's constants that are set");

	/* subgroup sizes: the device has 4 to 32 */
	c.static_state.state.subgroup_control_size = 1;
	c.static_state.state.subgroup_minimum_size_log2 = 2;
	c.static_state.state.subgroup_maximum_size_log2 = 5;
	granite_pipeline_build_compute(&device, &c, &program, false, &vr);
	CHECK((c_info.stage.flags & VK_PIPELINE_SHADER_STAGE_CREATE_ALLOW_VARYING_SUBGROUP_SIZE_BIT_EXT) &&
			!(c_info.stage.flags & VK_PIPELINE_SHADER_STAGE_CREATE_REQUIRE_FULL_SUBGROUPS_BIT_EXT) &&
			c_required_subgroup == 0, "any size the device has: varying");
	c.static_state.state.subgroup_minimum_size_log2 = 3;
	c.static_state.state.subgroup_full_group = 1;
	granite_pipeline_build_compute(&device, &c, &program, false, &vr);
	CHECK(!(c_info.stage.flags & VK_PIPELINE_SHADER_STAGE_CREATE_ALLOW_VARYING_SUBGROUP_SIZE_BIT_EXT) &&
			(c_info.stage.flags & VK_PIPELINE_SHADER_STAGE_CREATE_REQUIRE_FULL_SUBGROUPS_BIT_EXT) &&
			c_required_subgroup == 8, "a narrower range: the smallest size asked for, full groups");
	c.static_state.state.subgroup_minimum_size_log2 = 1;
	c.static_state.state.subgroup_maximum_size_log2 = 4;
	granite_pipeline_build_compute(&device, &c, &program, false, &vr);
	CHECK(c_required_subgroup == 4, "below the device's smallest: the device's smallest");

	/* from the cache alone */
	setup(&c);
	program.modules[GRANITE_SHADER_STAGE_COMPUTE] = (VkShaderModule)(size_t)0x43;
	p = granite_pipeline_build_compute(&device, &c, &program, true, &vr);
	CHECK(p != VK_NULL_HANDLE && (c_info.flags & VK_PIPELINE_CREATE_FAIL_ON_PIPELINE_COMPILE_REQUIRED_BIT_EXT),
			"from the cache alone: asked not to compile");
	create_result = VK_PIPELINE_COMPILE_REQUIRED_EXT;
	p = granite_pipeline_build_compute(&device, &c, &program, true, &vr);
	CHECK(p == VK_NULL_HANDLE && vr == VK_PIPELINE_COMPILE_REQUIRED_EXT, "not in the cache: none");
	create_result = VK_SUCCESS;
	device.cache_control = false;
	created = 0;
	p = granite_pipeline_build_compute(&device, &c, &program, true, &vr);
	CHECK(p == VK_NULL_HANDLE && created == 0, "a device that cannot be asked is not asked");
	p = granite_pipeline_build_compute(&device, &c, &program, false, NULL);
	CHECK(p != VK_NULL_HANDLE, "but compiles when that is allowed, with no result wanted");
}

int main(void)
{
	table.vkCreateGraphicsPipelines = fake_create_graphics;
	table.vkCreateComputePipelines = fake_create_compute;

	test_state_words();
	test_hash();
	test_graphics();
	test_compute();

	if (failures)
	{
		fprintf(stderr, "test_vk_pipeline_compile: %u failure(s)\n", failures);
		return 1;
	}
	printf("test_vk_pipeline_compile: OK\n");
	return 0;
}
