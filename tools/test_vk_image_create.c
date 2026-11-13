/* parallel-rdp's image creation (vulkan/image_create.c) with the real
 * memory allocator, through a stand-in device table.
 *
 * What a usage needs of a format, and the stages and accesses it and a
 * layout allow. The UNORM and sRGB formats that pair up. An image's own
 * view is an array view when it has layers or is forced to be one.
 *
 * An image is a 2D, optimally tiled, undefined image of what was asked
 * for; one to upload to is a transfer source and destination too. A
 * storage or mutable-sRGB image is mutable, the latter with the list of
 * its two formats where the driver takes one. It is shared between the
 * families of the queues it is used on only when they differ. Its
 * memory is device local, of the render target kind for what is
 * rendered or written to, and bound at the allocation's offset. An
 * image that cannot be created, given memory or bound leaves nothing.
 *
 * A view covers the levels and layers asked for, with the usage of the
 * image less storage for an sRGB format. A depth and stencil image that
 * is sampled gets a view of each aspect; an attachment with several
 * layers gets a view of each layer. When one of them cannot be made the
 * ones made are destroyed. */
#include <stdio.h>
#include <string.h>
#include <boolean.h>

#include "image_create.h"
#include "frame_garbage.h" /* GRANITE_VK_TO_BITS */

static unsigned failures;

#define CHECK(cond, what) \
	do \
	{ \
		if (!(cond)) \
		{ \
			fprintf(stderr, "test_vk_image_create: FAIL (line %d): %s\n", __LINE__, what); \
			failures++; \
		} \
	} while (0)

#define MIB ((uint64_t)1024 * 1024)

#define TYPE_HOST   0 /* host visible */
#define TYPE_DEVICE 1 /* device local */

/* ---- the stand-in device ---- */
static VkImageCreateInfo created;
static uint32_t created_families[8];
static VkFormat created_formats[4];
static uint32_t created_format_count;
static unsigned images_created, images_destroyed, memory_allocated, memory_freed, binds;
static VkDeviceMemory bound_memory;
static VkDeviceSize bound_offset;
static uint32_t allocated_type;
static uint32_t allowed_types = 3;
static bool fail_create, fail_bind, fail_memory, prefer_dedicated;

static VkImageViewCreateInfo view_infos[16];
static VkImageUsageFlags view_usages[16];
static unsigned views_created, views_destroyed;
static unsigned fail_view_at = ~0u;

static VkResult VKAPI_CALL fake_create_image(VkDevice d, const VkImageCreateInfo *info,
		const VkAllocationCallbacks *a, VkImage *image)
{
	const VkBaseInStructure *next;
	uint32_t i;
	(void)d; (void)a;
	if (fail_create)
		return VK_ERROR_OUT_OF_DEVICE_MEMORY;
	created = *info;
	created_format_count = 0;
	for (i = 0; i < info->queueFamilyIndexCount && i < 8; i++)
		created_families[i] = info->pQueueFamilyIndices[i];
	for (next = (const VkBaseInStructure *)info->pNext; next; next = next->pNext)
	{
		if (next->sType == VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO_KHR)
		{
			const VkImageFormatListCreateInfoKHR *list = (const VkImageFormatListCreateInfoKHR *)next;
			created_format_count = list->viewFormatCount;
			for (i = 0; i < list->viewFormatCount && i < 4; i++)
				created_formats[i] = list->pViewFormats[i];
		}
	}
	images_created++;
	*image = GRANITE_VK_FROM_BITS(VkImage, 0x1000 + images_created);
	return VK_SUCCESS;
}

static void VKAPI_CALL fake_destroy_image(VkDevice d, VkImage image, const VkAllocationCallbacks *a)
{
	(void)d; (void)a;
	CHECK(image != VK_NULL_HANDLE, "an image that was made is destroyed");
	images_destroyed++;
}

static void VKAPI_CALL fake_requirements(VkDevice d, VkImage image, VkMemoryRequirements *reqs)
{
	(void)d; (void)image;
	reqs->size = 64 * 1024;
	reqs->alignment = 4096;
	reqs->memoryTypeBits = allowed_types;
}

static void VKAPI_CALL fake_requirements2(VkDevice d, const VkImageMemoryRequirementsInfo2 *info,
		VkMemoryRequirements2 *reqs)
{
	VkBaseOutStructure *next;
	(void)d; (void)info;
	for (next = (VkBaseOutStructure *)reqs->pNext; next; next = next->pNext)
		if (next->sType == VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS)
			((VkMemoryDedicatedRequirements *)next)->prefersDedicatedAllocation = prefer_dedicated;
}

static VkResult VKAPI_CALL fake_bind(VkDevice d, VkImage image, VkDeviceMemory memory, VkDeviceSize offset)
{
	(void)d; (void)image;
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
	if (fail_memory)
		return VK_ERROR_OUT_OF_DEVICE_MEMORY;
	allocated_type = info->memoryTypeIndex;
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
	*data = (void *)((size_t)GRANITE_VK_TO_BITS(memory) * 0x100000u);
	return VK_SUCCESS;
}

static void VKAPI_CALL fake_unmap(VkDevice d, VkDeviceMemory memory)
{
	(void)d; (void)memory;
}

static VkResult VKAPI_CALL fake_create_view(VkDevice d, const VkImageViewCreateInfo *info,
		const VkAllocationCallbacks *a, VkImageView *view)
{
	const VkBaseInStructure *next;
	(void)d; (void)a;
	if (views_created == fail_view_at)
		return VK_ERROR_OUT_OF_HOST_MEMORY;
	if (views_created < 16)
	{
		view_infos[views_created] = *info;
		view_usages[views_created] = 0;
		for (next = (const VkBaseInStructure *)info->pNext; next; next = next->pNext)
			if (next->sType == VK_STRUCTURE_TYPE_IMAGE_VIEW_USAGE_CREATE_INFO)
				view_usages[views_created] = ((const VkImageViewUsageCreateInfo *)next)->usage;
	}
	views_created++;
	*view = GRANITE_VK_FROM_BITS(VkImageView, 0x3000 + views_created);
	return VK_SUCCESS;
}

static void VKAPI_CALL fake_destroy_view(VkDevice d, VkImageView view, const VkAllocationCallbacks *a)
{
	(void)d; (void)a;
	CHECK(view != VK_NULL_HANDLE, "a view that was made is destroyed");
	views_destroyed++;
}

static void VKAPI_CALL fake_memory_properties2(VkPhysicalDevice gpu, VkPhysicalDeviceMemoryProperties2 *props)
{
	(void)gpu; (void)props;
}
PFN_vkGetPhysicalDeviceMemoryProperties2 vkGetPhysicalDeviceMemoryProperties2 = fake_memory_properties2;

static struct VolkDeviceTable table;
static VkPhysicalDeviceMemoryProperties props;
static granite_device_allocator_t allocator;
static granite_image_env_t env;
#define DEV ((VkDevice)(size_t)1)

static granite_image_desc_t image_desc(VkFormat format, VkImageUsageFlags usage)
{
	granite_image_desc_t desc;
	memset(&desc, 0, sizeof(desc));
	desc.format = format;
	desc.width = 320;
	desc.height = 240;
	desc.levels = 1;
	desc.layers = 1;
	desc.samples = VK_SAMPLE_COUNT_1_BIT;
	desc.usage = usage;
	return desc;
}

static void test_helpers(void)
{
	granite_image_desc_t desc = image_desc(VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT);
	VkFormat formats[2];

	CHECK(granite_format_is_srgb(VK_FORMAT_R8G8B8A8_SRGB) && granite_format_is_srgb(VK_FORMAT_B8G8R8A8_SRGB) &&
			granite_format_is_srgb(VK_FORMAT_BC7_SRGB_BLOCK) && !granite_format_is_srgb(VK_FORMAT_R8G8B8A8_UNORM) &&
			!granite_format_is_srgb(VK_FORMAT_R16G16_SFLOAT), "the sRGB formats");
	CHECK(granite_srgb_view_formats(VK_FORMAT_R8G8B8A8_SRGB, formats) == 2 && formats[0] == VK_FORMAT_R8G8B8A8_UNORM &&
			formats[1] == VK_FORMAT_R8G8B8A8_SRGB, "an sRGB format pairs with its UNORM format");
	CHECK(granite_srgb_view_formats(VK_FORMAT_B8G8R8A8_UNORM, formats) == 2 && formats[0] == VK_FORMAT_B8G8R8A8_UNORM &&
			formats[1] == VK_FORMAT_B8G8R8A8_SRGB, "and a UNORM format with its sRGB one, UNORM first");
	CHECK(granite_srgb_view_formats(VK_FORMAT_A8B8G8R8_SRGB_PACK32, formats) == 2 &&
			formats[0] == VK_FORMAT_A8B8G8R8_UNORM_PACK32, "the packed formats too");
	CHECK(granite_srgb_view_formats(VK_FORMAT_R8G8B8A8_UINT, formats) == 0 &&
			granite_srgb_view_formats(VK_FORMAT_R16G16_SFLOAT, formats) == 0, "other formats have no pair");

	CHECK(granite_image_usage_features(VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
				VK_IMAGE_USAGE_TRANSFER_SRC_BIT) ==
			(VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT),
			"a sampled attachment needs a format that can be both");
	CHECK(granite_image_usage_features(VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) ==
			(VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT | VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) &&
			granite_image_usage_features(VK_IMAGE_USAGE_TRANSFER_DST_BIT) == 0, "storage, depth; transfers need nothing");

	CHECK(granite_image_usage_stages(VK_IMAGE_USAGE_TRANSFER_DST_BIT) == VK_PIPELINE_STAGE_TRANSFER_BIT &&
			granite_image_usage_access(VK_IMAGE_USAGE_TRANSFER_SRC_BIT) ==
			(VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT), "a transfer image is used in transfers");
	CHECK(granite_image_usage_stages(VK_IMAGE_USAGE_SAMPLED_BIT) ==
			(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
			 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT) &&
			granite_image_usage_access(VK_IMAGE_USAGE_SAMPLED_BIT) == VK_ACCESS_SHADER_READ_BIT,
			"a sampled image is read in every shader stage");
	CHECK(granite_image_usage_stages(VK_IMAGE_USAGE_STORAGE_BIT) == VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT &&
			granite_image_usage_access(VK_IMAGE_USAGE_STORAGE_BIT) ==
			(VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT), "a storage image is read and written in compute");
	CHECK(granite_image_usage_stages(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) ==
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT &&
			granite_image_usage_access(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) ==
			(VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT), "a colour attachment");
	CHECK(granite_image_usage_stages(VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) ==
			(VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT) &&
			granite_image_usage_access(VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) ==
			(VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT),
			"a depth attachment");
	CHECK(granite_image_usage_stages(VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT) == VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT &&
			granite_image_usage_access(VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT) == VK_ACCESS_INPUT_ATTACHMENT_READ_BIT,
			"an input attachment");
	CHECK(granite_image_usage_stages(0) == 0 && granite_image_usage_access(0) == 0, "no usage, nothing");

	CHECK(granite_image_layout_access(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) ==
			(VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_INPUT_ATTACHMENT_READ_BIT) &&
			granite_image_layout_access(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) ==
			(VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT) &&
			granite_image_layout_access(VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) ==
			(VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT) &&
			granite_image_layout_access(VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL) ==
			(VK_ACCESS_INPUT_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT) &&
			granite_image_layout_access(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) == VK_ACCESS_TRANSFER_READ_BIT &&
			granite_image_layout_access(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) == VK_ACCESS_TRANSFER_WRITE_BIT,
			"a layout allows the accesses it is for");
	CHECK(granite_image_layout_access(VK_IMAGE_LAYOUT_GENERAL) == ~(VkAccessFlags)0, "the general layout allows any");

	CHECK(granite_image_usage_has_view(VK_IMAGE_USAGE_SAMPLED_BIT) &&
			granite_image_usage_has_view(VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT) &&
			granite_image_usage_has_view(VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) &&
			!granite_image_usage_has_view(VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT),
			"an image only transferred to and from has no view");

	CHECK(granite_image_view_type(&desc) == VK_IMAGE_VIEW_TYPE_2D, "one layer: a 2D view");
	desc.force_array = true;
	CHECK(granite_image_view_type(&desc) == VK_IMAGE_VIEW_TYPE_2D_ARRAY, "forced: an array view");
	desc.force_array = false;
	desc.layers = 2;
	CHECK(granite_image_view_type(&desc) == VK_IMAGE_VIEW_TYPE_2D_ARRAY, "layers: an array view");
}

static void test_create(void)
{
	granite_image_desc_t desc = image_desc(VK_FORMAT_R8G8B8A8_UNORM,
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
	granite_allocation_t alloc, alloc2;
	VkImage image, image2;
	VkImageUsageFlags usage;
	VkImageCreateFlags flags;
	unsigned before;

	desc.layers = 2;
	CHECK(granite_image_create(&env, &desc, false, &image, &alloc, &usage, &flags), "a render target is created");
	CHECK(image != VK_NULL_HANDLE && images_created == 1 && created.sType == VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO &&
			created.imageType == VK_IMAGE_TYPE_2D && created.format == VK_FORMAT_R8G8B8A8_UNORM &&
			created.extent.width == 320 && created.extent.height == 240 && created.extent.depth == 1 &&
			created.mipLevels == 1 && created.arrayLayers == 2 && created.samples == VK_SAMPLE_COUNT_1_BIT,
			"a 2D image of the size, layers and format asked for");
	CHECK(created.tiling == VK_IMAGE_TILING_OPTIMAL && created.initialLayout == VK_IMAGE_LAYOUT_UNDEFINED,
			"optimally tiled, undefined");
	CHECK(created.usage == desc.usage && usage == desc.usage && created.flags == 0 && flags == 0 && created.pNext == NULL,
			"with its usage and nothing more");
	CHECK(created.sharingMode == VK_SHARING_MODE_EXCLUSIVE && created.queueFamilyIndexCount == 0, "not shared");
	CHECK(allocated_type == TYPE_DEVICE && alloc.memory_type == TYPE_DEVICE &&
			alloc.mode == GRANITE_ALLOCATION_OPTIMAL_RENDER_TARGET, "its memory is device local, of the render target kind");
	CHECK(binds == 1 && bound_memory == alloc.base && bound_offset == alloc.offset && alloc.base != VK_NULL_HANDLE,
			"bound at the allocation's offset");

	/* a second one in the same block */
	CHECK(granite_image_create(&env, &desc, false, &image2, &alloc2, &usage, &flags) && alloc2.base == alloc.base &&
			alloc2.offset != alloc.offset && bound_offset == alloc2.offset, "a second one is cut from the same block");
	granite_allocation_free(&alloc2, &allocator);
	granite_allocation_free(&alloc, &allocator);

	/* a texture to upload to */
	desc = image_desc(VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT);
	CHECK(granite_image_create(&env, &desc, true, &image, &alloc, &usage, &flags), "a texture to upload to");
	CHECK(created.usage == (VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT) &&
			usage == created.usage, "is a transfer source and destination too");
	CHECK(alloc.mode == GRANITE_ALLOCATION_OPTIMAL_RESOURCE, "its memory is of the resource kind");
	CHECK(created.sharingMode == VK_SHARING_MODE_EXCLUSIVE, "and it is not shared, whatever the transfer family");
	granite_allocation_free(&alloc, &allocator);

	/* storage: mutable, render target memory */
	desc = image_desc(VK_FORMAT_R8G8B8A8_UINT, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
	CHECK(granite_image_create(&env, &desc, false, &image, &alloc, &usage, &flags) &&
			created.flags == VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT && flags == VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT &&
			created.pNext == NULL && alloc.mode == GRANITE_ALLOCATION_OPTIMAL_RENDER_TARGET,
			"a storage image is mutable, without a format list, in render target memory");
	granite_allocation_free(&alloc, &allocator);

	/* mutable sRGB */
	desc = image_desc(VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
	desc.mutable_srgb = true;
	desc.flags = VK_IMAGE_CREATE_EXTENDED_USAGE_BIT;
	CHECK(granite_image_create(&env, &desc, false, &image, &alloc, &usage, &flags) &&
			created.flags == (VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT | VK_IMAGE_CREATE_EXTENDED_USAGE_BIT),
			"a mutable-sRGB image is mutable, with the flags it was given");
	CHECK(created_format_count == 2 && created_formats[0] == VK_FORMAT_R8G8B8A8_UNORM &&
			created_formats[1] == VK_FORMAT_R8G8B8A8_SRGB, "and lists its two formats");
	granite_allocation_free(&alloc, &allocator);
	env.format_list = false;
	CHECK(granite_image_create(&env, &desc, false, &image, &alloc, &usage, &flags) && created.pNext == NULL &&
			(created.flags & VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT), "no list on a driver that takes none");
	granite_allocation_free(&alloc, &allocator);
	env.format_list = true;
	desc.format = VK_FORMAT_R16G16B16A16_SFLOAT;
	CHECK(granite_image_create(&env, &desc, false, &image, &alloc, &usage, &flags) && created.pNext == NULL &&
			(created.flags & VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT), "nor for a format without an sRGB pair");
	granite_allocation_free(&alloc, &allocator);

	/* sharing */
	desc = image_desc(VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
	desc.on_graphics = true;
	desc.on_compute = true;
	CHECK(granite_image_create(&env, &desc, false, &image, &alloc, &usage, &flags) &&
			created.sharingMode == VK_SHARING_MODE_EXCLUSIVE && created.queueFamilyIndexCount == 0,
			"used on two queues of one family: not shared");
	granite_allocation_free(&alloc, &allocator);
	env.compute_family = 2;
	CHECK(granite_image_create(&env, &desc, false, &image, &alloc, &usage, &flags) &&
			created.sharingMode == VK_SHARING_MODE_CONCURRENT && created.queueFamilyIndexCount == 2 &&
			created_families[0] == 0 && created_families[1] == 2, "of two families: shared between them");
	granite_allocation_free(&alloc, &allocator);
	desc.on_graphics = false;
	CHECK(granite_image_create(&env, &desc, false, &image, &alloc, &usage, &flags) &&
			created.sharingMode == VK_SHARING_MODE_EXCLUSIVE && created.queueFamilyIndexCount == 0,
			"used on the compute queue alone: not shared");
	granite_allocation_free(&alloc, &allocator);
	env.transfer_family = 5;
	CHECK(granite_image_create(&env, &desc, true, &image, &alloc, &usage, &flags) &&
			created.sharingMode == VK_SHARING_MODE_CONCURRENT && created.queueFamilyIndexCount == 3 &&
			created_families[0] == 2 && created_families[1] == 5 && created_families[2] == 0,
			"uploaded to as well: shared with the transfer and graphics families");
	granite_allocation_free(&alloc, &allocator);
	desc.on_compute = false;
	CHECK(granite_image_create(&env, &desc, true, &image, &alloc, &usage, &flags) &&
			created.sharingMode == VK_SHARING_MODE_EXCLUSIVE, "not asked to be shared: not shared, uploaded to or not");
	granite_allocation_free(&alloc, &allocator);
	env.compute_family = 0;
	env.transfer_family = 0;

	/* memory the image is allowed */
	allowed_types = 1u << TYPE_HOST;
	CHECK(granite_image_create(&env, &desc, false, &image, &alloc, &usage, &flags) && alloc.memory_type == TYPE_HOST,
			"allowed host memory only: that");
	granite_allocation_free(&alloc, &allocator);
	allowed_types = 3;

	/* a block of its own for the driver that wants one */
	prefer_dedicated = true;
	before = memory_allocated;
	CHECK(granite_image_create(&env, &desc, false, &image, &alloc, &usage, &flags) && alloc.alloc == NULL &&
			memory_allocated == before + 1 && bound_offset == 0, "a block of its own where the driver prefers one");
	granite_allocation_free(&alloc, &allocator);
	prefer_dedicated = false;

	/* failures leave nothing */
	images_created = images_destroyed = 0;
	fail_create = true;
	CHECK(!granite_image_create(&env, &desc, false, &image, &alloc, &usage, &flags) && image == VK_NULL_HANDLE &&
			images_destroyed == 0, "refused by the driver: nothing made, nothing to destroy");
	fail_create = false;

	allowed_types = 0;
	CHECK(!granite_image_create(&env, &desc, false, &image, &alloc, &usage, &flags) && image == VK_NULL_HANDLE &&
			images_created == 1 && images_destroyed == 1, "no memory type for it: the image is destroyed");
	allowed_types = 3;

	prefer_dedicated = true;
	fail_memory = true;
	CHECK(!granite_image_create(&env, &desc, false, &image, &alloc, &usage, &flags) && image == VK_NULL_HANDLE &&
			images_destroyed == 2, "no memory: the image is destroyed");
	fail_memory = false;

	fail_bind = true;
	memory_allocated = memory_freed = 0;
	CHECK(!granite_image_create(&env, &desc, false, &image, &alloc, &usage, &flags) && image == VK_NULL_HANDLE &&
			images_destroyed == 3 && memory_allocated == 1 && memory_freed == 1,
			"memory that cannot be bound: the image and its memory are given back");
	fail_bind = false;
	prefer_dedicated = false;
}

static granite_image_view_desc_t view_desc(VkFormat format, VkImageUsageFlags usage, uint32_t levels, uint32_t layers)
{
	granite_image_view_desc_t desc;
	memset(&desc, 0, sizeof(desc));
	desc.image = GRANITE_VK_FROM_BITS(VkImage, 0x1234);
	desc.format = format;
	desc.type = layers > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
	desc.levels = levels;
	desc.layers = layers;
	desc.usage = usage;
	return desc;
}

static void reset_views(void)
{
	views_created = views_destroyed = 0;
	memset(view_infos, 0, sizeof(view_infos));
}

static void test_views(void)
{
	granite_image_view_desc_t desc;
	granite_image_views_t views;
	const VkImageViewCreateInfo *v;

	/* a plain texture */
	reset_views();
	desc = view_desc(VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, 1, 1);
	CHECK(granite_image_views_create(DEV, &table, &desc, &views) && views_created == 1 &&
			views.view != VK_NULL_HANDLE && views.depth == VK_NULL_HANDLE && views.stencil == VK_NULL_HANDLE &&
			views.num_layer_views == 0, "a texture has one view");
	v = &view_infos[0];
	CHECK(v->sType == VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO && v->image == desc.image &&
			v->format == VK_FORMAT_R8G8B8A8_UNORM && v->viewType == VK_IMAGE_VIEW_TYPE_2D &&
			v->subresourceRange.aspectMask == VK_IMAGE_ASPECT_COLOR_BIT && v->subresourceRange.baseMipLevel == 0 &&
			v->subresourceRange.levelCount == 1 && v->subresourceRange.baseArrayLayer == 0 &&
			v->subresourceRange.layerCount == 1, "of the image, its format, all of it");
	CHECK(v->components.r == VK_COMPONENT_SWIZZLE_IDENTITY && v->components.a == VK_COMPONENT_SWIZZLE_IDENTITY,
			"components as they are");
	CHECK(view_usages[0] == VK_IMAGE_USAGE_SAMPLED_BIT, "used as the image is, transfers aside");
	granite_image_views_destroy(DEV, &table, &views);
	CHECK(views_destroyed == 1 && views.view == VK_NULL_HANDLE, "destroyed");
	granite_image_views_destroy(DEV, &table, &views);
	CHECK(views_destroyed == 1, "once");

	/* an sRGB view of a storage image */
	reset_views();
	desc = view_desc(VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT |
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, 1, 1);
	CHECK(granite_image_views_create(DEV, &table, &desc, &views) &&
			view_usages[0] == (VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT),
			"an sRGB view is not a storage view");
	granite_image_views_destroy(DEV, &table, &views);
	desc.format = VK_FORMAT_R8G8B8A8_UNORM;
	CHECK(granite_image_views_create(DEV, &table, &desc, &views) &&
			view_usages[1] == (VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT |
				VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT), "a UNORM one is");
	granite_image_views_destroy(DEV, &table, &views);

	/* no view without a usage for one */
	reset_views();
	desc = view_desc(VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_TRANSFER_SRC_BIT, 1, 1);
	CHECK(!granite_image_views_create(DEV, &table, &desc, &views) && views_created == 0 && views.view == VK_NULL_HANDLE,
			"an image only transferred from gets no view");

	/* a render target with two layers */
	reset_views();
	desc = view_desc(VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, 1, 2);
	desc.base_layer = 0;
	CHECK(granite_image_views_create(DEV, &table, &desc, &views) && views_created == 3 && views.num_layer_views == 2 &&
			views.layer_views[0] != VK_NULL_HANDLE && views.layer_views[1] != VK_NULL_HANDLE &&
			views.layer_views[0] != views.layer_views[1] && views.view != views.layer_views[0],
			"an attachment of two layers has a view of each besides its own");
	CHECK(view_infos[0].viewType == VK_IMAGE_VIEW_TYPE_2D && view_infos[0].subresourceRange.baseArrayLayer == 0 &&
			view_infos[0].subresourceRange.layerCount == 1 && view_infos[0].subresourceRange.levelCount == 1 &&
			view_infos[1].viewType == VK_IMAGE_VIEW_TYPE_2D && view_infos[1].subresourceRange.baseArrayLayer == 1 &&
			view_infos[1].subresourceRange.layerCount == 1, "each a 2D view of one layer, level 0");
	CHECK(view_infos[2].viewType == VK_IMAGE_VIEW_TYPE_2D_ARRAY && view_infos[2].subresourceRange.layerCount == 2,
			"its own view has both");
	CHECK(view_usages[0] == (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT), "with the same usage");
	granite_image_views_destroy(DEV, &table, &views);
	CHECK(views_destroyed == 3 && views.num_layer_views == 0, "all three destroyed");

	/* a sampled image of two layers is not rendered to */
	reset_views();
	desc = view_desc(VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT, 1, 2);
	CHECK(granite_image_views_create(DEV, &table, &desc, &views) && views_created == 1 && views.num_layer_views == 0,
			"a texture of two layers has no layer views");
	granite_image_views_destroy(DEV, &table, &views);

	/* one layer of it, from layer 1 */
	reset_views();
	desc = view_desc(VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, 1, 1);
	desc.base_layer = 1;
	desc.base_level = 0;
	CHECK(granite_image_views_create(DEV, &table, &desc, &views) && views_created == 1 && views.num_layer_views == 0 &&
			view_infos[0].subresourceRange.baseArrayLayer == 1 && view_infos[0].subresourceRange.layerCount == 1,
			"a view of one layer is that layer alone");
	granite_image_views_destroy(DEV, &table, &views);

	/* mip levels of an attachment: layer views from the view's base */
	reset_views();
	desc = view_desc(VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, 3, 1);
	desc.base_level = 1;
	desc.base_layer = 2;
	CHECK(granite_image_views_create(DEV, &table, &desc, &views) && views_created == 2 && views.num_layer_views == 1 &&
			view_infos[0].subresourceRange.baseMipLevel == 1 && view_infos[0].subresourceRange.levelCount == 1 &&
			view_infos[0].subresourceRange.baseArrayLayer == 2 && view_infos[1].subresourceRange.levelCount == 3 &&
			view_infos[1].subresourceRange.baseMipLevel == 1,
			"an attachment with levels: a view of its first level to render to");
	granite_image_views_destroy(DEV, &table, &views);

	/* too many layers to render to */
	reset_views();
	desc = view_desc(VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, 1, GRANITE_IMAGE_MAX_LAYER_VIEWS + 1);
	CHECK(!granite_image_views_create(DEV, &table, &desc, &views) && views_created == views_destroyed &&
			views.view == VK_NULL_HANDLE, "more layers than there are layer views for: refused, nothing left");
	desc.layers = GRANITE_IMAGE_MAX_LAYER_VIEWS;
	CHECK(granite_image_views_create(DEV, &table, &desc, &views) &&
			views.num_layer_views == GRANITE_IMAGE_MAX_LAYER_VIEWS, "as many as there are: made");
	granite_image_views_destroy(DEV, &table, &views);
	CHECK(views_created == views_destroyed, "and destroyed");

	/* depth */
	reset_views();
	desc = view_desc(VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, 1, 1);
	CHECK(granite_image_views_create(DEV, &table, &desc, &views) && views_created == 1 &&
			views.depth == VK_NULL_HANDLE && view_infos[0].subresourceRange.aspectMask == VK_IMAGE_ASPECT_DEPTH_BIT,
			"a depth image has one view, of depth");
	granite_image_views_destroy(DEV, &table, &views);

	/* depth and stencil, sampled */
	reset_views();
	desc = view_desc(VK_FORMAT_D24_UNORM_S8_UINT,
			VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, 1, 1);
	CHECK(granite_image_views_create(DEV, &table, &desc, &views) && views_created == 3 &&
			views.depth != VK_NULL_HANDLE && views.stencil != VK_NULL_HANDLE && views.depth != views.stencil &&
			views.view != views.depth, "a sampled depth and stencil image has a view of each aspect too");
	CHECK(view_infos[0].subresourceRange.aspectMask == VK_IMAGE_ASPECT_DEPTH_BIT &&
			view_infos[1].subresourceRange.aspectMask == VK_IMAGE_ASPECT_STENCIL_BIT &&
			view_infos[2].subresourceRange.aspectMask == (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT),
			"depth, stencil, and both");
	granite_image_views_destroy(DEV, &table, &views);
	CHECK(views_destroyed == 3, "all destroyed");

	/* depth and stencil, attachment only */
	reset_views();
	desc.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
	CHECK(granite_image_views_create(DEV, &table, &desc, &views) && views_created == 1 &&
			views.depth == VK_NULL_HANDLE && views.stencil == VK_NULL_HANDLE,
			"one that is only an attachment has its one view");
	granite_image_views_destroy(DEV, &table, &views);

	/* a view that cannot be made takes the others with it */
	reset_views();
	desc = view_desc(VK_FORMAT_D24_UNORM_S8_UINT,
			VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, 1, 2);
	fail_view_at = 1; /* the stencil view */
	CHECK(!granite_image_views_create(DEV, &table, &desc, &views) && views_created == 1 && views_destroyed == 1 &&
			views.depth == VK_NULL_HANDLE, "the stencil view fails: the depth view is destroyed");
	reset_views();
	fail_view_at = 3; /* the second layer view */
	CHECK(!granite_image_views_create(DEV, &table, &desc, &views) && views_created == 3 && views_destroyed == 3 &&
			views.num_layer_views == 0, "a layer view fails: those before it are destroyed");
	reset_views();
	fail_view_at = 4; /* the view itself */
	CHECK(!granite_image_views_create(DEV, &table, &desc, &views) && views_created == 4 && views_destroyed == 4 &&
			views.view == VK_NULL_HANDLE, "the view itself fails: all the others are destroyed");
	fail_view_at = ~0u;
}

int main(void)
{
	memset(&table, 0, sizeof(table));
	table.vkCreateImage = fake_create_image;
	table.vkDestroyImage = fake_destroy_image;
	table.vkGetImageMemoryRequirements = fake_requirements;
	table.vkGetImageMemoryRequirements2 = fake_requirements2;
	table.vkBindImageMemory = fake_bind;
	table.vkCreateImageView = fake_create_view;
	table.vkDestroyImageView = fake_destroy_view;
	table.vkAllocateMemory = fake_allocate;
	table.vkFreeMemory = fake_free;
	table.vkMapMemory = fake_map;
	table.vkUnmapMemory = fake_unmap;

	memset(&props, 0, sizeof(props));
	props.memoryHeapCount = 1;
	props.memoryHeaps[0].size = 1024 * 1024 * MIB;
	props.memoryTypeCount = 2;
	props.memoryTypes[TYPE_HOST].propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
		VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
	props.memoryTypes[TYPE_DEVICE].propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

	CHECK(granite_device_allocator_init(&allocator, DEV, (VkPhysicalDevice)(size_t)2, &table, &props, 64, false, false),
			"the allocator comes up");

	memset(&env, 0, sizeof(env));
	env.device = DEV;
	env.table = &table;
	env.allocator = &allocator;
	env.memory = &props;
	env.format_list = true;

	test_helpers();
	test_create();
	test_views();

	granite_device_allocator_free(&allocator);

	if (failures)
	{
		fprintf(stderr, "test_vk_image_create: %u failure(s)\n", failures);
		return 1;
	}
	printf("test_vk_image_create: OK\n");
	return 0;
}
