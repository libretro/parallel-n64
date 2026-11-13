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

#ifndef GRANITE_IMAGE_UPLOAD_H
#define GRANITE_IMAGE_UPLOAD_H

#include <stdint.h>
#include <boolean.h>
#include <retro_common_api.h>
#include "volk.h"
#include "texture_format.h"

RETRO_BEGIN_DECLS

/* One layer of one level of the texels a 2D image starts with.
 * @row_length is the source's, in texels; 0 for the level's own. */
typedef struct granite_image_initial_data
{
	const void *data;
	unsigned row_length;
} granite_image_initial_data_t;

/* Copies @initial into @layout->buffer the way a 2D layout lays it out.
 * @initial is level after level, each level's layers in order:
 * layout->mip_levels * layout->array_layers of them. */
void granite_image_upload_fill(const granite_texture_layout_t *layout,
		const granite_image_initial_data_t *initial);

/* The queues an image's first commands can run on */
typedef struct granite_image_upload_queues
{
	VkQueue graphics;
	VkQueue compute;
	VkQueue transfer;
	uint32_t graphics_family;
	uint32_t compute_family;
	uint32_t transfer_family;
} granite_image_upload_queues_t;

typedef struct granite_image_upload_desc
{
	VkImage image;
	VkFormat format;
	uint32_t levels;
	uint32_t layers;
	/* what the image is in once created; UNDEFINED for no change */
	VkImageLayout initial_layout;
	/* everything the image can be used with */
	VkPipelineStageFlags stages;
	VkAccessFlags access;
	/* the image is shared by queue families, not owned by one */
	bool concurrent;
	/* and the compute queue is one of them */
	bool on_compute;
	/* texels are copied in */
	bool has_data;
} granite_image_upload_desc_t;

/* What it takes to bring a new image to its first layout */
typedef struct granite_image_upload_plan
{
	/* a graphics command buffer is needed at all */
	bool transition;
	/* the copy goes to the transfer queue in a command buffer of its
	 * own, which the graphics queue then waits for with @wait_stages */
	bool split_transfer;
	VkPipelineStageFlags wait_stages;
	/* the transfer queue's family hands the image to the graphics one:
	 * @release after the copy, @acquire in the graphics commands */
	bool hand_over;
	VkImageMemoryBarrier release;
	VkImageMemoryBarrier acquire;
	/* the graphics commands change the layout: from @old_layout, after
	 * @src_stage and @src_access, to desc->initial_layout for
	 * desc->stages and @dst_access */
	bool final_barrier;
	VkImageLayout old_layout;
	VkPipelineStageFlags src_stage;
	VkAccessFlags src_access;
	VkAccessFlags dst_access;
	/* the compute queue is another queue and waits for the graphics
	 * commands with @compute_stages */
	bool share_compute;
	VkPipelineStageFlags compute_stages;
	/* otherwise: the graphics queue is flushed after the commands */
	bool flush_graphics;
} granite_image_upload_plan_t;

void granite_image_upload_plan(const granite_image_upload_queues_t *queues,
		const granite_image_upload_desc_t *desc, granite_image_upload_plan_t *plan);

RETRO_END_DECLS

#endif
