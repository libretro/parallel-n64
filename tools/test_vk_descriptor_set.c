/* parallel-rdp's descriptor set allocator (vulkan/descriptor_set.c),
 * through a stand-in device table.
 *
 * The layout it makes has each binding the shader uses, of the kind its
 * mask says, with its array size, stages and immutable sampler; a push
 * layout says so and takes its uniform buffers without a dynamic offset.
 *
 * A set is asked for by the hash of its contents. The first time it is
 * one to write; asked for again it is the same set, already written. Sets
 * come sixteen to a pool. One that nothing asks for in eight frames is
 * written for nothing any more: it is handed out for the next new hash
 * instead of a new pool, and its old hash is then one to write again. One
 * asked for every frame stays. Each thread index has its own sets. A pool
 * or sets the driver refuses give no set and leave nothing behind. */
#include <stdio.h>
#include <string.h>
#include <boolean.h>

#include "descriptor_set.h"

static unsigned failures;

#define CHECK(cond, what) \
	do \
	{ \
		if (!(cond)) \
		{ \
			fprintf(stderr, "test_vk_descriptor_set: FAIL (line %d): %s\n", __LINE__, what); \
			failures++; \
		} \
	} while (0)

static VkDescriptorSetLayoutCreateInfo layout_info;
static VkDescriptorSetLayoutBinding layout_bindings[GRANITE_NUM_BINDINGS];
static VkSampler layout_samplers[GRANITE_NUM_BINDINGS];
static VkDescriptorPoolSize pool_sizes[GRANITE_NUM_BINDINGS];
static unsigned num_pool_sizes;
static unsigned layouts_created, layouts_destroyed, pools_created, pools_destroyed, sets_allocated;
static bool fail_layout, fail_pool, fail_sets;
static char set_storage[4096];

static VkResult VKAPI_CALL fake_create_layout(VkDevice d, const VkDescriptorSetLayoutCreateInfo *info,
		const VkAllocationCallbacks *a, VkDescriptorSetLayout *layout)
{
	uint32_t i;
	(void)d; (void)a;
	if (fail_layout)
		return VK_ERROR_OUT_OF_HOST_MEMORY;
	layout_info = *info;
	memset(layout_samplers, 0, sizeof(layout_samplers));
	for (i = 0; i < info->bindingCount; i++)
	{
		layout_bindings[i] = info->pBindings[i];
		if (info->pBindings[i].pImmutableSamplers)
			layout_samplers[i] = info->pBindings[i].pImmutableSamplers[0];
	}
	layouts_created++;
	*layout = (VkDescriptorSetLayout)(size_t)(0x100 + layouts_created);
	return VK_SUCCESS;
}

static void VKAPI_CALL fake_destroy_layout(VkDevice d, VkDescriptorSetLayout layout, const VkAllocationCallbacks *a)
{
	(void)d; (void)layout; (void)a;
	layouts_destroyed++;
}

static VkResult VKAPI_CALL fake_create_pool(VkDevice d, const VkDescriptorPoolCreateInfo *info,
		const VkAllocationCallbacks *a, VkDescriptorPool *pool)
{
	uint32_t i;
	(void)d; (void)a;
	if (fail_pool)
		return VK_ERROR_OUT_OF_DEVICE_MEMORY;
	CHECK(info->maxSets == GRANITE_NUM_SETS_PER_POOL, "a pool holds sixteen sets");
	num_pool_sizes = info->poolSizeCount;
	for (i = 0; i < info->poolSizeCount; i++)
		pool_sizes[i] = info->pPoolSizes[i];
	pools_created++;
	*pool = (VkDescriptorPool)(size_t)(0x200 + pools_created);
	return VK_SUCCESS;
}

static VkResult VKAPI_CALL fake_reset_pool(VkDevice d, VkDescriptorPool pool, VkDescriptorPoolResetFlags flags)
{
	(void)d; (void)pool; (void)flags;
	return VK_SUCCESS;
}

static void VKAPI_CALL fake_destroy_pool(VkDevice d, VkDescriptorPool pool, const VkAllocationCallbacks *a)
{
	(void)d; (void)pool; (void)a;
	pools_destroyed++;
}

static VkResult VKAPI_CALL fake_allocate_sets(VkDevice d, const VkDescriptorSetAllocateInfo *info, VkDescriptorSet *sets)
{
	uint32_t i;
	(void)d;
	if (fail_sets)
		return VK_ERROR_OUT_OF_POOL_MEMORY;
	CHECK(info->descriptorSetCount == GRANITE_NUM_SETS_PER_POOL, "sixteen at a time");
	for (i = 0; i < info->descriptorSetCount; i++)
	{
		CHECK(info->pSetLayouts[i] == (VkDescriptorSetLayout)(size_t)(0x100 + layouts_created), "of the layout");
		sets[i] = (VkDescriptorSet)(size_t)(set_storage + sets_allocated);
		sets_allocated++;
	}
	return VK_SUCCESS;
}

static const VkDescriptorSetLayoutBinding *binding(uint32_t n)
{
	uint32_t i;
	for (i = 0; i < layout_info.bindingCount; i++)
		if (layout_bindings[i].binding == n)
			return &layout_bindings[i];
	return NULL;
}

static VkSampler sampler_of(uint32_t n)
{
	uint32_t i;
	for (i = 0; i < layout_info.bindingCount; i++)
		if (layout_bindings[i].binding == n)
			return layout_samplers[i];
	return VK_NULL_HANDLE;
}

int main(void)
{
	struct VolkDeviceTable table;
	granite_set_allocator_t allocator;
	granite_descriptor_set_layout_t layout;
	uint32_t stages[GRANITE_NUM_BINDINGS];
	VkSampler samplers[GRANITE_NUM_BINDINGS];
	VkDescriptorSet first[40], set, kept;
	const VkDescriptorSetLayoutBinding *b;
	bool cached, distinct = true;
	unsigned i, j, frame;

	memset(&table, 0, sizeof(table));
	table.vkCreateDescriptorSetLayout = fake_create_layout;
	table.vkDestroyDescriptorSetLayout = fake_destroy_layout;
	table.vkCreateDescriptorPool = fake_create_pool;
	table.vkResetDescriptorPool = fake_reset_pool;
	table.vkDestroyDescriptorPool = fake_destroy_pool;
	table.vkAllocateDescriptorSets = fake_allocate_sets;

	/* ---- the layout ---- */
	memset(&layout, 0, sizeof(layout));
	memset(stages, 0, sizeof(stages));
	memset(samplers, 0, sizeof(samplers));
	layout.sampled_image_mask = 1u << 0;
	layout.uniform_buffer_mask = 1u << 1;
	layout.storage_buffer_mask = 1u << 2;
	layout.storage_image_mask = 1u << 3;
	layout.sampler_mask = 1u << 5;
	layout.separate_image_mask = 1u << 6;
	layout.sampled_texel_buffer_mask = 1u << 7;
	layout.storage_texel_buffer_mask = 1u << 8;
	layout.input_attachment_mask = 1u << 9;
	/* (in the layout, but no stage uses it) */
	layout.sampled_image_mask |= 1u << 20;
	layout.immutable_sampler_mask = (1u << 0) | (1u << 5);
	for (i = 0; i < GRANITE_NUM_BINDINGS; i++)
		layout.array_size[i] = 1;
	layout.array_size[3] = 4;
	for (i = 0; i < 10; i++)
		stages[i] = i == 4 ? 0 : (i & 1 ? VK_SHADER_STAGE_FRAGMENT_BIT : VK_SHADER_STAGE_COMPUTE_BIT);
	samplers[0] = (VkSampler)(size_t)0x51;
	samplers[5] = (VkSampler)(size_t)0x55;

	CHECK(granite_set_allocator_init(&allocator, (VkDevice)(size_t)1, &table, &layout, stages, samplers, false, 2)
			&& allocator.set_layout != VK_NULL_HANDLE, "the layout is made");
	CHECK(layout_info.bindingCount == 9 && layout_info.flags == 0, "of the nine bindings a stage uses");
	CHECK((b = binding(0)) && b->descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
			&& b->stageFlags == VK_SHADER_STAGE_COMPUTE_BIT && b->descriptorCount == 1
			&& sampler_of(0) == (VkSampler)(size_t)0x51, "a sampled image, with its immutable sampler");
	CHECK((b = binding(1)) && b->descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC
			&& b->stageFlags == VK_SHADER_STAGE_FRAGMENT_BIT, "a uniform buffer, its offset dynamic");
	CHECK((b = binding(2)) && b->descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, "a storage buffer");
	CHECK((b = binding(3)) && b->descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE && b->descriptorCount == 4,
			"an array of four storage images");
	CHECK(binding(4) == NULL && binding(20) == NULL, "none for a binding no stage uses");
	CHECK((b = binding(5)) && b->descriptorType == VK_DESCRIPTOR_TYPE_SAMPLER && sampler_of(5) == (VkSampler)(size_t)0x55,
			"a sampler, immutable");
	CHECK((b = binding(6)) && b->descriptorType == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE && sampler_of(6) == VK_NULL_HANDLE,
			"a separate image");
	CHECK((b = binding(7)) && b->descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER
			&& (b = binding(8)) && b->descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER
			&& (b = binding(9)) && b->descriptorType == VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,
			"texel buffers and an input attachment");

	/* ---- sets ---- */
	set = granite_set_allocator_find(&allocator, 0, 1000, &cached);
	CHECK(set != VK_NULL_HANDLE && !cached && pools_created == 1 && sets_allocated == 16,
			"the first set asked for is one to write, from a new pool");
	CHECK(num_pool_sizes == 9 && pool_sizes[3].type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE
			&& pool_sizes[3].descriptorCount == 4 * 16 && pool_sizes[1].type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC
			&& pool_sizes[1].descriptorCount == 16, "with room for sixteen sets' descriptors");
	CHECK(granite_set_allocator_find(&allocator, 0, 1000, &cached) == set && cached,
			"asked for again, it is the same set, written");
	first[0] = set;
	for (i = 1; i < 40; i++)
	{
		first[i] = granite_set_allocator_find(&allocator, 0, 1000 + i, &cached);
		if (cached || first[i] == VK_NULL_HANDLE)
			distinct = false;
		for (j = 0; j < i; j++)
			if (first[i] == first[j])
				distinct = false;
	}
	CHECK(distinct && pools_created == 3 && sets_allocated == 48, "forty hashes: forty sets, from three pools");
	distinct = true;
	for (i = 0; i < 40; i++)
		if (granite_set_allocator_find(&allocator, 0, 1000 + i, &cached) != first[i] || !cached)
			distinct = false;
	CHECK(distinct, "each its own again");

	/* another thread index has its own */
	set = granite_set_allocator_find(&allocator, 1, 1000, &cached);
	CHECK(set != VK_NULL_HANDLE && !cached && set != first[0] && pools_created == 4,
			"another thread's set for the same hash is another, to write");
	CHECK(granite_set_allocator_find(&allocator, 2, 1000, &cached) == VK_NULL_HANDLE && !cached,
			"a thread index there is not gets none");

	/* ---- eight frames ---- */
	kept = first[7];
	for (frame = 0; frame < GRANITE_DESCRIPTOR_RING_SIZE - 1; frame++)
	{
		granite_set_allocator_begin_frame(&allocator);
		CHECK(granite_set_allocator_find(&allocator, 0, 1007, &cached) == kept && cached,
				"a set asked for every frame stays as it is");
	}
	CHECK(granite_set_allocator_find(&allocator, 0, 1003, &cached) == first[3] && cached,
			"one not asked for in seven frames is still there");
	granite_set_allocator_begin_frame(&allocator);
	CHECK(granite_set_allocator_find(&allocator, 0, 1007, &cached) == kept && cached
			&& granite_set_allocator_find(&allocator, 0, 1003, &cached) == first[3] && cached,
			"and, asked for then, stays");
	set = granite_set_allocator_find(&allocator, 0, 1005, &cached);
	CHECK(set != VK_NULL_HANDLE && !cached, "one not asked for in eight is to be written again");
	j = pools_created;
	distinct = true;
	/* (38 forgotten less the one just taken, and the 8 of the third pool
	 * never handed out) */
	for (i = 0; i < 45; i++)
	{
		set = granite_set_allocator_find(&allocator, 0, 5000 + i, &cached);
		if (cached || set == VK_NULL_HANDLE || set == kept || set == first[3])
			distinct = false;
	}
	CHECK(distinct && pools_created == j, "the sets of the forgotten hashes are handed out for new ones: no new pool");
	set = granite_set_allocator_find(&allocator, 0, 6000, &cached);
	CHECK(set != VK_NULL_HANDLE && !cached && pools_created == j + 1, "and when they are used up, a new pool");

	/* ---- what the driver refuses ---- */
	granite_set_allocator_clear(&allocator);
	CHECK(pools_destroyed == pools_created, "a clear destroys the pools");
	fail_pool = true;
	j = pools_created;
	CHECK(granite_set_allocator_find(&allocator, 0, 1, &cached) == VK_NULL_HANDLE && !cached && pools_created == j,
			"no pool: no set");
	fail_pool = false;
	fail_sets = true;
	CHECK(granite_set_allocator_find(&allocator, 0, 1, &cached) == VK_NULL_HANDLE && !cached
			&& pools_created == j + 1 && pools_destroyed == j + 1,
			"no sets from the pool: no set, and the pool is destroyed");
	fail_sets = false;
	set = granite_set_allocator_find(&allocator, 0, 1, &cached);
	CHECK(set != VK_NULL_HANDLE && !cached && granite_set_allocator_find(&allocator, 0, 1, &cached) == set && cached,
			"and it works once the driver does");

	granite_set_allocator_free(&allocator);
	CHECK(pools_destroyed == pools_created && layouts_destroyed == 1, "freed: the pools and the layout destroyed");
	granite_set_allocator_free(&allocator);
	CHECK(layouts_destroyed == 1, "once");

	/* ---- a push layout ---- */
	j = pools_created;
	CHECK(granite_set_allocator_init(&allocator, (VkDevice)(size_t)1, &table, &layout, stages, NULL, true, 2),
			"a push layout");
	CHECK(layout_info.flags == VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR
			&& (b = binding(1)) && b->descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
			&& sampler_of(0) == VK_NULL_HANDLE,
			"says so, and has its uniform buffers without a dynamic offset");
	CHECK(granite_set_allocator_find(&allocator, 0, 1, &cached) == VK_NULL_HANDLE && pools_created == j,
			"and no sets");
	granite_set_allocator_free(&allocator);

	fail_layout = true;
	CHECK(granite_set_allocator_init(&allocator, (VkDevice)(size_t)1, &table, &layout, stages, NULL, false, 1)
			&& allocator.set_layout == VK_NULL_HANDLE, "a layout the driver refuses is none");
	granite_set_allocator_free(&allocator);
	CHECK(layouts_destroyed == 2, "and none destroyed");

	if (failures)
	{
		fprintf(stderr, "test_vk_descriptor_set: %u failed\n", failures);
		return 1;
	}
	printf("test_vk_descriptor_set: ok\n");
	return 0;
}
