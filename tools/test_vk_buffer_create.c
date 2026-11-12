/* parallel-rdp's buffer creation (vulkan/buffer_create.c) with the real
 * memory allocator, through a stand-in device table.
 *
 * A memory type is the first of those allowed that has what is asked
 * for. Each buffer domain takes its best type and settles for the next
 * best; a device that must hand out cached host memory does. Image
 * domains likewise. The queue families a buffer is shared between are
 * the distinct ones the device has.
 *
 * A buffer is created for its usage plus transfers both ways, shared
 * when the device has more than one queue family, with memory of its
 * domain bound at the allocation's offset. When memory the host can map
 * on the device has run out, the buffer gets host or device memory
 * instead. A buffer that cannot be created, given memory or bound leaves
 * nothing behind. */
#include <stdio.h>
#include <string.h>
#include <boolean.h>

#include "buffer_create.h"
#include "frame_garbage.h" /* GRANITE_VK_TO_BITS */

static unsigned failures;

#define CHECK(cond, what) \
	do \
	{ \
		if (!(cond)) \
		{ \
			fprintf(stderr, "test_vk_buffer_create: FAIL (line %d): %s\n", __LINE__, what); \
			failures++; \
		} \
	} while (0)

#define MIB ((uint64_t)1024 * 1024)

#define TYPE_DEVICE   0 /* device local */
#define TYPE_HOST     1 /* host visible */
#define TYPE_COHERENT 2 /* host visible, coherent */
#define TYPE_CACHED   3 /* host visible, coherent, cached */
#define TYPE_BAR      4 /* device local, host visible, coherent */
#define TYPE_LAZY     5 /* lazily allocated */
#define ALL_TYPES     0x3fu

/* ---- the stand-in device ---- */
static VkBufferCreateInfo created_info;
static uint32_t created_families[8];
static unsigned buffers_created, buffers_destroyed, memory_allocated, memory_freed, binds;
static VkDeviceMemory bound_memory;
static VkDeviceSize bound_offset;
static uint32_t allocated_type[64];
static uint32_t allowed_types = ALL_TYPES;
static uint32_t fail_type = ~0u;
static bool fail_create, fail_bind;

static VkResult VKAPI_CALL fake_create_buffer(VkDevice d, const VkBufferCreateInfo *info,
		const VkAllocationCallbacks *a, VkBuffer *buffer)
{
	uint32_t i;
	(void)d; (void)a;
	if (fail_create)
		return VK_ERROR_OUT_OF_HOST_MEMORY;
	created_info = *info;
	for (i = 0; i < info->queueFamilyIndexCount && i < 8; i++)
		created_families[i] = info->pQueueFamilyIndices[i];
	buffers_created++;
	*buffer = GRANITE_VK_FROM_BITS(VkBuffer, 0x1000 + buffers_created);
	return VK_SUCCESS;
}

static void VKAPI_CALL fake_destroy_buffer(VkDevice d, VkBuffer buffer, const VkAllocationCallbacks *a)
{
	(void)d; (void)a;
	CHECK(buffer != VK_NULL_HANDLE, "a buffer that was made is destroyed");
	buffers_destroyed++;
}

static void VKAPI_CALL fake_requirements(VkDevice d, const VkBufferMemoryRequirementsInfo2 *info,
		VkMemoryRequirements2 *reqs)
{
	(void)d; (void)info;
	reqs->memoryRequirements.size = created_info.size;
	reqs->memoryRequirements.alignment = 64;
	reqs->memoryRequirements.memoryTypeBits = allowed_types;
}

static VkResult VKAPI_CALL fake_bind(VkDevice d, VkBuffer buffer, VkDeviceMemory memory, VkDeviceSize offset)
{
	(void)d; (void)buffer;
	if (fail_bind)
		return VK_ERROR_OUT_OF_DEVICE_MEMORY;
	binds++;
	bound_memory = memory;
	bound_offset = offset;
	return VK_SUCCESS;
}

static VkResult VKAPI_CALL fake_allocate(VkDevice d, const VkMemoryAllocateInfo *info,
		const VkAllocationCallbacks *a, VkDeviceMemory *memory)
{
	(void)d; (void)a;
	if (info->memoryTypeIndex == fail_type)
		return VK_ERROR_OUT_OF_DEVICE_MEMORY;
	if (memory_allocated < 64)
		allocated_type[memory_allocated] = info->memoryTypeIndex;
	memory_allocated++;
	*memory = GRANITE_VK_FROM_BITS(VkDeviceMemory, 0x2000 + memory_allocated);
	return VK_SUCCESS;
}

static void VKAPI_CALL fake_free(VkDevice d, VkDeviceMemory memory, const VkAllocationCallbacks *a)
{
	(void)d; (void)memory; (void)a;
	memory_freed++;
}

static VkResult VKAPI_CALL fake_map(VkDevice d, VkDeviceMemory memory, VkDeviceSize offset, VkDeviceSize size,
		VkMemoryMapFlags flags, void **data)
{
	(void)d; (void)offset; (void)size; (void)flags;
	/* never read or written */
	*data = (void *)((size_t)GRANITE_VK_TO_BITS(memory) * 0x100000u);
	return VK_SUCCESS;
}

static void VKAPI_CALL fake_unmap(VkDevice d, VkDeviceMemory memory)
{
	(void)d; (void)memory;
}

static void VKAPI_CALL fake_memory_properties2(VkPhysicalDevice gpu, VkPhysicalDeviceMemoryProperties2 *props)
{
	(void)gpu; (void)props;
}
PFN_vkGetPhysicalDeviceMemoryProperties2 vkGetPhysicalDeviceMemoryProperties2 = fake_memory_properties2;

static struct VolkDeviceTable table;
static VkPhysicalDeviceMemoryProperties props;
static granite_device_allocator_t allocator;
static granite_buffer_env_t env;
static uint32_t families[4];

static void test_memory_types(void)
{
	VkPhysicalDeviceMemoryProperties few;

	CHECK(granite_find_memory_type(&props, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, ALL_TYPES) == TYPE_HOST,
			"the first type with what is asked for");
	CHECK(granite_find_memory_type(&props, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, ALL_TYPES & ~(1u << TYPE_HOST)) ==
			TYPE_COHERENT, "among those allowed");
	CHECK(granite_find_memory_type(&props, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
				ALL_TYPES) == TYPE_BAR, "with all of what is asked for");
	CHECK(granite_find_memory_type(&props, VK_MEMORY_PROPERTY_PROTECTED_BIT, ALL_TYPES) == GRANITE_NO_MEMORY_TYPE,
			"or none");
	CHECK(granite_find_memory_type(&props, 0, 1u << TYPE_LAZY) == TYPE_LAZY, "nothing asked for: the first allowed");

	CHECK(granite_find_buffer_memory_type(&props, GRANITE_BUFFER_DOMAIN_DEVICE, ALL_TYPES, false) == TYPE_DEVICE,
			"a device buffer is device local");
	CHECK(granite_find_buffer_memory_type(&props, GRANITE_BUFFER_DOMAIN_DEVICE, 1u << TYPE_HOST, false) == TYPE_HOST,
			"or whatever it is allowed");
	CHECK(granite_find_buffer_memory_type(&props, GRANITE_BUFFER_DOMAIN_LINKED_DEVICE_HOST, ALL_TYPES, false) == TYPE_BAR,
			"a linked buffer is device memory the host can map");
	CHECK(granite_find_buffer_memory_type(&props, GRANITE_BUFFER_DOMAIN_LINKED_DEVICE_HOST,
				ALL_TYPES & ~(1u << TYPE_BAR), false) == TYPE_COHERENT, "or coherent host memory");
	CHECK(granite_find_buffer_memory_type(&props, GRANITE_BUFFER_DOMAIN_LINKED_DEVICE_HOST,
				(1u << TYPE_DEVICE) | (1u << TYPE_HOST), false) == GRANITE_NO_MEMORY_TYPE, "and nothing less");
	CHECK(granite_find_buffer_memory_type(&props, GRANITE_BUFFER_DOMAIN_LINKED_DEVICE_HOST_PREFER_DEVICE,
				ALL_TYPES & ~(1u << TYPE_BAR), false) == TYPE_DEVICE, "preferring the device: device local");
	CHECK(granite_find_buffer_memory_type(&props, GRANITE_BUFFER_DOMAIN_HOST, ALL_TYPES, false) == TYPE_COHERENT,
			"a host buffer is coherent");
	CHECK(granite_find_buffer_memory_type(&props, GRANITE_BUFFER_DOMAIN_HOST, (1u << TYPE_HOST) | (1u << TYPE_DEVICE),
				false) == TYPE_HOST, "or at least visible");
	CHECK(granite_find_buffer_memory_type(&props, GRANITE_BUFFER_DOMAIN_CACHED_HOST, ALL_TYPES, false) == TYPE_CACHED,
			"a cached buffer is cached");
	CHECK(granite_find_buffer_memory_type(&props, GRANITE_BUFFER_DOMAIN_CACHED_HOST, ALL_TYPES & ~(1u << TYPE_CACHED),
				false) == TYPE_HOST, "or at least visible");

	/* a device with a cached type that is not coherent and a coherent
	 * one that is not cached */
	memset(&few, 0, sizeof(few));
	few.memoryTypeCount = 3;
	few.memoryTypes[0].propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
	few.memoryTypes[1].propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
	few.memoryTypes[2].propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
	CHECK(granite_find_buffer_memory_type(&few, GRANITE_BUFFER_DOMAIN_CACHED_COHERENT_HOST_PREFER_COHERENT, 7, false) == 1,
			"cached and coherent, coherent if not both");
	CHECK(granite_find_buffer_memory_type(&few, GRANITE_BUFFER_DOMAIN_CACHED_COHERENT_HOST_PREFER_CACHED, 7, false) == 2,
			"cached and coherent, cached if not both");
	CHECK(granite_find_buffer_memory_type(&few, GRANITE_BUFFER_DOMAIN_CACHED_COHERENT_HOST_PREFER_CACHED, 1, false) == 0,
			"or neither");
	CHECK(granite_find_buffer_memory_type(&props, GRANITE_BUFFER_DOMAIN_CACHED_COHERENT_HOST_PREFER_COHERENT, ALL_TYPES,
				false) == TYPE_CACHED, "both when the device has it");

	/* forced to cached host memory */
	CHECK(granite_find_buffer_memory_type(&few, GRANITE_BUFFER_DOMAIN_HOST, 7, true) == 2 &&
			granite_find_buffer_memory_type(&few, GRANITE_BUFFER_DOMAIN_CACHED_COHERENT_HOST_PREFER_COHERENT, 7, true) == 2,
			"forced: host memory is cached");
	CHECK(granite_find_buffer_memory_type(&props, GRANITE_BUFFER_DOMAIN_LINKED_DEVICE_HOST, ALL_TYPES, true) == TYPE_CACHED,
			"a linked buffer too");
	CHECK(granite_find_buffer_memory_type(&props, GRANITE_BUFFER_DOMAIN_LINKED_DEVICE_HOST_PREFER_DEVICE, ALL_TYPES, true) ==
			TYPE_DEVICE, "one preferring the device is device local");
	CHECK(granite_find_buffer_memory_type(&props, GRANITE_BUFFER_DOMAIN_DEVICE, ALL_TYPES, true) == TYPE_DEVICE,
			"a device buffer is not affected");

	CHECK(granite_find_image_memory_type(&props, GRANITE_IMAGE_DOMAIN_PHYSICAL, ALL_TYPES) == TYPE_DEVICE,
			"an image is device local");
	CHECK(granite_find_image_memory_type(&props, GRANITE_IMAGE_DOMAIN_PHYSICAL, 1u << TYPE_HOST) == TYPE_HOST,
			"or whatever it is allowed");
	CHECK(granite_find_image_memory_type(&props, GRANITE_IMAGE_DOMAIN_TRANSIENT, ALL_TYPES) == TYPE_LAZY,
			"a transient image is lazily allocated");
	CHECK(granite_find_image_memory_type(&props, GRANITE_IMAGE_DOMAIN_TRANSIENT, ALL_TYPES & ~(1u << TYPE_LAZY)) ==
			TYPE_DEVICE, "or device local");
	CHECK(granite_find_image_memory_type(&props, GRANITE_IMAGE_DOMAIN_TRANSIENT, 1u << TYPE_HOST) == GRANITE_NO_MEMORY_TYPE,
			"and nothing else");
	CHECK(granite_find_image_memory_type(&props, GRANITE_IMAGE_DOMAIN_LINEAR_HOST_CACHED, ALL_TYPES) == TYPE_CACHED &&
			granite_find_image_memory_type(&props, GRANITE_IMAGE_DOMAIN_LINEAR_HOST_CACHED, 1u << TYPE_HOST) == TYPE_HOST,
			"a cached host image is cached, or visible");
	CHECK(granite_find_image_memory_type(&props, GRANITE_IMAGE_DOMAIN_LINEAR_HOST, ALL_TYPES) == TYPE_HOST,
			"a host image is visible");
}

static void test_helpers(void)
{
	const uint32_t f[5] = { 2, VK_QUEUE_FAMILY_IGNORED, 0, 2, 0 };
	uint32_t unique[5];

	CHECK(granite_unique_queue_families(f, 5, unique) == 2 && unique[0] == 2 && unique[1] == 0,
			"the distinct families, without the queues the device lacks");
	CHECK(granite_unique_queue_families(f + 1, 1, unique) == 0, "no queue, no family");

	CHECK(granite_buffer_usage_stages(VK_BUFFER_USAGE_TRANSFER_SRC_BIT) == VK_PIPELINE_STAGE_TRANSFER_BIT &&
			granite_buffer_usage_access(VK_BUFFER_USAGE_TRANSFER_DST_BIT) ==
			(VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT), "a transfer buffer is used in transfers");
	CHECK(granite_buffer_usage_stages(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT) == VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT &&
			granite_buffer_usage_access(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT) ==
			(VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT), "a storage buffer in compute, read and written");
	CHECK(granite_buffer_usage_stages(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT) ==
			(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
			 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT) &&
			granite_buffer_usage_access(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT) == VK_ACCESS_UNIFORM_READ_BIT,
			"a uniform buffer in every shader stage, read");
	CHECK(granite_buffer_usage_stages(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT) ==
			(VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT) &&
			granite_buffer_usage_access(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
				VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT) ==
			(VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT),
			"vertex, index and indirect buffers where they are read");
	CHECK(granite_buffer_usage_stages(0) == 0 && granite_buffer_usage_access(0) == 0, "no usage, nothing");
}

static void test_create(void)
{
	granite_allocation_t alloc, alloc2;
	VkBuffer buffer, buffer2;
	uint32_t type;

	/* one queue family */
	families[0] = families[1] = families[2] = 0;
	families[3] = VK_QUEUE_FAMILY_IGNORED;
	CHECK(granite_buffer_create(&env, GRANITE_BUFFER_DOMAIN_DEVICE, 4096, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &buffer,
				&alloc, &type), "a device buffer is created");
	CHECK(buffer != VK_NULL_HANDLE && type == TYPE_DEVICE && buffers_created == 1, "with device memory");
	CHECK(created_info.sType == VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO && created_info.size == 4096 &&
			created_info.usage == (VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
				VK_BUFFER_USAGE_TRANSFER_DST_BIT), "for its usage and transfers both ways");
	CHECK(created_info.sharingMode == VK_SHARING_MODE_EXCLUSIVE && created_info.queueFamilyIndexCount == 0 &&
			created_info.pNext == NULL, "one family: not shared");
	CHECK(binds == 1 && bound_memory == alloc.base && bound_offset == alloc.offset && alloc.base != VK_NULL_HANDLE,
			"its memory is bound at the allocation's offset");
	CHECK(alloc.mode == GRANITE_ALLOCATION_LINEAR_DEVICE_HIGH_PRIORITY && alloc.memory_type == TYPE_DEVICE,
			"a storage buffer's memory is high priority");

	/* a second one shares the block, at another offset */
	CHECK(granite_buffer_create(&env, GRANITE_BUFFER_DOMAIN_DEVICE, 4096, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &buffer2,
				&alloc2, &type), "a second one");
	CHECK(alloc2.base == alloc.base && alloc2.offset != alloc.offset && bound_offset == alloc2.offset,
			"is cut from the same block, at its own offset");
	granite_allocation_free(&alloc2, &allocator);
	granite_allocation_free(&alloc, &allocator);

	/* modes by domain */
	CHECK(granite_buffer_create(&env, GRANITE_BUFFER_DOMAIN_DEVICE, 256, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &buffer,
				&alloc, &type) && alloc.mode == GRANITE_ALLOCATION_LINEAR_DEVICE, "a device vertex buffer: device memory");
	granite_allocation_free(&alloc, &allocator);
	CHECK(granite_buffer_create(&env, GRANITE_BUFFER_DOMAIN_CACHED_COHERENT_HOST_PREFER_CACHED, 256,
				VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &buffer, &alloc, &type) && type == TYPE_CACHED &&
			alloc.mode == GRANITE_ALLOCATION_LINEAR_HOST_MAPPABLE && alloc.host_base != NULL,
			"a host buffer: cached host memory, mapped");
	granite_allocation_free(&alloc, &allocator);

	/* several queue families */
	families[1] = 3;
	CHECK(granite_buffer_create(&env, GRANITE_BUFFER_DOMAIN_HOST, 256, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, &buffer,
				&alloc, &type) && type == TYPE_COHERENT, "a host buffer on a device with two queue families");
	CHECK(created_info.sharingMode == VK_SHARING_MODE_CONCURRENT && created_info.queueFamilyIndexCount == 2 &&
			created_families[0] == 0 && created_families[1] == 3, "is shared between them");
	granite_allocation_free(&alloc, &allocator);
	families[1] = 0;

	/* linked memory has run out */
	fail_type = TYPE_BAR;
	CHECK(granite_buffer_create(&env, GRANITE_BUFFER_DOMAIN_LINKED_DEVICE_HOST, 256, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
				&buffer, &alloc, &type) && type == TYPE_COHERENT && alloc.memory_type == TYPE_COHERENT,
			"linked memory run out: host memory instead");
	granite_allocation_free(&alloc, &allocator);
	CHECK(granite_buffer_create(&env, GRANITE_BUFFER_DOMAIN_LINKED_DEVICE_HOST_PREFER_DEVICE, 256,
				VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &buffer, &alloc, &type) && type == TYPE_DEVICE,
			"preferring the device: device memory instead");
	granite_allocation_free(&alloc, &allocator);
	fail_type = ~0u;
	CHECK(granite_buffer_create(&env, GRANITE_BUFFER_DOMAIN_LINKED_DEVICE_HOST, 256, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
				&buffer, &alloc, &type) && type == TYPE_BAR, "linked memory when there is some");
	granite_allocation_free(&alloc, &allocator);

	/* failures leave nothing */
	buffers_created = buffers_destroyed = 0;
	fail_type = TYPE_COHERENT;
	CHECK(!granite_buffer_create(&env, GRANITE_BUFFER_DOMAIN_HOST, 256, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &buffer, &alloc,
				&type), "host memory run out: the buffer is not made");
	CHECK(buffer == VK_NULL_HANDLE && type == GRANITE_NO_MEMORY_TYPE && buffers_created == 1 && buffers_destroyed == 1,
			"and the VkBuffer is destroyed");
	fail_type = ~0u;

	allowed_types = 1u << TYPE_LAZY;
	CHECK(!granite_buffer_create(&env, GRANITE_BUFFER_DOMAIN_HOST, 256, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &buffer, &alloc,
				&type) && buffer == VK_NULL_HANDLE && buffers_destroyed == 2, "no memory type for it: not made, destroyed");
	allowed_types = ALL_TYPES;

	fail_create = true;
	CHECK(!granite_buffer_create(&env, GRANITE_BUFFER_DOMAIN_HOST, 256, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &buffer, &alloc,
				&type) && buffer == VK_NULL_HANDLE && buffers_destroyed == 2, "refused by the driver: nothing to destroy");
	fail_create = false;

	fail_bind = true;
	memory_allocated = memory_freed = 0;
	/* (large enough for a block of its own, so that freeing it is seen) */
	CHECK(!granite_buffer_create(&env, GRANITE_BUFFER_DOMAIN_DEVICE, (VkDeviceSize)(128 * MIB),
				VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &buffer, &alloc, &type) && buffer == VK_NULL_HANDLE,
			"memory that cannot be bound: not made");
	CHECK(buffers_destroyed == 3 && memory_allocated == 1 && memory_freed == 1, "the buffer and its memory are given back");
	fail_bind = false;
}

int main(void)
{
	memset(&table, 0, sizeof(table));
	table.vkCreateBuffer = fake_create_buffer;
	table.vkDestroyBuffer = fake_destroy_buffer;
	table.vkGetBufferMemoryRequirements2 = fake_requirements;
	table.vkBindBufferMemory = fake_bind;
	table.vkAllocateMemory = fake_allocate;
	table.vkFreeMemory = fake_free;
	table.vkMapMemory = fake_map;
	table.vkUnmapMemory = fake_unmap;

	memset(&props, 0, sizeof(props));
	props.memoryHeapCount = 1;
	props.memoryHeaps[0].size = 1024 * 1024 * MIB;
	props.memoryTypeCount = 6;
	props.memoryTypes[TYPE_DEVICE].propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
	props.memoryTypes[TYPE_HOST].propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
	props.memoryTypes[TYPE_COHERENT].propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
		VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
	props.memoryTypes[TYPE_CACHED].propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
		VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
	props.memoryTypes[TYPE_BAR].propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
	props.memoryTypes[TYPE_LAZY].propertyFlags = VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT;

	CHECK(granite_device_allocator_init(&allocator, (VkDevice)(size_t)1, (VkPhysicalDevice)(size_t)2, &table, &props,
				64, false, false), "the allocator comes up");

	env.device = (VkDevice)(size_t)1;
	env.table = &table;
	env.allocator = &allocator;
	env.memory = &props;
	env.queue_families = families;
	env.num_queue_families = 4;
	env.force_host_cached = false;

	test_memory_types();
	test_helpers();
	test_create();

	granite_device_allocator_free(&allocator);

	if (failures)
	{
		fprintf(stderr, "test_vk_buffer_create: %u failure(s)\n", failures);
		return 1;
	}
	printf("test_vk_buffer_create: OK\n");
	return 0;
}
