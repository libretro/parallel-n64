/* parallel-rdp's render passes (vulkan/render_pass.c), through a stand-in
 * device table.
 *
 * A pass has one subpass that draws to all its attachments, the depth one
 * last. Each colour attachment is cleared, loaded or left undefined and
 * stored or discarded as its bits say, in the layout its image is kept
 * in; the depth one as the op flags say, its stencil with it when the
 * format has one, read-only when asked. A transient attachment is neither
 * loaded nor stored unless the flags allow it and comes in undefined; a
 * swapchain image comes in as it was presented when loaded and goes out
 * ready to present. A pass that changes a layout on the way in waits for
 * what came before, and one that does not has no dependency.
 *
 * The hash is the one in the pipeline records on disk: it is checked
 * against values worked out from the hash's definition. The compatible
 * hash ignores loads, stores and layouts; the sample count is in both.
 * A signature makes the description of a compatible pass with the same
 * compatible hash.
 *
 * A refused pass and a pass without attachments report it and leave no
 * handle. */
#include <stdio.h>
#include <string.h>
#include <boolean.h>

#include "render_pass.h"

static unsigned failures;

#define CHECK(cond, what) \
	do \
	{ \
		if (!(cond)) \
		{ \
			fprintf(stderr, "test_vk_render_pass: FAIL (line %d): %s\n", __LINE__, what); \
			failures++; \
		} \
	} while (0)

static VkRenderPassCreateInfo rp_info;
static VkAttachmentDescription rp_att[GRANITE_NUM_ATTACHMENTS + 1];
static VkSubpassDescription rp_sub;
static VkAttachmentReference rp_colors[GRANITE_NUM_ATTACHMENTS];
static VkAttachmentReference rp_depth;
static VkSubpassDependency rp_dep;
static unsigned created, destroyed;
static bool fail_create;

static VkResult VKAPI_CALL fake_create(VkDevice d, const VkRenderPassCreateInfo *info,
		const VkAllocationCallbacks *a, VkRenderPass *pass)
{
	uint32_t i;
	(void)d; (void)a;
	if (fail_create)
	{
		/* a failed call leaves the handle undefined */
		*pass = (VkRenderPass)(size_t)0xdead;
		return VK_ERROR_OUT_OF_DEVICE_MEMORY;
	}
	rp_info = *info;
	for (i = 0; i < info->attachmentCount; i++)
		rp_att[i] = info->pAttachments[i];
	rp_sub = info->pSubpasses[0];
	for (i = 0; i < rp_sub.colorAttachmentCount; i++)
		rp_colors[i] = rp_sub.pColorAttachments[i];
	rp_depth = *rp_sub.pDepthStencilAttachment;
	memset(&rp_dep, 0, sizeof(rp_dep));
	if (info->dependencyCount)
		rp_dep = info->pDependencies[0];
	created++;
	*pass = (VkRenderPass)(size_t)(0x500 + created);
	return VK_SUCCESS;
}

static void VKAPI_CALL fake_destroy(VkDevice d, VkRenderPass pass, const VkAllocationCallbacks *a)
{
	(void)d; (void)a;
	CHECK(pass != (VkRenderPass)(size_t)0xdead, "an undefined handle is not destroyed");
	destroyed++;
}

static struct VolkDeviceTable table;
#define DEVICE ((VkDevice)(size_t)1)

static granite_attachment_desc_t image(VkFormat format)
{
	granite_attachment_desc_t a;
	memset(&a, 0, sizeof(a));
	a.format = format;
	a.samples = 1;
	a.swapchain_layout = VK_IMAGE_LAYOUT_UNDEFINED;
	return a;
}

static void test_color_depth(void)
{
	granite_render_pass_desc_t desc;
	granite_render_pass_t pass;

	memset(&desc, 0, sizeof(desc));
	desc.num_color = 2;
	desc.color[0] = image(VK_FORMAT_R8G8B8A8_UNORM);
	desc.color[1] = image(VK_FORMAT_R16G16_SFLOAT);
	desc.color[1].general = true;
	desc.depth_stencil = image(VK_FORMAT_D24_UNORM_S8_UINT);
	desc.clear_attachments = 1;
	desc.load_attachments = 2;
	desc.store_attachments = 1;
	desc.op_flags = GRANITE_RENDER_PASS_OP_CLEAR_DEPTH_STENCIL_BIT | GRANITE_RENDER_PASS_OP_STORE_DEPTH_STENCIL_BIT;

	CHECK(granite_render_pass_init(&pass, DEVICE, &table, &desc), "a pass is made");
	CHECK(pass.render_pass != VK_NULL_HANDLE && created == 1, "by the driver");
	CHECK(rp_info.sType == VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO && rp_info.attachmentCount == 3 &&
			rp_info.subpassCount == 1 && rp_info.pNext == NULL, "three attachments, one subpass");
	CHECK(rp_info.dependencyCount == 0, "no layout changes on the way in, no dependency");

	CHECK(rp_att[0].format == VK_FORMAT_R8G8B8A8_UNORM && rp_att[0].samples == VK_SAMPLE_COUNT_1_BIT &&
			rp_att[0].loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR && rp_att[0].storeOp == VK_ATTACHMENT_STORE_OP_STORE,
			"the first colour is cleared and stored");
	CHECK(rp_att[0].initialLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL &&
			rp_att[0].finalLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL &&
			rp_colors[0].layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL && rp_colors[0].attachment == 0,
			"in the colour layout throughout");
	CHECK(rp_att[0].stencilLoadOp == VK_ATTACHMENT_LOAD_OP_DONT_CARE &&
			rp_att[0].stencilStoreOp == VK_ATTACHMENT_STORE_OP_DONT_CARE && rp_att[0].flags == 0, "with no stencil");
	CHECK(rp_att[1].format == VK_FORMAT_R16G16_SFLOAT && rp_att[1].loadOp == VK_ATTACHMENT_LOAD_OP_LOAD &&
			rp_att[1].storeOp == VK_ATTACHMENT_STORE_OP_DONT_CARE, "the second is loaded and discarded");
	CHECK(rp_att[1].initialLayout == VK_IMAGE_LAYOUT_GENERAL && rp_att[1].finalLayout == VK_IMAGE_LAYOUT_GENERAL &&
			rp_colors[1].layout == VK_IMAGE_LAYOUT_GENERAL && rp_colors[1].attachment == 1,
			"in the general layout its image is kept in");
	CHECK(rp_att[2].format == VK_FORMAT_D24_UNORM_S8_UINT && rp_att[2].loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR &&
			rp_att[2].storeOp == VK_ATTACHMENT_STORE_OP_STORE && rp_att[2].stencilLoadOp == VK_ATTACHMENT_LOAD_OP_CLEAR &&
			rp_att[2].stencilStoreOp == VK_ATTACHMENT_STORE_OP_STORE, "depth and stencil are cleared and stored");
	CHECK(rp_att[2].initialLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL &&
			rp_att[2].finalLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL &&
			rp_depth.layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL && rp_depth.attachment == 2,
			"in the depth layout, the last attachment");
	CHECK(rp_sub.pipelineBindPoint == VK_PIPELINE_BIND_POINT_GRAPHICS && rp_sub.colorAttachmentCount == 2 &&
			rp_sub.inputAttachmentCount == 0 && rp_sub.pResolveAttachments == NULL &&
			rp_sub.preserveAttachmentCount == 0 && rp_sub.flags == 0, "the subpass draws to both colours");

	CHECK(pass.num_color == 2 && pass.samples == 1 && pass.color_formats[1] == VK_FORMAT_R16G16_SFLOAT &&
			pass.depth_stencil_format == VK_FORMAT_D24_UNORM_S8_UINT, "the pass knows its attachments");
	CHECK(granite_render_pass_has_depth(&pass) && granite_render_pass_has_stencil(&pass), "and its depth and stencil");
	CHECK(pass.signature.recordable == 1 && pass.signature.num_color == 2 &&
			pass.signature.formats[0] == VK_FORMAT_R8G8B8A8_UNORM && pass.signature.samples[1] == 1 &&
			pass.signature.depth_format == VK_FORMAT_D24_UNORM_S8_UINT &&
			pass.signature.samples[GRANITE_NUM_ATTACHMENTS] == 1 && pass.signature.lazy == 0 &&
			pass.signature.op_flags == desc.op_flags && pass.signature.num_subpasses == 0, "and its signature");

	granite_render_pass_free(&pass);
	CHECK(destroyed == 1 && pass.render_pass == VK_NULL_HANDLE, "and is destroyed");
	granite_render_pass_free(&pass);
	CHECK(destroyed == 1, "once");

	/* depth without stencil, loaded, read-only; nothing stored */
	desc.depth_stencil = image(VK_FORMAT_D32_SFLOAT);
	desc.op_flags = GRANITE_RENDER_PASS_OP_LOAD_DEPTH_STENCIL_BIT | GRANITE_RENDER_PASS_OP_DEPTH_STENCIL_READ_ONLY_BIT;
	CHECK(granite_render_pass_init(&pass, DEVICE, &table, &desc), "a pass with read-only depth");
	CHECK(rp_att[2].loadOp == VK_ATTACHMENT_LOAD_OP_LOAD && rp_att[2].storeOp == VK_ATTACHMENT_STORE_OP_DONT_CARE &&
			rp_att[2].stencilLoadOp == VK_ATTACHMENT_LOAD_OP_DONT_CARE &&
			rp_att[2].stencilStoreOp == VK_ATTACHMENT_STORE_OP_DONT_CARE, "loads depth alone and stores nothing");
	CHECK(rp_att[2].initialLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL &&
			rp_att[2].finalLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL &&
			rp_depth.layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, "in the read-only layout");
	CHECK(granite_render_pass_has_depth(&pass) && !granite_render_pass_has_stencil(&pass), "depth, no stencil");
	granite_render_pass_free(&pass);

	/* a general depth image stays general, read-only or not */
	desc.depth_stencil.general = true;
	CHECK(granite_render_pass_init(&pass, DEVICE, &table, &desc), "a pass with a general depth image");
	CHECK(rp_att[2].initialLayout == VK_IMAGE_LAYOUT_GENERAL && rp_att[2].finalLayout == VK_IMAGE_LAYOUT_GENERAL &&
			rp_depth.layout == VK_IMAGE_LAYOUT_GENERAL && rp_info.dependencyCount == 0, "keeps it general");
	granite_render_pass_free(&pass);

	/* colour only, neither cleared nor loaded */
	memset(&desc, 0, sizeof(desc));
	desc.num_color = 1;
	desc.color[0] = image(VK_FORMAT_R8G8B8A8_UNORM);
	desc.color[0].samples = 4;
	desc.store_attachments = 1;
	CHECK(granite_render_pass_init(&pass, DEVICE, &table, &desc), "a pass without depth");
	CHECK(rp_info.attachmentCount == 1 && rp_depth.attachment == VK_ATTACHMENT_UNUSED, "has no depth attachment");
	CHECK(rp_att[0].loadOp == VK_ATTACHMENT_LOAD_OP_DONT_CARE && rp_att[0].samples == VK_SAMPLE_COUNT_4_BIT &&
			pass.samples == 4, "its colour comes in undefined, with its samples");
	CHECK(!granite_render_pass_has_depth(&pass) && !granite_render_pass_has_stencil(&pass), "no depth, no stencil");
	granite_render_pass_free(&pass);

	/* depth only */
	memset(&desc, 0, sizeof(desc));
	desc.depth_stencil = image(VK_FORMAT_D16_UNORM);
	desc.depth_stencil.samples = 2;
	CHECK(granite_render_pass_init(&pass, DEVICE, &table, &desc), "a pass of depth alone");
	CHECK(rp_info.attachmentCount == 1 && rp_sub.colorAttachmentCount == 0 && rp_depth.attachment == 0 &&
			pass.samples == 2, "has it as its only attachment");
	granite_render_pass_free(&pass);
}

static void test_transient_swapchain(void)
{
	granite_render_pass_desc_t desc;
	granite_render_pass_t pass;

	/* transient colour and depth, load and store asked for but not
	 * allowed */
	memset(&desc, 0, sizeof(desc));
	desc.num_color = 1;
	desc.color[0] = image(VK_FORMAT_R8G8B8A8_UNORM);
	desc.color[0].transient = true;
	desc.depth_stencil = image(VK_FORMAT_D24_UNORM_S8_UINT);
	desc.depth_stencil.transient = true;
	desc.load_attachments = 1;
	desc.store_attachments = 1;
	desc.op_flags = GRANITE_RENDER_PASS_OP_LOAD_DEPTH_STENCIL_BIT | GRANITE_RENDER_PASS_OP_STORE_DEPTH_STENCIL_BIT;
	CHECK(granite_render_pass_init(&pass, DEVICE, &table, &desc), "a pass of transient attachments");
	CHECK(rp_att[0].loadOp == VK_ATTACHMENT_LOAD_OP_DONT_CARE && rp_att[0].storeOp == VK_ATTACHMENT_STORE_OP_DONT_CARE &&
			rp_att[0].initialLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
			rp_att[0].finalLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			"neither loads nor stores the colour, which comes in undefined");
	CHECK(rp_att[1].loadOp == VK_ATTACHMENT_LOAD_OP_DONT_CARE && rp_att[1].stencilLoadOp == VK_ATTACHMENT_LOAD_OP_DONT_CARE &&
			rp_att[1].storeOp == VK_ATTACHMENT_STORE_OP_DONT_CARE &&
			rp_att[1].stencilStoreOp == VK_ATTACHMENT_STORE_OP_DONT_CARE &&
			rp_att[1].initialLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
			rp_att[1].finalLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, "nor the depth");
	CHECK(rp_info.dependencyCount == 1 && rp_dep.srcSubpass == VK_SUBPASS_EXTERNAL && rp_dep.dstSubpass == 0 &&
			rp_dep.dependencyFlags == 0, "and waits for what came before");
	CHECK(rp_dep.srcStageMask == (VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT) &&
			rp_dep.dstStageMask == (VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
				VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT) &&
			rp_dep.srcAccessMask == (VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT) &&
			rp_dep.dstAccessMask == (VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
				VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT),
			"on colour and depth");
	CHECK(pass.signature.lazy == 3 && pass.signature.recordable == 1, "both are lazy in the signature");
	granite_render_pass_free(&pass);

	/* allowed */
	desc.op_flags |= GRANITE_RENDER_PASS_OP_ENABLE_TRANSIENT_LOAD_BIT | GRANITE_RENDER_PASS_OP_ENABLE_TRANSIENT_STORE_BIT;
	CHECK(granite_render_pass_init(&pass, DEVICE, &table, &desc), "a pass that may load and store them");
	CHECK(rp_att[0].loadOp == VK_ATTACHMENT_LOAD_OP_LOAD && rp_att[0].storeOp == VK_ATTACHMENT_STORE_OP_STORE &&
			rp_att[0].initialLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, "loads and stores the colour");
	CHECK(rp_att[1].loadOp == VK_ATTACHMENT_LOAD_OP_LOAD && rp_att[1].stencilLoadOp == VK_ATTACHMENT_LOAD_OP_LOAD &&
			rp_att[1].storeOp == VK_ATTACHMENT_STORE_OP_STORE && rp_att[1].stencilStoreOp == VK_ATTACHMENT_STORE_OP_STORE &&
			rp_att[1].initialLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, "and the depth");
	CHECK(rp_info.dependencyCount == 1, "and still waits");
	granite_render_pass_free(&pass);

	/* a transient depth alone waits on depth alone */
	desc.color[0].transient = false;
	desc.op_flags = 0;
	CHECK(granite_render_pass_init(&pass, DEVICE, &table, &desc), "a pass with a transient depth alone");
	CHECK(rp_info.dependencyCount == 1 && rp_dep.srcStageMask == VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT &&
			rp_dep.srcAccessMask == VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, "waits on depth alone");
	CHECK(rp_att[0].loadOp == VK_ATTACHMENT_LOAD_OP_LOAD, "and loads its colour");
	granite_render_pass_free(&pass);

	/* a swapchain image, cleared */
	memset(&desc, 0, sizeof(desc));
	desc.num_color = 1;
	desc.color[0] = image(VK_FORMAT_B8G8R8A8_SRGB);
	desc.color[0].swapchain_layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	desc.clear_attachments = 1;
	desc.store_attachments = 1;
	CHECK(granite_render_pass_init(&pass, DEVICE, &table, &desc), "a pass to a swapchain image");
	CHECK(rp_att[0].initialLayout == VK_IMAGE_LAYOUT_UNDEFINED && rp_att[0].finalLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR &&
			rp_colors[0].layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			"takes it undefined and leaves it ready to present");
	CHECK(rp_info.dependencyCount == 1 && rp_dep.srcStageMask == VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT &&
			rp_dep.dstStageMask == VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, "waiting on colour");
	CHECK(pass.signature.recordable == 0, "and is not one to record");
	granite_render_pass_free(&pass);

	/* loaded */
	desc.clear_attachments = 0;
	desc.load_attachments = 1;
	CHECK(granite_render_pass_init(&pass, DEVICE, &table, &desc), "a pass that loads a swapchain image");
	CHECK(rp_att[0].initialLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR && rp_att[0].finalLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
			"takes it as it was presented");
	CHECK(rp_dep.srcStageMask == (VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT),
			"and waits for the bottom of the pipe too");
	granite_render_pass_free(&pass);
}

static void test_hash(void)
{
	/* worked out from the hash's definition for these two descriptions:
	 * what the pipeline records on disk hold */
	const uint64_t hash_a = ((uint64_t)0x6ce9de06u << 32) | 0xff101ad8u;
	const uint64_t hash_b = ((uint64_t)0x7f35600cu << 32) | 0x705a16f6u;
	granite_render_pass_desc_t a, b, c, from;
	granite_render_pass_t pass;
	pipeline_record_pass_t sig;

	memset(&a, 0, sizeof(a));
	a.num_color = 1;
	a.color[0] = image(VK_FORMAT_R8G8B8A8_UNORM);
	CHECK(granite_render_pass_hash(&a, true) == hash_a, "the compatible hash of a colour pass is the one on disk");

	memset(&b, 0, sizeof(b));
	b.num_color = 1;
	b.color[0] = image(VK_FORMAT_R8G8B8A8_UNORM);
	b.depth_stencil = image(VK_FORMAT_D32_SFLOAT);
	b.depth_stencil.transient = true;
	b.op_flags = 5;
	b.clear_attachments = 1;
	b.store_attachments = 1;
	CHECK(granite_render_pass_hash(&b, false) == hash_b, "the hash of a colour and depth pass is the one on disk");

	/* compatible: not what is loaded, stored or cleared, nor layouts */
	c = b;
	c.op_flags = 0;
	c.clear_attachments = 0;
	c.load_attachments = 1;
	c.store_attachments = 0;
	c.color[0].general = true;
	CHECK(granite_render_pass_hash(&b, true) == granite_render_pass_hash(&c, true), "compatible passes hash alike");
	CHECK(granite_render_pass_hash(&b, false) != granite_render_pass_hash(&c, false), "but are not one pass");
	CHECK(granite_render_pass_hash(&b, true) != granite_render_pass_hash(&b, false), "nor is a pass its compatible one");
	c = b;
	c.color[0].general = true;
	CHECK(granite_render_pass_hash(&b, false) != granite_render_pass_hash(&c, false), "a layout makes another pass");

	/* not compatible */
	c = b;
	c.color[0].format = VK_FORMAT_R8G8B8A8_SRGB;
	CHECK(granite_render_pass_hash(&b, true) != granite_render_pass_hash(&c, true), "another format is not compatible");
	c = b;
	c.depth_stencil.format = VK_FORMAT_D16_UNORM;
	CHECK(granite_render_pass_hash(&b, true) != granite_render_pass_hash(&c, true), "nor another depth format");
	c = b;
	c.depth_stencil.transient = false;
	CHECK(granite_render_pass_hash(&b, true) != granite_render_pass_hash(&c, true), "nor a depth that is not transient");
	c = b;
	c.color[0].swapchain_layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	CHECK(granite_render_pass_hash(&b, true) != granite_render_pass_hash(&c, true), "nor a swapchain image");
	c = b;
	c.color[0].samples = 4;
	c.depth_stencil.samples = 4;
	CHECK(granite_render_pass_hash(&b, true) != granite_render_pass_hash(&c, true), "nor another sample count");
	CHECK(granite_render_pass_hash(&b, false) != granite_render_pass_hash(&c, false), "which is another pass");
	c = b;
	c.depth_stencil.samples = 4;
	CHECK(granite_render_pass_hash(&b, true) != granite_render_pass_hash(&c, true), "nor another depth sample count");
	c = a;
	c.color[0].samples = 4;
	CHECK(granite_render_pass_hash(&a, true) != granite_render_pass_hash(&c, true), "nor another colour sample count");

	/* a signature makes the compatible pass again */
	c = b;
	c.color[0].samples = 4;
	c.depth_stencil.samples = 4;
	CHECK(granite_render_pass_init(&pass, DEVICE, &table, &c), "a pass to take a signature from");
	sig = pass.signature;
	granite_render_pass_free(&pass);
	CHECK(granite_render_pass_desc_from_signature(&from, &sig), "a signature gives a description");
	CHECK(granite_render_pass_hash(&from, true) == granite_render_pass_hash(&c, true), "with the same compatible hash");
	CHECK(from.num_color == 1 && from.color[0].format == VK_FORMAT_R8G8B8A8_UNORM && from.color[0].samples == 4 &&
			!from.color[0].transient && from.depth_stencil.format == VK_FORMAT_D32_SFLOAT &&
			from.depth_stencil.samples == 4 && from.depth_stencil.transient && from.op_flags == 5,
			"and the same attachments");
	CHECK(granite_render_pass_init(&pass, DEVICE, &table, &from), "from which a pass is made");
	CHECK(rp_info.attachmentCount == 2 && rp_att[0].samples == VK_SAMPLE_COUNT_4_BIT &&
			rp_att[1].format == VK_FORMAT_D32_SFLOAT, "with those attachments");
	granite_render_pass_free(&pass);

	sig.recordable = 0;
	CHECK(!granite_render_pass_desc_from_signature(&from, &sig), "one not recordable gives none");
	sig.recordable = 1;
	sig.num_subpasses = 1;
	CHECK(!granite_render_pass_desc_from_signature(&from, &sig), "nor one with subpasses of its own");
	sig.num_subpasses = 0;
	sig.num_color = GRANITE_NUM_ATTACHMENTS + 1;
	CHECK(!granite_render_pass_desc_from_signature(&from, &sig), "nor one with too many attachments");
	sig.num_color = 0;
	sig.depth_format = VK_FORMAT_UNDEFINED;
	CHECK(!granite_render_pass_desc_from_signature(&from, &sig), "nor one with none");
}

static void test_failures(void)
{
	granite_render_pass_desc_t desc;
	granite_render_pass_t pass;

	memset(&desc, 0, sizeof(desc));
	created = destroyed = 0;
	CHECK(!granite_render_pass_init(&pass, DEVICE, &table, &desc), "a pass without attachments is refused");
	CHECK(created == 0 && pass.render_pass == VK_NULL_HANDLE, "without asking the driver");
	granite_render_pass_free(&pass);

	desc.num_color = GRANITE_NUM_ATTACHMENTS + 1;
	CHECK(!granite_render_pass_init(&pass, DEVICE, &table, &desc) && created == 0, "so is one with too many");

	desc.num_color = 1;
	desc.color[0] = image(VK_FORMAT_R8G8B8A8_UNORM);
	fail_create = true;
	CHECK(!granite_render_pass_init(&pass, DEVICE, &table, &desc), "a pass the driver refuses is reported");
	CHECK(pass.render_pass == VK_NULL_HANDLE, "and has no handle");
	granite_render_pass_free(&pass);
	CHECK(destroyed == 0, "nor anything to destroy");
	fail_create = false;
}

int main(void)
{
	table.vkCreateRenderPass = fake_create;
	table.vkDestroyRenderPass = fake_destroy;

	test_color_depth();
	test_transient_swapchain();
	test_hash();
	test_failures();

	if (failures)
	{
		fprintf(stderr, "test_vk_render_pass: %u failure(s)\n", failures);
		return 1;
	}
	printf("test_vk_render_pass: OK\n");
	return 0;
}
