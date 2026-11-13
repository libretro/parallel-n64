/* parallel-rdp's sampler (vulkan/sampler.c), through a stand-in device
 * table.
 *
 * The one sampler in use filters linearly within a level, takes the
 * nearest level of all of them, and clamps to the edge, with no
 * comparison and no anisotropy. Creating it gives it a cookie no other
 * object has; a driver that refuses leaves no sampler. Freeing it
 * destroys it once. */
#include <stdio.h>
#include <string.h>
#include <boolean.h>

#include "sampler.h"
#include "frame_garbage.h" /* GRANITE_VK_TO_BITS */

static unsigned failures;

#define CHECK(cond, what) \
	do \
	{ \
		if (!(cond)) \
		{ \
			fprintf(stderr, "test_vk_sampler: FAIL (line %d): %s\n", __LINE__, what); \
			failures++; \
		} \
	} while (0)

#define DEV ((VkDevice)(size_t)1)

static VkSamplerCreateInfo created;
static unsigned samplers_created, samplers_destroyed;
static VkSampler destroyed;
static bool fail_create;

static VkResult VKAPI_CALL fake_create(VkDevice d, const VkSamplerCreateInfo *info, const VkAllocationCallbacks *a,
		VkSampler *sampler)
{
	(void)a;
	CHECK(d == DEV, "on the device given");
	if (fail_create)
	{
		/* (a driver may leave anything behind) */
		*sampler = GRANITE_VK_FROM_BITS(VkSampler, 0xdead);
		return VK_ERROR_OUT_OF_HOST_MEMORY;
	}
	created = *info;
	samplers_created++;
	*sampler = GRANITE_VK_FROM_BITS(VkSampler, 0x100 + samplers_created);
	return VK_SUCCESS;
}

static void VKAPI_CALL fake_destroy(VkDevice d, VkSampler sampler, const VkAllocationCallbacks *a)
{
	(void)a;
	CHECK(d == DEV, "on the device given");
	destroyed = sampler;
	samplers_destroyed++;
}

int main(void)
{
	struct VolkDeviceTable table;
	VkSamplerCreateInfo info;
	granite_sampler_t a, b;

	memset(&table, 0, sizeof(table));
	table.vkCreateSampler = fake_create;
	table.vkDestroySampler = fake_destroy;

	memset(&info, 0xff, sizeof(info));
	granite_sampler_linear_clamp_info(&info);
	CHECK(info.sType == VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO && info.pNext == NULL && info.flags == 0, "a sampler's info");
	CHECK(info.magFilter == VK_FILTER_LINEAR && info.minFilter == VK_FILTER_LINEAR, "linear within a level");
	CHECK(info.mipmapMode == VK_SAMPLER_MIPMAP_MODE_NEAREST && info.minLod == 0.0f && info.maxLod == VK_LOD_CLAMP_NONE &&
			info.mipLodBias == 0.0f, "the nearest level, of all of them");
	CHECK(info.addressModeU == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE &&
			info.addressModeV == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE &&
			info.addressModeW == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, "clamped to the edge");
	CHECK(info.compareEnable == VK_FALSE && info.anisotropyEnable == VK_FALSE && info.maxAnisotropy == 1.0f &&
			info.unnormalizedCoordinates == VK_FALSE, "no comparison, no anisotropy, normalized");

	CHECK(granite_sampler_init_linear_clamp(&a, DEV, &table) && a.sampler == GRANITE_VK_FROM_BITS(VkSampler, 0x101) &&
			samplers_created == 1, "the sampler is created");
	CHECK(memcmp(&created, &info, sizeof(info)) == 0, "with that info");
	CHECK(granite_sampler_init_linear_clamp(&b, DEV, &table) && a.cookie != 0 && b.cookie != 0 && a.cookie != b.cookie,
			"each with a cookie of its own");

	granite_sampler_free(&a, DEV, &table);
	CHECK(samplers_destroyed == 1 && destroyed == GRANITE_VK_FROM_BITS(VkSampler, 0x101) && a.sampler == VK_NULL_HANDLE &&
			a.cookie == 0, "freeing destroys it");
	granite_sampler_free(&a, DEV, &table);
	CHECK(samplers_destroyed == 1, "once");
	granite_sampler_free(&b, DEV, &table);
	CHECK(samplers_destroyed == 2 && destroyed == GRANITE_VK_FROM_BITS(VkSampler, 0x102), "the other one too");

	fail_create = true;
	a.sampler = GRANITE_VK_FROM_BITS(VkSampler, 0x999);
	a.cookie = 77;
	CHECK(!granite_sampler_init_linear_clamp(&a, DEV, &table) && a.sampler == VK_NULL_HANDLE && a.cookie == 0,
			"a driver that refuses leaves no sampler");
	granite_sampler_free(&a, DEV, &table);
	CHECK(samplers_destroyed == 2, "and nothing to destroy");

	if (failures)
	{
		fprintf(stderr, "test_vk_sampler: %u failure(s)\n", failures);
		return 1;
	}
	printf("test_vk_sampler: OK\n");
	return 0;
}
