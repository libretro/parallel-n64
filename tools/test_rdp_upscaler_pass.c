/* parallel-RDP's upscaler passes (parallel-rdp/upscaler_pass.c), on a
 * real device (vulkan/device.c) over a stand-in device table.
 *
 * The motion pass makes three images the size asked for: motion
 * vectors (R16G16_SFLOAT), depth (D32_SFLOAT, D16_UNORM without it) and
 * a translucency mask (R8_UNORM), each an attachment that can be
 * sampled. It clears them, draws the geometry's runs of triangles with
 * each run's depth test, depth write and translucency, and leaves all
 * three to be sampled. The vertices are copied once; the push constants
 * carry the resolution, the jitter and the far plane. Geometry without
 * vertices still clears. When an image cannot be made nothing is handed
 * out. At load the four depth states the pass can draw with are handed
 * to the pipeline list. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <boolean.h>

#include "upscaler_pass.h"
#include "pipeline_record.h"

static unsigned failures;

#define CHECK(cond, what) \
	do \
	{ \
		if (!(cond)) \
		{ \
			fprintf(stderr, "test_rdp_upscaler_pass: FAIL (line %d): %s\n", __LINE__, what); \
			failures++; \
		} \
	} while (0)

#define DEV ((VkDevice)(size_t)1)
#define GPU ((VkPhysicalDevice)(size_t)2)

/* ---- the stand-in device ---- */
static uint64_t next_handle = 0x1000;
static unsigned made, unmade;

static VkResult VKAPI_CALL create_any(VkDevice d, const void *info, const VkAllocationCallbacks *a, uint64_t *out)
{
	(void)d; (void)info; (void)a;
	made++;
	*out = ++next_handle;
	return VK_SUCCESS;
}

static void VKAPI_CALL destroy_any(VkDevice d, uint64_t handle, const VkAllocationCallbacks *a)
{
	(void)d; (void)handle; (void)a;
	unmade++;
}

#define MAX_IMAGES 16
static unsigned num_images, images_destroyed;
static VkImageCreateInfo image_infos[MAX_IMAGES];
static VkImage image_handles[MAX_IMAGES];
static VkImageLayout image_layouts[MAX_IMAGES];

static VkResult VKAPI_CALL create_image(VkDevice d, const VkImageCreateInfo *info, const VkAllocationCallbacks *a,
		VkImage *out)
{
	(void)d; (void)a;
	*out = GRANITE_VK_FROM_BITS(VkImage, ++next_handle);
	if (num_images < MAX_IMAGES)
	{
		image_infos[num_images] = *info;
		image_handles[num_images] = *out;
		image_layouts[num_images] = VK_IMAGE_LAYOUT_UNDEFINED;
		num_images++;
	}
	return VK_SUCCESS;
}

static void VKAPI_CALL destroy_image(VkDevice d, VkImage image, const VkAllocationCallbacks *a)
{
	(void)d; (void)image; (void)a;
	images_destroyed++;
}

static int image_index(VkImage image)
{
	unsigned i;
	for (i = 0; i < num_images; i++)
		if (image_handles[i] == image)
			return (int)i;
	return -1;
}

/* which image a view is of */
#define MAX_VIEWS 64
static unsigned num_views;
static VkImageView view_handles[MAX_VIEWS];
static VkImage view_images[MAX_VIEWS];

static VkResult VKAPI_CALL create_view(VkDevice d, const VkImageViewCreateInfo *info, const VkAllocationCallbacks *a,
		VkImageView *out)
{
	(void)d; (void)a;
	made++;
	*out = GRANITE_VK_FROM_BITS(VkImageView, ++next_handle);
	if (num_views < MAX_VIEWS)
	{
		view_handles[num_views] = *out;
		view_images[num_views] = info->image;
		num_views++;
	}
	return VK_SUCCESS;
}

/* the last render pass and framebuffer made */
static VkAttachmentLoadOp pass_load_ops[4];
static VkFormat framebuffer_formats[4];

static VkResult VKAPI_CALL create_pass(VkDevice d, const VkRenderPassCreateInfo *info, const VkAllocationCallbacks *a,
		VkRenderPass *out)
{
	uint32_t i;
	(void)d; (void)a;
	made++;
	for (i = 0; i < info->attachmentCount && i < 4; i++)
		pass_load_ops[i] = info->pAttachments[i].loadOp;
	*out = GRANITE_VK_FROM_BITS(VkRenderPass, ++next_handle);
	return VK_SUCCESS;
}

static VkResult VKAPI_CALL create_framebuffer(VkDevice d, const VkFramebufferCreateInfo *info,
		const VkAllocationCallbacks *a, VkFramebuffer *out)
{
	uint32_t i, j;
	(void)d; (void)a;
	made++;
	for (i = 0; i < info->attachmentCount && i < 4; i++)
	{
		framebuffer_formats[i] = VK_FORMAT_UNDEFINED;
		for (j = 0; j < num_views; j++)
			if (view_handles[j] == info->pAttachments[i] && image_index(view_images[j]) >= 0)
				framebuffer_formats[i] = image_infos[image_index(view_images[j])].format;
	}
	*out = GRANITE_VK_FROM_BITS(VkFramebuffer, ++next_handle);
	return VK_SUCCESS;
}

static void VKAPI_CALL buffer_requirements(VkDevice d, const VkBufferMemoryRequirementsInfo2 *info,
		VkMemoryRequirements2 *reqs)
{
	(void)d; (void)info;
	reqs->memoryRequirements.size = 1 << 20;
	reqs->memoryRequirements.alignment = 16;
	reqs->memoryRequirements.memoryTypeBits = 1;
}

static void VKAPI_CALL image_requirements(VkDevice d, VkImage image, VkMemoryRequirements *reqs)
{
	(void)d; (void)image;
	reqs->size = 4096;
	reqs->alignment = 16;
	reqs->memoryTypeBits = 1;
}

static void VKAPI_CALL image_requirements2(VkDevice d, const VkImageMemoryRequirementsInfo2 *info,
		VkMemoryRequirements2 *reqs)
{
	image_requirements(d, info->image, &reqs->memoryRequirements);
}

static VkResult VKAPI_CALL bind_any(VkDevice d, uint64_t object, VkDeviceMemory memory, VkDeviceSize offset)
{
	(void)d; (void)object; (void)memory; (void)offset;
	return VK_SUCCESS;
}

#define MAX_MEMORY 32
static uint8_t *memory_data[MAX_MEMORY];
static size_t memory_sizes[MAX_MEMORY];
static unsigned num_memory, memory_freed;

static VkResult VKAPI_CALL allocate_memory(VkDevice d, const VkMemoryAllocateInfo *info,
		const VkAllocationCallbacks *a, VkDeviceMemory *memory)
{
	(void)d; (void)a;
	if (num_memory == MAX_MEMORY)
		return VK_ERROR_OUT_OF_DEVICE_MEMORY;
	memory_sizes[num_memory] = (size_t)info->allocationSize;
	memory_data[num_memory] = (uint8_t *)calloc(1, memory_sizes[num_memory]);
	*memory = GRANITE_VK_FROM_BITS(VkDeviceMemory, ++num_memory);
	return VK_SUCCESS;
}

static void VKAPI_CALL free_memory(VkDevice d, VkDeviceMemory memory, const VkAllocationCallbacks *a)
{
	(void)d; (void)memory; (void)a;
	memory_freed++;
}

static VkResult VKAPI_CALL map_memory(VkDevice d, VkDeviceMemory memory, VkDeviceSize offset, VkDeviceSize size,
		VkMemoryMapFlags flags, void **data)
{
	(void)d; (void)size; (void)flags;
	*data = memory_data[GRANITE_VK_TO_BITS(memory) - 1] + offset;
	return VK_SUCCESS;
}

static void VKAPI_CALL unmap_memory(VkDevice d, VkDeviceMemory memory)
{
	(void)d; (void)memory;
}

static VkResult VKAPI_CALL ok_ranges(VkDevice d, uint32_t count, const VkMappedMemoryRange *ranges)
{
	(void)d; (void)count; (void)ranges;
	return VK_SUCCESS;
}

/* how often @bytes are in memory the host wrote */
static unsigned times_in_memory(const void *bytes, size_t size)
{
	unsigned i, found = 0;
	size_t at;
	for (i = 0; i < num_memory; i++)
		for (at = 0; at + size <= memory_sizes[i]; at += 4)
			if (!memcmp(memory_data[i] + at, bytes, size))
				found++;
	return found;
}

static void VKAPI_CALL memory_properties2(VkPhysicalDevice gpu, VkPhysicalDeviceMemoryProperties2 *props)
{
	(void)gpu; (void)props;
}
PFN_vkGetPhysicalDeviceMemoryProperties2 vkGetPhysicalDeviceMemoryProperties2 = memory_properties2;

static bool have_d32 = true, have_r8 = true;
static void VKAPI_CALL format_properties2(VkPhysicalDevice gpu, VkFormat format, VkFormatProperties2 *props)
{
	bool ok = true;
	(void)gpu;
	if (format == VK_FORMAT_D32_SFLOAT)
		ok = have_d32;
	if (format == VK_FORMAT_R8_UNORM)
		ok = have_r8;
	memset(&props->formatProperties, 0, sizeof(props->formatProperties));
	props->formatProperties.optimalTilingFeatures = ok ? ~(VkFormatFeatureFlags)0 : 0;
}
PFN_vkGetPhysicalDeviceFormatProperties2 vkGetPhysicalDeviceFormatProperties2 = format_properties2;

static VkResult VKAPI_CALL tool_properties(VkPhysicalDevice gpu, uint32_t *count, VkPhysicalDeviceToolProperties *props)
{
	(void)gpu; (void)props;
	*count = 0;
	return VK_SUCCESS;
}
PFN_vkGetPhysicalDeviceToolPropertiesEXT vkGetPhysicalDeviceToolPropertiesEXT = tool_properties;

static char cb_storage[64];
static unsigned cb_allocated;
static VkResult VKAPI_CALL allocate_cbs(VkDevice d, const VkCommandBufferAllocateInfo *info, VkCommandBuffer *out)
{
	(void)d; (void)info;
	*out = (VkCommandBuffer)&cb_storage[cb_allocated++ % sizeof(cb_storage)];
	return VK_SUCCESS;
}

static void VKAPI_CALL free_cbs(VkDevice d, VkCommandPool pool, uint32_t count, const VkCommandBuffer *buffers)
{
	(void)d; (void)pool; (void)count; (void)buffers;
}

static VkResult VKAPI_CALL ok_pool(VkDevice d, VkCommandPool pool, VkFlags flags)
{
	(void)d; (void)pool; (void)flags;
	return VK_SUCCESS;
}

static void VKAPI_CALL trim_pool(VkDevice d, VkCommandPool pool, VkCommandPoolTrimFlags flags)
{
	(void)d; (void)pool; (void)flags;
}

static VkResult VKAPI_CALL ok_cb(VkCommandBuffer cmd, const void *info)
{
	(void)cmd; (void)info;
	return VK_SUCCESS;
}

static VkResult VKAPI_CALL end_cb(VkCommandBuffer cmd)
{
	(void)cmd;
	return VK_SUCCESS;
}

static VkResult VKAPI_CALL queue_submit(VkQueue queue, uint32_t count, const VkSubmitInfo *submits, VkFence fence)
{
	(void)queue; (void)count; (void)submits; (void)fence;
	return VK_SUCCESS;
}

static VkResult VKAPI_CALL wait_idle(VkDevice d)
{
	(void)d;
	return VK_SUCCESS;
}

static VkResult VKAPI_CALL wait_semaphores(VkDevice d, const VkSemaphoreWaitInfo *info, uint64_t timeout)
{
	(void)d; (void)info; (void)timeout;
	return VK_SUCCESS;
}

/* ---- what is recorded ---- */
static unsigned in_pass, passes_begun, passes_ended, bad_layouts;
static uint32_t pass_clear_count;
static VkClearValue pass_clears[4];

static void VKAPI_CALL cmd_barrier(VkCommandBuffer cmd, VkPipelineStageFlags src, VkPipelineStageFlags dst,
		VkDependencyFlags flags, uint32_t memory_count, const VkMemoryBarrier *memory, uint32_t buffer_count,
		const VkBufferMemoryBarrier *buffers, uint32_t image_count, const VkImageMemoryBarrier *images)
{
	uint32_t i;
	(void)cmd; (void)src; (void)dst; (void)flags; (void)memory_count; (void)memory; (void)buffer_count; (void)buffers;
	CHECK(!in_pass, "no barrier inside the pass");
	for (i = 0; i < image_count; i++)
	{
		int n = image_index(images[i].image);
		if (n < 0 || image_layouts[n] != images[i].oldLayout)
			bad_layouts++;
		else
			image_layouts[n] = images[i].newLayout;
	}
}

static void VKAPI_CALL cmd_begin_pass(VkCommandBuffer cmd, const VkRenderPassBeginInfo *info, VkSubpassContents c)
{
	uint32_t i;
	(void)cmd; (void)c;
	in_pass = 1;
	passes_begun++;
	pass_clear_count = info->clearValueCount;
	for (i = 0; i < info->clearValueCount && i < 4; i++)
		pass_clears[i] = info->pClearValues[i];
}

static void VKAPI_CALL cmd_end_pass(VkCommandBuffer cmd)
{
	(void)cmd;
	in_pass = 0;
	passes_ended++;
}

#define MAX_PIPELINES 8
static unsigned num_pipelines;
static struct
{
	VkBool32 depth_test, depth_write, blend;
	VkCompareOp compare;
	VkCullModeFlags cull;
	uint32_t num_attributes, stride, color_attachments;
	VkVertexInputAttributeDescription attributes[8];
} pipelines[MAX_PIPELINES];

static VkResult VKAPI_CALL create_graphics(VkDevice d, VkPipelineCache cache, uint32_t count,
		const VkGraphicsPipelineCreateInfo *infos, const VkAllocationCallbacks *a, VkPipeline *out)
{
	uint32_t i;
	(void)d; (void)cache; (void)count; (void)a;
	if (num_pipelines < MAX_PIPELINES)
	{
		pipelines[num_pipelines].depth_test = infos->pDepthStencilState->depthTestEnable;
		pipelines[num_pipelines].depth_write = infos->pDepthStencilState->depthWriteEnable;
		pipelines[num_pipelines].compare = infos->pDepthStencilState->depthCompareOp;
		pipelines[num_pipelines].blend = infos->pColorBlendState->pAttachments[0].blendEnable;
		pipelines[num_pipelines].cull = infos->pRasterizationState->cullMode;
		pipelines[num_pipelines].color_attachments = infos->pColorBlendState->attachmentCount;
		pipelines[num_pipelines].num_attributes = infos->pVertexInputState->vertexAttributeDescriptionCount;
		pipelines[num_pipelines].stride = infos->pVertexInputState->vertexBindingDescriptionCount ?
			infos->pVertexInputState->pVertexBindingDescriptions[0].stride : 0;
		for (i = 0; i < infos->pVertexInputState->vertexAttributeDescriptionCount && i < 8; i++)
			pipelines[num_pipelines].attributes[infos->pVertexInputState->pVertexAttributeDescriptions[i].location & 7] =
				infos->pVertexInputState->pVertexAttributeDescriptions[i];
	}
	*out = GRANITE_VK_FROM_BITS(VkPipeline, 0x9000 + ++num_pipelines);
	return VK_SUCCESS;
}

static VkPipeline bound_pipeline;
static void VKAPI_CALL cmd_bind_pipeline(VkCommandBuffer cmd, VkPipelineBindPoint point, VkPipeline pipeline)
{
	(void)cmd; (void)point;
	bound_pipeline = pipeline;
}

static void VKAPI_CALL cmd_ignore_range(VkCommandBuffer cmd, uint32_t first, uint32_t count, const void *data)
{
	(void)cmd; (void)first; (void)count; (void)data;
}

static void VKAPI_CALL cmd_bind_vertex(VkCommandBuffer cmd, uint32_t first, uint32_t count, const VkBuffer *buffers,
		const VkDeviceSize *offsets)
{
	(void)cmd; (void)first; (void)count; (void)buffers; (void)offsets;
}

static uint8_t constants[64];
static void VKAPI_CALL cmd_push(VkCommandBuffer cmd, VkPipelineLayout layout, VkShaderStageFlags stages,
		uint32_t offset, uint32_t size, const void *data)
{
	(void)cmd; (void)layout; (void)stages;
	if (offset + size <= sizeof(constants))
		memcpy(constants + offset, data, size);
}

static float constant(unsigned offset)
{
	float value;
	memcpy(&value, constants + offset, sizeof(value));
	return value;
}

#define MAX_DRAWS 8
static unsigned num_draws;
static struct
{
	uint32_t count, first;
	unsigned pipeline;
	float translucency;
} draws[MAX_DRAWS];

static void VKAPI_CALL cmd_draw(VkCommandBuffer cmd, uint32_t count, uint32_t instances, uint32_t first,
		uint32_t first_instance)
{
	(void)cmd;
	CHECK(in_pass && instances == 1 && first_instance == 0, "one instance, inside the pass");
	if (num_draws < MAX_DRAWS)
	{
		draws[num_draws].count = count;
		draws[num_draws].first = first;
		draws[num_draws].pipeline = (unsigned)(GRANITE_VK_TO_BITS(bound_pipeline) - 0x9000);
		draws[num_draws].translucency = constant(8);
	}
	num_draws++;
}

/* ---- the fixture ---- */
static granite_context_t context;
static granite_device_t dev;

static void fill_context(void)
{
	struct VolkDeviceTable *t = &context.device_table;
	unsigned i;

	memset(&context, 0, sizeof(context));
	context.gpu = GPU;
	context.device = DEV;
	for (i = 0; i < GRANITE_QUEUE_COUNT; i++)
	{
		context.queue_info.queues[i] = GRANITE_VK_FROM_BITS(VkQueue, 0x100);
		context.queue_info.family_indices[i] = 0;
	}
	context.gpu_props.limits.nonCoherentAtomSize = 64;
	context.gpu_props.limits.minUniformBufferOffsetAlignment = 64;
	context.gpu_props.limits.maxBoundDescriptorSets = 8;
	context.mem_props.memoryHeapCount = 1;
	context.mem_props.memoryHeaps[0].size = (VkDeviceSize)1 << 32;
	context.mem_props.memoryTypeCount = 1;
	context.mem_props.memoryTypes[0].propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
		| VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
	context.ext.timeline_semaphore_features.timelineSemaphore = VK_TRUE;
	context.ext.subgroup_size_control_properties.minSubgroupSize = 4;
	context.ext.subgroup_size_control_properties.maxSubgroupSize = 64;

	t->vkCreateImageView = create_view;
	t->vkCreateRenderPass = create_pass;
	t->vkCreateFramebuffer = create_framebuffer;
#define ANY(name) \
	t->vkCreate##name = (PFN_vkCreate##name)create_any; \
	t->vkDestroy##name = (PFN_vkDestroy##name)destroy_any
	ANY(Buffer);
	t->vkDestroyImageView = (PFN_vkDestroyImageView)destroy_any;
	ANY(Semaphore);
	ANY(Fence);
	ANY(Sampler);
	ANY(CommandPool);
	ANY(ShaderModule);
	ANY(DescriptorSetLayout);
	ANY(PipelineLayout);
	ANY(DescriptorUpdateTemplate);
	t->vkDestroyRenderPass = (PFN_vkDestroyRenderPass)destroy_any;
	t->vkDestroyFramebuffer = (PFN_vkDestroyFramebuffer)destroy_any;
#undef ANY
	t->vkDestroyPipeline = (PFN_vkDestroyPipeline)destroy_any;
	t->vkCreateImage = create_image;
	t->vkDestroyImage = destroy_image;
	t->vkGetBufferMemoryRequirements2 = buffer_requirements;
	t->vkGetImageMemoryRequirements = image_requirements;
	t->vkGetImageMemoryRequirements2 = image_requirements2;
	t->vkBindBufferMemory = (PFN_vkBindBufferMemory)bind_any;
	t->vkBindImageMemory = (PFN_vkBindImageMemory)bind_any;
	t->vkAllocateMemory = allocate_memory;
	t->vkFreeMemory = free_memory;
	t->vkMapMemory = map_memory;
	t->vkUnmapMemory = unmap_memory;
	t->vkFlushMappedMemoryRanges = ok_ranges;
	t->vkInvalidateMappedMemoryRanges = ok_ranges;
	t->vkWaitSemaphoresKHR = wait_semaphores;
	t->vkResetCommandPool = (PFN_vkResetCommandPool)ok_pool;
	t->vkTrimCommandPool = trim_pool;
	t->vkAllocateCommandBuffers = allocate_cbs;
	t->vkFreeCommandBuffers = free_cbs;
	t->vkBeginCommandBuffer = (PFN_vkBeginCommandBuffer)ok_cb;
	t->vkEndCommandBuffer = end_cb;
	t->vkQueueSubmit = queue_submit;
	t->vkDeviceWaitIdle = wait_idle;
	t->vkCmdPipelineBarrier = cmd_barrier;
	t->vkCmdBeginRenderPass = cmd_begin_pass;
	t->vkCmdEndRenderPass = cmd_end_pass;
	t->vkCreateGraphicsPipelines = create_graphics;
	t->vkCmdBindPipeline = cmd_bind_pipeline;
	t->vkCmdSetViewport = (PFN_vkCmdSetViewport)cmd_ignore_range;
	t->vkCmdSetScissor = (PFN_vkCmdSetScissor)cmd_ignore_range;
	t->vkCmdBindVertexBuffers = cmd_bind_vertex;
	t->vkCmdPushConstants = cmd_push;
	t->vkCmdDraw = cmd_draw;
}

/* ---- at load ---- */
static unsigned known, known_states, known_bad;

static void add_known(void *ctx, granite_command_buffer_t *cb, uint32_t kind)
{
	const granite_pipeline_state_t *state = &cb->pipeline_state.static_state;
	if (ctx != (void *)&known || kind != PIPELINE_RECORD_GRAPHICS || !cb->pipeline_state.program
			|| state->state.depth_compare != VK_COMPARE_OP_LESS_OR_EQUAL || state->state.cull_mode != VK_CULL_MODE_NONE
			|| !cb->actual_render_pass)
		known_bad++;
	known++;
	known_states |= 1u << (state->state.depth_test * 2 + state->state.depth_write);
}

static void test_known_pipelines(void)
{
	granite_command_buffer_t *cb = granite_device_request_command_buffer(&dev, GRANITE_QUEUE_GRAPHICS);
	unsigned before = num_images;

	rdp_upscaler_queue_known_pipelines(&dev, cb, add_known, &known);
	CHECK(known == 4 && known_states == 0xf && known_bad == 0,
			"at load the pass's four depth states are handed over, each ready to build");
	CHECK(num_images == before + 3 && image_infos[before].extent.width == 1 && image_infos[before].extent.height == 1
			&& passes_begun == 1 && passes_ended == 1 && num_pipelines == 0 && num_draws == 0,
			"on 1x1 targets, without building or drawing anything");
	granite_device_submit(&dev, cb, NULL, 0, NULL);
}

static void test_color_program(void)
{
	granite_program_t *program = rdp_upscaler_color_program(&dev);
	CHECK(program && program->shaders[GRANITE_SHADER_STAGE_COMPUTE] && !program->shaders[GRANITE_SHADER_STAGE_FRAGMENT]
			&& program->shaders[GRANITE_SHADER_STAGE_COMPUTE]->layout.push_constant_size == 24
			&& program->shaders[GRANITE_SHADER_STAGE_COMPUTE]->layout.spec_constant_mask == 0xf
			&& program->shaders[GRANITE_SHADER_STAGE_COMPUTE]->layout.sets[0].storage_image_mask == 1
			&& program->shaders[GRANITE_SHADER_STAGE_COMPUTE]->layout.sets[0].storage_buffer_mask == 2
			&& program->shaders[GRANITE_SHADER_STAGE_COMPUTE]->layout.sets[0].sampled_texel_buffer_mask == 4,
			"the color program is a compute program with its layout");
	CHECK(rdp_upscaler_color_program(&dev) == program, "made once");
}

static const VkImageCreateInfo *info_of(const granite_image_t *image)
{
	int n = image_index(image->image);
	return n < 0 ? &image_infos[MAX_IMAGES - 1] : &image_infos[n];
}

static VkImageLayout layout_of(const granite_image_t *image)
{
	int n = image_index(image->image);
	return n < 0 ? VK_IMAGE_LAYOUT_MAX_ENUM : image_layouts[n];
}

static void drop(rdp_upscaler_targets_t *targets)
{
	if (targets->motion_vectors)
		granite_image_unref(targets->motion_vectors);
	if (targets->depth)
		granite_image_unref(targets->depth);
	if (targets->translucent)
		granite_image_unref(targets->translucent);
}

static void test_motion_pass(void)
{
	static rdp_upscaler_vertex_t vertices[9];
	static const rdp_upscaler_draw_t runs[3] = {
		{ 0, 3, RDP_UPSCALER_DEPTH_TEST | RDP_UPSCALER_DEPTH_WRITE },
		{ 3, 3, RDP_UPSCALER_DEPTH_TEST | RDP_UPSCALER_TRANSLUCENT },
		{ 6, 3, RDP_UPSCALER_DEPTH_WRITE | RDP_UPSCALER_RECT }
	};
	const VkImageUsageFlags color_usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	rdp_upscaler_geometry_t geometry;
	rdp_upscaler_targets_t targets;
	granite_command_buffer_t *cb;
	unsigned i, before, before_passes;

	for (i = 0; i < 9; i++)
	{
		vertices[i].x = 100.0f + (float)i;
		vertices[i].y = 7.0f * (float)i;
		vertices[i].z = 0.125f;
		vertices[i].motion_x = -1.5f;
		vertices[i].plane[17] = 0.5f + (float)i;
	}
	memset(&geometry, 0, sizeof(geometry));
	geometry.vertices = vertices;
	geometry.draws = runs;
	geometry.num_vertices = 9;
	geometry.num_draws = 3;
	geometry.farthest = 0.75f;
	geometry.jitter_x = 0.25f;
	geometry.jitter_y = -0.375f;
	geometry.reset = true;

	cb = granite_device_request_command_buffer(&dev, GRANITE_QUEUE_GRAPHICS);
	/* (whatever was drawn with before) */
	cb->pipeline_state.static_state.state.blend_enable = 1;
	cb->pipeline_state.static_state.state.depth_compare = VK_COMPARE_OP_ALWAYS;
	before = num_images;
	before_passes = passes_begun;
	memset(&targets, 0xff, sizeof(targets));
	rdp_upscaler_motion_pass(&dev, cb, &geometry, 640, 480, &targets);

	CHECK(targets.motion_vectors && targets.depth && targets.translucent && num_images == before + 3,
			"three images are handed out");
	if (!targets.motion_vectors || !targets.depth || !targets.translucent)
		return;
	CHECK(granite_refs_only(&targets.motion_vectors->refs) && granite_refs_only(&targets.depth->refs)
			&& granite_refs_only(&targets.translucent->refs), "each the holder's alone");
	CHECK(info_of(targets.motion_vectors)->format == VK_FORMAT_R16G16_SFLOAT
			&& info_of(targets.depth)->format == VK_FORMAT_D32_SFLOAT
			&& info_of(targets.translucent)->format == VK_FORMAT_R8_UNORM, "motion vectors, depth and translucency");
	CHECK(info_of(targets.motion_vectors)->extent.width == 640 && info_of(targets.depth)->extent.height == 480
			&& info_of(targets.translucent)->extent.width == 640 && info_of(targets.translucent)->extent.height == 480,
			"of the size asked for");
	CHECK(info_of(targets.motion_vectors)->usage == color_usage && info_of(targets.translucent)->usage == color_usage
			&& info_of(targets.depth)->usage == (VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT),
			"attachments that can be sampled, and nothing else");
	CHECK(targets.jitter_x == 0.25f && targets.jitter_y == -0.375f && targets.reset, "with the frame's jitter and reset");

	CHECK(passes_begun == before_passes + 1 && passes_ended == passes_begun && pass_clear_count == 3
			&& pass_clears[0].color.float32[0] == 0.0f && pass_clears[1].color.float32[0] == 0.0f
			&& pass_clears[2].depthStencil.depth == 1.0f, "one pass, which clears all three, depth to far");
	CHECK(bad_layouts == 0 && layout_of(targets.motion_vectors) == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			&& layout_of(targets.depth) == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			&& layout_of(targets.translucent) == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			"each goes from no layout to its attachment layout and is left to be sampled");

	CHECK(num_draws == 3 && draws[0].count == 3 && draws[0].first == 0 && draws[1].first == 3 && draws[2].first == 6
			&& draws[2].count == 3, "each run is drawn");
	CHECK(draws[0].translucency == 0.0f && draws[1].translucency == 1.0f && draws[2].translucency == 0.0f,
			"with its translucency");
	CHECK(num_pipelines == 3 && draws[0].pipeline == 1 && draws[1].pipeline == 2 && draws[2].pipeline == 3,
			"and its depth state");
	CHECK(pipelines[0].depth_test && pipelines[0].depth_write && pipelines[1].depth_test && !pipelines[1].depth_write
			&& !pipelines[2].depth_test && pipelines[2].depth_write, "test and write as the run says");
	CHECK(!pipelines[0].blend, "opaque, whatever was drawn before");
	CHECK(pass_load_ops[0] == VK_ATTACHMENT_LOAD_OP_CLEAR && pass_load_ops[1] == VK_ATTACHMENT_LOAD_OP_CLEAR
			&& pass_load_ops[2] == VK_ATTACHMENT_LOAD_OP_CLEAR, "(the pass is one that clears)");
	CHECK(framebuffer_formats[0] == VK_FORMAT_R16G16_SFLOAT && framebuffer_formats[1] == VK_FORMAT_R8_UNORM
			&& framebuffer_formats[2] == VK_FORMAT_D32_SFLOAT, "motion vectors are output 0, translucency output 1");
	CHECK(pipelines[0].compare == VK_COMPARE_OP_LESS_OR_EQUAL && pipelines[0].cull == VK_CULL_MODE_NONE
			&& pipelines[0].color_attachments == 2, "nearer or equal passes, nothing is culled, two colour outputs");
	CHECK(pipelines[0].num_attributes == 8 && pipelines[0].stride == sizeof(rdp_upscaler_vertex_t)
			&& pipelines[0].attributes[0].format == VK_FORMAT_R32G32B32_SFLOAT && pipelines[0].attributes[0].offset == 0
			&& pipelines[0].attributes[1].format == VK_FORMAT_R32G32_SFLOAT
			&& pipelines[0].attributes[1].offset == offsetof(rdp_upscaler_vertex_t, motion_x)
			&& pipelines[0].attributes[2].offset == offsetof(rdp_upscaler_vertex_t, plane)
			&& pipelines[0].attributes[7].offset == offsetof(rdp_upscaler_vertex_t, plane) + 15 * sizeof(float)
			&& pipelines[0].attributes[7].format == VK_FORMAT_R32G32B32_SFLOAT,
			"the vertex is position, motion and six planes");
	CHECK(times_in_memory(vertices, sizeof(vertices)) == 1, "the vertices are copied, once");
	CHECK(constant(0) == 1.0f / 640.0f && constant(4) == 1.0f / 480.0f, "the constants carry the resolution");
	CHECK(constant(16) == 0.25f + 320.0f && constant(20) == -0.375f + 240.0f && constant(24) == 0.75f,
			"the jittered centre and the far plane");
	drop(&targets);

	/* ---- nothing to draw: the targets are still cleared ---- */
	geometry.num_vertices = 0;
	geometry.num_draws = 0;
	geometry.reset = false;
	num_draws = 0;
	before_passes = passes_begun;
	rdp_upscaler_motion_pass(&dev, cb, &geometry, 320, 240, &targets);
	CHECK(targets.motion_vectors && passes_begun == before_passes + 1 && passes_ended == passes_begun
			&& pass_clear_count == 3 && num_draws == 0 && !targets.reset
			&& layout_of(targets.depth) == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			"without geometry the pass only clears");
	drop(&targets);

	/* ---- no D32: D16 ---- */
	have_d32 = false;
	rdp_upscaler_motion_pass(&dev, cb, &geometry, 320, 240, &targets);
	CHECK(targets.depth && info_of(targets.depth)->format == VK_FORMAT_D16_UNORM, "without D32_SFLOAT depth is D16_UNORM");
	drop(&targets);
	have_d32 = true;

	/* ---- an image that cannot be made ---- */
	have_r8 = false;
	before_passes = passes_begun;
	memset(&targets, 0xff, sizeof(targets));
	rdp_upscaler_motion_pass(&dev, cb, &geometry, 320, 240, &targets);
	CHECK(!targets.motion_vectors && !targets.depth && !targets.translucent && passes_begun == before_passes
			&& bad_layouts == 0, "when one image cannot be made none is handed out and nothing is drawn");
	have_r8 = true;

	granite_device_submit(&dev, cb, NULL, 0, NULL);
}

int main(void)
{
	unsigned i;

	fill_context();
	granite_device_init(&dev);
	granite_device_set_context(&dev, &context);

	test_color_program();
	test_known_pipelines();
	test_motion_pass();

	granite_device_free(&dev);
	CHECK(images_destroyed == num_images && num_images > 0, "every image went back");
	CHECK(made == unmade + 0 || unmade >= made - num_pipelines, "and everything else the device made");
	for (i = 0; i < num_memory; i++)
		free(memory_data[i]);

	if (failures)
	{
		fprintf(stderr, "test_rdp_upscaler_pass: %u failure(s)\n", failures);
		return 1;
	}
	printf("test_rdp_upscaler_pass: ok\n");
	return 0;
}
