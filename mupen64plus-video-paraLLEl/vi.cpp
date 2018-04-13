#include <string.h>

#include "Gfx #1.3.h"
#include <libretro.h>
#include <libretro_vulkan.h>
#include "rdp.hpp"
#include "z64.h"
#include <utility>

extern retro_log_printf_t log_cb;
extern retro_environment_t environ_cb;

using namespace RDP;
using namespace Vulkan;
using namespace std;

namespace VI
{
unsigned width;
unsigned height;
bool valid;
static bool dupe_possible;

static vector<retro_vulkan_image> retro_images;

retro_vulkan_image blank_retro_image;
ImageHandle blank_image;

void deinit()
{
	retro_images.clear();
	blank_image.reset();
}

void set_num_frames(unsigned count)
{
	retro_images.resize(count);
	dupe_possible = false;

	// Create a default image.
	blank_image = device->create_image_2d(VK_FORMAT_R8G8B8A8_UNORM, 320, 240);
	auto cmd = device->request_command_buffer();
	cmd.begin_stream();
	auto staging = device->request_buffer(BufferType::Staging, 320 * 240 * sizeof(uint32_t));
	memset(staging.map(), 0, 320 * 240 * sizeof(uint32_t));
	staging.unmap();
	cmd.end_stream();

	cmd.prepare_image(*blank_image);
	cmd.copy_to_image(*blank_image, staging, 0, 0, 0, 0, 320, 240, 0);
	cmd.complete_image(*blank_image);

	auto fence = device->submit(cmd);
	device->wait(fence);

	vulkan->set_image(vulkan->handle, &blank_retro_image, 0, nullptr, VK_QUEUE_FAMILY_IGNORED);
	width = 320;
	height = 240;
	valid = true;

	blank_retro_image.image_view = blank_image->view;
	blank_retro_image.image_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	blank_retro_image.create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	blank_retro_image.create_info.image = blank_image->image;
	blank_retro_image.create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
	blank_retro_image.create_info.format = VK_FORMAT_R8G8B8A8_UNORM;
	blank_retro_image.create_info.subresourceRange.baseMipLevel = 0;
	blank_retro_image.create_info.subresourceRange.baseArrayLayer = 0;
	blank_retro_image.create_info.subresourceRange.levelCount = 1;
	blank_retro_image.create_info.subresourceRange.layerCount = 1;
	blank_retro_image.create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	blank_retro_image.create_info.components.r = VK_COMPONENT_SWIZZLE_R;
	blank_retro_image.create_info.components.g = VK_COMPONENT_SWIZZLE_G;
	blank_retro_image.create_info.components.b = VK_COMPONENT_SWIZZLE_B;
	blank_retro_image.create_info.components.a = VK_COMPONENT_SWIZZLE_A;
}

void complete_frame(void)
{
	if (!renderer)
		return;

	renderer->complete_frame();

	// Random VI checking. We have to do a better job in the future.
	uint32_t fb = *GET_GFX_INFO(VI_ORIGIN_REG) & 0x00ffffff;
	const int v_sync = *GET_GFX_INFO(VI_V_SYNC_REG) & 0x000003ff;
	const int ispal = (v_sync > 550);

#if 0
      const int x_add = *GET_GFX_INFO(VI_X_SCALE_REG);
      const int y_add = *GET_GFX_INFO(VI_Y_SCALE_REG);
      const int x1 = (*GET_GFX_INFO(VI_H_START_REG) >> 16) & 0x03ff;
      const int y1 = (*GET_GFX_INFO(VI_V_START_REG) >> 16) & 0x03ff;
      const int x2 = (*GET_GFX_INFO(VI_H_START_REG) >>  0) & 0x03ff;
      const int y2 = (*GET_GFX_INFO(VI_V_START_REG) >>  0) & 0x03ff;
      const int delta_x = x2 - x1;
      const int delta_y = y2 - y1;
      const int vi_width = *GET_GFX_INFO(VI_WIDTH_REG) & 0x00000fff;
      const int vitype = *GET_GFX_INFO(VI_STATUS_REG) & 0x00000003;
#endif
	int vactivelines = v_sync - (ispal ? 47 : 37);
	if (vactivelines < 0)
	{
		log_cb(RETRO_LOG_WARN, "Inactive frame.\n");
		vulkan->set_image(vulkan->handle, &blank_retro_image, 0, nullptr, VK_QUEUE_FAMILY_IGNORED);
		return;
	}

	//fprintf(stderr, "VI: %u, %u x %u, XSCALE: %u, YSCALE: %u, VSYNC: %u, VI_WIDTH: %u\n", fb, delta_x, delta_y, x_add, y_add, v_sync, vi_width);

	unsigned index = vulkan->get_sync_index(vulkan->handle);
	auto &outputs = renderer->get_vi_outputs();
	Renderer::VIOutput *best = nullptr;

	// Find the last VI output which points to a buffer which is inside.
	if (!outputs.empty())
	{
		auto itr = begin(outputs);
		for (; itr != end(outputs); ++itr)
		{
			uint32_t wrap = WRAP_ADDR(fb - itr->framebuffer.addr);
			uint32_t size = itr->framebuffer.color_size();
			bool within = wrap < size;
			if (within)
				best = &*itr;
		}
	}

	if (best)
	{
		auto &last = *best;
		retro_images[index].image_view = last.image->view;
		retro_images[index].image_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		retro_images[index].create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		retro_images[index].create_info.image = last.image->image;
		retro_images[index].create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
		retro_images[index].create_info.format = VK_FORMAT_R8G8B8A8_UNORM;
		retro_images[index].create_info.subresourceRange.baseMipLevel = 0;
		retro_images[index].create_info.subresourceRange.baseArrayLayer = 0;
		retro_images[index].create_info.subresourceRange.levelCount = 1;
		retro_images[index].create_info.subresourceRange.layerCount = 1;
		retro_images[index].create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		retro_images[index].create_info.components.r = VK_COMPONENT_SWIZZLE_R;
		retro_images[index].create_info.components.g = VK_COMPONENT_SWIZZLE_G;
		retro_images[index].create_info.components.b = VK_COMPONENT_SWIZZLE_B;
		retro_images[index].create_info.components.a = VK_COMPONENT_SWIZZLE_A;

		vulkan->set_image(vulkan->handle, &retro_images[index], 0, nullptr, VK_QUEUE_FAMILY_IGNORED);
		width = last.framebuffer.allocated_width;
		height = last.framebuffer.allocated_height;
		valid = true;
		dupe_possible = true;
	}
	else if (dupe_possible)
	{
		// Here we should upload from CPU possibly to emulate games which are software rendered.
		valid = false;
	}
	else
	{
		vulkan->set_image(vulkan->handle, &blank_retro_image, 0, nullptr, VK_QUEUE_FAMILY_IGNORED);
		width = 320;
		height = 240;
		valid = true;
	}
}
}
