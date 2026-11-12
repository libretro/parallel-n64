/* parallel-rdp's framebuffer allocator (vulkan/framebuffer.c), through a
 * stand-in device table.
 *
 * A framebuffer is made for a hash with the views, size and render pass
 * given, one layer, and found by the hash from then on, as long as it is
 * asked for: one not asked for in eight frames is given to the destroy
 * callback and is no longer found. One asked for every frame stays. Nodes
 * are used again: after the first block, frames that retire as many as
 * they make allocate nothing, which the test sees as the same nodes
 * coming back. Clearing gives every framebuffer to the callback and
 * keeps the nodes. A framebuffer the driver refuses has no handle and is
 * not given to the callback. */
#include <stdio.h>
#include <string.h>
#include <boolean.h>

#include "framebuffer.h"

static unsigned failures;

#define CHECK(cond, what) \
	do \
	{ \
		if (!(cond)) \
		{ \
			fprintf(stderr, "test_vk_framebuffer: FAIL (line %d): %s\n", __LINE__, what); \
			failures++; \
		} \
	} while (0)

static VkFramebufferCreateInfo fb_info;
static VkImageView fb_views[9];
static unsigned created, destroyed;
static size_t destroyed_sum;
static bool fail_create;

static VkResult VKAPI_CALL fake_create(VkDevice d, const VkFramebufferCreateInfo *info,
		const VkAllocationCallbacks *a, VkFramebuffer *fb)
{
	uint32_t i;
	(void)a;
	CHECK(d == (VkDevice)(size_t)7, "the device is the allocator's");
	if (fail_create)
	{
		*fb = (VkFramebuffer)(size_t)0xdead;
		return VK_ERROR_OUT_OF_DEVICE_MEMORY;
	}
	fb_info = *info;
	for (i = 0; i < info->attachmentCount && i < 9; i++)
		fb_views[i] = info->pAttachments[i];
	created++;
	*fb = (VkFramebuffer)(size_t)(0x1000 + created);
	return VK_SUCCESS;
}

static void on_destroy(void *userdata, VkFramebuffer fb)
{
	CHECK(userdata == (void *)&destroyed, "the callback gets its userdata");
	CHECK(fb != VK_NULL_HANDLE && fb != (VkFramebuffer)(size_t)0xdead, "and a framebuffer that was made");
	destroyed++;
	destroyed_sum += (size_t)fb;
}

int main(void)
{
	static struct VolkDeviceTable table;
	static int owner_a, owner_b;
	granite_framebuffer_allocator_t alloc;
	granite_framebuffer_t *a, *b, *n, *first_nodes[40];
	VkImageView views[3];
	unsigned i, frame, reused;

	table.vkCreateFramebuffer = fake_create;
	views[0] = (VkImageView)(size_t)0x11;
	views[1] = (VkImageView)(size_t)0x22;
	views[2] = (VkImageView)(size_t)0x33;

	granite_framebuffer_allocator_init(&alloc, (VkDevice)(size_t)7, &table, on_destroy, &destroyed);
	CHECK(granite_framebuffer_allocator_find(&alloc, 100) == NULL, "an empty allocator finds nothing");

	a = granite_framebuffer_allocator_create(&alloc, 100, (VkRenderPass)(size_t)0x50, &owner_a, views, 3, 640, 480);
	CHECK(a && a->framebuffer == (VkFramebuffer)(size_t)0x1001 && a->width == 640 && a->height == 480 &&
			a->render_pass == &owner_a, "a framebuffer is made");
	CHECK(fb_info.sType == VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO && fb_info.renderPass == (VkRenderPass)(size_t)0x50 &&
			fb_info.attachmentCount == 3 && fb_info.width == 640 && fb_info.height == 480 && fb_info.layers == 1 &&
			fb_views[0] == views[0] && fb_views[2] == views[2], "of the views and size given, one layer");
	CHECK(granite_framebuffer_allocator_find(&alloc, 100) == a, "and found by its hash");
	/* hashes of one bucket, and ones that differ in the high word */
	b = granite_framebuffer_allocator_create(&alloc, 100 + GRANITE_FRAMEBUFFER_BUCKETS, (VkRenderPass)(size_t)0x51,
			&owner_b, views, 1, 320, 240);
	n = granite_framebuffer_allocator_create(&alloc, ((uint64_t)1 << 40) + 100, (VkRenderPass)(size_t)0x51,
			&owner_b, views, 1, 16, 16);
	CHECK(b != a && n != a && n != b, "other hashes get their own");
	CHECK(granite_framebuffer_allocator_find(&alloc, 100) == a &&
			granite_framebuffer_allocator_find(&alloc, 100 + GRANITE_FRAMEBUFFER_BUCKETS) == b &&
			granite_framebuffer_allocator_find(&alloc, ((uint64_t)1 << 40) + 100) == n &&
			b->render_pass == &owner_b && b->width == 320, "each found by its own hash");

	/* a is asked for every frame, b and n never again */
	for (frame = 1; frame < GRANITE_FRAMEBUFFER_RING_SIZE; frame++)
	{
		granite_framebuffer_allocator_begin_frame(&alloc);
		CHECK(granite_framebuffer_allocator_find(&alloc, 100) == a, "one asked for every frame stays");
		CHECK(destroyed == 0, "nothing goes before eight frames");
	}
	granite_framebuffer_allocator_begin_frame(&alloc);
	CHECK(destroyed == 2 && destroyed_sum == 0x1002 + 0x1003, "the two not asked for in eight frames go");
	CHECK(granite_framebuffer_allocator_find(&alloc, 100 + GRANITE_FRAMEBUFFER_BUCKETS) == NULL &&
			granite_framebuffer_allocator_find(&alloc, ((uint64_t)1 << 40) + 100) == NULL, "and are not found");
	CHECK(granite_framebuffer_allocator_find(&alloc, 100) == a && a->framebuffer == (VkFramebuffer)(size_t)0x1001,
			"the one asked for is still there");

	/* asked for in the frame before it would go: it stays eight more */
	for (frame = 0; frame < GRANITE_FRAMEBUFFER_RING_SIZE - 1; frame++)
		granite_framebuffer_allocator_begin_frame(&alloc);
	CHECK(granite_framebuffer_allocator_find(&alloc, 100) == a, "found in its last frame");
	for (frame = 0; frame < GRANITE_FRAMEBUFFER_RING_SIZE - 1; frame++)
		granite_framebuffer_allocator_begin_frame(&alloc);
	CHECK(destroyed == 2, "it stays for eight more");
	granite_framebuffer_allocator_begin_frame(&alloc);
	CHECK(destroyed == 3 && granite_framebuffer_allocator_find(&alloc, 100) == NULL, "and then goes");

	/* nodes are used again: more framebuffers than a block, twice */
	for (i = 0; i < 40; i++)
		first_nodes[i] = granite_framebuffer_allocator_create(&alloc, 1000 + i, (VkRenderPass)(size_t)0x50, &owner_a,
				views, 1, 8, 8);
	for (i = 0; i < 40; i++)
		CHECK(granite_framebuffer_allocator_find(&alloc, 1000 + i) == first_nodes[i], "forty framebuffers are kept apart");
	destroyed = 0;
	granite_framebuffer_allocator_clear(&alloc);
	CHECK(destroyed == 40, "clearing gives them all to the callback");
	CHECK(granite_framebuffer_allocator_find(&alloc, 1000) == NULL, "and none is found");
	reused = 0;
	for (i = 0; i < 40; i++)
	{
		unsigned j;
		n = granite_framebuffer_allocator_create(&alloc, 2000 + i, (VkRenderPass)(size_t)0x50, &owner_a, views, 1, 8, 8);
		for (j = 0; j < 40; j++)
			if (n == first_nodes[j])
				reused++;
	}
	CHECK(reused == 40, "the next forty use the same nodes");

	/* refused by the driver */
	destroyed = 0;
	granite_framebuffer_allocator_clear(&alloc);
	CHECK(destroyed == 40, "cleared again");
	fail_create = true;
	n = granite_framebuffer_allocator_create(&alloc, 3000, (VkRenderPass)(size_t)0x50, &owner_a, views, 1, 8, 8);
	CHECK(n && n->framebuffer == VK_NULL_HANDLE && n->width == 8 && n->render_pass == &owner_a,
			"a refused framebuffer has no handle");
	CHECK(granite_framebuffer_allocator_find(&alloc, 3000) == n, "and is kept, not asked of the driver again");
	fail_create = false;
	destroyed = 0;
	granite_framebuffer_allocator_free(&alloc);
	CHECK(destroyed == 0, "and is not given to the callback");
	CHECK(granite_framebuffer_allocator_find(&alloc, 3000) == NULL, "the freed allocator is empty");

	/* usable after a free, and without a callback */
	n = granite_framebuffer_allocator_create(&alloc, 1, (VkRenderPass)(size_t)0x50, &owner_a, views, 1, 8, 8);
	CHECK(n && granite_framebuffer_allocator_find(&alloc, 1) == n, "it makes framebuffers again");
	alloc.destroy = NULL;
	granite_framebuffer_allocator_free(&alloc);

	if (failures)
	{
		fprintf(stderr, "test_vk_framebuffer: %u failure(s)\n", failures);
		return 1;
	}
	printf("test_vk_framebuffer: OK\n");
	return 0;
}
