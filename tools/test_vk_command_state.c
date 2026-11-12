/* parallel-rdp's command buffer state (vulkan/command_state.c), through a
 * stand-in device table.
 *
 * State is set and only marked dirty; a flush brings the VkCommandBuffer
 * up to it. The pipeline is asked for again when the baked state, the
 * program or (for a draw) the vertex layout changed, and when there is
 * none; setting the layout that is set asks for nothing. A pipeline is
 * bound only if it is not the one bound, and state it bakes in that is
 * otherwise dynamic is set again after it. Without a pipeline nothing is
 * flushed.
 *
 * A flush pushes the push constants when they or their layout changed,
 * with the program's stages and size; sets the viewport and the scissor
 * (clipped to what Vulkan takes) when dirty; binds the dirty vertex
 * buffers the pipeline reads, a run of them a call, and leaves the ones
 * it does not read dirty. A compute flush sets none of the draw state.
 * A second flush with nothing changed does nothing.
 *
 * The render area is the part of the area asked for that is inside the
 * framebuffer. Push constants past the end of the block and vertex
 * attributes or buffers out of range are refused. */
#include <stdio.h>
#include <string.h>
#include <boolean.h>

#include "command_state.h"

static unsigned failures;

#define CHECK(cond, what) \
	do \
	{ \
		if (!(cond)) \
		{ \
			fprintf(stderr, "test_vk_command_state: FAIL (line %d): %s\n", __LINE__, what); \
			failures++; \
		} \
	} while (0)

#define H(x) ((size_t)(x))

static unsigned pipeline_binds, pushes, viewports, scissors, vbo_binds;
static VkPipeline bound_pipeline;
static VkPipelineBindPoint bound_point;
static VkShaderStageFlags push_stages;
static uint32_t push_size, push_offset;
static uint8_t push_data[GRANITE_PUSH_CONSTANT_SIZE];
static VkPipelineLayout push_layout;
static VkViewport set_viewport;
static VkRect2D set_scissor;
/* each call: first binding, count, and the buffers and offsets by binding */
static uint32_t vbo_first[4], vbo_count[4];
static VkBuffer vbo_buffers[4];
static VkDeviceSize vbo_offsets[4];

static void VKAPI_CALL fake_bind_pipeline(VkCommandBuffer cmd, VkPipelineBindPoint point, VkPipeline pipeline)
{
	CHECK(cmd == (VkCommandBuffer)H(9), "into the command buffer");
	pipeline_binds++;
	bound_pipeline = pipeline;
	bound_point = point;
}

static void VKAPI_CALL fake_push_constants(VkCommandBuffer cmd, VkPipelineLayout layout, VkShaderStageFlags stages,
		uint32_t offset, uint32_t size, const void *data)
{
	(void)cmd;
	pushes++;
	push_layout = layout;
	push_stages = stages;
	push_offset = offset;
	push_size = size;
	memcpy(push_data, data, size);
}

static void VKAPI_CALL fake_set_viewport(VkCommandBuffer cmd, uint32_t first, uint32_t count, const VkViewport *vp)
{
	(void)cmd;
	CHECK(first == 0 && count == 1, "one viewport");
	viewports++;
	set_viewport = *vp;
}

static void VKAPI_CALL fake_set_scissor(VkCommandBuffer cmd, uint32_t first, uint32_t count, const VkRect2D *rect)
{
	(void)cmd;
	CHECK(first == 0 && count == 1, "one scissor");
	scissors++;
	set_scissor = *rect;
}

static void VKAPI_CALL fake_bind_vbos(VkCommandBuffer cmd, uint32_t first, uint32_t count, const VkBuffer *buffers,
		const VkDeviceSize *offsets)
{
	uint32_t i;
	(void)cmd;
	if (vbo_binds < 4)
	{
		vbo_first[vbo_binds] = first;
		vbo_count[vbo_binds] = count;
	}
	vbo_binds++;
	for (i = 0; i < count && first + i < 4; i++)
	{
		vbo_buffers[first + i] = buffers[i];
		vbo_offsets[first + i] = offsets[i];
	}
}

static struct VolkDeviceTable table;
static granite_combined_layout_t resources, resources_b;
static granite_bind_layout_t layout, layout_b;
static granite_command_state_t s;
static granite_pipeline_compile_t compile;

static void reset_counts(void)
{
	pipeline_binds = pushes = viewports = scissors = vbo_binds = 0;
}

static void setup(bool compute)
{
	/* no descriptor sets: those are test_vk_resource_bindings' */
	granite_combined_layout_init(&resources);
	resources.push_constant_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
	resources.push_constant_range.size = 24;
	granite_combined_layout_bake(&resources);
	memset(&layout, 0, sizeof(layout));
	layout.hash = 1;
	layout.layout = (VkPipelineLayout)H(0x40);
	layout.resource_layout = &resources;

	/* another layout, without push constants */
	granite_combined_layout_init(&resources_b);
	granite_combined_layout_bake(&resources_b);
	layout_b = layout;
	layout_b.hash = 2;
	layout_b.layout = (VkPipelineLayout)H(0x41);
	layout_b.resource_layout = &resources_b;

	memset(&compile, 0, sizeof(compile));
	memset(&s, 0xee, sizeof(s));
	granite_command_state_init(&s, (VkDevice)H(1), &table, (VkCommandBuffer)H(9));
	granite_command_state_begin(&s, compute);
	reset_counts();
}

static granite_pipeline_t pipe_of(size_t handle, uint32_t dynamic_mask)
{
	granite_pipeline_t p;
	p.pipeline = (VkPipeline)handle;
	p.dynamic_mask = dynamic_mask;
	return p;
}

static void test_draw(void)
{
	const uint32_t vp_scissor = GRANITE_COMMAND_BUFFER_DIRTY_VIEWPORT_BIT | GRANITE_COMMAND_BUFFER_DIRTY_SCISSOR_BIT;
	granite_pipeline_t p;
	VkRect2D area, r;
	uint8_t constants[24];
	unsigned i;

	setup(false);
	CHECK(s.thread_index == 0 && s.pipeline.pipeline == VK_NULL_HANDLE && s.layout == NULL && !s.is_compute &&
			s.dirty == ~(uint32_t)0 && s.dirty_vbos == ~(uint32_t)0 && s.vbo_buffers[3] == VK_NULL_HANDLE,
			"a pass begins with nothing set and everything dirty");
	CHECK(!granite_command_state_flush(&s) && pipeline_binds + pushes + viewports + scissors + vbo_binds == 0,
			"no pipeline: nothing is flushed");

	/* the pass's area, larger than its 320x240 framebuffer */
	area.offset.x = 16;
	area.offset.y = 8;
	area.extent.width = 4096;
	area.extent.height = 100;
	r = granite_command_state_set_render_area(&s, &area, 320, 240);
	CHECK(r.offset.x == 16 && r.offset.y == 8 && r.extent.width == 304 && r.extent.height == 100,
			"the render area is what of the area lies in the framebuffer");
	CHECK(s.viewport.x == 16.0f && s.viewport.y == 8.0f && s.viewport.width == 304.0f && s.viewport.height == 100.0f &&
			s.viewport.minDepth == 0.0f && s.viewport.maxDepth == 1.0f && s.scissor.extent.width == 304,
			"and the viewport and scissor");
	area.offset.x = 400;
	area.offset.y = 0;
	area.extent.width = 10;
	r = granite_command_state_set_render_area(&s, &area, 320, 240);
	CHECK(r.offset.x == 320 && r.extent.width == 0, "an area outside it is empty");
	area.offset.x = 0;
	area.extent.width = 0xffffffffu;
	area.extent.height = 0xffffffffu;
	r = granite_command_state_set_render_area(&s, &area, 320, 240);
	CHECK(r.offset.x == 0 && r.extent.width == 320 && r.extent.height == 240, "the whole of it by default");

	for (i = 0; i < sizeof(constants); i++)
		constants[i] = (uint8_t)(i + 1);
	granite_command_state_push_constants(&s, constants, 0, sizeof(constants));
	granite_command_state_set_vertex_attrib(&s, &compile, 0, 0, VK_FORMAT_R32G32_SFLOAT, 0);
	granite_command_state_set_vertex_attrib(&s, &compile, 1, 2, VK_FORMAT_R8G8B8A8_UNORM, 8);
	granite_command_state_set_vertex_binding(&s, &compile, 0, (VkBuffer)H(0x80), 64, 12, VK_VERTEX_INPUT_RATE_VERTEX);
	granite_command_state_set_vertex_binding(&s, &compile, 1, (VkBuffer)H(0x81), 0, 4, VK_VERTEX_INPUT_RATE_VERTEX);
	granite_command_state_set_vertex_binding(&s, &compile, 2, (VkBuffer)H(0x82), 32, 4, VK_VERTEX_INPUT_RATE_INSTANCE);
	CHECK(compile.attribs[1].binding == 2 && compile.attribs[1].format == VK_FORMAT_R8G8B8A8_UNORM &&
			compile.attribs[1].offset == 8 && compile.strides[0] == 12 &&
			compile.input_rates[2] == VK_VERTEX_INPUT_RATE_INSTANCE, "the vertex layout goes to the pipeline's state");

	granite_command_state_set_program(&s, &layout);
	CHECK(granite_command_state_pipeline_dirty(&s), "a program set: the pipeline is asked for");
	/* the pipeline reads buffers 0 and 2, and leaves viewport and scissor
	 * dynamic */
	s.active_vbos = 0x5;
	p = pipe_of(0x100, vp_scissor);
	granite_command_state_set_pipeline(&s, &p);
	CHECK(pipeline_binds == 1 && bound_pipeline == (VkPipeline)H(0x100) && bound_point == VK_PIPELINE_BIND_POINT_GRAPHICS,
			"and bound for graphics");
	CHECK(!granite_command_state_pipeline_dirty(&s), "it is not asked for again");

	CHECK(granite_command_state_flush(&s), "the state is flushed");
	CHECK(pushes == 1 && push_layout == layout.layout && push_offset == 0 && push_size == 24 &&
			push_stages == (VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT) &&
			memcmp(push_data, constants, 24) == 0, "the push constants, for the program's stages and size");
	CHECK(viewports == 1 && set_viewport.width == 320.0f && scissors == 1 && set_scissor.extent.height == 240,
			"the viewport and scissor");
	CHECK(vbo_binds == 2 && vbo_first[0] == 0 && vbo_count[0] == 1 && vbo_first[1] == 2 && vbo_count[1] == 1 &&
			vbo_buffers[0] == (VkBuffer)H(0x80) && vbo_offsets[0] == 64 && vbo_buffers[2] == (VkBuffer)H(0x82) &&
			vbo_offsets[2] == 32, "the two vertex buffers the pipeline reads");
	CHECK((s.dirty_vbos & 0x7) == 0x2, "the one it does not read stays dirty");

	reset_counts();
	CHECK(granite_command_state_flush(&s), "a second flush");
	CHECK(pipeline_binds + pushes + viewports + scissors + vbo_binds == 0, "does nothing");

	/* the same things set again: still nothing */
	granite_command_state_set_vertex_attrib(&s, &compile, 1, 2, VK_FORMAT_R8G8B8A8_UNORM, 8);
	granite_command_state_set_vertex_binding(&s, &compile, 0, (VkBuffer)H(0x80), 64, 12, VK_VERTEX_INPUT_RATE_VERTEX);
	CHECK(!granite_command_state_pipeline_dirty(&s), "the same vertex layout: no new pipeline");
	granite_command_state_flush(&s);
	CHECK(vbo_binds == 0, "the same buffer: not bound again");

	/* another offset: bound again, same pipeline */
	granite_command_state_set_vertex_binding(&s, &compile, 0, (VkBuffer)H(0x80), 128, 12, VK_VERTEX_INPUT_RATE_VERTEX);
	CHECK(!granite_command_state_pipeline_dirty(&s), "another offset: no new pipeline");
	granite_command_state_flush(&s);
	CHECK(vbo_binds == 1 && vbo_first[0] == 0 && vbo_count[0] == 1 && vbo_offsets[0] == 128, "but the buffer is bound again");

	/* another stride: a new pipeline, and one that reads 0 to 2 */
	reset_counts();
	granite_command_state_set_vertex_binding(&s, &compile, 0, (VkBuffer)H(0x80), 128, 16, VK_VERTEX_INPUT_RATE_VERTEX);
	CHECK(granite_command_state_pipeline_dirty(&s), "another stride: the pipeline is asked for");
	s.active_vbos = 0x7;
	p = pipe_of(0x101, 0);
	granite_command_state_set_pipeline(&s, &p);
	CHECK(pipeline_binds == 1 && bound_pipeline == (VkPipeline)H(0x101), "another pipeline is bound");
	granite_command_state_flush(&s);
	CHECK(vbo_binds == 1 && vbo_first[0] == 1 && vbo_count[0] == 1 && vbo_buffers[1] == (VkBuffer)H(0x81),
			"the buffer it reads that was never bound is bound now");
	CHECK(viewports == 1 && scissors == 1, "it baked viewport and scissor in: both are set again");
	CHECK(pushes == 0, "the push constants are not");

	/* a run of dirty buffers is one call */
	reset_counts();
	granite_command_state_set_vertex_binding(&s, &compile, 0, (VkBuffer)H(0x90), 0, 16, VK_VERTEX_INPUT_RATE_VERTEX);
	granite_command_state_set_vertex_binding(&s, &compile, 1, (VkBuffer)H(0x91), 0, 4, VK_VERTEX_INPUT_RATE_VERTEX);
	granite_command_state_set_vertex_binding(&s, &compile, 2, (VkBuffer)H(0x92), 0, 4, VK_VERTEX_INPUT_RATE_INSTANCE);
	granite_command_state_flush(&s);
	CHECK(vbo_binds == 1 && vbo_first[0] == 0 && vbo_count[0] == 3 && vbo_buffers[2] == (VkBuffer)H(0x92),
			"three dirty buffers in a row are bound in one call");

	/* the same pipeline again is not bound again */
	reset_counts();
	s.dirty |= GRANITE_COMMAND_BUFFER_DIRTY_STATIC_STATE_BIT;
	CHECK(granite_command_state_pipeline_dirty(&s), "baked state changed: the pipeline is asked for");
	granite_command_state_set_pipeline(&s, &p);
	granite_command_state_flush(&s);
	CHECK(pipeline_binds == 0 && viewports == 0 && scissors == 0, "the one bound already is not bound again");

	/* a pipeline that leaves the viewport dynamic and bakes the scissor */
	p = pipe_of(0x102, GRANITE_COMMAND_BUFFER_DIRTY_VIEWPORT_BIT);
	s.dirty |= GRANITE_COMMAND_BUFFER_DIRTY_STATIC_STATE_BIT;
	granite_command_state_pipeline_dirty(&s);
	granite_command_state_set_pipeline(&s, &p);
	granite_command_state_flush(&s);
	CHECK(pipeline_binds == 1 && viewports == 0 && scissors == 1, "only what the pipeline bakes in is set again");

	/* scissor: set, and clipped */
	reset_counts();
	r.offset.x = -10;
	r.offset.y = 4;
	r.extent.width = 100;
	r.extent.height = 0xffffffffu;
	granite_command_state_set_scissor(&s, &r);
	granite_command_state_flush(&s);
	CHECK(scissors == 1 && set_scissor.offset.x == 0 && set_scissor.extent.width == 90 && set_scissor.offset.y == 4 &&
			set_scissor.extent.height == 0x7fffffffu - 4, "a scissor is clipped to what Vulkan takes");
	CHECK(s.scissor.offset.x == -10, "the one set is kept as set");
	r.offset.x = 5;
	r.offset.y = -3;
	r.extent.width = 0xffffffffu;
	r.extent.height = 50;
	granite_command_state_set_scissor(&s, &r);
	granite_command_state_flush(&s);
	CHECK(scissors == 2 && set_scissor.offset.x == 5 && set_scissor.extent.width == 0x7fffffffu - 5 &&
			set_scissor.offset.y == 0 && set_scissor.extent.height == 47, "in width as in height");

	/* push constants: part of the block, and past its end */
	reset_counts();
	constants[0] = 0xaa;
	granite_command_state_push_constants(&s, constants, 20, 1);
	granite_command_state_flush(&s);
	CHECK(pushes == 1 && push_data[20] == 0xaa && push_data[19] == 20, "part of the block is updated and all of it pushed");
	reset_counts();
	granite_command_state_push_constants(&s, constants, GRANITE_PUSH_CONSTANT_SIZE - 8, 9);
	granite_command_state_push_constants(&s, constants, GRANITE_PUSH_CONSTANT_SIZE + 1, 0);
	granite_command_state_push_constants(&s, constants, 4, (size_t)-1);
	granite_command_state_flush(&s);
	CHECK(pushes == 0 && s.bindings.push_constant_data[GRANITE_PUSH_CONSTANT_SIZE - 8] == 0, "bytes past its end are refused");
	granite_command_state_push_constants(&s, constants, GRANITE_PUSH_CONSTANT_SIZE - 8, 8);
	granite_command_state_flush(&s);
	CHECK(pushes == 1 && s.bindings.push_constant_data[GRANITE_PUSH_CONSTANT_SIZE - 1] == 8, "its last bytes are not");

	/* out of range vertex state */
	s.dirty = 0;
	s.dirty_vbos = 0;
	granite_command_state_set_vertex_attrib(&s, &compile, GRANITE_NUM_VERTEX_ATTRIBS, 0, VK_FORMAT_R32_SFLOAT, 0);
	granite_command_state_set_vertex_attrib(&s, &compile, 0, GRANITE_NUM_VERTEX_BUFFERS, VK_FORMAT_R32_SFLOAT, 0);
	granite_command_state_set_vertex_binding(&s, &compile, GRANITE_NUM_VERTEX_BUFFERS, (VkBuffer)H(0x99), 0, 4,
			VK_VERTEX_INPUT_RATE_VERTEX);
	CHECK(s.dirty == 0 && s.dirty_vbos == 0 && compile.attribs[0].binding == 0,
			"an attribute or buffer out of range is refused");

	/* programs and layouts */
	reset_counts();
	granite_command_state_set_program(&s, &layout);
	CHECK(s.pipeline.pipeline == VK_NULL_HANDLE && granite_command_state_pipeline_dirty(&s),
			"another program of the same layout: the pipeline is asked for");
	p = pipe_of(0x102, GRANITE_COMMAND_BUFFER_DIRTY_VIEWPORT_BIT);
	granite_command_state_set_pipeline(&s, &p);
	granite_command_state_flush(&s);
	CHECK(pipeline_binds == 1 && pushes == 0, "bound, and the push constants are not pushed again");

	reset_counts();
	granite_command_state_set_program(&s, &layout_b);
	granite_command_state_pipeline_dirty(&s);
	p = pipe_of(0x103, vp_scissor);
	granite_command_state_set_pipeline(&s, &p);
	CHECK(granite_command_state_flush(&s) && s.layout == &layout_b, "a program of another layout");
	CHECK(pushes == 0 && !(s.dirty & GRANITE_COMMAND_BUFFER_DIRTY_PUSH_CONSTANTS_BIT),
			"one without push constants pushes none");
	granite_command_state_set_program(&s, &layout);
	granite_command_state_pipeline_dirty(&s);
	p = pipe_of(0x100, vp_scissor);
	granite_command_state_set_pipeline(&s, &p);
	granite_command_state_flush(&s);
	CHECK(pushes == 1 && push_layout == layout.layout, "back to one with them: pushed again");

	/* no program, and no pipeline to be had */
	reset_counts();
	granite_command_state_set_program(&s, NULL);
	CHECK(s.pipeline.pipeline == VK_NULL_HANDLE && s.layout == &layout, "no program: no pipeline, the layout kept");
	CHECK(!granite_command_state_flush(&s), "and nothing to flush with");
	granite_command_state_set_program(&s, &layout);
	CHECK(granite_command_state_pipeline_dirty(&s), "a program again: the pipeline is asked for");
	p = pipe_of(0, 0);
	granite_command_state_set_pipeline(&s, &p);
	CHECK(!granite_command_state_flush(&s) && pipeline_binds == 0, "none to be had: nothing bound or flushed");
	CHECK(granite_command_state_pipeline_dirty(&s), "and it is asked for again the next time");
	p = pipe_of(0x100, vp_scissor);
	granite_command_state_set_pipeline(&s, &p);
	CHECK(granite_command_state_flush(&s) && pipeline_binds == 1, "until there is one");

	/* a new pass forgets it all but the push constants */
	granite_command_state_begin(&s, false);
	CHECK(s.pipeline.pipeline == VK_NULL_HANDLE && s.layout == NULL && s.vbo_buffers[0] == VK_NULL_HANDLE &&
			s.dirty == ~(uint32_t)0 && s.bindings.push_constant_data[20] == 0xaa,
			"a new pass forgets everything but the push constants");
}

static void test_compute(void)
{
	granite_pipeline_t p;
	uint8_t constants[4] = { 1, 2, 3, 4 };

	setup(true);
	CHECK(s.is_compute, "compute state");
	resources.push_constant_range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	granite_command_state_push_constants(&s, constants, 0, 4);
	granite_command_state_set_program(&s, &layout);
	CHECK(granite_command_state_pipeline_dirty(&s), "the pipeline is asked for");
	p = pipe_of(0x200, 0);
	granite_command_state_set_pipeline(&s, &p);
	CHECK(pipeline_binds == 1 && bound_point == VK_PIPELINE_BIND_POINT_COMPUTE, "and bound for compute");
	CHECK(granite_command_state_flush(&s), "the state is flushed");
	CHECK(pushes == 1 && push_stages == VK_SHADER_STAGE_COMPUTE_BIT && push_data[3] == 4, "the push constants");
	CHECK(viewports == 0 && scissors == 0 && vbo_binds == 0, "and none of the draw state");

	/* the vertex layout is not a compute pipeline's */
	s.dirty |= GRANITE_COMMAND_BUFFER_DIRTY_STATIC_VERTEX_BIT;
	CHECK(!granite_command_state_pipeline_dirty(&s), "a vertex layout change asks for no compute pipeline");
	s.dirty |= GRANITE_COMMAND_BUFFER_DIRTY_STATIC_STATE_BIT;
	CHECK(granite_command_state_pipeline_dirty(&s), "baked state does");
}

int main(void)
{
	table.vkCmdBindPipeline = fake_bind_pipeline;
	table.vkCmdPushConstants = fake_push_constants;
	table.vkCmdSetViewport = fake_set_viewport;
	table.vkCmdSetScissor = fake_set_scissor;
	table.vkCmdBindVertexBuffers = fake_bind_vbos;

	test_draw();
	test_compute();

	if (failures)
	{
		fprintf(stderr, "test_vk_command_state: %u failure(s)\n", failures);
		return 1;
	}
	printf("test_vk_command_state: OK\n");
	return 0;
}
