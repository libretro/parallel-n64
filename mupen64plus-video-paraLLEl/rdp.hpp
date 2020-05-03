#ifndef PARALLEL_RDP_HPP
#define PARALLEL_RDP_HPP

#include "vulkan_headers.hpp"

#include <libretro.h>
#include <libretro_vulkan.h>
#include <memory>
#include <vector>

#include "rdp_device.hpp"
#include "context.hpp"
#include "device.hpp"

namespace RDP
{
bool init();
void deinit();
void begin_frame();

void process_commands();
extern const struct retro_hw_render_interface_vulkan *vulkan;

extern std::unique_ptr<Vulkan::Context> context;
extern std::unique_ptr<Vulkan::Device> device;
extern std::unique_ptr<CommandProcessor> frontend;

extern unsigned width;
extern unsigned height;
extern bool synchronous;

void complete_frame();
void deinit();
}

#endif
