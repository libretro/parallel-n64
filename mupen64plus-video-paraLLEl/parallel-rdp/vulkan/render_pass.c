/* Copyright (c) 2017-2022 Hans-Kristian Arntzen
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <string.h>
#include "render_pass.h"
#include "logging.h"

#define HASH_PRIME (((uint64_t)0x100 << 32) | 0x000001b3u)
#define HASH_SEED  (((uint64_t)0xcbf29ce4u << 32) | 0x84222325u)

static uint64_t hash_u32(uint64_t h, uint32_t value)
{
	return (h * HASH_PRIME) ^ value;
}

static bool format_has_depth(VkFormat format)
{
	switch (format)
	{
		case VK_FORMAT_D16_UNORM:
		case VK_FORMAT_D16_UNORM_S8_UINT:
		case VK_FORMAT_D24_UNORM_S8_UINT:
		case VK_FORMAT_D32_SFLOAT:
		case VK_FORMAT_X8_D24_UNORM_PACK32:
		case VK_FORMAT_D32_SFLOAT_S8_UINT:
			return true;
		default:
			break;
	}
	return false;
}

static bool format_has_stencil(VkFormat format)
{
	switch (format)
	{
		case VK_FORMAT_D16_UNORM_S8_UINT:
		case VK_FORMAT_D24_UNORM_S8_UINT:
		case VK_FORMAT_D32_SFLOAT_S8_UINT:
		case VK_FORMAT_S8_UINT:
			return true;
		default:
			break;
	}
	return false;
}

/* The hash is in the pipeline records on disk, by which a pass is found
 * again: what goes into it, and in which order, must stay. */
uint64_t granite_render_pass_hash(const granite_render_pass_desc_t *desc, bool compatible)
{
	bool has_depth = desc->depth_stencil.format != VK_FORMAT_UNDEFINED;
	uint64_t h = HASH_SEED;
	uint32_t lazy = 0;
	uint32_t optimal = 0;
	unsigned i;

	for (i = 0; i < desc->num_color; i++)
	{
		if (desc->color[i].transient)
			lazy |= (uint32_t)1 << i;
		if (!desc->color[i].general)
			optimal |= (uint32_t)1 << i;
		/* it changes the pass's dependencies */
		h = hash_u32(h, (uint32_t)desc->color[i].swapchain_layout);
	}

	if (has_depth)
	{
		if (desc->depth_stencil.transient)
			lazy |= (uint32_t)1 << desc->num_color;
		if (!desc->depth_stencil.general)
			optimal |= (uint32_t)1 << desc->num_color;
	}

	/* the base layer, the layers and the subpasses of the passes there
	 * once were: none, one, the default */
	h = hash_u32(h, 0);
	h = hash_u32(h, 1);
	h = hash_u32(h, 0);

	for (i = 0; i < desc->num_color; i++)
		h = hash_u32(h, (uint32_t)desc->color[i].format);
	h = hash_u32(h, desc->num_color);
	h = hash_u32(h, (uint32_t)desc->depth_stencil.format);

	if (!compatible)
	{
		h = hash_u32(h, desc->op_flags);
		h = hash_u32(h, desc->clear_attachments);
		h = hash_u32(h, desc->load_attachments);
		h = hash_u32(h, desc->store_attachments);
		h = hash_u32(h, optimal);
	}

	/* it changes the pass's dependencies as well */
	h = hash_u32(h, lazy);

	/* Passes of other sample counts are not compatible. One sample is
	 * left out, for the hashes on disk. */
	for (i = 0; i < desc->num_color; i++)
		if (desc->color[i].samples > 1)
			h = hash_u32(h, (i << 8) | desc->color[i].samples);
	if (has_depth && desc->depth_stencil.samples > 1)
		h = hash_u32(h, ((uint32_t)GRANITE_NUM_ATTACHMENTS << 8) | desc->depth_stencil.samples);

	return h;
}

static void fill_signature(pipeline_record_pass_t *sig, const granite_render_pass_desc_t *desc)
{
	unsigned i;

	memset(sig, 0, sizeof(*sig));
	sig->recordable = 1;
	sig->num_color = desc->num_color;
	sig->op_flags = desc->op_flags;
	sig->depth_format = (uint32_t)desc->depth_stencil.format;
	for (i = 0; i < desc->num_color; i++)
	{
		sig->formats[i] = (uint32_t)desc->color[i].format;
		sig->samples[i] = desc->color[i].samples;
		if (desc->color[i].transient)
			sig->lazy |= (uint32_t)1 << i;
		/* a swapchain's layout is not in the signature */
		if (desc->color[i].swapchain_layout != VK_IMAGE_LAYOUT_UNDEFINED)
			sig->recordable = 0;
	}
	if (desc->depth_stencil.format != VK_FORMAT_UNDEFINED)
	{
		sig->samples[GRANITE_NUM_ATTACHMENTS] = desc->depth_stencil.samples;
		if (desc->depth_stencil.transient)
			sig->lazy |= (uint32_t)1 << desc->num_color;
	}
}

bool granite_render_pass_desc_from_signature(granite_render_pass_desc_t *desc, const pipeline_record_pass_t *sig)
{
	unsigned i;

	/* a pass with subpasses of its own is not made any more */
	if (!sig->recordable || sig->num_color > GRANITE_NUM_ATTACHMENTS || sig->num_subpasses != 0)
		return false;
	if (sig->num_color == 0 && sig->depth_format == VK_FORMAT_UNDEFINED)
		return false;

	memset(desc, 0, sizeof(*desc));
	desc->num_color = sig->num_color;
	desc->op_flags = sig->op_flags;
	for (i = 0; i < sig->num_color; i++)
	{
		desc->color[i].format = (VkFormat)sig->formats[i];
		desc->color[i].samples = sig->samples[i] ? sig->samples[i] : 1;
		desc->color[i].swapchain_layout = VK_IMAGE_LAYOUT_UNDEFINED;
		desc->color[i].transient = ((sig->lazy >> i) & 1) != 0;
	}
	desc->depth_stencil.format = (VkFormat)sig->depth_format;
	if (sig->depth_format != VK_FORMAT_UNDEFINED)
	{
		desc->depth_stencil.samples = sig->samples[GRANITE_NUM_ATTACHMENTS] ? sig->samples[GRANITE_NUM_ATTACHMENTS] : 1;
		desc->depth_stencil.swapchain_layout = VK_IMAGE_LAYOUT_UNDEFINED;
		desc->depth_stencil.transient = ((sig->lazy >> sig->num_color) & 1) != 0;
	}
	return true;
}

bool granite_render_pass_init(granite_render_pass_t *pass, VkDevice device, const struct VolkDeviceTable *table,
		const granite_render_pass_desc_t *desc)
{
	bool has_depth = desc->depth_stencil.format != VK_FORMAT_UNDEFINED;
	bool transient_store = (desc->op_flags & GRANITE_RENDER_PASS_OP_ENABLE_TRANSIENT_STORE_BIT) != 0;
	bool transient_load = (desc->op_flags & GRANITE_RENDER_PASS_OP_ENABLE_TRANSIENT_LOAD_BIT) != 0;
	VkAttachmentDescription attachments[GRANITE_NUM_ATTACHMENTS + 1];
	VkAttachmentReference colors[GRANITE_NUM_ATTACHMENTS];
	VkAttachmentReference depth;
	VkSubpassDescription subpass;
	VkSubpassDependency dependency;
	VkRenderPassCreateInfo info;
	/* an attachment whose layout the pass changes on the way in: what
	 * comes before the pass must be done with it first */
	bool external_color = false;
	bool external_depth = false;
	bool external_bottom_of_pipe = false;
	unsigned i;

	memset(pass, 0, sizeof(*pass));
	pass->device = device;
	pass->table = table;
	if (desc->num_color > GRANITE_NUM_ATTACHMENTS || (desc->num_color == 0 && !has_depth))
	{
		LOGE("A render pass has one to %u colour attachments, or depth.\n", (unsigned)GRANITE_NUM_ATTACHMENTS);
		return false;
	}

	pass->num_color = desc->num_color;
	pass->depth_stencil_format = desc->depth_stencil.format;
	fill_signature(&pass->signature, desc);

	for (i = 0; i < desc->num_color; i++)
	{
		const granite_attachment_desc_t *image = &desc->color[i];
		VkImageLayout natural = image->general ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		VkAttachmentDescription *att = &attachments[i];
		VkImageLayout layout;
		bool implicit = false;

		pass->color_formats[i] = image->format;
		pass->samples = image->samples;

		att->flags = 0;
		att->format = image->format;
		att->samples = (VkSampleCountFlagBits)image->samples;
		if (desc->clear_attachments & ((uint32_t)1 << i))
			att->loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		else if (desc->load_attachments & ((uint32_t)1 << i))
			att->loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
		else
			att->loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		att->storeOp = (desc->store_attachments & ((uint32_t)1 << i)) ?
			VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE;
		att->stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		att->stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		att->finalLayout = VK_IMAGE_LAYOUT_UNDEFINED;

		if (image->transient)
		{
			if (transient_load)
				att->initialLayout = natural;
			else
			{
				/* nothing to load: a clean discard */
				if (att->loadOp == VK_ATTACHMENT_LOAD_OP_LOAD)
					att->loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
				att->initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			}

			if (!transient_store)
				att->storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			implicit = true;
		}
		else if (image->swapchain_layout != VK_IMAGE_LAYOUT_UNDEFINED)
		{
			if (att->loadOp == VK_ATTACHMENT_LOAD_OP_LOAD)
			{
				att->initialLayout = image->swapchain_layout;
				/* It was presented: that ends at the bottom of the
				 * pipe, with nothing left to make available. */
				external_bottom_of_pipe = true;
			}
			else
				att->initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

			att->finalLayout = image->swapchain_layout;
			implicit = true;
		}
		else
			att->initialLayout = natural;

		/* drawn to in the layout it comes in, when that is general */
		layout = att->initialLayout == VK_IMAGE_LAYOUT_GENERAL ?
			VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		/* and left in it: no transition out */
		if (att->finalLayout == VK_IMAGE_LAYOUT_UNDEFINED)
			att->finalLayout = layout;

		colors[i].attachment = i;
		colors[i].layout = layout;
		if (implicit || att->initialLayout != layout)
			external_color = true;
	}

	depth.attachment = VK_ATTACHMENT_UNUSED;
	depth.layout = VK_IMAGE_LAYOUT_UNDEFINED;
	if (has_depth)
	{
		const granite_attachment_desc_t *image = &desc->depth_stencil;
		VkAttachmentDescription *att = &attachments[desc->num_color];
		VkAttachmentLoadOp load_op = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		VkAttachmentStoreOp store_op = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		VkImageLayout optimal = (desc->op_flags & GRANITE_RENDER_PASS_OP_DEPTH_STENCIL_READ_ONLY_BIT) ?
			VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		VkImageLayout natural = image->general ? VK_IMAGE_LAYOUT_GENERAL : optimal;
		VkImageLayout layout;
		bool implicit = false;

		if (desc->op_flags & GRANITE_RENDER_PASS_OP_CLEAR_DEPTH_STENCIL_BIT)
			load_op = VK_ATTACHMENT_LOAD_OP_CLEAR;
		else if (desc->op_flags & GRANITE_RENDER_PASS_OP_LOAD_DEPTH_STENCIL_BIT)
			load_op = VK_ATTACHMENT_LOAD_OP_LOAD;
		if (desc->op_flags & GRANITE_RENDER_PASS_OP_STORE_DEPTH_STENCIL_BIT)
			store_op = VK_ATTACHMENT_STORE_OP_STORE;

		pass->samples = image->samples;

		att->flags = 0;
		att->format = image->format;
		att->samples = (VkSampleCountFlagBits)image->samples;
		att->loadOp = load_op;
		att->storeOp = store_op;
		if (format_has_stencil(image->format))
		{
			att->stencilLoadOp = load_op;
			att->stencilStoreOp = store_op;
		}
		else
		{
			att->stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			att->stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		}
		att->initialLayout = natural;

		if (image->transient)
		{
			if (!transient_load)
			{
				if (att->loadOp == VK_ATTACHMENT_LOAD_OP_LOAD)
					att->loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
				if (att->stencilLoadOp == VK_ATTACHMENT_LOAD_OP_LOAD)
					att->stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
				att->initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			}

			if (!transient_store)
			{
				att->storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
				att->stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			}
			implicit = true;
		}

		layout = att->initialLayout == VK_IMAGE_LAYOUT_GENERAL ? VK_IMAGE_LAYOUT_GENERAL : optimal;
		att->finalLayout = layout;

		depth.attachment = desc->num_color;
		depth.layout = layout;
		if (implicit || att->initialLayout != layout)
			external_depth = true;
	}

	memset(&subpass, 0, sizeof(subpass));
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = desc->num_color;
	subpass.pColorAttachments = colors;
	subpass.pDepthStencilAttachment = &depth;

	memset(&info, 0, sizeof(info));
	info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	info.attachmentCount = desc->num_color + (has_depth ? 1 : 0);
	info.pAttachments = attachments;
	info.subpassCount = 1;
	info.pSubpasses = &subpass;

	if (external_color || external_depth)
	{
		memset(&dependency, 0, sizeof(dependency));
		dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
		dependency.dstSubpass = 0;

		if (external_bottom_of_pipe)
			dependency.srcStageMask |= VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;

		if (external_color)
		{
			dependency.srcStageMask |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			dependency.dstStageMask |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			dependency.srcAccessMask |= VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			dependency.dstAccessMask |= VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		}

		if (external_depth)
		{
			dependency.srcStageMask |= VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
			dependency.dstStageMask |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
				VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
			dependency.srcAccessMask |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
			dependency.dstAccessMask |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
				VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
		}

		info.dependencyCount = 1;
		info.pDependencies = &dependency;
	}

	if (table->vkCreateRenderPass(device, &info, NULL, &pass->render_pass) != VK_SUCCESS)
	{
		LOGE("Failed to create render pass.\n");
		pass->render_pass = VK_NULL_HANDLE;
		return false;
	}

	return true;
}

void granite_render_pass_free(granite_render_pass_t *pass)
{
	if (pass->table && pass->render_pass != VK_NULL_HANDLE)
		pass->table->vkDestroyRenderPass(pass->device, pass->render_pass, NULL);
	pass->render_pass = VK_NULL_HANDLE;
}

bool granite_render_pass_has_depth(const granite_render_pass_t *pass)
{
	return format_has_depth(pass->depth_stencil_format);
}

bool granite_render_pass_has_stencil(const granite_render_pass_t *pass)
{
	return format_has_stencil(pass->depth_stencil_format);
}
