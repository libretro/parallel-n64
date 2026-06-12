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
/* Redirect command ingest to an HLE-synthesized host FIFO for the next
 * process_commands() call(s); pass (nullptr,0,0) to restore guest-RDRAM
 * ingest.  Used by the HLE-emitter graphics path (parallel_hle.cpp). */
void set_hle_cmd_buffer(const uint8_t *buf, uint32_t base_byte_addr, uint32_t len_bytes);
extern const struct retro_hw_render_interface_vulkan *vulkan;

extern unsigned width;
extern unsigned height;
extern unsigned upscaling;
extern unsigned downscaling_steps;
extern unsigned overscan;
extern bool synchronous, divot_filter, gamma_dither, vi_aa, vi_scale, dither_filter, interlacing;
extern bool native_texture_lod, native_tex_rect;

void complete_frame();
void deinit();

void profile_refresh_begin();
void profile_refresh_end();
}

#endif
