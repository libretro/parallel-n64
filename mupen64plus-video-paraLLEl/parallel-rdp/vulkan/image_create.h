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

#ifndef GRANITE_IMAGE_CREATE_H
#define GRANITE_IMAGE_CREATE_H

#include <stddef.h>
#include <stdint.h>
#include <boolean.h>
#include <retro_common_api.h>
#include "volk.h"
#include "memory_allocator.h"

RETRO_BEGIN_DECLS

/* The layers of an image that get a view of their own to render to */
#define GRANITE_IMAGE_MAX_LAYER_VIEWS 8

/* A 2D image, optimally tiled, in device memory */
typedef struct granite_image_desc
{
	VkFormat format;
	uint32_t width;
	uint32_t height;
	uint32_t levels;
	uint32_t layers;
	VkSampleCountFlagBits samples;
	VkImageUsageFlags usage;
	VkImageCreateFlags flags;
	/* its view is an array view even with one layer */
	bool force_array;
	/* views of the other of its UNORM and sRGB formats can be made */
	bool mutable_srgb;
	/* used on the graphics queue and on the compute queue at once: shared
	 * between their families when they differ */
	bool on_graphics;
	bool on_compute;
} granite_image_desc_t;

/* What creating an image needs of the device */
typedef struct granite_image_env
{
	VkDevice device;
	const struct VolkDeviceTable *table;
	granite_device_allocator_t *allocator;
	const VkPhysicalDeviceMemoryProperties *memory;
	uint32_t graphics_family;
	uint32_t compute_family;
	uint32_t transfer_family;
	/* VK_KHR_image_format_list */
	bool format_list;
} granite_image_env_t;

/* What a view, and the views that go with it, are of */
typedef struct granite_image_view_desc
{
	VkImage image;
	VkFormat format;
	VkImageViewType type;
	uint32_t base_level;
	uint32_t levels;
	uint32_t base_layer;
	uint32_t layers;
	/* of the image */
	VkImageUsageFlags usage;
} granite_image_view_desc_t;

typedef struct granite_image_views
{
	/* every level and layer, all aspects */
	VkImageView view;
	/* of a depth and stencil image that is more than an attachment: each
	 * aspect alone, to sample. Otherwise VK_NULL_HANDLE. */
	VkImageView depth;
	VkImageView stencil;
	/* of an attachment with several levels or layers: level 0 of each
	 * layer, to render to */
	VkImageView layer_views[GRANITE_IMAGE_MAX_LAYER_VIEWS];
	unsigned num_layer_views;
} granite_image_views_t;

/* The sRGB formats (those a storage image cannot have) */
bool granite_format_is_srgb(VkFormat format);
/* The UNORM and sRGB formats of the pair @format is of, in @formats;
 * returns 2, or 0 when it is of no such pair */
unsigned granite_srgb_view_formats(VkFormat format, VkFormat *formats);

/* What an image of @usage needs its format to support */
VkFormatFeatureFlags granite_image_usage_features(VkImageUsageFlags usage);
/* The stages and accesses an image of @usage can be used in */
VkPipelineStageFlags granite_image_usage_stages(VkImageUsageFlags usage);
VkAccessFlags granite_image_usage_access(VkImageUsageFlags usage);
/* The accesses an image in @layout can have; all of them for a layout
 * that allows anything */
VkAccessFlags granite_image_layout_access(VkImageLayout layout);
/* An image of @usage can have views */
bool granite_image_usage_has_view(VkImageUsageFlags usage);

/* The type of an image's own view */
VkImageViewType granite_image_view_type(const granite_image_desc_t *desc);

/* Creates the image with its memory bound. @upload: it is filled from a
 * buffer on the transfer queue, so it is a transfer source and
 * destination too and, when shared, shared with that queue's family.
 * @usage and @flags: what it was created with. On failure nothing is
 * left and @image is VK_NULL_HANDLE. */
bool granite_image_create(const granite_image_env_t *env, const granite_image_desc_t *desc, bool upload,
		VkImage *image, granite_allocation_t *allocation, VkImageUsageFlags *usage, VkImageCreateFlags *flags);

/* Creates the view and those that go with it. On failure none is left. */
bool granite_image_views_create(VkDevice device, const struct VolkDeviceTable *table,
		const granite_image_view_desc_t *desc, granite_image_views_t *views);
void granite_image_views_destroy(VkDevice device, const struct VolkDeviceTable *table,
		granite_image_views_t *views);

RETRO_END_DECLS

#endif
