/* parallel-rdp's resource bindings (vulkan/resource_bindings.c) with the
 * real descriptor set allocator, through a stand-in device table.
 *
 * Binding the resource that is bound already dirties nothing; another
 * one dirties its set. Another offset into the same uniform buffer only
 * asks for the set to be bound again.
 *
 * Flushing binds the dirty sets the layout uses and no others. A set
 * with new contents is written through its update template; the same
 * contents again get the set written before, not written again. The
 * uniform buffer offsets go along as dynamic offsets, in binding order.
 * A set whose offsets alone changed is bound again as it is, with no set
 * looked up or written. A push set is pushed with its offsets in the
 * descriptors, which are zero again afterwards.
 *
 * The hash of a set has every kind of binding the layout has, and only
 * those. A change of layout dirties the sets from the first whose layout
 * differs, all of them when the push constants differ, none for the same
 * layout. A set that cannot be had stays dirty. */
#include <stdio.h>
#include <string.h>
#include <boolean.h>

#include "resource_bindings.h"

static unsigned failures;

#define CHECK(cond, what) \
	do \
	{ \
		if (!(cond)) \
		{ \
			fprintf(stderr, "test_vk_resource_bindings: FAIL (line %d): %s\n", __LINE__, what); \
			failures++; \
		} \
	} while (0)

static char set_storage[8192];
static unsigned layouts_created, sets_allocated;
static bool fail_sets;

static unsigned updates, binds, pushes;
static VkDescriptorSet updated_set, bound_set[GRANITE_NUM_DESCRIPTOR_SETS];
static VkDescriptorUpdateTemplate updated_template, pushed_template;
static const void *updated_data;
static uint32_t bound_first, bound_offset_count, bound_offsets[GRANITE_NUM_BINDINGS];
static VkPipelineBindPoint bound_point;
static VkPipelineLayout bound_layout;
static VkDeviceSize pushed_offsets[2];
static uint32_t pushed_set;

static VkResult VKAPI_CALL fake_create_layout(VkDevice d, const VkDescriptorSetLayoutCreateInfo *info,
		const VkAllocationCallbacks *a, VkDescriptorSetLayout *layout)
{
	(void)d; (void)info; (void)a;
	layouts_created++;
	*layout = (VkDescriptorSetLayout)(size_t)(0x100 + layouts_created);
	return VK_SUCCESS;
}

static void VKAPI_CALL fake_destroy_layout(VkDevice d, VkDescriptorSetLayout layout, const VkAllocationCallbacks *a)
{
	(void)d; (void)layout; (void)a;
}

static VkResult VKAPI_CALL fake_create_pool(VkDevice d, const VkDescriptorPoolCreateInfo *info,
		const VkAllocationCallbacks *a, VkDescriptorPool *pool)
{
	(void)d; (void)info; (void)a;
	*pool = (VkDescriptorPool)(size_t)0x200;
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
}

static VkResult VKAPI_CALL fake_allocate_sets(VkDevice d, const VkDescriptorSetAllocateInfo *info, VkDescriptorSet *sets)
{
	uint32_t i;
	(void)d;
	if (fail_sets)
		return VK_ERROR_OUT_OF_POOL_MEMORY;
	for (i = 0; i < info->descriptorSetCount; i++)
		sets[i] = (VkDescriptorSet)(size_t)(set_storage + sets_allocated++);
	return VK_SUCCESS;
}

static void VKAPI_CALL fake_update(VkDevice d, VkDescriptorSet set, VkDescriptorUpdateTemplate templ, const void *data)
{
	CHECK(d == (VkDevice)(size_t)3, "written on the device given");
	updates++;
	updated_set = set;
	updated_template = templ;
	updated_data = data;
}

static void VKAPI_CALL fake_bind(VkCommandBuffer cmd, VkPipelineBindPoint point, VkPipelineLayout layout,
		uint32_t first, uint32_t count, const VkDescriptorSet *sets, uint32_t offset_count, const uint32_t *offsets)
{
	uint32_t i;
	CHECK(cmd == (VkCommandBuffer)(size_t)5 && count == 1, "one set a bind, into the command buffer given");
	binds++;
	bound_point = point;
	bound_layout = layout;
	bound_first = first;
	bound_set[first] = sets[0];
	bound_offset_count = offset_count;
	for (i = 0; i < offset_count; i++)
		bound_offsets[i] = offsets[i];
}

static void VKAPI_CALL fake_push(VkCommandBuffer cmd, VkDescriptorUpdateTemplate templ, VkPipelineLayout layout,
		uint32_t set, const void *data)
{
	const granite_resource_binding_t *b = (const granite_resource_binding_t *)data;
	(void)cmd; (void)layout;
	pushes++;
	pushed_template = templ;
	pushed_set = set;
	/* what the descriptors hold during the push */
	pushed_offsets[0] = b[0].u.buffer.offset;
	pushed_offsets[1] = b[3].u.buffer.offset;
}

#define DEVICE ((VkDevice)(size_t)3)
#define CMD    ((VkCommandBuffer)(size_t)5)
#define H(x)   ((size_t)(x))

static struct VolkDeviceTable table;
static granite_combined_layout_t combined;
static granite_set_allocator_t allocators[GRANITE_NUM_DESCRIPTOR_SETS];
static VkDescriptorUpdateTemplate templates[GRANITE_NUM_DESCRIPTOR_SETS];
static granite_bind_layout_t layout;
static granite_resource_bindings_t b;

/* set 0: uniform buffers at 0 and 3, a sampled image at 1; set 2: a
 * storage buffer at 0, a storage image at 1, a texel buffer at 2, a
 * separate image at 4 and a sampler at 5 */
static void setup(bool push)
{
	granite_resource_layout_t frag;
	unsigned i;

	memset(&frag, 0, sizeof(frag));
	frag.sets[0].uniform_buffer_mask = (1u << 0) | (1u << 3);
	frag.sets[0].sampled_image_mask = 1u << 1;
	frag.sets[0].fp_mask = 1u << 1;
	frag.sets[0].array_size[0] = frag.sets[0].array_size[1] = frag.sets[0].array_size[3] = 1;
	frag.sets[2].storage_buffer_mask = 1u << 0;
	frag.sets[2].storage_image_mask = 1u << 1;
	frag.sets[2].sampled_texel_buffer_mask = 1u << 2;
	frag.sets[2].separate_image_mask = 1u << 4;
	frag.sets[2].sampler_mask = 1u << 5;
	for (i = 0; i < 6; i++)
		frag.sets[2].array_size[i] = 1;
	granite_combined_layout_init(&combined);
	granite_combined_layout_add(&combined, GRANITE_SHADER_STAGE_FRAGMENT, &frag);
	granite_combined_layout_bake(&combined);

	memset(&layout, 0, sizeof(layout));
	for (i = 0; i < GRANITE_NUM_DESCRIPTOR_SETS; i++)
	{
		granite_set_allocator_init(&allocators[i], DEVICE, &table, &combined.sets[i], combined.stages_for_bindings[i],
				NULL, push && i == 0, 2);
		layout.allocators[i] = &allocators[i];
		templates[i] = (VkDescriptorUpdateTemplate)H(0x300 + i);
	}
	layout.hash = 1;
	layout.layout = (VkPipelineLayout)H(0x400);
	layout.resource_layout = &combined;
	layout.update_templates = templates;
	layout.push_set_mask = push ? 1 : 0;

	memset(&b, 0xee, sizeof(b));
	granite_bindings_init(&b);
	updates = binds = pushes = 0;
}

static void teardown(void)
{
	unsigned i;
	for (i = 0; i < GRANITE_NUM_DESCRIPTOR_SETS; i++)
		granite_set_allocator_free(&allocators[i]);
}

static void bind_all(void)
{
	granite_bindings_set_uniform_buffer(&b, 0, 0, (VkBuffer)H(0x10), 101, 64, 256);
	granite_bindings_set_uniform_buffer(&b, 0, 3, (VkBuffer)H(0x11), 102, 128, 512);
	granite_bindings_set_texture(&b, 0, 1, (VkImageView)H(0x20), (VkImageView)H(0x21),
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 103);
	granite_bindings_set_sampler(&b, 0, 1, (VkSampler)H(0x30), 104);
	granite_bindings_set_storage_buffer(&b, 2, 0, (VkBuffer)H(0x12), 105, 32, 96);
	granite_bindings_set_texture(&b, 2, 1, (VkImageView)H(0x22), (VkImageView)H(0x23), VK_IMAGE_LAYOUT_GENERAL, 106);
	granite_bindings_set_buffer_view(&b, 2, 2, (VkBufferView)H(0x40), 107);
	granite_bindings_set_texture(&b, 2, 4, (VkImageView)H(0x24), (VkImageView)H(0x25),
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 108);
	granite_bindings_set_sampler(&b, 2, 5, (VkSampler)H(0x31), 109);
}

static void flush(void)
{
	granite_bindings_flush(&b, &layout, DEVICE, &table, CMD, VK_PIPELINE_BIND_POINT_GRAPHICS, 1);
}

static void test_setters(void)
{
	setup(false);
	CHECK(b.dirty_sets == ~(uint32_t)0 && b.dirty_sets_dynamic == 0 && b.cookies[3][31] == 0 &&
			b.secondary_cookies[0][0] == 0 && b.push_constant_data[127] == 0 && b.allocated_sets[3] == VK_NULL_HANDLE,
			"new bindings: nothing bound, everything dirty");
	b.dirty_sets = 0;

	granite_bindings_set_uniform_buffer(&b, 1, 2, (VkBuffer)H(0x10), 7, 64, 256);
	CHECK(b.dirty_sets == 2 && b.dirty_sets_dynamic == 0, "a uniform buffer dirties its set");
	CHECK(b.bindings[1][2].u.buffer.buffer == (VkBuffer)H(0x10) && b.bindings[1][2].u.buffer.offset == 0 &&
			b.bindings[1][2].u.buffer.range == 256 && b.bindings[1][2].dynamic_offset == 64 && b.cookies[1][2] == 7,
			"its offset is kept apart, as a dynamic one");
	b.dirty_sets = 0;
	granite_bindings_set_uniform_buffer(&b, 1, 2, (VkBuffer)H(0x10), 7, 64, 256);
	CHECK(b.dirty_sets == 0 && b.dirty_sets_dynamic == 0, "the same again changes nothing");
	granite_bindings_set_uniform_buffer(&b, 1, 2, (VkBuffer)H(0x10), 7, 128, 256);
	CHECK(b.dirty_sets == 0 && b.dirty_sets_dynamic == 2 && b.bindings[1][2].dynamic_offset == 128,
			"another offset only asks for a rebind");
	b.dirty_sets_dynamic = 0;
	granite_bindings_set_uniform_buffer(&b, 1, 2, (VkBuffer)H(0x10), 7, 128, 512);
	CHECK(b.dirty_sets == 2, "another range dirties the set");
	b.dirty_sets = 0;
	granite_bindings_set_uniform_buffer(&b, 1, 2, (VkBuffer)H(0x11), 8, 128, 512);
	CHECK(b.dirty_sets == 2 && b.bindings[1][2].u.buffer.buffer == (VkBuffer)H(0x11), "so does another buffer");
	b.dirty_sets = 0;

	granite_bindings_set_storage_buffer(&b, 3, 0, (VkBuffer)H(0x12), 9, 32, 96);
	CHECK(b.dirty_sets == 8 && b.bindings[3][0].u.buffer.offset == 32 && b.bindings[3][0].u.buffer.range == 96 &&
			b.bindings[3][0].dynamic_offset == 0, "a storage buffer has its offset in the descriptor");
	b.dirty_sets = 0;
	granite_bindings_set_storage_buffer(&b, 3, 0, (VkBuffer)H(0x12), 9, 32, 96);
	CHECK(b.dirty_sets == 0, "the same again changes nothing");
	granite_bindings_set_storage_buffer(&b, 3, 0, (VkBuffer)H(0x12), 9, 48, 96);
	CHECK(b.dirty_sets == 8 && b.dirty_sets_dynamic == 0, "another offset dirties the set");
	b.dirty_sets = 0;

	granite_bindings_set_texture(&b, 0, 5, (VkImageView)H(0x20), (VkImageView)H(0x21),
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 11);
	CHECK(b.dirty_sets == 1 && b.bindings[0][5].u.image.fp.imageView == (VkImageView)H(0x20) &&
			b.bindings[0][5].u.image.integer.imageView == (VkImageView)H(0x21) &&
			b.bindings[0][5].u.image.integer.imageLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			"a texture has its float and integer views");
	b.dirty_sets = 0;
	granite_bindings_set_sampler(&b, 0, 5, (VkSampler)H(0x30), 12);
	CHECK(b.dirty_sets == 1 && b.bindings[0][5].u.image.fp.sampler == (VkSampler)H(0x30) &&
			b.bindings[0][5].u.image.integer.sampler == (VkSampler)H(0x30) &&
			b.bindings[0][5].u.image.fp.imageView == (VkImageView)H(0x20), "a sampler goes beside both, the views kept");
	b.dirty_sets = 0;
	granite_bindings_set_texture(&b, 0, 5, (VkImageView)H(0x20), (VkImageView)H(0x21),
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 11);
	granite_bindings_set_sampler(&b, 0, 5, (VkSampler)H(0x30), 12);
	CHECK(b.dirty_sets == 0, "the same texture and sampler again change nothing");
	granite_bindings_set_texture(&b, 0, 5, (VkImageView)H(0x20), (VkImageView)H(0x21), VK_IMAGE_LAYOUT_GENERAL, 11);
	CHECK(b.dirty_sets == 1, "the same texture in another layout dirties the set");
	CHECK(b.bindings[0][5].u.image.fp.sampler == (VkSampler)H(0x30) && b.secondary_cookies[0][5] == 12,
			"and keeps its sampler");
	b.dirty_sets = 0;

	granite_bindings_set_buffer_view(&b, 2, 9, (VkBufferView)H(0x40), 13);
	CHECK(b.dirty_sets == 4 && b.bindings[2][9].u.buffer_view == (VkBufferView)H(0x40), "a buffer view dirties its set");
	b.dirty_sets = 0;
	granite_bindings_set_buffer_view(&b, 2, 9, (VkBufferView)H(0x40), 13);
	CHECK(b.dirty_sets == 0, "the same again changes nothing");

	/* a buffer where a sampler was: the sampler is forgotten */
	granite_bindings_set_uniform_buffer(&b, 0, 5, (VkBuffer)H(0x10), 14, 0, 16);
	CHECK(b.secondary_cookies[0][5] == 0, "a buffer bound over a texture forgets its sampler");

	b.push_constant_data[5] = 9;
	granite_bindings_invalidate(&b);
	CHECK(b.cookies[1][2] == 0 && b.cookies[3][0] == 0 && b.dirty_sets == ~(uint32_t)0 && b.push_constant_data[5] == 9,
			"invalidating forgets what is bound and keeps the push constants");
	teardown();
}

static void test_flush(void)
{
	VkDescriptorSet first0, first2;

	setup(false);
	bind_all();
	flush();
	CHECK(updates == 2 && binds == 2 && pushes == 0, "two sets in use: two written and bound");
	CHECK(b.dirty_sets == ~(uint32_t)0x5 && b.dirty_sets_dynamic == 0, "they are clean, the unused ones not touched");
	CHECK(bound_set[0] != VK_NULL_HANDLE && bound_set[2] != VK_NULL_HANDLE && bound_set[0] != bound_set[2] &&
			b.allocated_sets[0] == bound_set[0] && b.allocated_sets[2] == bound_set[2], "each its own set");
	CHECK(updated_set == bound_set[2] && updated_template == templates[2] && updated_data == (const void *)b.bindings[2],
			"written through its template from its bindings");
	CHECK(bound_point == VK_PIPELINE_BIND_POINT_GRAPHICS && bound_layout == layout.layout && bound_first == 2 &&
			bound_offset_count == 0, "set 2 has no dynamic offsets");
	first0 = bound_set[0];
	first2 = bound_set[2];

	updates = binds = 0;
	flush();
	CHECK(updates == 0 && binds == 0, "nothing dirty, nothing done");

	/* only an offset */
	granite_bindings_set_uniform_buffer(&b, 0, 3, (VkBuffer)H(0x11), 102, 192, 512);
	flush();
	CHECK(updates == 0 && binds == 1 && bound_first == 0 && bound_set[0] == first0, "an offset alone: the same set bound again");
	CHECK(bound_offset_count == 2 && bound_offsets[0] == 64 && bound_offsets[1] == 192, "with the offsets, in binding order");
	CHECK(b.dirty_sets_dynamic == 0, "and it is clean");

	/* new contents, then the old ones again */
	updates = binds = 0;
	granite_bindings_set_texture(&b, 0, 1, (VkImageView)H(0x28), (VkImageView)H(0x29),
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 203);
	flush();
	CHECK(updates == 1 && binds == 1 && bound_set[0] != first0 && updated_set == bound_set[0] &&
			updated_template == templates[0], "new contents: another set, written");
	CHECK(bound_offset_count == 2 && bound_offsets[1] == 192, "bound with the offsets");
	updates = binds = 0;
	granite_bindings_set_texture(&b, 0, 1, (VkImageView)H(0x20), (VkImageView)H(0x21),
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 103);
	flush();
	CHECK(updates == 0 && binds == 1 && bound_set[0] == first0, "the old contents: the set written for them, not written again");

	/* contents and offset at once: one bind */
	updates = binds = 0;
	granite_bindings_set_uniform_buffer(&b, 0, 0, (VkBuffer)H(0x10), 101, 0, 256);
	granite_bindings_set_sampler(&b, 0, 1, (VkSampler)H(0x38), 204);
	flush();
	CHECK(updates == 1 && binds == 1 && bound_offsets[0] == 0 && b.dirty_sets_dynamic == 0,
			"contents and offset changed: one write, one bind");

	/* a set the layout does not use stays dirty and unbound */
	updates = binds = 0;
	granite_bindings_set_storage_buffer(&b, 1, 0, (VkBuffer)H(0x12), 300, 0, 16);
	granite_bindings_set_uniform_buffer(&b, 3, 0, (VkBuffer)H(0x12), 301, 0, 16);
	granite_bindings_set_uniform_buffer(&b, 3, 0, (VkBuffer)H(0x12), 301, 8, 16);
	flush();
	CHECK(updates == 0 && binds == 0 && (b.dirty_sets & 0xa) == 0xa && (b.dirty_sets_dynamic & 8),
			"sets the layout does not use are left alone");

	/* compute bind point, another thread's sets */
	updates = binds = 0;
	granite_bindings_set_storage_buffer(&b, 2, 0, (VkBuffer)H(0x12), 105, 64, 96);
	granite_bindings_flush(&b, &layout, DEVICE, &table, CMD, VK_PIPELINE_BIND_POINT_COMPUTE, 0);
	CHECK(updates == 1 && binds == 1 && bound_point == VK_PIPELINE_BIND_POINT_COMPUTE && bound_set[2] != first2,
			"bound for compute when asked");

	/* no set to be had: it stays dirty and is tried again */
	updates = binds = 0;
	fail_sets = true;
	granite_bindings_set_storage_buffer(&b, 2, 0, (VkBuffer)H(0x12), 105, 128, 96);
	granite_bindings_flush(&b, &layout, DEVICE, &table, CMD, VK_PIPELINE_BIND_POINT_COMPUTE, 0);
	/* (the thread's pool still has sets: use them up) */
	while (updates && updates < 64)
	{
		granite_bindings_set_storage_buffer(&b, 2, 0, (VkBuffer)H(0x12), 105, 128 + 16 * updates, 96);
		granite_bindings_flush(&b, &layout, DEVICE, &table, CMD, VK_PIPELINE_BIND_POINT_COMPUTE, 0);
		if (b.dirty_sets & 4)
			break;
	}
	CHECK(b.dirty_sets & 4, "a set that cannot be had stays dirty");
	updates = binds = 0;
	granite_bindings_flush(&b, &layout, DEVICE, &table, CMD, VK_PIPELINE_BIND_POINT_COMPUTE, 0);
	CHECK(updates == 0 && binds == 0 && (b.dirty_sets & 4), "and is neither written nor bound");
	fail_sets = false;
	granite_bindings_flush(&b, &layout, DEVICE, &table, CMD, VK_PIPELINE_BIND_POINT_COMPUTE, 0);
	CHECK(updates == 1 && binds == 1 && !(b.dirty_sets & 4), "until one can");
	teardown();
}

static void test_push(void)
{
	setup(true);
	bind_all();
	flush();
	CHECK(pushes == 1 && pushed_set == 0 && pushed_template == templates[0], "a push set is pushed through its template");
	CHECK(updates == 1 && binds == 1 && bound_first == 2, "the other set is written and bound");
	CHECK(pushed_offsets[0] == 64 && pushed_offsets[1] == 128, "the offsets are in the descriptors for the push");
	CHECK(b.bindings[0][0].u.buffer.offset == 0 && b.bindings[0][3].u.buffer.offset == 0, "and zero again after it");
	CHECK(b.allocated_sets[0] == VK_NULL_HANDLE, "it has no set");

	pushes = updates = binds = 0;
	granite_bindings_set_uniform_buffer(&b, 0, 3, (VkBuffer)H(0x11), 102, 192, 512);
	flush();
	CHECK(pushes == 1 && updates == 0 && binds == 0 && pushed_offsets[1] == 192 && b.dirty_sets_dynamic == 0,
			"another offset pushes it again");
	teardown();
}

static void test_hash(void)
{
	granite_resource_bindings_t c;
	const granite_descriptor_set_layout_t *s0, *s2;
	uint64_t h0, h2;

	setup(false);
	bind_all();
	s0 = &combined.sets[0];
	s2 = &combined.sets[2];
	h0 = granite_bindings_hash_set(&b, s0, 0);
	h2 = granite_bindings_hash_set(&b, s2, 2);
	CHECK(h0 != h2, "two sets, two hashes");

	c = b; c.cookies[0][0]++;
	CHECK(h0 != granite_bindings_hash_set(&c, s0, 0), "a uniform buffer is in the hash");
	c = b; c.bindings[0][3].u.buffer.range++;
	CHECK(h0 != granite_bindings_hash_set(&c, s0, 0), "with its range");
	c = b; c.bindings[0][3].dynamic_offset++;
	CHECK(h0 == granite_bindings_hash_set(&c, s0, 0), "but not its dynamic offset");
	c = b; c.cookies[0][1]++;
	CHECK(h0 != granite_bindings_hash_set(&c, s0, 0), "a sampled image is in the hash");
	c = b; c.secondary_cookies[0][1]++;
	CHECK(h0 != granite_bindings_hash_set(&c, s0, 0), "with its sampler");
	c = b; c.bindings[0][1].u.image.fp.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
	CHECK(h0 != granite_bindings_hash_set(&c, s0, 0), "and its layout");
	c = b; c.cookies[0][2]++; c.secondary_cookies[0][0]++; c.cookies[0][9]++;
	CHECK(h0 == granite_bindings_hash_set(&c, s0, 0), "bindings the set does not have are not");

	c = b; c.cookies[2][0]++;
	CHECK(h2 != granite_bindings_hash_set(&c, s2, 2), "a storage buffer is in the hash");
	c = b; c.bindings[2][0].u.buffer.offset++;
	CHECK(h2 != granite_bindings_hash_set(&c, s2, 2), "with its offset");
	c = b; c.bindings[2][0].u.buffer.range++;
	CHECK(h2 != granite_bindings_hash_set(&c, s2, 2), "and range");
	c = b; c.cookies[2][1]++;
	CHECK(h2 != granite_bindings_hash_set(&c, s2, 2), "a storage image");
	c = b; c.bindings[2][1].u.image.fp.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	CHECK(h2 != granite_bindings_hash_set(&c, s2, 2), "with its layout");
	c = b; c.cookies[2][2]++;
	CHECK(h2 != granite_bindings_hash_set(&c, s2, 2), "a texel buffer");
	c = b; c.cookies[2][4]++;
	CHECK(h2 != granite_bindings_hash_set(&c, s2, 2), "a separate image");
	c = b; c.secondary_cookies[2][4]++;
	CHECK(h2 == granite_bindings_hash_set(&c, s2, 2), "which has no sampler");
	c = b; c.secondary_cookies[2][5]++;
	CHECK(h2 != granite_bindings_hash_set(&c, s2, 2), "a separate sampler");
	c = b; c.cookies[2][5]++;
	CHECK(h2 == granite_bindings_hash_set(&c, s2, 2), "which has no image");
	teardown();
}

static void test_layout_change(void)
{
	granite_combined_layout_t other;
	granite_bind_layout_t next;

	setup(false);
	b.dirty_sets = 0;
	CHECK(granite_bindings_set_layout(&b, NULL, &layout) && b.dirty_sets == ~(uint32_t)0,
			"the first layout: every set and the push constants");
	b.dirty_sets = 0;
	CHECK(!granite_bindings_set_layout(&b, &layout, &layout) && b.dirty_sets == 0, "the same layout: nothing");

	other = combined;
	next = layout;
	next.hash = 2;
	next.resource_layout = &other;
	CHECK(!granite_bindings_set_layout(&b, &layout, &next) && b.dirty_sets == 0,
			"another layout of the same sets: nothing");
	next.allocators[2] = &allocators[3];
	CHECK(!granite_bindings_set_layout(&b, &layout, &next) && b.dirty_sets == ~(uint32_t)0x3,
			"another set 2: the sets from 2 on");
	b.dirty_sets = 0;
	next.allocators[0] = &allocators[1];
	CHECK(!granite_bindings_set_layout(&b, &layout, &next) && b.dirty_sets == ~(uint32_t)0, "another set 0: all of them");
	b.dirty_sets = 0;
	next = layout;
	next.hash = 3;
	next.resource_layout = &other;
	other.push_constant_layout_hash++;
	CHECK(granite_bindings_set_layout(&b, &layout, &next) && b.dirty_sets == ~(uint32_t)0,
			"other push constants: every set and the push constants");
	teardown();
}

int main(void)
{
	table.vkCreateDescriptorSetLayout = fake_create_layout;
	table.vkDestroyDescriptorSetLayout = fake_destroy_layout;
	table.vkCreateDescriptorPool = fake_create_pool;
	table.vkResetDescriptorPool = fake_reset_pool;
	table.vkDestroyDescriptorPool = fake_destroy_pool;
	table.vkAllocateDescriptorSets = fake_allocate_sets;
	table.vkUpdateDescriptorSetWithTemplate = fake_update;
	table.vkCmdBindDescriptorSets = fake_bind;
	table.vkCmdPushDescriptorSetWithTemplateKHR = fake_push;

	test_setters();
	test_flush();
	test_push();
	test_hash();
	test_layout_change();

	if (failures)
	{
		fprintf(stderr, "test_vk_resource_bindings: %u failure(s)\n", failures);
		return 1;
	}
	printf("test_vk_resource_bindings: OK\n");
	return 0;
}
