/* parallel-rdp's command buffer (vulkan/command_buffer.c), with the
 * real state tracking, caches, render targets and pipeline building
 * through a stand-in device table.
 *
 * A new command buffer has one reference and records outside a render
 * pass. Transfers and barriers go to the VkCommandBuffer as given, a
 * barrier after all graphics stages narrowed on a driver that wants
 * that. A render pass begins with its framebuffer, area and clear
 * values and ends back outside.
 *
 * Nothing is dispatched without a program. The first dispatch with one
 * builds its pipeline, keeps it in the program and reports it once; the
 * next finds it there. Asked not to block, a pipeline the driver has
 * not cached is not built, and nothing is recorded. A subgroup size the
 * device cannot give makes no pipeline. Constant data comes from a
 * uniform block asked of the device when the one held has no room.
 * Ending ends the VkCommandBuffer once and gives the blocks back, and
 * the last reference returns the object to its pool. */
#include <stdio.h>
#include <string.h>
#include <boolean.h>

#include "command_buffer.h"
#include "pipeline_record.h"

static unsigned failures;

#define CHECK(cond, what) \
	do \
	{ \
		if (!(cond)) \
		{ \
			fprintf(stderr, "test_vk_command_buffer: FAIL (line %d): %s\n", __LINE__, what); \
			failures++; \
		} \
	} while (0)

#define DEV ((VkDevice)(size_t)1)
#define CMD ((VkCommandBuffer)(size_t)0x10)
#define CACHE ((VkPipelineCache)(size_t)0x20)
#define BUF(n) ((VkBuffer)(size_t)(0x100 + (n)))

/* (device_caps.c is linked for its subgroup check only) */
PFN_vkGetPhysicalDeviceToolPropertiesEXT vkGetPhysicalDeviceToolPropertiesEXT;
PFN_vkGetPhysicalDeviceFormatProperties2 vkGetPhysicalDeviceFormatProperties2;

/* ---- the stand-in device ---- */
static unsigned fills, copies, barriers, binds, dispatches, indirects, draws, ends, passes_begun, passes_ended;
static unsigned compute_created, graphics_created, pipelines_destroyed, compile_required;
static VkBuffer fill_buffer, copy_src, copy_dst, indirect_buffer;
static VkDeviceSize fill_offset, fill_size;
static uint32_t fill_value, copy_count, indirect_offset, dispatch_x, dispatch_y, dispatch_z;
static VkPipelineStageFlags barrier_src, barrier_dst;
static uint32_t barrier_globals;
static VkAccessFlags barrier_src_access;
static VkPipeline bound_pipeline;
static VkPipelineBindPoint bound_point;
static VkRenderPassBeginInfo begun;
static VkClearValue begun_clear[GRANITE_NUM_ATTACHMENTS + 1];
static VkPipelineCreateFlags compute_flags;
static bool driver_has_cached = true;

static void VKAPI_CALL fake_fill(VkCommandBuffer c, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize size, uint32_t data)
{
	CHECK(c == CMD, "into the command buffer");
	fills++;
	fill_buffer = buffer;
	fill_offset = offset;
	fill_size = size;
	fill_value = data;
}

static void VKAPI_CALL fake_copy(VkCommandBuffer c, VkBuffer src, VkBuffer dst, uint32_t count, const VkBufferCopy *regions)
{
	(void)c; (void)regions;
	copies++;
	copy_src = src;
	copy_dst = dst;
	copy_count = count;
}

static void VKAPI_CALL fake_barrier(VkCommandBuffer c, VkPipelineStageFlags src, VkPipelineStageFlags dst,
		VkDependencyFlags flags, uint32_t num_globals, const VkMemoryBarrier *globals, uint32_t num_buffers,
		const VkBufferMemoryBarrier *buffers, uint32_t num_images, const VkImageMemoryBarrier *images)
{
	(void)c; (void)flags; (void)num_buffers; (void)buffers; (void)num_images; (void)images;
	barriers++;
	barrier_src = src;
	barrier_dst = dst;
	barrier_globals = num_globals;
	barrier_src_access = num_globals ? globals[0].srcAccessMask : 0;
}

static void VKAPI_CALL fake_bind_pipeline(VkCommandBuffer c, VkPipelineBindPoint point, VkPipeline pipeline)
{
	(void)c;
	binds++;
	bound_point = point;
	bound_pipeline = pipeline;
}

static void VKAPI_CALL fake_dispatch(VkCommandBuffer c, uint32_t x, uint32_t y, uint32_t z)
{
	(void)c;
	dispatches++;
	dispatch_x = x;
	dispatch_y = y;
	dispatch_z = z;
}

static void VKAPI_CALL fake_dispatch_indirect(VkCommandBuffer c, VkBuffer buffer, VkDeviceSize offset)
{
	(void)c;
	indirects++;
	indirect_buffer = buffer;
	indirect_offset = (uint32_t)offset;
}

static void VKAPI_CALL fake_draw(VkCommandBuffer c, uint32_t vertices, uint32_t instances, uint32_t first_vertex,
		uint32_t first_instance)
{
	(void)c; (void)vertices; (void)instances; (void)first_vertex; (void)first_instance;
	draws++;
}

static VkResult VKAPI_CALL fake_end(VkCommandBuffer c)
{
	(void)c;
	ends++;
	return VK_SUCCESS;
}

static void VKAPI_CALL fake_begin_pass(VkCommandBuffer c, const VkRenderPassBeginInfo *info, VkSubpassContents contents)
{
	uint32_t i;
	(void)c;
	CHECK(contents == VK_SUBPASS_CONTENTS_INLINE, "its commands are inline");
	passes_begun++;
	begun = *info;
	for (i = 0; i < info->clearValueCount && i <= GRANITE_NUM_ATTACHMENTS; i++)
		begun_clear[i] = info->pClearValues[i];
}

static void VKAPI_CALL fake_end_pass(VkCommandBuffer c)
{
	(void)c;
	passes_ended++;
}

static void VKAPI_CALL fake_set_viewport(VkCommandBuffer c, uint32_t first, uint32_t count, const VkViewport *viewports)
{
	(void)c; (void)first; (void)count; (void)viewports;
}

static void VKAPI_CALL fake_set_scissor(VkCommandBuffer c, uint32_t first, uint32_t count, const VkRect2D *scissors)
{
	(void)c; (void)first; (void)count; (void)scissors;
}

static VkResult VKAPI_CALL fake_create_compute(VkDevice d, VkPipelineCache cache, uint32_t count,
		const VkComputePipelineCreateInfo *info, const VkAllocationCallbacks *a, VkPipeline *pipeline)
{
	(void)d; (void)count; (void)a;
	CHECK(cache == CACHE, "built with the command buffer's pipeline cache");
	compute_flags = info->flags;
	if ((info->flags & VK_PIPELINE_CREATE_FAIL_ON_PIPELINE_COMPILE_REQUIRED_BIT_EXT) && !driver_has_cached)
	{
		compile_required++;
		*pipeline = VK_NULL_HANDLE;
		return VK_PIPELINE_COMPILE_REQUIRED_EXT;
	}
	compute_created++;
	*pipeline = (VkPipeline)(size_t)(0x800 + compute_created);
	return VK_SUCCESS;
}

static VkResult VKAPI_CALL fake_create_graphics(VkDevice d, VkPipelineCache cache, uint32_t count,
		const VkGraphicsPipelineCreateInfo *info, const VkAllocationCallbacks *a, VkPipeline *pipeline)
{
	(void)d; (void)cache; (void)count; (void)info; (void)a;
	graphics_created++;
	*pipeline = (VkPipeline)(size_t)(0x900 + graphics_created);
	return VK_SUCCESS;
}

static void VKAPI_CALL fake_destroy_pipeline(VkDevice d, VkPipeline pipeline, const VkAllocationCallbacks *a)
{
	(void)d; (void)pipeline; (void)a;
	pipelines_destroyed++;
}

static VkResult VKAPI_CALL fake_create_handle(VkDevice d, const void *info, const VkAllocationCallbacks *a, uint64_t *handle)
{
	static uint64_t next = 0x5000;
	(void)d; (void)info; (void)a;
	*handle = ++next;
	return VK_SUCCESS;
}

static void VKAPI_CALL fake_destroy_handle(VkDevice d, uint64_t handle, const VkAllocationCallbacks *a)
{
	(void)d; (void)handle; (void)a;
}

/* ---- the device's side ---- */
static granite_buffer_t block_buffer;
static uint8_t block_memory[4096];
static unsigned block_requests, vertex_requests, blocks_returned;
static VkDeviceSize block_request_size;
static unsigned records;
static uint32_t record_kind;
static uint64_t record_hash;
static int user_token;

static void request_block(void *user, bool vertex, granite_buffer_block_t *block, VkDeviceSize size)
{
	CHECK(user == &user_token, "the device's pointer comes back");
	if (!size)
	{
		blocks_returned++;
		memset(block, 0, sizeof(*block));
		return;
	}
	block_requests++;
	if (vertex)
		vertex_requests++;
	block_request_size = size;
	memset(block, 0, sizeof(*block));
	block->handle = &block_buffer;
	block->mapped = block_memory;
	block->size = sizeof(block_memory);
	block->alignment = 16;
	block->spill_size = 0;
}

static void record_pipeline(void *user, const granite_pipeline_compile_t *compile, uint32_t kind)
{
	(void)user;
	records++;
	record_kind = kind;
	record_hash = compile->hash;
}

int main(void)
{
	static const uint32_t code_compute[] = { 0x07230203u, 1, 1, 1 };
	static const uint32_t code_vertex[] = { 0x07230203u, 2, 2, 2 };
	static const uint32_t code_fragment[] = { 0x07230203u, 3, 3, 3 };
	struct VolkDeviceTable table;
	granite_workarounds_t workarounds;
	granite_device_features_t features;
	granite_program_cache_t programs;
	granite_framebuffer_allocator_t framebuffers;
	granite_cmd_device_t dev;
	granite_lf_pool_t pool;
	granite_command_buffer_t *cb, *again;
	granite_buffer_t a, b;
	granite_program_t *compute, *graphics;
	granite_resource_layout_t fragment_layout;
	granite_image_t image;
	granite_image_view_t view;
	granite_render_pass_info_t pass;
	granite_pipeline_compile_t extracted;
	VkBufferCopy region;
	VkMemoryBarrier global;
	uint8_t *data;
	uint64_t first_hash;

	memset(&table, 0, sizeof(table));
	table.vkCmdFillBuffer = fake_fill;
	table.vkCmdCopyBuffer = fake_copy;
	table.vkCmdPipelineBarrier = fake_barrier;
	table.vkCmdBindPipeline = fake_bind_pipeline;
	table.vkCmdDispatch = fake_dispatch;
	table.vkCmdDispatchIndirect = fake_dispatch_indirect;
	table.vkCmdDraw = fake_draw;
	table.vkEndCommandBuffer = fake_end;
	table.vkCmdBeginRenderPass = fake_begin_pass;
	table.vkCmdEndRenderPass = fake_end_pass;
	table.vkCmdSetViewport = fake_set_viewport;
	table.vkCmdSetScissor = fake_set_scissor;
	table.vkCreateComputePipelines = fake_create_compute;
	table.vkCreateGraphicsPipelines = fake_create_graphics;
	table.vkDestroyPipeline = fake_destroy_pipeline;
	table.vkCreateShaderModule = (PFN_vkCreateShaderModule)fake_create_handle;
	table.vkDestroyShaderModule = (PFN_vkDestroyShaderModule)fake_destroy_handle;
	table.vkCreateDescriptorSetLayout = (PFN_vkCreateDescriptorSetLayout)fake_create_handle;
	table.vkDestroyDescriptorSetLayout = (PFN_vkDestroyDescriptorSetLayout)fake_destroy_handle;
	table.vkCreatePipelineLayout = (PFN_vkCreatePipelineLayout)fake_create_handle;
	table.vkDestroyPipelineLayout = (PFN_vkDestroyPipelineLayout)fake_destroy_handle;
	table.vkCreateDescriptorUpdateTemplate = (PFN_vkCreateDescriptorUpdateTemplate)fake_create_handle;
	table.vkDestroyDescriptorUpdateTemplate = (PFN_vkDestroyDescriptorUpdateTemplate)fake_destroy_handle;
	table.vkCreateRenderPass = (PFN_vkCreateRenderPass)fake_create_handle;
	table.vkDestroyRenderPass = (PFN_vkDestroyRenderPass)fake_destroy_handle;
	table.vkCreateFramebuffer = (PFN_vkCreateFramebuffer)fake_create_handle;

	memset(&workarounds, 0, sizeof(workarounds));
	memset(&features, 0, sizeof(features));
	features.pipeline_creation_cache_control_features.pipelineCreationCacheControl = VK_TRUE;
	features.subgroup_size_control_properties.minSubgroupSize = 4;
	features.subgroup_size_control_properties.maxSubgroupSize = 64;
	granite_program_cache_init(&programs, DEV, &table, false, 0, 8, 1);
	granite_framebuffer_allocator_init(&framebuffers, DEV, &table, NULL, NULL);
	memset(&dev, 0, sizeof(dev));
	dev.device = DEV;
	dev.table = &table;
	dev.workarounds = &workarounds;
	dev.features = &features;
	dev.programs = &programs;
	dev.framebuffers = &framebuffers;
	dev.user = &user_token;
	dev.request_block = request_block;
	dev.record_pipeline = record_pipeline;
	granite_cb_pool_slots_init(&pool);

	memset(&a, 0, sizeof(a));
	memset(&b, 0, sizeof(b));
	memset(&block_buffer, 0, sizeof(block_buffer));
	a.buffer = BUF(1);
	a.cookie = 16;
	b.buffer = BUF(2);
	b.cookie = 32;
	block_buffer.buffer = BUF(3);
	block_buffer.cookie = 48;

	/* ---- a new one ---- */
	cb = granite_cb_new(&pool, &dev, CMD, CACHE, GRANITE_QUEUE_COMPUTE);
	CHECK(cb && granite_refs_only(&cb->refs) && cb->cmd == CMD && cb->type == GRANITE_QUEUE_COMPUTE && cb->dev == &dev
			&& cb->table == &table && cb->pipeline_state.cache == CACHE, "a command buffer with one reference, as given");
	CHECK(cb->state.is_compute && !cb->framebuffer && !cb->actual_render_pass && !cb->is_ended
			&& !cb->pipeline_state.program && !cb->vbo_block.mapped && !cb->ubo_block.mapped,
			"recording outside a render pass, with nothing set");
	CHECK(cb->pipeline_state.subgroup_size_tag == (4u | (64u << 8)), "its pipelines are tagged with the device's subgroup sizes");

	/* ---- transfers and barriers ---- */
	granite_cb_fill_buffer(cb, &a, 0xabcd, 64, 128);
	CHECK(fills == 1 && fill_buffer == BUF(1) && fill_offset == 64 && fill_size == 128 && fill_value == 0xabcd,
			"a fill goes in as given");
	region.srcOffset = 0;
	region.dstOffset = 0;
	region.size = 16;
	granite_cb_copy_buffer(cb, &a, &b, &region, 1);
	CHECK(copies == 1 && copy_src == BUF(2) && copy_dst == BUF(1) && copy_count == 1, "a copy goes from source to destination");

	memset(&global, 0, sizeof(global));
	global.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	global.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
	granite_cb_barrier(cb, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 1, &global, 0, NULL, 0, NULL);
	CHECK(barriers == 1 && barrier_src == VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT && barrier_dst == VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
			&& barrier_globals == 1 && barrier_src_access == VK_ACCESS_SHADER_WRITE_BIT, "a barrier goes in as given");
	workarounds.optimize_all_graphics_barrier = true;
	granite_cb_barrier(cb, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 1, &global, 0, NULL, 0, NULL);
	CHECK(barriers == 2 && barrier_src != VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT && barrier_src != 0,
			"on a driver that wants it, all graphics stages are narrowed");
	workarounds.optimize_all_graphics_barrier = false;

	/* ---- dispatching ---- */
	granite_cb_dispatch(cb, 1, 2, 3);
	CHECK(dispatches == 0 && compute_created == 0, "nothing is dispatched without a program");

	/* (a shader with one specialization constant) */
	memset(&fragment_layout, 0, sizeof(fragment_layout));
	fragment_layout.spec_constant_mask = 1;
	compute = granite_program_cache_compute(&programs,
			granite_program_cache_shader(&programs, code_compute, sizeof(code_compute), &fragment_layout));
	granite_cb_set_program(cb, compute);
	CHECK(cb->pipeline_state.program == compute, "the program is set");
	granite_cb_dispatch(cb, 1, 2, 3);
	CHECK(dispatches == 1 && dispatch_x == 1 && dispatch_y == 2 && dispatch_z == 3, "with one it is");
	CHECK(compute_created == 1 && binds == 1 && bound_point == VK_PIPELINE_BIND_POINT_COMPUTE
			&& bound_pipeline == (VkPipeline)(size_t)0x801, "its pipeline is built and bound");
	CHECK(!(compute_flags & VK_PIPELINE_CREATE_FAIL_ON_PIPELINE_COMPILE_REQUIRED_BIT_EXT), "however long it takes");
	first_hash = cb->pipeline_state.hash;
	CHECK(granite_pipeline_map_find(&compute->pipelines, first_hash).pipeline == (VkPipeline)(size_t)0x801,
			"kept in the program under its hash");
	CHECK(records == 1 && record_kind == PIPELINE_RECORD_COMPUTE && record_hash == first_hash, "and reported once");

	granite_cb_dispatch(cb, 4, 4, 4);
	granite_cb_dispatch_indirect(cb, &a, 24);
	CHECK(dispatches == 2 && indirects == 1 && indirect_buffer == BUF(1) && indirect_offset == 24 && compute_created == 1
			&& binds == 1 && records == 1, "the next dispatches use it as it is");

	/* other state: another pipeline of the same program */
	cb->pipeline_state.potential_static_state.spec_constants[0] = 7;
	cb->pipeline_state.potential_static_state.spec_constant_mask = 1;
	granite_cb_set_dirty(cb, GRANITE_COMMAND_BUFFER_DIRTY_STATIC_STATE_BIT);
	granite_cb_dispatch(cb, 1, 1, 1);
	CHECK(compute_created == 2 && cb->pipeline_state.hash != first_hash && records == 2 && binds == 2,
			"other state is another pipeline, built and reported");

	/* the state, to build elsewhere */
	memset(&extracted, 0xff, sizeof(extracted));
	granite_cb_extract_pipeline_state(cb, &extracted);
	CHECK(extracted.program == compute && extracted.hash == cb->pipeline_state.hash && extracted.cache == CACHE,
			"the state extracted carries its program and hash");

	/* state changed and not yet flushed: the hash is of the state as it is */
	cb->pipeline_state.potential_static_state.spec_constants[0] = 99;
	granite_cb_extract_pipeline_state(cb, &extracted);
	CHECK(extracted.hash != cb->pipeline_state.hash, "extracting works the hash out again");

	/* another command buffer with the same state finds the pipeline */
	{
		granite_command_buffer_t *other = granite_cb_new(&pool, &dev, CMD, CACHE, GRANITE_QUEUE_COMPUTE);
		unsigned before = compute_created, before_binds = binds;

		granite_cb_set_program(other, compute);
		other->pipeline_state.potential_static_state.spec_constants[0] = 7;
		other->pipeline_state.potential_static_state.spec_constant_mask = 1;
		granite_cb_dispatch(other, 1, 1, 1);
		CHECK(compute_created == before && binds == before_binds + 1 && bound_pipeline == (VkPipeline)(size_t)0x802,
				"a pipeline the program has is found, not built again");
		granite_cb_unref(other);
	}

	/* not blocking */
	cb->pipeline_state.potential_static_state.spec_constants[0] = 8;
	granite_cb_set_dirty(cb, GRANITE_COMMAND_BUFFER_DIRTY_STATIC_STATE_BIT);
	driver_has_cached = false;
	CHECK(!granite_cb_flush_state(cb, false) && compile_required == 1 && compute_created == 2 && records == 2,
			"asked not to block, a pipeline the driver has not cached is not built");
	CHECK(compute_flags & VK_PIPELINE_CREATE_FAIL_ON_PIPELINE_COMPILE_REQUIRED_BIT_EXT, "the driver is told not to compile");
	CHECK(granite_pipeline_map_find(&compute->pipelines, cb->pipeline_state.hash).pipeline == VK_NULL_HANDLE,
			"and nothing is kept for it");
	driver_has_cached = true;
	granite_cb_set_dirty(cb, GRANITE_COMMAND_BUFFER_DIRTY_STATIC_STATE_BIT);
	CHECK(granite_cb_flush_state(cb, false) && compute_created == 3, "one it has cached is");

	/* the same pipeline built by two: the second yields */
	extracted = cb->pipeline_state;
	CHECK(granite_pipeline_compile_build_compute(&dev, &extracted, GRANITE_COMPILE_ASYNC_THREAD).pipeline
			== granite_pipeline_map_find(&compute->pipelines, extracted.hash).pipeline
			&& compute_created == 4 && pipelines_destroyed == 1 && records == 3,
			"a pipeline built for a hash that has one is destroyed, and the first handed back");

	/* a subgroup size the device cannot give */
	cb->pipeline_state.static_state.state.subgroup_control_size = 1;
	cb->pipeline_state.static_state.state.subgroup_minimum_size_log2 = 2;
	cb->pipeline_state.static_state.state.subgroup_maximum_size_log2 = 6;
	granite_cb_set_dirty(cb, GRANITE_COMMAND_BUFFER_DIRTY_STATIC_STATE_BIT);
	CHECK(!granite_cb_flush_state(cb, true) && compute_created == 4, "a subgroup size the device cannot give makes no pipeline");
	cb->pipeline_state.static_state.state.subgroup_control_size = 0;

	/* ---- constant data ---- */
	data = (uint8_t *)granite_cb_allocate_constant_data(cb, 0, 0, 256);
	CHECK(data == block_memory && block_requests == 1 && vertex_requests == 0 && block_request_size == 256,
			"constant data comes from a uniform block asked of the device");
	data = (uint8_t *)granite_cb_allocate_constant_data(cb, 0, 1, 256);
	CHECK(data && data != block_memory && block_requests == 1, "the next piece from the same block");

	/* ---- a render pass ---- */
	memset(&image, 0, sizeof(image));
	memset(&view, 0, sizeof(view));
	image.info.width = 320;
	image.info.height = 240;
	image.info.format = VK_FORMAT_R8G8B8A8_UNORM;
	image.info.samples = VK_SAMPLE_COUNT_1_BIT;
	view.cookie = 64;
	view.views.view = (VkImageView)(size_t)0x700;
	view.info.image = &image;
	view.info.format = VK_FORMAT_R8G8B8A8_UNORM;
	granite_render_pass_info_init(&pass);
	pass.num_color_attachments = 1;
	pass.color_attachments[0] = &view;
	pass.clear_attachments = 1;
	pass.store_attachments = 1;
	pass.clear_color[0].float32[0] = 0.5f;
	CHECK(granite_cb_begin_render_pass(cb, &pass) && passes_begun == 1, "a render pass begins");
	CHECK(cb->framebuffer && cb->actual_render_pass && begun.framebuffer == cb->framebuffer->framebuffer
			&& begun.renderPass == cb->actual_render_pass->impl.render_pass
			&& cb->pipeline_state.compatible_render_pass == cb->framebuffer->render_pass, "with its framebuffer and pass");
	CHECK(begun.renderArea.extent.width == 320 && begun.renderArea.extent.height == 240 && begun.clearValueCount == 1
			&& begun_clear[0].color.float32[0] == 0.5f, "over the framebuffer, with its clear value");
	CHECK(!cb->state.is_compute && cb->pipeline_state.program == NULL, "and graphics state to set");

	memset(&fragment_layout, 0, sizeof(fragment_layout));
	fragment_layout.output_mask = 1;
	graphics = granite_program_cache_graphics(&programs,
			granite_program_cache_shader(&programs, code_vertex, sizeof(code_vertex), NULL),
			granite_program_cache_shader(&programs, code_fragment, sizeof(code_fragment), &fragment_layout));
	granite_cb_set_program(cb, graphics);
	granite_cb_set_opaque_state(cb);
	CHECK(cb->pipeline_state.static_state.state.depth_test && cb->pipeline_state.static_state.state.depth_write
			&& cb->pipeline_state.static_state.state.cull_mode == VK_CULL_MODE_BACK_BIT
			&& cb->pipeline_state.static_state.state.write_mask == ~0u && !cb->pipeline_state.static_state.state.blend_enable,
			"opaque state: depth tested and written, back faces culled, no blending");
	data = (uint8_t *)granite_cb_allocate_vertex_data(cb, 0, 64, 16, VK_VERTEX_INPUT_RATE_VERTEX);
	CHECK(data && vertex_requests == 1, "vertex data comes from a vertex block");
	granite_cb_draw(cb, 3, 1, 0, 0);
	CHECK(draws == 1 && graphics_created == 1 && bound_point == VK_PIPELINE_BIND_POINT_GRAPHICS
			&& record_kind == PIPELINE_RECORD_GRAPHICS, "a draw builds, binds and reports its graphics pipeline");
	granite_cb_draw(cb, 3, 1, 0, 0);
	CHECK(draws == 2 && graphics_created == 1, "once");
	CHECK(!(cb->state.dirty & GRANITE_COMMAND_BUFFER_DIRTY_STATIC_STATE_BIT), "(flushed: nothing dirty)");
	granite_cb_set_opaque_state(cb);
	CHECK(cb->state.dirty & GRANITE_COMMAND_BUFFER_DIRTY_STATIC_STATE_BIT, "setting opaque state makes the pipeline state dirty");

	granite_cb_end_render_pass(cb);
	CHECK(passes_ended == 1 && !cb->framebuffer && !cb->actual_render_pass && !cb->pipeline_state.compatible_render_pass
			&& cb->state.is_compute && !cb->pipeline_state.program, "the pass ends back outside, with nothing set");

	/* ---- ending ---- */
	granite_cb_end(cb);
	CHECK(ends == 1 && cb->is_ended && blocks_returned == 2 && !cb->ubo_block.mapped && !cb->vbo_block.mapped,
			"ending ends the VkCommandBuffer and gives both blocks back");
	granite_cb_end(cb);
	granite_cb_end_recording(cb);
	CHECK(ends == 1 && blocks_returned == 2, "once");

	granite_cb_ref(cb);
	granite_cb_unref(cb);
	CHECK(granite_refs_only(&cb->refs), "a reference added and dropped leaves it");
	granite_cb_unref(cb);
	again = granite_cb_new(&pool, &dev, CMD, CACHE, GRANITE_QUEUE_GRAPHICS);
	CHECK(again == cb && !again->is_ended && again->type == GRANITE_QUEUE_GRAPHICS && !again->pipeline_state.program
			&& again->state.is_compute, "its last reference returns it to the pool, and the next one starts over");
	granite_cb_unref(again);

	granite_framebuffer_allocator_free(&framebuffers);
	granite_program_cache_free(&programs, NULL, NULL);
	granite_lf_pool_clear(&pool);

	if (failures)
	{
		fprintf(stderr, "test_vk_command_buffer: %u failure(s)\n", failures);
		return 1;
	}
	printf("test_vk_command_buffer: ok\n");
	return 0;
}
