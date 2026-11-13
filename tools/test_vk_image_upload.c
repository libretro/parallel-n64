/* parallel-rdp's image upload (vulkan/image_upload.c).
 *
 * An image created with texels gets them through a staging buffer:
 * granite_image_upload_fill() packs the caller's rows into it, level
 * after level and layer after layer, dropping the padding of sources
 * that are wider than the level.
 *
 * granite_image_upload_plan() says what commands bring the new image to
 * its first layout: nothing at all for an image left undefined, one
 * layout change for an empty one, and for one with texels a copy on the
 * graphics queue, or on the transfer queue with a semaphore, plus a
 * change of owner when that queue is of another family and the image is
 * not shared. An image the compute queue shares makes that queue wait
 * for the commands; a shared one otherwise flushes the graphics queue. */
#include <stdio.h>
#include <string.h>
#include <boolean.h>

#include "image_upload.h"
#include "frame_garbage.h" /* GRANITE_VK_FROM_BITS */

static unsigned failures;

#define CHECK(cond, what) \
	do \
	{ \
		if (!(cond)) \
		{ \
			fprintf(stderr, "test_vk_image_upload: FAIL (line %d): %s\n", __LINE__, what); \
			failures++; \
		} \
	} while (0)

#define Q(n) GRANITE_VK_FROM_BITS(VkQueue, n)
#define IMAGE GRANITE_VK_FROM_BITS(VkImage, 0x77)
#define STAGES (VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT)
#define ACCESS (VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT)

static void test_fill(void)
{
	granite_texture_layout_t layout;
	granite_image_initial_data_t initial[4];
	uint8_t buffer[256];
	/* level 0 is 4x2 texels of 4 bytes: two layers, the second from a
	 * source 6 texels wide; level 1 is 2x1 */
	uint8_t l0a[4 * 2 * 4], l0b[6 * 2 * 4], l1a[2 * 4], l1b[2 * 4];
	unsigned i, y;

	for (i = 0; i < sizeof(l0a); i++)
		l0a[i] = (uint8_t)(0x10 + i);
	for (i = 0; i < sizeof(l0b); i++)
		l0b[i] = (uint8_t)(0x80 + i);
	for (i = 0; i < sizeof(l1a); i++)
	{
		l1a[i] = (uint8_t)(0x40 + i);
		l1b[i] = (uint8_t)(0x60 + i);
	}

	granite_texture_layout_set(&layout, VK_IMAGE_TYPE_2D, VK_FORMAT_R8G8B8A8_UNORM, 4, 2, 1, 2, 2);
	CHECK(layout.mip_levels == 2 && layout.array_layers == 2 && layout.required_size + 1 <= sizeof(buffer),
			"the layout is not the one this test is written for");
	memset(buffer, 0xee, sizeof(buffer));
	layout.buffer = buffer;

	memset(initial, 0, sizeof(initial));
	initial[0].data = l0a;
	initial[1].data = l0b;
	initial[1].row_length = 6;
	initial[2].data = l1a;
	initial[3].data = l1b;
	granite_image_upload_fill(&layout, initial);

	CHECK(!memcmp(granite_texture_layout_data(&layout, 0, 0), l0a, sizeof(l0a)), "level 0, layer 0 was not copied as it is");
	for (y = 0; y < 2; y++)
		CHECK(!memcmp(granite_texture_layout_data(&layout, 1, 0) + y * 16, l0b + y * 24, 16),
				"a wider source's rows were not taken by its own row length");
	CHECK(!memcmp(granite_texture_layout_data(&layout, 0, 1), l1a, sizeof(l1a)), "level 1, layer 0 is not its own data");
	CHECK(!memcmp(granite_texture_layout_data(&layout, 1, 1), l1b, sizeof(l1b)), "level 1, layer 1 is not its own data");
	CHECK(buffer[layout.required_size] == 0xee, "the fill wrote past the layout");
}

static granite_image_upload_queues_t one_queue(void)
{
	granite_image_upload_queues_t q;
	q.graphics = q.compute = q.transfer = Q(1);
	q.graphics_family = q.compute_family = q.transfer_family = 3;
	return q;
}

static granite_image_upload_desc_t sampled(bool has_data)
{
	granite_image_upload_desc_t d;
	memset(&d, 0, sizeof(d));
	d.image = IMAGE;
	d.format = VK_FORMAT_R8G8B8A8_UNORM;
	d.levels = 3;
	d.layers = 2;
	d.initial_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	d.stages = STAGES;
	d.access = ACCESS;
	d.has_data = has_data;
	return d;
}

static void test_plan_empty(void)
{
	granite_image_upload_queues_t q = one_queue();
	granite_image_upload_desc_t d = sampled(false);
	granite_image_upload_plan_t p;

	d.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
	d.concurrent = d.on_compute = true;
	q.compute = Q(2);
	memset(&p, 0xff, sizeof(p));
	granite_image_upload_plan(&q, &d, &p);
	CHECK(!p.transition && !p.final_barrier && !p.split_transfer && !p.hand_over && !p.share_compute && !p.flush_graphics,
			"an image left undefined needs no commands");

	q = one_queue();
	d = sampled(false);
	granite_image_upload_plan(&q, &d, &p);
	CHECK(p.transition && p.final_barrier && !p.split_transfer && !p.hand_over, "an empty image takes one layout change");
	CHECK(p.old_layout == VK_IMAGE_LAYOUT_UNDEFINED && p.src_stage == VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT && p.src_access == 0,
			"an empty image's layout change waits for nothing");
	CHECK(p.dst_access == VK_ACCESS_SHADER_READ_BIT, "the layout change is for what the image can do in its layout");
	CHECK(!p.share_compute && !p.flush_graphics, "an image of one queue needs no flush");

	d.concurrent = true;
	granite_image_upload_plan(&q, &d, &p);
	CHECK(!p.share_compute && p.flush_graphics, "a shared image is flushed to the graphics queue");

	d.initial_layout = VK_IMAGE_LAYOUT_GENERAL;
	granite_image_upload_plan(&q, &d, &p);
	CHECK(p.dst_access == ACCESS, "a general layout allows all the image's access");
}

static void test_plan_data(void)
{
	granite_image_upload_queues_t q = one_queue();
	granite_image_upload_desc_t d = sampled(true);
	granite_image_upload_plan_t p;

	granite_image_upload_plan(&q, &d, &p);
	CHECK(p.transition && !p.split_transfer && !p.hand_over && p.final_barrier, "one queue copies and changes layout itself");
	CHECK(p.old_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && p.src_stage == VK_PIPELINE_STAGE_TRANSFER_BIT
			&& p.src_access == VK_ACCESS_TRANSFER_WRITE_BIT && p.dst_access == VK_ACCESS_SHADER_READ_BIT,
			"the layout change follows the copy's write");
	CHECK(!p.share_compute && !p.flush_graphics, "nothing is shared");

	/* a transfer queue of the same family */
	q.transfer = Q(2);
	granite_image_upload_plan(&q, &d, &p);
	CHECK(p.split_transfer && p.wait_stages == STAGES, "another queue copies, and graphics waits with the image's stages");
	CHECK(!p.hand_over && p.final_barrier && p.src_access == 0 && p.old_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			"within a family the semaphore is all the layout change waits for");

	/* of another family */
	q.transfer_family = 5;
	granite_image_upload_plan(&q, &d, &p);
	CHECK(p.split_transfer && p.hand_over && !p.final_barrier, "another family hands the image over, layout included");
	CHECK(p.release.sType == VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER && p.release.image == IMAGE
			&& p.release.srcQueueFamilyIndex == 5 && p.release.dstQueueFamilyIndex == 3
			&& p.release.oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
			&& p.release.newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			&& p.release.srcAccessMask == VK_ACCESS_TRANSFER_WRITE_BIT && p.release.dstAccessMask == 0,
			"the release is not from the transfer family after its write");
	CHECK(p.release.subresourceRange.aspectMask == VK_IMAGE_ASPECT_COLOR_BIT
			&& p.release.subresourceRange.baseMipLevel == 0 && p.release.subresourceRange.levelCount == 3
			&& p.release.subresourceRange.baseArrayLayer == 0 && p.release.subresourceRange.layerCount == 2,
			"the release does not cover the whole image");
	CHECK(p.acquire.srcAccessMask == 0 && p.acquire.dstAccessMask == VK_ACCESS_SHADER_READ_BIT, "the acquire is not for the image's use");
	p.acquire.srcAccessMask = p.release.srcAccessMask;
	p.acquire.dstAccessMask = p.release.dstAccessMask;
	CHECK(!memcmp(&p.acquire, &p.release, sizeof(p.release)), "release and acquire differ in more than access");

	/* a shared image changes no owner */
	d.concurrent = true;
	granite_image_upload_plan(&q, &d, &p);
	CHECK(p.split_transfer && !p.hand_over && p.final_barrier && p.flush_graphics, "a shared image is not handed over");
}

static void test_plan_compute(void)
{
	granite_image_upload_queues_t q = one_queue();
	granite_image_upload_desc_t d = sampled(true);
	granite_image_upload_plan_t p;

	d.concurrent = d.on_compute = true;
	granite_image_upload_plan(&q, &d, &p);
	CHECK(!p.share_compute && p.flush_graphics, "compute on the graphics queue itself waits for nothing");

	q.compute = Q(4);
	granite_image_upload_plan(&q, &d, &p);
	CHECK(p.share_compute && !p.flush_graphics && p.compute_stages == STAGES,
			"a compute queue of the same family waits with all the image's stages");

	q.compute_family = 9;
	granite_image_upload_plan(&q, &d, &p);
	CHECK(p.share_compute && p.compute_stages == (VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT),
			"a compute family waits only with the stages it has");

	d.on_compute = false;
	granite_image_upload_plan(&q, &d, &p);
	CHECK(!p.share_compute && p.flush_graphics, "an image compute does not share makes it wait for nothing");
}

int main(void)
{
	test_fill();
	test_plan_empty();
	test_plan_data();
	test_plan_compute();

	if (failures)
	{
		fprintf(stderr, "test_vk_image_upload: %u failure(s)\n", failures);
		return 1;
	}
	printf("test_vk_image_upload: ok\n");
	return 0;
}
