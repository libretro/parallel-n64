/* parallel-rdp's render targets (vulkan/render_target.c), with the real
 * render pass cache and framebuffer allocator through a stand-in device
 * table.
 *
 * A render pass description starts with no attachments, the whole
 * framebuffer and depth cleared to 1. What the pass is made from is its
 * views' formats, sample counts and layouts and its load, store and
 * clear flags. The clear values are those of the attachments it clears,
 * the depth one after the colour ones.
 *
 * The framebuffer is made of the views' render target views, as large
 * as the smallest of them, for the pass pipelines are made for; asked
 * for again in the same frame it is the same one, and other views or
 * another pass give another. */
#include <stdio.h>
#include <string.h>
#include <boolean.h>

#include "render_target.h"

static unsigned failures;

#define CHECK(cond, what) \
	do \
	{ \
		if (!(cond)) \
		{ \
			fprintf(stderr, "test_vk_render_target: FAIL (line %d): %s\n", __LINE__, what); \
			failures++; \
		} \
	} while (0)

#define DEV ((VkDevice)(size_t)1)
#define VIEW(n) ((VkImageView)(size_t)(0x700 + (n)))

static unsigned passes_created, passes_destroyed, framebuffers_created, framebuffers_destroyed;
static VkFramebufferCreateInfo fb_info;
static VkImageView fb_views[GRANITE_NUM_ATTACHMENTS + 1];

static VkResult VKAPI_CALL fake_create_render_pass(VkDevice d, const VkRenderPassCreateInfo *info,
		const VkAllocationCallbacks *a, VkRenderPass *pass)
{
	(void)d; (void)info; (void)a;
	passes_created++;
	*pass = (VkRenderPass)(size_t)(0x500 + passes_created);
	return VK_SUCCESS;
}

static void VKAPI_CALL fake_destroy_render_pass(VkDevice d, VkRenderPass pass, const VkAllocationCallbacks *a)
{
	(void)d; (void)pass; (void)a;
	passes_destroyed++;
}

static VkResult VKAPI_CALL fake_create_framebuffer(VkDevice d, const VkFramebufferCreateInfo *info,
		const VkAllocationCallbacks *a, VkFramebuffer *framebuffer)
{
	uint32_t i;
	(void)a;
	CHECK(d == DEV, "on the device given");
	fb_info = *info;
	for (i = 0; i < info->attachmentCount && i <= GRANITE_NUM_ATTACHMENTS; i++)
		fb_views[i] = info->pAttachments[i];
	framebuffers_created++;
	*framebuffer = (VkFramebuffer)(size_t)(0x600 + framebuffers_created);
	return VK_SUCCESS;
}

static void destroy_framebuffer(void *userdata, VkFramebuffer framebuffer)
{
	(void)userdata; (void)framebuffer;
	framebuffers_destroyed++;
}

/* an image and its view, as the device hands them out */
static void make_view(granite_image_t *image, granite_image_view_t *view, unsigned n, VkFormat format,
		uint32_t width, uint32_t height, VkSampleCountFlagBits samples, bool general)
{
	memset(image, 0, sizeof(*image));
	memset(view, 0, sizeof(*view));
	image->info.width = width;
	image->info.height = height;
	image->info.format = format;
	image->info.samples = samples;
	image->general_layout = general;
	view->cookie = 0x1000 + n * 16;
	view->views.view = VIEW(n);
	view->info.image = image;
	view->info.format = format;
}

int main(void)
{
	struct VolkDeviceTable table;
	granite_program_cache_t cache;
	granite_framebuffer_allocator_t allocator;
	granite_image_t color_image, color2_image, depth_image, small_image;
	granite_image_view_t color, color2, depth, small;
	granite_render_pass_info_t info;
	granite_render_pass_desc_t desc;
	VkClearValue values[GRANITE_NUM_ATTACHMENTS + 1];
	granite_cached_render_pass_t *pass, *compatible;
	const granite_framebuffer_t *fb, *fb2;
	unsigned count;

	memset(&table, 0, sizeof(table));
	table.vkCreateRenderPass = fake_create_render_pass;
	table.vkDestroyRenderPass = fake_destroy_render_pass;
	table.vkCreateFramebuffer = fake_create_framebuffer;
	granite_program_cache_init(&cache, DEV, &table, false, 0, 8, 1);
	granite_framebuffer_allocator_init(&allocator, DEV, &table, destroy_framebuffer, NULL);

	make_view(&color_image, &color, 1, VK_FORMAT_R8G8B8A8_UNORM, 640, 480, VK_SAMPLE_COUNT_1_BIT, false);
	make_view(&color2_image, &color2, 2, VK_FORMAT_R8_UINT, 640, 480, VK_SAMPLE_COUNT_4_BIT, true);
	make_view(&depth_image, &depth, 3, VK_FORMAT_D32_SFLOAT, 640, 480, VK_SAMPLE_COUNT_1_BIT, false);
	make_view(&small_image, &small, 4, VK_FORMAT_R8G8B8A8_UNORM, 320, 600, VK_SAMPLE_COUNT_1_BIT, false);

	/* ---- the description ---- */
	memset(&info, 0xff, sizeof(info));
	granite_render_pass_info_init(&info);
	CHECK(info.num_color_attachments == 0 && info.depth_stencil == NULL && info.color_attachments[0] == NULL
			&& info.op_flags == 0 && info.clear_attachments == 0 && info.load_attachments == 0
			&& info.store_attachments == 0, "a new description draws to nothing");
	CHECK(info.render_area.offset.x == 0 && info.render_area.offset.y == 0
			&& info.render_area.extent.width == UINT32_MAX && info.render_area.extent.height == UINT32_MAX,
			"over the whole framebuffer");
	CHECK(info.clear_depth_stencil.depth == 1.0f && info.clear_depth_stencil.stencil == 0, "with depth cleared to 1");

	info.num_color_attachments = 2;
	info.color_attachments[0] = &color;
	info.color_attachments[1] = &color2;
	info.depth_stencil = &depth;
	info.op_flags = GRANITE_RENDER_PASS_OP_CLEAR_DEPTH_STENCIL_BIT | GRANITE_RENDER_PASS_OP_STORE_DEPTH_STENCIL_BIT;
	info.clear_attachments = 1u << 1;
	info.load_attachments = 1u << 0;
	info.store_attachments = 3;
	memset(&desc, 0xff, sizeof(desc));
	granite_render_pass_info_desc(&info, &desc);
	CHECK(desc.num_color == 2 && desc.color[0].format == VK_FORMAT_R8G8B8A8_UNORM && desc.color[0].samples == 1
			&& !desc.color[0].general && !desc.color[0].transient
			&& desc.color[1].format == VK_FORMAT_R8_UINT && desc.color[1].samples == 4 && desc.color[1].general,
			"the pass is made from its views' formats, sample counts and layouts");
	CHECK(desc.depth_stencil.format == VK_FORMAT_D32_SFLOAT && desc.depth_stencil.samples == 1 && !desc.depth_stencil.general,
			"the depth view's too");
	CHECK(desc.op_flags == info.op_flags && desc.clear_attachments == 2 && desc.load_attachments == 1
			&& desc.store_attachments == 3, "and from what it loads, stores and clears");
	info.depth_stencil = NULL;
	granite_render_pass_info_desc(&info, &desc);
	CHECK(desc.depth_stencil.format == VK_FORMAT_UNDEFINED, "no depth view: no depth attachment");
	info.depth_stencil = &depth;

	/* ---- clear values ---- */
	info.clear_color[1].uint32[0] = 7;
	info.clear_depth_stencil.depth = 0.25f;
	memset(values, 0, sizeof(values));
	count = granite_render_pass_info_clear_values(&info, values);
	CHECK(count == 3 && values[1].color.uint32[0] == 7 && values[2].depthStencil.depth == 0.25f,
			"the cleared colour attachment's value at its index, the depth value after the colour ones");
	info.op_flags = GRANITE_RENDER_PASS_OP_STORE_DEPTH_STENCIL_BIT;
	CHECK(granite_render_pass_info_clear_values(&info, values) == 2, "depth not cleared: up to the last cleared colour");
	info.clear_attachments = 0;
	CHECK(granite_render_pass_info_clear_values(&info, values) == 0, "nothing cleared: no values");
	info.clear_attachments = 1;
	CHECK(granite_render_pass_info_clear_values(&info, values) == 1, "the first cleared: one value");
	info.op_flags = GRANITE_RENDER_PASS_OP_CLEAR_DEPTH_STENCIL_BIT;
	info.depth_stencil = NULL;
	CHECK(granite_render_pass_info_clear_values(&info, values) == 1, "no depth view: nothing of depth to clear");
	info.depth_stencil = &depth;

	/* ---- passes ---- */
	pass = granite_render_target_pass(&cache, &info, false);
	compatible = granite_render_target_pass(&cache, &info, true);
	granite_render_pass_info_desc(&info, &desc);
	CHECK(pass && compatible && pass->node.hash == granite_render_pass_hash(&desc, false)
			&& compatible->node.hash == granite_render_pass_hash(&desc, true), "a pass is its description's");
	CHECK(granite_render_target_pass(&cache, &info, false) == pass, "the same again");

	/* ---- framebuffers ---- */
	fb = granite_render_target_framebuffer(&allocator, &cache, &info);
	CHECK(fb && fb->framebuffer != VK_NULL_HANDLE && framebuffers_created == 1, "a framebuffer is made");
	CHECK(fb->render_pass == compatible && fb_info.renderPass == compatible->impl.render_pass,
			"for the pass pipelines are made for");
	CHECK(fb_info.attachmentCount == 3 && fb_views[0] == VIEW(1) && fb_views[1] == VIEW(2) && fb_views[2] == VIEW(3),
			"of the views, depth last");
	CHECK(fb->width == 640 && fb->height == 480 && fb_info.width == 640 && fb_info.height == 480 && fb_info.layers == 1,
			"as large as they are");
	CHECK(granite_render_target_framebuffer(&allocator, &cache, &info) == fb && framebuffers_created == 1,
			"asked for again it is the same one");

	/* what the pass does with its attachments does not change it */
	info.clear_attachments = 3;
	info.load_attachments = 0;
	CHECK(granite_render_target_framebuffer(&allocator, &cache, &info) == fb && framebuffers_created == 1,
			"nor does a pass that clears instead of loading need another");

	/* other views */
	info.color_attachments[0] = &small;
	fb2 = granite_render_target_framebuffer(&allocator, &cache, &info);
	CHECK(fb2 && fb2 != fb && framebuffers_created == 2 && fb_views[0] == VIEW(4), "another view: another framebuffer");
	CHECK(fb2->width == 320 && fb2->height == 480, "as large as the smallest view each way");
	info.color_attachments[0] = &color;

	/* another depth view, and the same views in another pass */
	{
		granite_image_t depth2_image;
		granite_image_view_t depth2;
		unsigned before = framebuffers_created;

		make_view(&depth2_image, &depth2, 9, VK_FORMAT_D32_SFLOAT, 640, 480, VK_SAMPLE_COUNT_1_BIT, false);
		info.depth_stencil = &depth2;
		fb2 = granite_render_target_framebuffer(&allocator, &cache, &info);
		CHECK(fb2 != fb && framebuffers_created == before + 1 && fb_views[2] == VIEW(9),
				"another depth view: another framebuffer");
		info.depth_stencil = &depth;

		color2_image.info.samples = VK_SAMPLE_COUNT_1_BIT;
		fb2 = granite_render_target_framebuffer(&allocator, &cache, &info);
		CHECK(fb2 != fb && framebuffers_created == before + 2 && fb2->render_pass != fb->render_pass,
				"the same views in a pass that is not compatible: another framebuffer");
		color2_image.info.samples = VK_SAMPLE_COUNT_4_BIT;
		CHECK(granite_render_target_framebuffer(&allocator, &cache, &info) == fb, "and the first one is still there");
	}

	/* a level of a view */
	color.info.base_level = 2;
	fb2 = granite_render_target_framebuffer(&allocator, &cache, &info);
	CHECK(fb2 == fb, "(the same views are the same framebuffer whatever their level says now)");
	color.info.base_level = 0;
	small.info.base_level = 1;
	small.cookie += 16;
	info.color_attachments[0] = &small;
	fb2 = granite_render_target_framebuffer(&allocator, &cache, &info);
	CHECK(fb2->width == 160 && fb2->height == 300, "a view of a smaller level is that level's size");
	info.color_attachments[0] = &color;

	/* without depth */
	info.depth_stencil = NULL;
	fb2 = granite_render_target_framebuffer(&allocator, &cache, &info);
	CHECK(fb2 != fb && fb_info.attachmentCount == 2 && fb_views[0] == VIEW(1) && fb_views[1] == VIEW(2),
			"no depth view: a framebuffer of the colour views");

	/* a layer view */
	color.views.num_layer_views = 2;
	color.views.layer_views[0] = VIEW(50);
	color.cookie += 16;
	fb2 = granite_render_target_framebuffer(&allocator, &cache, &info);
	CHECK(fb_views[0] == VIEW(50), "a view with layer views draws to its first layer");

	granite_framebuffer_allocator_free(&allocator);
	CHECK(framebuffers_destroyed == framebuffers_created, "every framebuffer goes back");
	granite_program_cache_free(&cache, NULL, NULL);
	CHECK(passes_destroyed == passes_created && passes_created >= 2, "and every pass");

	if (failures)
	{
		fprintf(stderr, "test_vk_render_target: %u failure(s)\n", failures);
		return 1;
	}
	printf("test_vk_render_target: ok\n");
	return 0;
}
