/* A stand-in Vulkan device for tests that run Granite's real device
 * (vulkan/device.c) over it: it makes handles, backs memory with the
 * host's, builds pipelines, and records what command buffers do (the
 * barriers and image layouts, passes, draws, dispatches, blits and buffer
 * copies). Include it after defining CHECK. */
#ifndef VK_TEST_DEVICE_H
#define VK_TEST_DEVICE_H

#include <stdlib.h>
#include <string.h>
#include "device.h"

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

#define MAX_IMAGES 96
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
#define MAX_VIEWS 256
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

/* the buffers made, and their sizes */
#define MAX_BUFFERS 64
static unsigned num_buffers;
static VkBuffer buffer_handles[MAX_BUFFERS];
static VkDeviceSize buffer_sizes[MAX_BUFFERS];

static VkResult VKAPI_CALL create_buffer(VkDevice d, const VkBufferCreateInfo *info, const VkAllocationCallbacks *a,
		VkBuffer *out)
{
	(void)d; (void)a;
	made++;
	*out = GRANITE_VK_FROM_BITS(VkBuffer, ++next_handle);
	if (num_buffers < MAX_BUFFERS)
	{
		buffer_handles[num_buffers] = *out;
		buffer_sizes[num_buffers] = info->size;
		num_buffers++;
	}
	return VK_SUCCESS;
}

static VkDeviceSize buffer_size(VkBuffer buffer)
{
	unsigned i;
	for (i = 0; i < num_buffers; i++)
		if (buffer_handles[i] == buffer)
			return buffer_sizes[i];
	return 1 << 20;
}

static void VKAPI_CALL buffer_requirements(VkDevice d, const VkBufferMemoryRequirementsInfo2 *info,
		VkMemoryRequirements2 *reqs)
{
	(void)d;
	reqs->memoryRequirements.size = (buffer_size(info->buffer) + 255) & ~(VkDeviceSize)255;
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

#define MAX_MEMORY 64
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

static void VKAPI_CALL memory_properties2(VkPhysicalDevice gpu, VkPhysicalDeviceMemoryProperties2 *props)
{
	(void)gpu; (void)props;
}
PFN_vkGetPhysicalDeviceMemoryProperties2 vkGetPhysicalDeviceMemoryProperties2 = memory_properties2;

static void VKAPI_CALL format_properties2(VkPhysicalDevice gpu, VkFormat format, VkFormatProperties2 *props)
{
	(void)gpu; (void)format;
	memset(&props->formatProperties, 0, sizeof(props->formatProperties));
	props->formatProperties.optimalTilingFeatures = ~(VkFormatFeatureFlags)0;
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

static unsigned submits_made, submit_waits;
static VkResult VKAPI_CALL queue_submit(VkQueue queue, uint32_t count, const VkSubmitInfo *submits, VkFence fence)
{
	uint32_t i;
	(void)queue; (void)fence;
	for (i = 0; i < count; i++)
	{
		submits_made++;
		submit_waits += submits[i].waitSemaphoreCount;
	}
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
/* which images a framebuffer draws to */
#define MAX_FRAMEBUFFERS 64
static unsigned num_framebuffers;
static struct
{
	VkFramebuffer handle;
	unsigned count;
	int images[2];
	uint32_t layers[2];
} framebuffers[MAX_FRAMEBUFFERS];
static uint32_t view_layers[MAX_VIEWS];

static VkResult VKAPI_CALL create_view_layer(VkDevice d, const VkImageViewCreateInfo *info,
		const VkAllocationCallbacks *a, VkImageView *out)
{
	if (num_views < MAX_VIEWS)
		view_layers[num_views] = info->subresourceRange.baseArrayLayer;
	return create_view(d, info, a, out);
}

static VkResult VKAPI_CALL create_pass(VkDevice d, const VkRenderPassCreateInfo *info, const VkAllocationCallbacks *a,
		VkRenderPass *out)
{
	(void)d; (void)a; (void)info;
	made++;
	*out = GRANITE_VK_FROM_BITS(VkRenderPass, ++next_handle);
	return VK_SUCCESS;
}

static VkResult VKAPI_CALL create_framebuffer(VkDevice d, const VkFramebufferCreateInfo *info,
		const VkAllocationCallbacks *a, VkFramebuffer *out)
{
	uint32_t i, j;
	(void)d; (void)a;
	made++;
	*out = GRANITE_VK_FROM_BITS(VkFramebuffer, ++next_handle);
	if (num_framebuffers < MAX_FRAMEBUFFERS)
	{
		framebuffers[num_framebuffers].handle = *out;
		framebuffers[num_framebuffers].count = info->attachmentCount;
		for (i = 0; i < info->attachmentCount && i < 2; i++)
		{
			framebuffers[num_framebuffers].images[i] = -1;
			for (j = 0; j < num_views; j++)
				if (view_handles[j] == info->pAttachments[i])
				{
					framebuffers[num_framebuffers].images[i] = image_index(view_images[j]);
					framebuffers[num_framebuffers].layers[i] = view_layers[j];
				}
		}
		num_framebuffers++;
	}
	return VK_SUCCESS;
}

/* everything a frame records, in order */
enum
{
	EV_BARRIER, EV_PASS, EV_END_PASS, EV_DRAW, EV_DISPATCH, EV_BLIT, EV_SUBMIT
};
#define MAX_EVENTS 256
static unsigned num_events, bad_layouts, in_pass;
static struct
{
	int type;
	/* EV_BARRIER: image, new layout. EV_PASS: attachments, first image,
	 * clears. EV_DRAW: pipeline, scissor. EV_DISPATCH: groups.
	 * EV_BLIT: destination and source images. */
	int a, b, c;
	/* EV_PASS: its two attachments are the two layers of one image */
	bool two_layers;
	VkRect2D scissor;
	uint8_t constants[120];
	uint32_t constants_size;
} events[MAX_EVENTS];

static uint8_t constants[120];
static uint32_t constants_size;
static VkRect2D scissor;
static VkPipeline bound_pipeline;

static void event(int type, int a, int b, int c)
{
	if (num_events < MAX_EVENTS)
	{
		events[num_events].type = type;
		events[num_events].a = a;
		events[num_events].b = b;
		events[num_events].c = c;
		events[num_events].scissor = scissor;
		memcpy(events[num_events].constants, constants, sizeof(constants));
		events[num_events].constants_size = constants_size;
	}
	num_events++;
}

static void VKAPI_CALL cmd_barrier(VkCommandBuffer cmd, VkPipelineStageFlags src, VkPipelineStageFlags dst,
		VkDependencyFlags flags, uint32_t memory_count, const VkMemoryBarrier *memory, uint32_t buffer_count,
		const VkBufferMemoryBarrier *buffers, uint32_t image_count, const VkImageMemoryBarrier *images)
{
	uint32_t i;
	(void)cmd; (void)src; (void)dst; (void)flags; (void)memory_count; (void)memory; (void)buffer_count; (void)buffers;
	CHECK(!in_pass, "no barrier inside a pass");
	for (i = 0; i < image_count; i++)
	{
		int n = image_index(images[i].image);
		/* (a pooled image starts each frame over) */
		if (n < 0 || (images[i].oldLayout != VK_IMAGE_LAYOUT_UNDEFINED && image_layouts[n] != images[i].oldLayout))
			bad_layouts++;
		else
			image_layouts[n] = images[i].newLayout;
		event(EV_BARRIER, n, (int)images[i].newLayout, 0);
	}
}

static void VKAPI_CALL cmd_begin_pass(VkCommandBuffer cmd, const VkRenderPassBeginInfo *info, VkSubpassContents c)
{
	unsigned i;
	(void)cmd; (void)c;
	in_pass = 1;
	for (i = 0; i < num_framebuffers; i++)
		if (framebuffers[i].handle == info->framebuffer)
		{
			event(EV_PASS, (int)framebuffers[i].count, framebuffers[i].images[0], (int)info->clearValueCount);
			if (num_events <= MAX_EVENTS)
				events[num_events - 1].two_layers = framebuffers[i].count == 2
					&& framebuffers[i].images[0] == framebuffers[i].images[1]
					&& framebuffers[i].layers[0] == 0 && framebuffers[i].layers[1] == 1;
			return;
		}
	event(EV_PASS, 0, -1, 0);
}

static void VKAPI_CALL cmd_end_pass(VkCommandBuffer cmd)
{
	(void)cmd;
	in_pass = 0;
	event(EV_END_PASS, 0, 0, 0);
}

#define MAX_PIPELINES 64
static unsigned num_pipelines;
static struct
{
	bool compute;
	VkBool32 blend;
	uint32_t color_attachments, write_mask;
	VkShaderModule fragment;
	uint32_t spec[4];
	uint32_t spec_count;
} pipelines[MAX_PIPELINES];

static void keep_spec(const VkSpecializationInfo *info)
{
	uint32_t i;
	if (!info || num_pipelines >= MAX_PIPELINES)
		return;
	for (i = 0; i < info->mapEntryCount; i++)
		if (info->pMapEntries[i].constantID < 4)
		{
			memcpy(&pipelines[num_pipelines].spec[info->pMapEntries[i].constantID],
					(const uint8_t *)info->pData + info->pMapEntries[i].offset, sizeof(uint32_t));
			pipelines[num_pipelines].spec_count++;
		}
}

static VkResult VKAPI_CALL create_graphics(VkDevice d, VkPipelineCache cache, uint32_t count,
		const VkGraphicsPipelineCreateInfo *infos, const VkAllocationCallbacks *a, VkPipeline *out)
{
	uint32_t i;
	(void)d; (void)cache; (void)count; (void)a;
	if (num_pipelines < MAX_PIPELINES)
	{
		pipelines[num_pipelines].compute = false;
		pipelines[num_pipelines].blend = infos->pColorBlendState->pAttachments[0].blendEnable;
		pipelines[num_pipelines].write_mask = infos->pColorBlendState->pAttachments[0].colorWriteMask;
		pipelines[num_pipelines].color_attachments = infos->pColorBlendState->attachmentCount;
		for (i = 0; i < infos->stageCount; i++)
			if (infos->pStages[i].stage == VK_SHADER_STAGE_FRAGMENT_BIT)
			{
				pipelines[num_pipelines].fragment = infos->pStages[i].module;
				keep_spec(infos->pStages[i].pSpecializationInfo);
			}
	}
	*out = GRANITE_VK_FROM_BITS(VkPipeline, 0x9000 + ++num_pipelines);
	return VK_SUCCESS;
}

static VkResult VKAPI_CALL create_compute(VkDevice d, VkPipelineCache cache, uint32_t count,
		const VkComputePipelineCreateInfo *infos, const VkAllocationCallbacks *a, VkPipeline *out)
{
	(void)d; (void)cache; (void)count; (void)a;
	if (num_pipelines < MAX_PIPELINES)
	{
		pipelines[num_pipelines].compute = true;
		/* (a compute pipeline's shader, in the same place) */
		pipelines[num_pipelines].fragment = infos->stage.module;
		keep_spec(infos->stage.pSpecializationInfo);
	}
	*out = GRANITE_VK_FROM_BITS(VkPipeline, 0x9000 + ++num_pipelines);
	return VK_SUCCESS;
}

static void VKAPI_CALL cmd_bind_pipeline(VkCommandBuffer cmd, VkPipelineBindPoint point, VkPipeline pipeline)
{
	(void)cmd; (void)point;
	bound_pipeline = pipeline;
}

static void VKAPI_CALL cmd_ignore_range(VkCommandBuffer cmd, uint32_t first, uint32_t count, const void *data)
{
	(void)cmd; (void)first; (void)count; (void)data;
}

static void VKAPI_CALL cmd_scissor(VkCommandBuffer cmd, uint32_t first, uint32_t count, const VkRect2D *rects)
{
	(void)cmd; (void)first; (void)count;
	scissor = rects[0];
}

static void VKAPI_CALL cmd_push(VkCommandBuffer cmd, VkPipelineLayout layout, VkShaderStageFlags stages,
		uint32_t offset, uint32_t size, const void *data)
{
	(void)cmd; (void)layout; (void)stages;
	if (offset + size <= sizeof(constants))
	{
		memcpy(constants + offset, data, size);
		constants_size = offset + size;
	}
}

static void VKAPI_CALL cmd_draw(VkCommandBuffer cmd, uint32_t count, uint32_t instances, uint32_t first,
		uint32_t first_instance)
{
	(void)cmd;
	CHECK(in_pass && count == 3 && instances == 1 && first == 0 && first_instance == 0,
			"one triangle over the target, inside a pass");
	event(EV_DRAW, (int)(GRANITE_VK_TO_BITS(bound_pipeline) - 0x9000), 0, 0);
}

static void VKAPI_CALL cmd_dispatch(VkCommandBuffer cmd, uint32_t x, uint32_t y, uint32_t z)
{
	(void)cmd;
	event(EV_DISPATCH, (int)x, (int)y, (int)z);
	events[num_events - 1].b = (int)y;
	if (num_events <= MAX_EVENTS)
		events[num_events - 1].c = (int)(GRANITE_VK_TO_BITS(bound_pipeline) - 0x9000) * 16 + (int)z;
}

static void VKAPI_CALL cmd_blit(VkCommandBuffer cmd, VkImage src, VkImageLayout src_layout, VkImage dst,
		VkImageLayout dst_layout, uint32_t count, const VkImageBlit *regions, VkFilter filter)
{
	(void)cmd; (void)count;
	CHECK(image_index(src) >= 0 && image_index(dst) >= 0 && image_layouts[image_index(src)] == src_layout
			&& image_layouts[image_index(dst)] == dst_layout, "a blit's images are in the layouts it names");
	CHECK(src_layout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL && dst_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
			&& filter == VK_FILTER_LINEAR && regions[0].srcOffsets[1].x == 2 * regions[0].dstOffsets[1].x
			&& regions[0].srcOffsets[1].y == 2 * regions[0].dstOffsets[1].y,
			"a downscale is a linear blit to half the size");
	event(EV_BLIT, image_index(dst), image_index(src), 0);
}

static VkResult VKAPI_CALL allocate_sets(VkDevice d, const VkDescriptorSetAllocateInfo *info, VkDescriptorSet *sets)
{
	uint32_t i;
	(void)d;
	for (i = 0; i < info->descriptorSetCount; i++)
		sets[i] = GRANITE_VK_FROM_BITS(VkDescriptorSet, ++next_handle);
	return VK_SUCCESS;
}

/* what the sets written hold in their first three bindings (a set is
 * written once and found again after that) */
#define MAX_SETS 128
static unsigned num_sets;
static granite_resource_binding_t sets_written[MAX_SETS][3];

static void VKAPI_CALL update_set(VkDevice d, VkDescriptorSet set, VkDescriptorUpdateTemplate tmpl, const void *data)
{
	(void)d; (void)set; (void)tmpl;
	if (num_sets < MAX_SETS)
		memcpy(sets_written[num_sets], data, sizeof(sets_written[0]));
	num_sets++;
}

/* a set written since @from with texel buffer @view at @binding */
static bool wrote_buffer_view(unsigned from, unsigned binding, const granite_buffer_view_t *view)
{
	unsigned i;
	for (i = from; i < num_sets && i < MAX_SETS; i++)
		if (sets_written[i][binding].u.buffer_view == view->view)
			return true;
	return false;
}

/* a set written since @from with @image at @binding, sampled by @sampler */
static bool wrote_texture(unsigned from, unsigned binding, const granite_image_t *image, VkSampler sampler)
{
	unsigned i;
	for (i = from; i < num_sets && i < MAX_SETS; i++)
		if (sets_written[i][binding].u.image.fp.imageView == image->view->views.view
				&& sets_written[i][binding].u.image.fp.sampler == sampler)
			return true;
	return false;
}

static void VKAPI_CALL cmd_bind_sets(VkCommandBuffer cmd, VkPipelineBindPoint point, VkPipelineLayout layout,
		uint32_t first, uint32_t count, const VkDescriptorSet *sets, uint32_t dynamic_count, const uint32_t *dynamic)
{
	(void)cmd; (void)point; (void)layout; (void)first; (void)count; (void)sets; (void)dynamic_count; (void)dynamic;
}

static void VKAPI_CALL update_sets(VkDevice d, uint32_t writes, const VkWriteDescriptorSet *write, uint32_t copies,
		const VkCopyDescriptorSet *copy)
{
	(void)d; (void)writes; (void)write; (void)copies; (void)copy;
}

static void VKAPI_CALL cmd_copy_to_image(VkCommandBuffer cmd, VkBuffer src, VkImage dst, VkImageLayout layout,
		uint32_t count, const VkBufferImageCopy *regions)
{
	(void)cmd; (void)src; (void)dst; (void)layout; (void)count; (void)regions;
}

/* the buffer copies recorded */
#define MAX_BUFFER_COPIES 512
static unsigned num_buffer_copies;
static struct
{
	VkBuffer src, dst;
	VkBufferCopy region;
} buffer_copies[MAX_BUFFER_COPIES];

static void VKAPI_CALL cmd_copy_buffer(VkCommandBuffer cmd, VkBuffer src, VkBuffer dst, uint32_t count,
		const VkBufferCopy *regions)
{
	uint32_t i;
	(void)cmd;
	for (i = 0; i < count; i++, num_buffer_copies++)
		if (num_buffer_copies < MAX_BUFFER_COPIES)
		{
			buffer_copies[num_buffer_copies].src = src;
			buffer_copies[num_buffer_copies].dst = dst;
			buffer_copies[num_buffer_copies].region = regions[i];
		}
}

static void VKAPI_CALL cmd_fill_buffer(VkCommandBuffer cmd, VkBuffer dst, VkDeviceSize offset, VkDeviceSize size,
		uint32_t data)
{
	(void)cmd; (void)dst; (void)offset; (void)size; (void)data;
}

/* how many events of @type the frame recorded */
static unsigned count_events(int type)
{
	unsigned i, n = 0;
	for (i = 0; i < num_events && i < MAX_EVENTS; i++)
		if (events[i].type == type)
			n++;
	return n;
}

/* the @nth event of @type */
static int find_event(int type, unsigned nth)
{
	unsigned i;
	for (i = 0; i < num_events && i < MAX_EVENTS; i++)
		if (events[i].type == type && nth-- == 0)
			return (int)i;
	return MAX_EVENTS - 1;
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
		context.queue_info.queues[i] = GRANITE_VK_FROM_BITS(VkQueue, 0x100 + i);
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

	t->vkCreateImageView = create_view_layer;
	t->vkCreateRenderPass = create_pass;
	t->vkCreateFramebuffer = create_framebuffer;
#define ANY(name) \
	t->vkCreate##name = (PFN_vkCreate##name)create_any; \
	t->vkDestroy##name = (PFN_vkDestroy##name)destroy_any
	t->vkCreateBuffer = create_buffer;
	t->vkDestroyBuffer = (PFN_vkDestroyBuffer)destroy_any;
	ANY(BufferView);
	t->vkDestroyImageView = (PFN_vkDestroyImageView)destroy_any;
	ANY(Semaphore);
	ANY(Fence);
	ANY(Sampler);
	ANY(CommandPool);
	ANY(ShaderModule);
	ANY(DescriptorSetLayout);
	ANY(DescriptorPool);
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
	t->vkResetDescriptorPool = (PFN_vkResetDescriptorPool)ok_pool;
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
	t->vkCreateComputePipelines = create_compute;
	t->vkCmdBindPipeline = cmd_bind_pipeline;
	t->vkCmdSetViewport = (PFN_vkCmdSetViewport)cmd_ignore_range;
	t->vkCmdSetScissor = cmd_scissor;
	t->vkCmdPushConstants = cmd_push;
	t->vkCmdDraw = cmd_draw;
	t->vkCmdDispatch = cmd_dispatch;
	t->vkCmdBlitImage = cmd_blit;
	t->vkAllocateDescriptorSets = allocate_sets;
	t->vkCmdCopyBufferToImage = cmd_copy_to_image;
	t->vkCmdCopyBuffer = cmd_copy_buffer;
	t->vkCmdFillBuffer = cmd_fill_buffer;
	t->vkUpdateDescriptorSetWithTemplate = update_set;
	t->vkUpdateDescriptorSetWithTemplateKHR = update_set;
	t->vkUpdateDescriptorSets = update_sets;
	t->vkCmdBindDescriptorSets = cmd_bind_sets;
}

#endif
