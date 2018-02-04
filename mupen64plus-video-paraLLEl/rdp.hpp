#ifndef PARALLEL_RDP_HPP
#define PARALLEL_RDP_HPP

#include "api/libretro.h"
#include "api/libretro_vulkan.h"
#include <memory>
#include <vector>

#include "rdp/frontend.hpp"
#include "rdp/rdp.hpp"
#include "rdp/vulkan.hpp"
#include "rdp/vulkan_util.hpp"

namespace RDP
{
bool init();
void deinit();
void begin_frame();
void set_dithering(unsigned type);

void process_commands();
void set_scissor_variables(const char *name);
extern const struct retro_hw_render_interface_vulkan *vulkan;

extern std::unique_ptr<Frontend> frontend;
extern std::unique_ptr<Renderer> renderer;
extern std::unique_ptr<Vulkan::Device> device;
extern std::unique_ptr<Vulkan::VulkanContext> context;
extern unsigned parallel_dithering;
}

namespace VI
{
extern unsigned width;
extern unsigned height;
extern bool valid;

void complete_frame();
void set_num_frames(unsigned count);
void deinit();
}

#endif
