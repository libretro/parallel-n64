#ifndef VK_UPSCALER_H
#define VK_UPSCALER_H

/* GLideN64 for a frontend's temporal upscaler
 * (RETRO_ENVIRONMENT_GET_HW_RENDER_UPSCALER_INTERFACE), on Vulkan. C89.
 *
 * The geometry side is parallel-RDP's, temporal_upscaler.h: this feeds it
 * the vertices GLideN64 draws where parallel-RDP feeds it the RDP's
 * edges, and decides the size frames are drawn at. vk_renderer.h draws
 * what comes out of it. All of it on the thread that draws. What the
 * frontend glue calls is in vk_device.h. */

#include <stdint.h>
#include <boolean.h>

#include "vk_renderer.h"
#include "vk_device.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- the renderer's side ---- */
/* What is drawn goes into this color image of the N64: @stride in pixels,
 * @size_log2 bytes a pixel, log2, -1 under a byte. */
void gliden64_upscaler_color_image(uint32_t address, uint32_t stride, int size_log2);

typedef struct gliden64_upscaler_view
{
   /* the viewport, in pixels of the framebuffer drawn into, y down */
   float viewport[4];
   /* vertices given in screen coordinates: to clip space, as the shader has it */
   float screen_scale[2];
   /* framebuffer pixels a native one */
   float scale;
   /* GL's: 0 none, 1 front, 2 back, 3 both */
   int cull;
} gliden64_upscaler_view;

/* Triangles about to be drawn: @indices NULL for none, @strip a strip.
 * @flags RDP_UPSCALER_DEPTH_TEST, _DEPTH_WRITE, _TRANSLUCENT. Returns in
 * @jitter how far to move them, in framebuffer pixels. */
void gliden64_upscaler_triangles(const vkr_tri_vertex *vertices, unsigned num_vertices,
      const uint16_t *indices, unsigned num_indices, bool strip,
      const gliden64_upscaler_view *view, uint64_t signature, unsigned flags,
      float *jitter);
/* A rectangle about to be drawn, by its vertices in clip space */
void gliden64_upscaler_rect(const vkr_rect_vertex *vertices, unsigned num_vertices,
      const gliden64_upscaler_view *view);

/* The framebuffer the frame presented next shows: the color image at
 * @address, of @width x @height native pixels, drawn at @scale; its
 * pixel (x, y) is the window's (x * map[0] + map[2], y * map[1] + map[3]). */
void gliden64_upscaler_shown(uint32_t address, uint32_t stride, int size_log2,
      unsigned width, unsigned height, float scale, const float *map);
/* At present: fills @frame for vkr_present_upscaled. False when the frame
 * has nothing for the upscaler. */
bool gliden64_upscaler_frame(vkr_upscaler_frame *frame);

#ifdef __cplusplus
}
#endif

#endif
