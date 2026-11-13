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
#include "image_upload.h"
#include "image_create.h"
#include "command_ops.h"

void granite_image_upload_fill(const granite_texture_layout_t *layout,
		const granite_image_initial_data_t *initial)
{
	uint32_t level, layer;
	unsigned index = 0;

	for (level = 0; level < layout->mip_levels; level++)
	{
		const granite_mip_info_t *mip = &layout->mips[level];
		size_t row_size = granite_texture_layout_row_size(layout, level);

		for (layer = 0; layer < layout->array_layers; layer++, index++)
		{
			uint32_t y;
			uint32_t src_row_length = initial[index].row_length ? initial[index].row_length : mip->row_length;
			size_t src_row_stride = granite_texture_layout_row_stride(layout, src_row_length);
			uint8_t *dst = granite_texture_layout_data(layout, layer, level);
			const uint8_t *src = (const uint8_t*)initial[index].data;

			for (y = 0; y < mip->block_image_height; y++)
				memcpy(dst + y * row_size, src + y * src_row_stride, row_size);
		}
	}
}

void granite_image_upload_plan(const granite_image_upload_queues_t *queues,
		const granite_image_upload_desc_t *desc, granite_image_upload_plan_t *plan)
{
	memset(plan, 0, sizeof(*plan));
	plan->dst_access = desc->access & granite_image_layout_access(desc->initial_layout);

	if (desc->has_data)
	{
		plan->transition = true;
		plan->final_barrier = true;
		plan->old_layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		plan->src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;

		if (queues->transfer != queues->graphics)
		{
			/* The graphics queue waits on a semaphore: nothing of the
			 * copy to wait for by access. */
			plan->split_transfer = true;
			plan->wait_stages = desc->stages;

			/* A semaphore is not enough between families unless the
			 * image is shared: ownership has to change hands, and the
			 * layout changes with it. */
			if (!desc->concurrent && queues->transfer_family != queues->graphics_family)
			{
				plan->hand_over = true;
				plan->final_barrier = false;

				plan->release.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
				plan->release.image = desc->image;
				plan->release.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
				plan->release.dstAccessMask = 0;
				plan->release.srcQueueFamilyIndex = queues->transfer_family;
				plan->release.dstQueueFamilyIndex = queues->graphics_family;
				plan->release.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
				plan->release.newLayout = desc->initial_layout;
				plan->release.subresourceRange.aspectMask = granite_format_aspect_mask(desc->format);
				plan->release.subresourceRange.levelCount = desc->levels;
				plan->release.subresourceRange.layerCount = desc->layers;

				plan->acquire = plan->release;
				plan->acquire.srcAccessMask = 0;
				plan->acquire.dstAccessMask = plan->dst_access;
			}
		}
		else
			plan->src_access = VK_ACCESS_TRANSFER_WRITE_BIT;
	}
	else if (desc->initial_layout != VK_IMAGE_LAYOUT_UNDEFINED)
	{
		plan->transition = true;
		plan->final_barrier = true;
		plan->old_layout = VK_IMAGE_LAYOUT_UNDEFINED;
		plan->src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
	}

	if (!plan->transition)
		return;

	/* A shared image has to be ready before the other queues use it */
	if (desc->on_compute && queues->graphics != queues->compute)
	{
		plan->share_compute = true;
		plan->compute_stages = desc->stages;
		if (queues->graphics_family != queues->compute_family)
			plan->compute_stages &= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;
	}
	else
		plan->flush_graphics = desc->concurrent;
}
