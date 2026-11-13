/* parallel-rdp's device (vulkan/device.c), whole, through a stand-in
 * device table.
 *
 * A device is set up from a context with two frame contexts and goes
 * round them. What is let go of on the owner thread is destroyed only
 * when the frame it was let go in comes round again; what another
 * thread lets go of goes through the release queues to the same end.
 * A command buffer is begun for one submission and counted until it is
 * submitted. A buffer the host can write is filled in place; one it
 * cannot is filled by the GPU, from a staging buffer or by a fill, in a
 * submission the other queues wait for. An image with data is uploaded
 * from a staging buffer and left in the layout asked for. The pipelines
 * built are recorded, and a record resolves to the same pipeline on a
 * new device that has the same shaders. Freeing the device gives
 * everything back. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <boolean.h>

#include "device.h"

static unsigned failures;

#define CHECK(cond, what) \
	do \
	{ \
		if (!(cond)) \
		{ \
			fprintf(stderr, "test_vk_device: FAIL (line %d): %s\n", __LINE__, what); \
			failures++; \
		} \
	} while (0)

#define DEV ((VkDevice)(size_t)1)
#define GPU ((VkPhysicalDevice)(size_t)2)
#define Q(n) GRANITE_VK_FROM_BITS(VkQueue, 0x100 + (n))
#define G GRANITE_QUEUE_GRAPHICS
#define C GRANITE_QUEUE_COMPUTE
#define T GRANITE_QUEUE_TRANSFER
#define BAD_FORMAT VK_FORMAT_R64_SFLOAT

/* ---- the stand-in device: everything made is counted in and out ---- */
enum
{
	K_BUFFER = 1, K_BUFFER_VIEW, K_IMAGE, K_IMAGE_VIEW, K_SEMAPHORE, K_FENCE, K_SAMPLER, K_POOL, K_MEMORY, K_OTHER,
	K_COUNT
};
#define MAX_OBJECTS 256
static unsigned created[K_COUNT], destroyed[K_COUNT];
static uint8_t *memory_data[MAX_OBJECTS];
static uint32_t memory_types[MAX_OBJECTS];
static unsigned char live[K_COUNT][MAX_OBJECTS];
static unsigned bad_destroys;

static uint64_t make(unsigned kind)
{
	unsigned n = ++created[kind];
	if (n < MAX_OBJECTS)
		live[kind][n] = 1;
	return ((uint64_t)kind << 16) | n;
}

static void unmake(unsigned kind, uint64_t handle)
{
	unsigned n = (unsigned)(handle & 0xffff);
	if ((handle >> 16) != kind || n >= MAX_OBJECTS || !live[kind][n])
	{
		bad_destroys++;
		return;
	}
	live[kind][n] = 0;
	destroyed[kind]++;
}

#define OBJECT(name, kind, vktype, infotype) \
	static VkResult VKAPI_CALL create_##name(VkDevice d, const infotype *info, const VkAllocationCallbacks *a, vktype *out) \
	{ \
		(void)d; (void)info; (void)a; \
		*out = GRANITE_VK_FROM_BITS(vktype, make(kind)); \
		return VK_SUCCESS; \
	} \
	static void VKAPI_CALL destroy_##name(VkDevice d, vktype handle, const VkAllocationCallbacks *a) \
	{ \
		(void)d; (void)a; \
		unmake(kind, GRANITE_VK_TO_BITS(handle)); \
	}

OBJECT(image_view, K_IMAGE_VIEW, VkImageView, VkImageViewCreateInfo)
OBJECT(semaphore, K_SEMAPHORE, VkSemaphore, VkSemaphoreCreateInfo)
OBJECT(fence, K_FENCE, VkFence, VkFenceCreateInfo)
OBJECT(sampler, K_SAMPLER, VkSampler, VkSamplerCreateInfo)
OBJECT(image, K_IMAGE, VkImage, VkImageCreateInfo)

static VkResult VKAPI_CALL create_other(VkDevice d, const void *info, const VkAllocationCallbacks *a, uint64_t *out)
{
	(void)d; (void)info; (void)a;
	*out = make(K_OTHER);
	return VK_SUCCESS;
}

static void VKAPI_CALL destroy_other(VkDevice d, uint64_t handle, const VkAllocationCallbacks *a)
{
	(void)d; (void)a;
	unmake(K_OTHER, handle);
}

static uint32_t pool_families[MAX_OBJECTS];
static VkResult VKAPI_CALL create_pool(VkDevice d, const VkCommandPoolCreateInfo *info, const VkAllocationCallbacks *a,
		VkCommandPool *out)
{
	uint64_t handle = make(K_POOL);
	(void)d; (void)a;
	pool_families[handle & 0xff] = info->queueFamilyIndex;
	*out = GRANITE_VK_FROM_BITS(VkCommandPool, handle);
	return VK_SUCCESS;
}

static void VKAPI_CALL destroy_pool(VkDevice d, VkCommandPool pool, const VkAllocationCallbacks *a)
{
	(void)d; (void)a;
	unmake(K_POOL, GRANITE_VK_TO_BITS(pool));
}

static unsigned view_creates;
static VkBufferViewCreateInfo view_create;
static VkResult VKAPI_CALL create_buffer_view(VkDevice d, const VkBufferViewCreateInfo *info,
		const VkAllocationCallbacks *a, VkBufferView *out)
{
	(void)d; (void)a;
	view_creates++;
	view_create = *info;
	*out = GRANITE_VK_FROM_BITS(VkBufferView, make(K_BUFFER_VIEW));
	return VK_SUCCESS;
}

static void VKAPI_CALL destroy_buffer_view(VkDevice d, VkBufferView view, const VkAllocationCallbacks *a)
{
	(void)d; (void)a;
	unmake(K_BUFFER_VIEW, GRANITE_VK_TO_BITS(view));
}

static VkDeviceSize buffer_sizes[MAX_OBJECTS];
static unsigned buffer_memory[MAX_OBJECTS];
static VkDeviceSize buffer_offsets[MAX_OBJECTS];
static VkBufferUsageFlags buffer_usages[MAX_OBJECTS];
static VkResult VKAPI_CALL create_buffer(VkDevice d, const VkBufferCreateInfo *info, const VkAllocationCallbacks *a,
		VkBuffer *out)
{
	uint64_t handle = make(K_BUFFER);
	(void)d; (void)a;
	buffer_sizes[handle & 0xff] = info->size;
	buffer_usages[handle & 0xff] = info->usage;
	*out = GRANITE_VK_FROM_BITS(VkBuffer, handle);
	return VK_SUCCESS;
}

static void VKAPI_CALL destroy_buffer(VkDevice d, VkBuffer buffer, const VkAllocationCallbacks *a)
{
	(void)d; (void)a;
	unmake(K_BUFFER, GRANITE_VK_TO_BITS(buffer));
}

static void VKAPI_CALL buffer_requirements(VkDevice d, const VkBufferMemoryRequirementsInfo2 *info,
		VkMemoryRequirements2 *reqs)
{
	(void)d;
	reqs->memoryRequirements.size = (buffer_sizes[GRANITE_VK_TO_BITS(info->buffer) & 0xff] + 15) & ~(VkDeviceSize)15;
	reqs->memoryRequirements.alignment = 16;
	reqs->memoryRequirements.memoryTypeBits = 3;
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

static VkResult VKAPI_CALL bind_buffer(VkDevice d, VkBuffer buffer, VkDeviceMemory memory, VkDeviceSize offset)
{
	(void)d;
	buffer_memory[GRANITE_VK_TO_BITS(buffer) & 0xff] = (unsigned)(GRANITE_VK_TO_BITS(memory) & 0xff);
	buffer_offsets[GRANITE_VK_TO_BITS(buffer) & 0xff] = offset;
	return VK_SUCCESS;
}

static unsigned host_queries;
static VkResult VKAPI_CALL host_pointer_properties(VkDevice d, VkExternalMemoryHandleTypeFlagBits type, const void *ptr,
		VkMemoryHostPointerPropertiesEXT *props)
{
	(void)d; (void)type; (void)ptr;
	host_queries++;
	props->memoryTypeBits = 2;
	return VK_SUCCESS;
}

static VkResult VKAPI_CALL bind_image(VkDevice d, VkImage image, VkDeviceMemory memory, VkDeviceSize offset)
{
	(void)d; (void)image; (void)memory; (void)offset;
	return VK_SUCCESS;
}

static void *imported_pointer;
static VkResult VKAPI_CALL allocate_memory(VkDevice d, const VkMemoryAllocateInfo *info,
		const VkAllocationCallbacks *a, VkDeviceMemory *memory)
{
	uint64_t handle = make(K_MEMORY);
	const VkBaseInStructure *next;
	(void)d; (void)a;
	for (next = (const VkBaseInStructure *)info->pNext; next; next = next->pNext)
		if (next->sType == VK_STRUCTURE_TYPE_IMPORT_MEMORY_HOST_POINTER_INFO_EXT)
			imported_pointer = ((const VkImportMemoryHostPointerInfoEXT *)next)->pHostPointer;
	memory_data[handle & 0xff] = (uint8_t *)malloc((size_t)info->allocationSize);
	memset(memory_data[handle & 0xff], 0xcd, (size_t)info->allocationSize);
	memory_types[handle & 0xff] = info->memoryTypeIndex;
	*memory = GRANITE_VK_FROM_BITS(VkDeviceMemory, handle);
	return VK_SUCCESS;
}

static void VKAPI_CALL free_memory(VkDevice d, VkDeviceMemory memory, const VkAllocationCallbacks *a)
{
	uint64_t handle = GRANITE_VK_TO_BITS(memory);
	(void)d; (void)a;
	unmake(K_MEMORY, handle);
	free(memory_data[handle & 0xff]);
	memory_data[handle & 0xff] = NULL;
}

static unsigned bad_maps;
static VkResult VKAPI_CALL map_memory(VkDevice d, VkDeviceMemory memory, VkDeviceSize offset, VkDeviceSize size,
		VkMemoryMapFlags flags, void **data)
{
	uint64_t handle = GRANITE_VK_TO_BITS(memory);
	(void)d; (void)size; (void)flags;
	/* (only memory the host can see) */
	if (memory_types[handle & 0xff] != 1)
		bad_maps++;
	*data = memory_data[handle & 0xff] + offset;
	return VK_SUCCESS;
}

static void VKAPI_CALL unmap_memory(VkDevice d, VkDeviceMemory memory)
{
	(void)d; (void)memory;
}

static VkResult VKAPI_CALL flush_ranges(VkDevice d, uint32_t count, const VkMappedMemoryRange *ranges)
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
	(void)gpu;
	props->formatProperties.linearTilingFeatures = 0;
	props->formatProperties.optimalTilingFeatures = format == BAD_FORMAT ? 0 : ~(VkFormatFeatureFlags)0;
	props->formatProperties.bufferFeatures = 0;
}
PFN_vkGetPhysicalDeviceFormatProperties2 vkGetPhysicalDeviceFormatProperties2 = format_properties2;

static VkResult VKAPI_CALL tool_properties(VkPhysicalDevice gpu, uint32_t *count, VkPhysicalDeviceToolProperties *props)
{
	(void)gpu; (void)props;
	*count = 0;
	return VK_SUCCESS;
}
PFN_vkGetPhysicalDeviceToolPropertiesEXT vkGetPhysicalDeviceToolPropertiesEXT = tool_properties;

/* ---- command buffers and queues ---- */
static char cb_storage[128];
static uint32_t cb_families[128];
static unsigned cb_allocated, cb_freed;
static VkResult VKAPI_CALL allocate_cbs(VkDevice d, const VkCommandBufferAllocateInfo *info, VkCommandBuffer *out)
{
	(void)d;
	if (cb_allocated == sizeof(cb_storage))
		return VK_ERROR_OUT_OF_DEVICE_MEMORY;
	cb_families[cb_allocated] = pool_families[GRANITE_VK_TO_BITS(info->commandPool) & 0xff];
	*out = (VkCommandBuffer)&cb_storage[cb_allocated++];
	return VK_SUCCESS;
}

static void VKAPI_CALL free_cbs(VkDevice d, VkCommandPool pool, uint32_t count, const VkCommandBuffer *buffers)
{
	(void)d; (void)pool; (void)buffers;
	cb_freed += count;
}

static VkResult VKAPI_CALL reset_pool(VkDevice d, VkCommandPool pool, VkCommandPoolResetFlags flags)
{
	(void)d; (void)pool; (void)flags;
	return VK_SUCCESS;
}

static void VKAPI_CALL trim_pool(VkDevice d, VkCommandPool pool, VkCommandPoolTrimFlags flags)
{
	(void)d; (void)pool; (void)flags;
}

static unsigned begun, ended;
static VkCommandBufferUsageFlags begin_flags;
static VkResult VKAPI_CALL begin_cb(VkCommandBuffer cmd, const VkCommandBufferBeginInfo *info)
{
	(void)cmd;
	CHECK(info->sType == VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO && !info->pNext, "a well-formed begin");
	begun++;
	begin_flags = info->flags;
	return VK_SUCCESS;
}

static VkResult VKAPI_CALL end_cb(VkCommandBuffer cmd)
{
	(void)cmd;
	ended++;
	return VK_SUCCESS;
}

static unsigned copies, fills, image_copies, barriers;
static VkBuffer copy_src, copy_dst, fill_dst, image_copy_src;
static VkBufferCopy copy_region;
static VkImage image_copy_dst;
static uint32_t image_copy_count;
static VkImageLayout last_new_layout, last_old_layout;
static uint8_t image_copy_first[4];

/* the bytes of a buffer the host can see */
static const uint8_t *buffer_bytes(VkBuffer buffer)
{
	unsigned id = (unsigned)(GRANITE_VK_TO_BITS(buffer) & 0xff);
	return memory_data[buffer_memory[id]] + buffer_offsets[id];
}

static void VKAPI_CALL cmd_copy(VkCommandBuffer cmd, VkBuffer src, VkBuffer dst, uint32_t count,
		const VkBufferCopy *regions)
{
	(void)cmd;
	copies++;
	copy_src = src;
	copy_dst = dst;
	CHECK(count == 1, "one region");
	copy_region = regions[0];
}

static void VKAPI_CALL cmd_fill(VkCommandBuffer cmd, VkBuffer dst, VkDeviceSize offset, VkDeviceSize size,
		uint32_t value)
{
	(void)cmd;
	CHECK(offset == 0 && size == VK_WHOLE_SIZE && value == 0, "all of it, with zero");
	fills++;
	fill_dst = dst;
}

static void VKAPI_CALL cmd_copy_to_image(VkCommandBuffer cmd, VkBuffer src, VkImage dst, VkImageLayout layout,
		uint32_t count, const VkBufferImageCopy *regions)
{
	(void)cmd;
	CHECK(layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, "into the transfer layout");
	image_copies++;
	image_copy_src = src;
	image_copy_dst = dst;
	image_copy_count = count;
	memcpy(image_copy_first, buffer_bytes(src) + regions[0].bufferOffset, 4);
}

static void VKAPI_CALL cmd_barrier(VkCommandBuffer cmd, VkPipelineStageFlags src, VkPipelineStageFlags dst,
		VkDependencyFlags flags, uint32_t memory_count, const VkMemoryBarrier *memory, uint32_t buffer_count,
		const VkBufferMemoryBarrier *buffers, uint32_t image_count, const VkImageMemoryBarrier *images)
{
	(void)cmd; (void)src; (void)dst; (void)flags; (void)memory_count; (void)memory; (void)buffer_count; (void)buffers;
	barriers++;
	if (image_count)
	{
		last_new_layout = images[image_count - 1].newLayout;
		last_old_layout = images[image_count - 1].oldLayout;
	}
}

#define MAX_SUBMITS 64
static unsigned num_submits;
static VkQueue submit_queues[MAX_SUBMITS];
static unsigned submit_cmds[MAX_SUBMITS], submit_waits[MAX_SUBMITS];
static VkResult VKAPI_CALL queue_submit(VkQueue queue, uint32_t count, const VkSubmitInfo *submits, VkFence fence)
{
	uint32_t i;
	(void)fence;
	if (num_submits == MAX_SUBMITS)
		return VK_ERROR_OUT_OF_HOST_MEMORY;
	submit_queues[num_submits] = queue;
	submit_cmds[num_submits] = 0;
	submit_waits[num_submits] = 0;
	for (i = 0; i < count; i++)
	{
		submit_cmds[num_submits] += submits[i].commandBufferCount;
		submit_waits[num_submits] += submits[i].waitSemaphoreCount;
	}
	num_submits++;
	return VK_SUCCESS;
}

static unsigned idle_waits, semaphore_waits, fence_resets;
static VkResult VKAPI_CALL wait_idle(VkDevice d)
{
	(void)d;
	idle_waits++;
	return VK_SUCCESS;
}

static VkResult VKAPI_CALL wait_semaphores(VkDevice d, const VkSemaphoreWaitInfo *info, uint64_t timeout)
{
	(void)d; (void)info; (void)timeout;
	semaphore_waits++;
	return VK_SUCCESS;
}

static VkResult VKAPI_CALL wait_fences(VkDevice d, uint32_t count, const VkFence *fences, VkBool32 all,
		uint64_t timeout)
{
	(void)d; (void)count; (void)fences; (void)all; (void)timeout;
	return VK_SUCCESS;
}

static VkResult VKAPI_CALL reset_fences(VkDevice d, uint32_t count, const VkFence *fences)
{
	(void)d; (void)fences;
	fence_resets += count;
	return VK_SUCCESS;
}

/* ---- the fixture ---- */
static granite_context_t context;
static granite_device_t dev;

static void fill_context(void)
{
	struct VolkDeviceTable *t = &context.device_table;
	unsigned i;

	memset(&context, 0, sizeof(context));
	context.instance = (VkInstance)(size_t)3;
	context.gpu = GPU;
	context.device = DEV;
	for (i = 0; i < GRANITE_QUEUE_COUNT; i++)
	{
		context.queue_info.queues[i] = Q(i);
		context.queue_info.family_indices[i] = i;
	}
	context.gpu_props.limits.nonCoherentAtomSize = 64;
	context.gpu_props.limits.minUniformBufferOffsetAlignment = 64;
	context.gpu_props.limits.maxBoundDescriptorSets = 8;
	context.mem_props.memoryHeapCount = 1;
	context.mem_props.memoryHeaps[0].size = (VkDeviceSize)1 << 32;
	context.mem_props.memoryTypeCount = 2;
	context.mem_props.memoryTypes[0].propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
	context.mem_props.memoryTypes[1].propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
		| VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
	context.ext.timeline_semaphore_features.timelineSemaphore = VK_TRUE;
	context.ext.subgroup_size_control_properties.minSubgroupSize = 4;
	context.ext.subgroup_size_control_properties.maxSubgroupSize = 64;

	t->vkCreateBuffer = create_buffer;
	t->vkDestroyBuffer = destroy_buffer;
	t->vkGetBufferMemoryRequirements2 = buffer_requirements;
	t->vkBindBufferMemory = bind_buffer;
	t->vkGetMemoryHostPointerPropertiesEXT = host_pointer_properties;
	t->vkCreateBufferView = create_buffer_view;
	t->vkDestroyBufferView = destroy_buffer_view;
	t->vkCreateImage = create_image;
	t->vkDestroyImage = destroy_image;
	t->vkGetImageMemoryRequirements = image_requirements;
	t->vkGetImageMemoryRequirements2 = image_requirements2;
	t->vkBindImageMemory = bind_image;
	t->vkCreateImageView = create_image_view;
	t->vkDestroyImageView = destroy_image_view;
	t->vkAllocateMemory = allocate_memory;
	t->vkFreeMemory = free_memory;
	t->vkMapMemory = map_memory;
	t->vkUnmapMemory = unmap_memory;
	t->vkFlushMappedMemoryRanges = flush_ranges;
	t->vkInvalidateMappedMemoryRanges = flush_ranges;
	t->vkCreateSemaphore = create_semaphore;
	t->vkDestroySemaphore = destroy_semaphore;
	t->vkCreateFence = create_fence;
	t->vkDestroyFence = destroy_fence;
	t->vkResetFences = reset_fences;
	t->vkWaitForFences = wait_fences;
	t->vkWaitSemaphoresKHR = wait_semaphores;
	t->vkCreateSampler = create_sampler;
	t->vkDestroySampler = destroy_sampler;
	t->vkCreateCommandPool = create_pool;
	t->vkDestroyCommandPool = destroy_pool;
	t->vkResetCommandPool = reset_pool;
	t->vkTrimCommandPool = trim_pool;
	t->vkAllocateCommandBuffers = allocate_cbs;
	t->vkFreeCommandBuffers = free_cbs;
	t->vkBeginCommandBuffer = begin_cb;
	t->vkEndCommandBuffer = end_cb;
	t->vkCmdCopyBuffer = cmd_copy;
	t->vkCmdFillBuffer = cmd_fill;
	t->vkCmdCopyBufferToImage = cmd_copy_to_image;
	t->vkCmdPipelineBarrier = cmd_barrier;
	t->vkQueueSubmit = queue_submit;
	t->vkDeviceWaitIdle = wait_idle;
	t->vkCreateShaderModule = (PFN_vkCreateShaderModule)create_other;
	t->vkDestroyShaderModule = (PFN_vkDestroyShaderModule)destroy_other;
	t->vkCreateDescriptorSetLayout = (PFN_vkCreateDescriptorSetLayout)create_other;
	t->vkDestroyDescriptorSetLayout = (PFN_vkDestroyDescriptorSetLayout)destroy_other;
	t->vkCreatePipelineLayout = (PFN_vkCreatePipelineLayout)create_other;
	t->vkDestroyPipelineLayout = (PFN_vkDestroyPipelineLayout)destroy_other;
	t->vkCreateDescriptorUpdateTemplate = (PFN_vkCreateDescriptorUpdateTemplate)create_other;
	t->vkDestroyDescriptorUpdateTemplate = (PFN_vkDestroyDescriptorUpdateTemplate)destroy_other;
	t->vkCreateRenderPass = (PFN_vkCreateRenderPass)create_other;
	t->vkDestroyRenderPass = (PFN_vkDestroyRenderPass)destroy_other;
}

static unsigned id_of(uint64_t handle)
{
	return (unsigned)(handle & 0xff);
}

static void test_setup(void)
{
	granite_device_init(&dev);
	CHECK(dev.device == VK_NULL_HANDLE && dev.num_frames == 0 && dev.num_thread_indices == 1
			&& dev.handle_owner.user == &dev, "a new device has nothing yet");
	granite_device_free(&dev);
	CHECK(idle_waits == 0 && bad_destroys == 0, "and can be freed as it is");

	granite_device_init(&dev);
	granite_device_set_context(&dev, &context);
	CHECK(created[K_POOL] == 2 * GRANITE_QUEUE_COUNT, "a command pool per frame context and queue");
	CHECK(dev.device == DEV && dev.gpu == GPU && dev.table == &context.device_table
			&& dev.queue_info.queues[C] == Q(C) && dev.mem_props.memoryTypeCount == 2
			&& dev.ext.timeline_semaphore_features.timelineSemaphore, "set up from its context");
	CHECK(dev.num_frames == 2 && dev.frame_index == 0 && granite_release_queue_frame(&dev.releases) == 0
			&& dev.per_frame[1].index == 1 && dev.per_frame[0].env == &dev.frame_env, "with two frame contexts");
	CHECK(dev.submission.timeline && dev.submission.queues[T].queue == Q(T)
			&& dev.submission.queues[G].timeline_semaphore != VK_NULL_HANDLE
			&& dev.per_frame[0].sync.timeline_semaphores[G] == dev.submission.queues[G].timeline_semaphore
			&& dev.submission.owner == &dev.handle_owner, "queues to submit to, each with its timeline");
	CHECK(dev.cmd_device.device == DEV && dev.cmd_device.user == &dev && dev.cmd_device.programs == &dev.program_cache
			&& dev.cmd_device.stock_samplers == dev.samplers && dev.cmd_device.workarounds == &dev.workarounds,
			"what command buffers need of it");
	CHECK(dev.samplers[GRANITE_STOCK_SAMPLER_LINEAR_CLAMP].sampler != VK_NULL_HANDLE && created[K_SAMPLER] == 1,
			"and its sampler");
	CHECK(dev.ubo.alignment == 64 && dev.ubo.spill_size == GRANITE_DEVICE_MAX_UBO_SIZE && dev.vbo.alignment == 16,
			"uniform blocks are aligned as the device needs");
}

static void test_command_buffers(void)
{
	granite_command_buffer_t *cb, *other;
	granite_fence_t *fence = NULL;
	unsigned before = num_submits;

	cb = granite_device_request_command_buffer(&dev, C);
	CHECK(cb && begun == 1 && begin_flags == VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
			&& cb->type == C && cb->dev == &dev.cmd_device && granite_refs_only(&cb->refs)
			&& dev.open_command_buffers == 1, "a command buffer is begun for one submission, and counted");
	CHECK(cb_families[(const char *)cb->cmd - cb_storage] == C, "from its queue's pool");
	other = granite_device_request_command_buffer_for_thread(&dev, 0, G);
	CHECK(other && other != cb && other->cmd != cb->cmd && other->type == G && other->state.thread_index == 0
			&& dev.open_command_buffers == 2, "and another for the graphics queue");

	granite_device_submit(&dev, cb, NULL, 0, NULL);
	CHECK(ended == 1 && num_submits == before && dev.open_command_buffers == 1
			&& dev.per_frame[0].num_submissions[C] == 1, "submitted: ended, and waiting in the frame");
	granite_device_submit(&dev, other, &fence, 0, NULL);
	CHECK(num_submits == before + 1 && submit_queues[before] == Q(G) && fence && dev.open_command_buffers == 0,
			"with a fence: sent at once");
	granite_fence_unref(fence);
	granite_device_flush_frame(&dev);
	CHECK(num_submits == before + 2 && submit_queues[before + 1] == Q(C) && submit_cmds[before + 1] == 1,
			"flushing sends the rest");
	CHECK(retro_atomic_load_acquire_int(&cb->refs) == 0 && retro_atomic_load_acquire_int(&other->refs) == 0,
			"and the device has let go of both");

	/* ---- ending a frame, or waiting for the device, sends what is left ---- */
	cb = granite_device_request_command_buffer(&dev, G);
	granite_device_submit(&dev, cb, NULL, 0, NULL);
	granite_device_next_frame_context(&dev);
	CHECK(num_submits == before + 3 && submit_queues[before + 2] == Q(G), "the next frame context sends what the frame had");
	cb = granite_device_request_command_buffer(&dev, G);
	granite_device_submit(&dev, cb, NULL, 0, NULL);
	granite_device_wait_idle(&dev);
	CHECK(num_submits == before + 4 && cb_freed == cb_allocated, "so does waiting for the device, which then trims its pools");
	granite_device_next_frame_context(&dev);
	before = num_submits - 2;

	/* ---- one-off initialisation ---- */
	cb = granite_device_request_command_buffer(&dev, C);
	granite_device_submit_initialization(&dev, cb, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
	granite_device_flush_frame(&dev);
	CHECK(dev.open_command_buffers == 0 && retro_atomic_load_acquire_int(&cb->refs) == 0,
			"an initialisation is submitted and let go of the same way");
	before = num_submits - 2;
	fence = NULL;
	granite_device_submit_empty(&dev, T, &fence);
	CHECK(num_submits == before + 3 && submit_queues[before + 2] == Q(T) && submit_cmds[before + 2] == 0 && fence,
			"an empty submission for a fence");
	granite_fence_unref(fence);
}

static void test_buffers_and_frames(void)
{
	static const uint8_t pattern[40] = "the quick brown fox jumps over the lazy";
	granite_buffer_info_t info;
	granite_buffer_t *host, *zeroed, *device, *filled;
	granite_buffer_view_info_t view_info;
	granite_buffer_view_t *view;
	uint8_t *data;
	unsigned before, before_buffers, before_destroyed, staging_id, host_id, zeroed_id, graphics_waits, i;
	bool ok;

	/* ---- memory the host writes ---- */
	memset(&info, 0, sizeof(info));
	info.domain = GRANITE_BUFFER_DOMAIN_HOST;
	info.size = sizeof(pattern);
	info.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
	before = num_submits;
	host = granite_device_create_buffer(&dev, &info, pattern);
	CHECK(host && granite_refs_only(&host->refs) && host->info.size == sizeof(pattern)
			&& host->info.usage == (VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT
				| VK_BUFFER_USAGE_TRANSFER_DST_BIT), "a host buffer, usable in transfers too");
	data = (uint8_t *)granite_device_map_host_buffer(&dev, host, GRANITE_MEMORY_ACCESS_READ_BIT, 0, info.size);
	CHECK(data && !memcmp(data, pattern, sizeof(pattern)) && num_submits == before && copies == 0,
			"starts with its data, written in place");
	granite_device_unmap_host_buffer(&dev, host, GRANITE_MEMORY_ACCESS_READ_BIT, 0, info.size);

	device = granite_device_create_buffer(&dev, &info, NULL);
	CHECK(device && buffer_bytes(device->buffer)[0] == 0xcd && buffer_bytes(device->buffer)[39] == 0xcd,
			"without data it is left as it comes");
	granite_buffer_unref(device);
	info.domain = GRANITE_BUFFER_DOMAIN_DEVICE;
	before = num_submits;
	device = granite_device_create_buffer(&dev, &info, NULL);
	CHECK(device && fills == 0 && copies == 0 && num_submits == before, "as is a device buffer");
	granite_buffer_unref(device);
	info.domain = GRANITE_BUFFER_DOMAIN_HOST;

	info.misc = GRANITE_BUFFER_MISC_ZERO_INITIALIZE_BIT;
	zeroed = granite_device_create_buffer(&dev, &info, NULL);
	data = zeroed ? (uint8_t *)granite_device_map_host_buffer(&dev, zeroed, GRANITE_MEMORY_ACCESS_READ_BIT, 0, info.size)
		: NULL;
	ok = data != NULL;
	for (i = 0; ok && i < sizeof(pattern); i++)
		if (data[i])
			ok = false;
	CHECK(ok && fills == 0, "or zeroed in place");
	before_buffers = created[K_BUFFER];
	CHECK(!granite_device_create_buffer(&dev, &info, pattern) && created[K_BUFFER] == before_buffers,
			"data and zeroing together are refused");

	/* ---- memory only the GPU writes ---- */
	info.misc = 0;
	info.domain = GRANITE_BUFFER_DOMAIN_DEVICE;
	info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	before = num_submits;
	before_buffers = created[K_BUFFER];
	before_destroyed = destroyed[K_BUFFER];
	graphics_waits = dev.submission.queues[G].waits.count;
	device = granite_device_create_buffer(&dev, &info, pattern);
	CHECK(device && created[K_BUFFER] == before_buffers + 2 && copies == 1 && copy_dst == device->buffer
			&& copy_src != device->buffer && copy_region.size == sizeof(pattern) && copy_region.srcOffset == 0
			&& copy_region.dstOffset == 0, "a device buffer with data is copied into from a staging buffer");
	staging_id = id_of(GRANITE_VK_TO_BITS(copy_src));
	CHECK(memory_data[1] && buffer_usages[staging_id] & VK_BUFFER_USAGE_TRANSFER_SRC_BIT, "(which can be copied from)");
	CHECK(num_submits == before + 1 && submit_queues[before] == Q(T) && submit_cmds[before] == 1
			&& dev.open_command_buffers == 0, "on the transfer queue, at once");
	CHECK(dev.submission.queues[C].waits.count == 1 && dev.submission.queues[G].waits.count == graphics_waits + 1,
			"and the other queues wait for the copy");
	CHECK(destroyed[K_BUFFER] == before_destroyed && live[K_BUFFER][staging_id], "the staging buffer outlives the call");

	info.misc = GRANITE_BUFFER_MISC_ZERO_INITIALIZE_BIT;
	before = num_submits;
	filled = granite_device_create_buffer(&dev, &info, NULL);
	CHECK(filled && fills == 1 && fill_dst == filled->buffer && num_submits == before + 1
			&& submit_queues[before] == Q(C) && submit_cmds[before] == 1 && submit_waits[before] == 1,
			"a zeroed device buffer is filled on the compute queue, after what that queue was waiting for");
	CHECK(dev.submission.queues[C].waits.count == 0 && dev.submission.queues[G].waits.count == graphics_waits + 2,
			"and the graphics queue waits for that too");
	CHECK(bad_maps == 0, "nothing the host cannot see is mapped");

	/* ---- a view ---- */
	view_info.buffer = host;
	view_info.format = VK_FORMAT_R32_UINT;
	view_info.offset = 8;
	view_info.range = 16;
	before = created[K_BUFFER_VIEW];
	view = granite_device_create_buffer_view(&dev, &view_info);
	CHECK(view_create.buffer == host->buffer && view_create.offset == 8 && view_create.range == 16
			&& view_create.format == VK_FORMAT_R32_UINT, "is made of the part asked for");
	CHECK(view && created[K_BUFFER_VIEW] == before + 1 && view->info.buffer == host && view->info.offset == 8
			&& view->info.range == 16 && view->info.format == VK_FORMAT_R32_UINT, "a view of a buffer");

	/* ---- what is let go waits for its frame ---- */
	host_id = id_of(GRANITE_VK_TO_BITS(host->buffer));
	zeroed_id = id_of(GRANITE_VK_TO_BITS(zeroed->buffer));
	before = destroyed[K_BUFFER_VIEW];
	CHECK(dev.frame_index == 0, "(in frame context 0)");
	i = (unsigned)granite_device_frame(&dev)->memory.count;
	granite_buffer_view_unref(view);
	granite_buffer_unref(host);
	CHECK(live[K_BUFFER][host_id] && destroyed[K_BUFFER_VIEW] == before, "let go: nothing is destroyed yet");
	CHECK(granite_device_frame(&dev)->garbage.lists[GRANITE_GARBAGE_BUFFER_VIEW].count == 0
			&& granite_device_frame(&dev)->memory.count == i,
			"(an object any thread may let go of goes through the release queues)");
	/* one only the owner thread lets go of: straight to the frame */
	zeroed->internal_sync = true;
	i = (unsigned)granite_device_frame(&dev)->garbage.lists[GRANITE_GARBAGE_BUFFER].count;
	before_buffers = (unsigned)granite_device_frame(&dev)->memory.count;
	granite_buffer_unref(zeroed);
	CHECK(granite_device_frame(&dev)->garbage.lists[GRANITE_GARBAGE_BUFFER].count == i + 1
			&& granite_device_frame(&dev)->memory.count == before_buffers + 1 && live[K_BUFFER][zeroed_id],
			"one only the owner thread lets go of is the current frame's to destroy");
	granite_device_next_frame_context(&dev);
	CHECK(dev.frame_index == 1 && granite_release_queue_frame(&dev.releases) == 1
			&& live[K_BUFFER][host_id] && live[K_BUFFER][zeroed_id], "the next frame context: still not");
	granite_device_next_frame_context(&dev);
	CHECK(dev.frame_index == 0 && !live[K_BUFFER][host_id] && !live[K_BUFFER][zeroed_id]
			&& destroyed[K_BUFFER_VIEW] == before + 1 && !live[K_BUFFER][staging_id],
			"their frame comes round: the buffers, the view and the staging buffer go");

	granite_buffer_unref(device);
	granite_buffer_unref(filled);

	/* ---- importing host memory needs the extension ---- */
	info.misc = 0;
	info.domain = GRANITE_BUFFER_DOMAIN_HOST;
	before_buffers = created[K_BUFFER];
	CHECK(!granite_device_create_imported_host_buffer(&dev, &info, (void *)pattern) && created[K_BUFFER] == before_buffers
			&& host_queries == 0, "host memory is not imported without the extension");
}

static void test_fences(void)
{
	VkFence a, b, c;
	size_t pooled;
	unsigned resets;

	create_fence(DEV, NULL, NULL, &a);
	create_fence(DEV, NULL, NULL, &b);
	create_fence(DEV, NULL, NULL, &c);
	pooled = dev.fences.count;
	resets = fence_resets;

	dev.handle_owner.recycle_fence(dev.handle_owner.user, a, true, true);
	CHECK(fence_resets == resets + 1 && dev.fences.count == pooled + 1,
			"a fence seen signalled on the owner thread is reset and reused at once");
	dev.handle_owner.recycle_fence(dev.handle_owner.user, b, false, true);
	CHECK(fence_resets == resets + 1 && dev.fences.count == pooled + 1
			&& granite_device_frame(&dev)->garbage.lists[GRANITE_GARBAGE_RECYCLE_FENCE].count == 1,
			"one nobody saw signalled waits for its frame");
	dev.handle_owner.recycle_fence(dev.handle_owner.user, c, true, false);
	CHECK(fence_resets == resets + 1 && granite_device_frame(&dev)->garbage.lists[GRANITE_GARBAGE_RECYCLE_FENCE].count == 1,
			"from another thread it goes through the release queue");
	granite_device_next_frame_context(&dev);
	granite_device_next_frame_context(&dev);
	CHECK(fence_resets == resets + 3 && dev.fences.count == pooled + 3, "and all are reset and back by then");

	{
		VkSemaphore semaphore;
		create_semaphore(DEV, NULL, NULL, &semaphore);
		granite_frame_garbage_push(&granite_device_frame(&dev)->garbage, GRANITE_GARBAGE_RECYCLE_SEMAPHORE,
				GRANITE_VK_TO_BITS(semaphore));
		granite_device_next_frame_context(&dev);
		granite_device_next_frame_context(&dev);
		CHECK(dev.semaphores.count == 1, "a semaphore to use again is kept (and goes with the device)");
	}
}

static void test_images(void)
{
	static const uint8_t texels[4 * 4 * 4] = { 1, 2, 3, 4 };
	granite_image_initial_data_t initial;
	granite_image_info_t info;
	granite_image_view_info_t view_info;
	granite_image_view_t *view;
	granite_image_t *image, *plain;
	unsigned before, before_buffers, before_images, before_barriers;

	memset(&info, 0, sizeof(info));
	info.width = 4;
	info.height = 4;
	info.depth = 1;
	info.levels = 1;
	info.layers = 1;
	info.format = VK_FORMAT_R8G8B8A8_UNORM;
	info.type = VK_IMAGE_TYPE_2D;
	info.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
	info.samples = VK_SAMPLE_COUNT_1_BIT;
	info.initial_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	/* ---- with data ---- */
	initial.data = texels;
	initial.row_length = 0;
	before = num_submits;
	before_buffers = created[K_BUFFER];
	before_images = created[K_IMAGE];
	image = granite_device_create_image(&dev, &info, &initial);
	CHECK(image && granite_refs_only(&image->refs) && created[K_IMAGE] == before_images + 1 && image->view
			&& image->info.width == 4 && (image->info.usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT),
			"an image with data, and a view of it");
	CHECK(created[K_BUFFER] == before_buffers + 1 && image_copies == 1 && image_copy_dst == image->image
			&& image_copy_count == 1 && id_of(GRANITE_VK_TO_BITS(image_copy_src)) == created[K_BUFFER],
			"is copied into from a staging buffer");
	CHECK(!memcmp(image_copy_first, texels, 4), "which holds its texels");
	CHECK(last_new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			&& last_old_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && dev.open_command_buffers == 0,
			"and left in the layout asked for");
	CHECK(image->stage_flags == granite_image_usage_stages(image->info.usage)
			&& image->access_flags == granite_image_usage_access(image->info.usage), "knowing where it can be used");
	CHECK(num_submits > before || dev.per_frame[dev.frame_index].num_submissions[G]
			|| dev.per_frame[dev.frame_index].num_submissions[T], "its upload is on its way");

	/* ---- without ---- */
	before_buffers = created[K_BUFFER];
	before_barriers = barriers;
	before = image_copies;
	plain = granite_device_create_image(&dev, &info, NULL);
	CHECK(plain && created[K_BUFFER] == before_buffers && image_copies == before && barriers == before_barriers + 1
			&& last_new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			&& last_old_layout == VK_IMAGE_LAYOUT_UNDEFINED
			&& !(plain->info.usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT),
			"an image without data only changes layout");

	/* ---- a view of its own ---- */
	memset(&view_info, 0, sizeof(view_info));
	view_info.image = plain;
	view_info.levels = VK_REMAINING_MIP_LEVELS;
	view_info.layers = VK_REMAINING_ARRAY_LAYERS;
	view_info.view_type = VK_IMAGE_VIEW_TYPE_2D;
	view = granite_device_create_image_view(&dev, &view_info);
	CHECK(view && view->info.image == plain && view->info.format == VK_FORMAT_R8G8B8A8_UNORM
			&& view->views.view != VK_NULL_HANDLE, "a view takes its format from the image");

	/* ---- refused ---- */
	before_images = created[K_IMAGE];
	before_buffers = created[K_BUFFER];
	info.format = BAD_FORMAT;
	CHECK(!granite_device_create_image(&dev, &info, &initial) && created[K_IMAGE] == before_images
			&& created[K_BUFFER] == before_buffers, "a format the device cannot use that way is refused");
	info.format = VK_FORMAT_R8G8B8A8_UNORM;
	info.depth = 2;
	CHECK(!granite_device_create_image(&dev, &info, NULL) && created[K_IMAGE] == before_images, "as is one with depth");
	info.depth = 1;
	info.type = VK_IMAGE_TYPE_3D;
	CHECK(!granite_device_create_image(&dev, &info, NULL) && created[K_IMAGE] == before_images, "or not 2D");

	before = destroyed[K_IMAGE];
	granite_image_view_unref(view);
	granite_image_unref(plain);
	granite_image_unref(image);
	CHECK(destroyed[K_IMAGE] == before, "let go: they wait for their frame");
	granite_device_wait_idle(&dev);
	CHECK(destroyed[K_IMAGE] == before + 2 && destroyed[K_IMAGE_VIEW] == created[K_IMAGE_VIEW]
			&& destroyed[K_BUFFER] == created[K_BUFFER], "an idle device has given everything let go of back");
}

static void test_blocks(void)
{
	granite_buffer_block_t block;
	unsigned before = created[K_BUFFER];

	memset(&block, 0, sizeof(block));
	dev.cmd_device.request_block(dev.cmd_device.user, true, &block, 64);
	CHECK(block.handle && block.mapped && block.size == 4 * 1024 && created[K_BUFFER] == before + 1
			&& ((granite_buffer_t *)block.handle)->internal_sync
			&& (buffer_usages[created[K_BUFFER]] & VK_BUFFER_USAGE_VERTEX_BUFFER_BIT),
			"a vertex block is a buffer the host writes");
	CHECK(granite_buffer_block_allocate(&block, 64).host == block.mapped, "written from its start");
	dev.cmd_device.request_block(dev.cmd_device.user, true, &block, 0);
	CHECK(!block.handle && granite_device_frame(&dev)->vbo_blocks.count == 1
			&& granite_device_frame(&dev)->ubo_blocks.count == 0, "given back, it stays with the frame that used it");

	dev.cmd_device.request_block(dev.cmd_device.user, false, &block, 64);
	CHECK(block.handle && block.size == 256 * 1024 && block.alignment == 64
			&& (buffer_usages[created[K_BUFFER]] & VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT), "a uniform block likewise");
	granite_buffer_block_allocate(&block, 64);
	dev.cmd_device.request_block(dev.cmd_device.user, false, &block, 0);
	CHECK(granite_device_frame(&dev)->ubo_blocks.count == 1, "with its own list");

	/* the frame comes round: its blocks are kept for the next use */
	granite_device_next_frame_context(&dev);
	granite_device_next_frame_context(&dev);
	CHECK(granite_device_frame(&dev)->vbo_blocks.count == 0 && dev.vbo.count == 1 && dev.ubo.count == 1
			&& destroyed[K_BUFFER] + 2 == created[K_BUFFER], "when the frame comes round its blocks are kept to use again");
	dev.cmd_device.request_block(dev.cmd_device.user, true, &block, 64);
	granite_buffer_block_allocate(&block, 64);
	dev.cmd_device.request_block(dev.cmd_device.user, true, &block, 0);
	dev.cmd_device.request_block(dev.cmd_device.user, true, &block, 64);
	granite_buffer_block_allocate(&block, 64);
	dev.cmd_device.request_block(dev.cmd_device.user, true, &block, 0);
	CHECK(created[K_BUFFER] == before + 3 && granite_device_frame(&dev)->vbo_blocks.count == 2 && dev.vbo.count == 0,
			"one is used again, and a second made");
	granite_device_next_frame_context(&dev);
	granite_device_next_frame_context(&dev);
	CHECK(dev.vbo.count == 2, "(both kept)");
	dev.cmd_device.request_block(dev.cmd_device.user, true, &block, 64);
	granite_buffer_block_allocate(&block, 64);
	dev.cmd_device.request_block(dev.cmd_device.user, true, &block, 0);
	CHECK(dev.vbo.count == 1 && granite_device_frame(&dev)->vbo_blocks.count == 1, "(one kept, one with the frame)");

	granite_device_wait_idle(&dev);
	CHECK(destroyed[K_BUFFER] == created[K_BUFFER] && granite_device_frame(&dev)->vbo_blocks.count == 0
			&& dev.vbo.count == 0 && dev.ubo.count == 0, "an idle device gives the blocks' buffers back");
	CHECK(destroyed[K_MEMORY] == created[K_MEMORY], "and all its memory");
}

static void test_frame_contexts(void)
{
	unsigned before = idle_waits;
	unsigned start;

	granite_device_init_frame_contexts(&dev, 4);
	start = dev.frame_index;
	CHECK(idle_waits == before + 1 && dev.num_frames == 4 && start < 2
			&& granite_release_queue_frame(&dev.releases) == start, "four frame contexts, on an idle device");
	granite_device_next_frame_context(&dev);
	granite_device_next_frame_context(&dev);
	granite_device_next_frame_context(&dev);
	CHECK(dev.frame_index == (start + 3) % 4, "it goes round all of them");
	granite_device_next_frame_context(&dev);
	CHECK(dev.frame_index == start && granite_release_queue_frame(&dev.releases) == start, "and starts again");
	while (dev.frame_index != 2)
		granite_device_next_frame_context(&dev);
	granite_device_init_frame_contexts(&dev, 1000);
	CHECK(dev.num_frames == GRANITE_RELEASE_FRAMES && dev.frame_index == 2, "no more than the release queues have room for");
	granite_device_init_frame_contexts(&dev, 2);
	CHECK(dev.num_frames == 2 && dev.frame_index == 0 && granite_release_queue_frame(&dev.releases) == 0,
			"fewer: the current one is one that exists");
}

static void test_records(void)
{
	static const uint32_t code[] = { 0x07230203u, 1, 1, 1 };
	static const uint32_t other_code[] = { 0x07230203u, 9, 9, 9 };
	granite_pipeline_compile_t compile, resolved;
	const pipeline_record_t *records;
	pipeline_record_t record, wrong;
	granite_program_t *program;
	granite_shader_t *shader;

	shader = granite_program_cache_shader(&dev.program_cache, code, sizeof(code), NULL);
	program = granite_program_cache_compute(&dev.program_cache, shader);
	CHECK(shader && program, "a compute program");

	memset(&compile, 0, sizeof(compile));
	compile.program = program;
	compile.cache = dev.pipeline_cache;
	compile.subgroup_size_tag = (4u << 0) | (64u << 8);
	granite_pipeline_compile_update_hash_compute(&compile);

	CHECK(granite_pipeline_record_table_get(&dev.pipeline_records, &records) == 0, "no pipeline was built yet");
	dev.cmd_device.record_pipeline(dev.cmd_device.user, &compile, PIPELINE_RECORD_COMPUTE);
	CHECK(granite_pipeline_record_table_get(&dev.pipeline_records, &records) == 1
			&& records[0].kind == PIPELINE_RECORD_COMPUTE && records[0].program_hash == program->node.hash
			&& records[0].shader_hashes[0] == shader->node.hash && records[0].pipeline_hash == compile.hash,
			"a built pipeline is recorded with what it was made from");
	record = records[0];

	memset(&resolved, 0xff, sizeof(resolved));
	CHECK(granite_device_resolve_pipeline_record(&dev, &record, &resolved) && resolved.program == program
			&& resolved.hash == compile.hash && resolved.compatible_render_pass == NULL,
			"the record resolves to the same pipeline");
	wrong = record;
	wrong.pipeline_hash ^= 1;
	CHECK(!granite_device_resolve_pipeline_record(&dev, &wrong, &resolved), "not when it hashes to another");
	wrong = record;
	wrong.program_hash ^= 1;
	wrong.shader_hashes[0] ^= 1;
	CHECK(!granite_device_resolve_pipeline_record(&dev, &wrong, &resolved), "nor without its program or shader");
	wrong = record;
	wrong.program_hash ^= 1;
	CHECK(!granite_device_resolve_pipeline_record(&dev, &wrong, &resolved),
			"nor when its shader makes another program than the one recorded");
	wrong = record;
	wrong.kind = PIPELINE_RECORD_GRAPHICS;
	CHECK(!granite_device_resolve_pipeline_record(&dev, &wrong, &resolved), "nor as the other kind of pipeline");

	/* ---- a new device: the program is made from its shader ---- */
	{
		granite_command_buffer_t *cb = granite_device_request_command_buffer(&dev, G);
		unsigned idle = idle_waits, submits = num_submits;
		granite_device_submit(&dev, cb, NULL, 0, NULL);
		granite_device_free(&dev);
		CHECK(idle_waits == idle + 1 && num_submits == submits + 1, "freeing a device first sends its work and waits for it");
	}
	granite_device_init(&dev);
	context.ext.supports_external_memory_host = true;
	context.ext.host_memory_properties.minImportedHostPointerAlignment = 4096;
	granite_device_set_context(&dev, &context);
	{
		static uint8_t host_memory[8192];
		void *aligned = (void *)(((size_t)host_memory + 4095) & ~(size_t)4095);
		granite_buffer_info_t info;
		granite_buffer_t *imported;

		memset(&info, 0, sizeof(info));
		info.domain = GRANITE_BUFFER_DOMAIN_HOST;
		info.size = 4096;
		info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
		imported = granite_device_create_imported_host_buffer(&dev, &info, aligned);
		CHECK(imported && host_queries == 1 && imported->info.size == 4096 && imported_pointer == aligned,
				"with the extension a buffer is the host's own memory");
		if (imported)
			granite_buffer_unref(imported);
	}
	CHECK(!granite_device_resolve_pipeline_record(&dev, &record, &resolved), "a new device without the shader cannot");
	granite_program_cache_shader(&dev.program_cache, other_code, sizeof(other_code), NULL);
	shader = granite_program_cache_shader(&dev.program_cache, code, sizeof(code), NULL);
	CHECK(shader && !granite_program_cache_find_program(&dev.program_cache, record.program_hash),
			"with the shader, and no program yet");
	CHECK(granite_device_resolve_pipeline_record(&dev, &record, &resolved) && resolved.program
			&& resolved.program->node.hash == record.program_hash
			&& resolved.program->shaders[GRANITE_SHADER_STAGE_COMPUTE] == shader && resolved.hash == record.pipeline_hash,
			"the program is made and the record resolves");
}

int main(void)
{
	unsigned k;

	fill_context();

	test_setup();
	test_command_buffers();
	test_buffers_and_frames();
	test_fences();
	test_images();
	test_blocks();
	test_frame_contexts();
	test_records();

	granite_device_free(&dev);
	for (k = 1; k < K_COUNT; k++)
		if (created[k] != destroyed[k])
		{
			fprintf(stderr, "test_vk_device: kind %u: %u made, %u destroyed\n", k, created[k], destroyed[k]);
			failures++;
		}
	CHECK(bad_destroys == 0, "nothing is destroyed twice, or as something else");
	CHECK(cb_freed == cb_allocated, "every command buffer went back");
	CHECK(idle_waits > 0 && semaphore_waits > 0, "the device was waited for");

	if (failures)
	{
		fprintf(stderr, "test_vk_device: %u failure(s)\n", failures);
		return 1;
	}
	printf("test_vk_device: ok\n");
	return 0;
}
