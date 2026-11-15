#ifndef GLITCH64_VK_H
#define GLITCH64_VK_H

/* Glitch64 on Vulkan: the gr* calls Glide64 makes, drawn through
 * GLideN64's Vulkan renderer (vk_renderer.h) on the frontend's device.
 * C89.
 *
 * Each of Glitch64's GL entry points hands its call here while
 * glitch64_vulkan is set; what Glide64 sees is the same. The frame is
 * drawn into a framebuffer of the window's size with GL's orientation,
 * rows bottom up, so what is read back is what glReadPixels gives. */

#include <stdint.h>
#include <boolean.h>

#include "glitch64_n64_combiner.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Glide64 runs on Vulkan: set before its window opens */
extern int glitch64_vulkan;

#ifdef HAVE_GLIDEN64
#define GLITCH64_VK(call) do { if (glitch64_vulkan) { call; return; } } while (0)
#define GLITCH64_VK_RETURN(call) do { if (glitch64_vulkan) return call; } while (0)
#else
#define GLITCH64_VK(call) do { } while (0)
#define GLITCH64_VK_RETURN(call) do { } while (0)
#endif

/* the frame last swapped, to the frontend */
void glitch64_vk_present(void);
/* the device is going away: everything made on it goes first */
void glitch64_vk_context_destroy(void);

uint32_t vk_grSstWinOpen(void);
void vk_grSstWinClose(void);
void vk_flush(void);
void vk_grDrawVertexArrayContiguous(uint32_t mode, uint32_t count, void *pointers);
void vk_grBufferClear(uint32_t color, uint32_t alpha, uint32_t depth);
void vk_grBufferSwap(void);
void vk_grClipWindow(uint32_t minx, uint32_t miny, uint32_t maxx, uint32_t maxy);
void vk_grColorMask(bool rgb, bool a);
void vk_grCullMode(int32_t mode);
void vk_grDepthBufferMode(int32_t mode);
void vk_grDepthBufferFunction(unsigned func);
void vk_grDepthMask(bool mask);
void vk_grDepthBiasLevel(int32_t level);
void vk_grAlphaBlendFunction(unsigned rgb_sf, unsigned rgb_df, unsigned alpha_sf, unsigned alpha_df);
void vk_grAlphaTestFunction(int32_t function, uint8_t value, int set_alpha_ref);
void vk_grAlphaTestReferenceValue(uint8_t value);
void vk_grFogMode(int32_t mode);
void vk_grStippleMode(int32_t mode);
void vk_grN64Combine(const glitch64_n64_combiner *c);
void vk_grN64Constants(const float prim[4], const float env[4], const float consts[4],
      const float level[4], const float key_center[3], const float key_scale[3]);
void vk_grTexSource(int32_t tmu, uint32_t start, void *info, int do_download);
void vk_grTexFilterClampMode(int32_t tmu, int32_t s_clamp, int32_t t_clamp, int32_t minf, int32_t magf);
int32_t vk_grLfbLock(int32_t writeMode, void *info);
int vk_grLfbReadScaledInit(void);
const uint8_t *vk_grLfbReadScaled(int32_t x, int32_t y, int32_t w, int32_t h,
      uint32_t dw, uint32_t dh, uint32_t *stride);
const uint8_t *vk_grLfbReadRect(int32_t x, int32_t y, int32_t w, int32_t h);
void vk_grLfbWriteRegion(uint32_t dst_x, uint32_t dst_y, uint32_t src_width, uint32_t src_height,
      int32_t src_stride, const void *src_data);
void vk_grDepthImageWrite(const void *words, uint32_t w, uint32_t h,
      float x0, float y0, float x1, float y1);

#ifdef __cplusplus
}
#endif

#endif
