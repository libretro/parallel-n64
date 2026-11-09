#ifndef VK_RENDERER_H
#define VK_RENDERER_H

/* GLideN64's Vulkan backend. C89. One owner thread (the frame path) calls
 * everything here; one worker builds pipelines behind it.
 *
 * Vertices and uniforms are written once, in their final form, into a ring
 * the GPU reads from; a draw never waits for a pipeline: without its own
 * it runs the generic one, which draws the same. */

#include <stdint.h>
#include <stddef.h>

#include "../CombinerValues.h"
#include "vk_shaders.h"

#ifdef __cplusplus
extern "C" {
#endif

struct retro_hw_render_interface_vulkan;
typedef struct vkr_renderer vkr_renderer;
typedef struct vkr_program vkr_program;
typedef struct vkr_readback vkr_readback;

enum vkr_format
{
   VKR_FMT_NONE = 0, VKR_FMT_RGBA8, VKR_FMT_RGB8, VKR_FMT_RGBA4, VKR_FMT_RGB5A1,
   VKR_FMT_R8, VKR_FMT_R16F, VKR_FMT_R32F, VKR_FMT_R32UI, VKR_FMT_DEPTH, VKR_FMT_COUNT
};

enum { VKR_BLEND_ZERO = 0, VKR_BLEND_ONE, VKR_BLEND_SRC_ALPHA, VKR_BLEND_DST_ALPHA,
   VKR_BLEND_INV_SRC_ALPHA, VKR_BLEND_CONST_ALPHA, VKR_BLEND_INV_CONST_ALPHA,
   VKR_BLEND_SRC1_COLOR, VKR_BLEND_INV_SRC1_COLOR, VKR_BLEND_SRC1_ALPHA, VKR_BLEND_INV_SRC1_ALPHA };
enum { VKR_CULL_NONE = 0, VKR_CULL_FRONT, VKR_CULL_BACK, VKR_CULL_BOTH };
enum { VKR_CMP_LEQUAL = 0, VKR_CMP_LESS, VKR_CMP_ALWAYS };
enum { VKR_TOPO_TRIANGLES = 0, VKR_TOPO_STRIP, VKR_TOPO_LINES };
enum { VKR_CAP_BLEND = 0, VKR_CAP_CULL, VKR_CAP_DEPTH_TEST, VKR_CAP_POLY_OFFSET, VKR_CAP_SCISSOR };
enum { VKR_FILTER_NEAREST = 0, VKR_FILTER_LINEAR = 1 };
enum { VKR_WRAP_CLAMP = 0, VKR_WRAP_REPEAT, VKR_WRAP_MIRROR };
#define VKR_KEEP (-1)

/* units a texture is bound to: the combiner's bindings */
#define VKR_UNIT_COUNT VKR_BIND_COUNT

typedef struct vkr_tri_vertex
{
   float x, y, z, w;
   float r, g, b, a;
   float s, t;
   uint32_t modify;
   float num_lights;
} vkr_tri_vertex;

typedef struct vkr_rect_vertex
{
   float x, y, z, w;
   float s0, t0, s1, t1;
} vkr_rect_vertex;

typedef struct vkr_caps
{
   int max_texture_size;
   float max_anisotropy;
   int max_samples;
   float max_line_width;
   int storage_images;
   int interlock;
} vkr_caps;

/* NULL on a device that cannot do it, or out of memory. */
/* @cache_path may be NULL. */
vkr_renderer *vkr_create(const struct retro_hw_render_interface_vulkan *vk,
      const vkr_shader_config *cfg, const char *cache_path);
void vkr_destroy(vkr_renderer *r);
const vkr_caps *vkr_get_caps(const vkr_renderer *r);

/* the window: what framebuffer 0 is and how it is presented */
void vkr_set_window(vkr_renderer *r, unsigned w, unsigned h);
void vkr_present(vkr_renderer *r);

/* ---- state ---- */
void vkr_enable(vkr_renderer *r, int cap, int on);
int  vkr_is_enabled(const vkr_renderer *r, int cap);
void vkr_set_clamp(vkr_renderer *r, int depth_clamp, int clip);
void vkr_cull(vkr_renderer *r, int mode);
void vkr_depth_write(vkr_renderer *r, int on);
void vkr_depth_compare(vkr_renderer *r, int cmp);
void vkr_viewport(vkr_renderer *r, int x, int y, int w, int h);
void vkr_scissor(vkr_renderer *r, int x, int y, int w, int h);
void vkr_blend(vkr_renderer *r, int src, int dst, int src_a, int dst_a);
void vkr_blend_color(vkr_renderer *r, float cr, float g, float b, float a);
void vkr_polygon_offset(vkr_renderer *r, float factor, float units);
void vkr_clear_color(vkr_renderer *r, float cr, float g, float b, float a);
void vkr_clear_depth(vkr_renderer *r);

/* ---- textures: handles, 0 is none ---- */
unsigned vkr_texture_new(vkr_renderer *r);
void vkr_texture_delete(vkr_renderer *r, unsigned h);
void vkr_texture_init(vkr_renderer *r, unsigned h, int fmt, unsigned samples,
      unsigned w, unsigned hgt, unsigned level, unsigned levels, const void *data, size_t data_bytes);
void vkr_texture_update(vkr_renderer *r, unsigned h, unsigned level,
      unsigned x, unsigned y, unsigned w, unsigned hgt, const void *data, size_t data_bytes);
void vkr_texture_params(vkr_renderer *r, unsigned h, int mag, int min, int mip_linear,
      int wrap_s, int wrap_t, float max_lod, float anisotropy);
void vkr_bind(vkr_renderer *r, unsigned unit, unsigned h);
void vkr_bind_image(vkr_renderer *r, unsigned unit, unsigned h);

/* ---- framebuffers: 0 is the window ---- */
unsigned vkr_fbo_new(vkr_renderer *r);
void vkr_fbo_delete(vkr_renderer *r, unsigned h);
void vkr_fbo_bind(vkr_renderer *r, int read, int draw, unsigned h);
void vkr_fbo_color(vkr_renderer *r, unsigned fbo, unsigned tex);
void vkr_fbo_depth(vkr_renderer *r, unsigned fbo, unsigned tex);
int  vkr_blit(vkr_renderer *r, unsigned read, unsigned draw,
      int sx0, int sy0, int sx1, int sy1, int dx0, int dy0, int dx1, int dy1,
      int color, int depth, int linear);

/* ---- CPU readers of GPU-written data ---- */
vkr_readback *vkr_readback_new(vkr_renderer *r, size_t bytes);
void vkr_readback_delete(vkr_renderer *r, vkr_readback *rb);
/* From the framebuffer bound for reading: its colour, or its depth. */
void vkr_readback_fbo(vkr_renderer *r, vkr_readback *rb, int x, int y, unsigned w, unsigned hgt, int depth);
void vkr_readback_tex(vkr_renderer *r, vkr_readback *rb, unsigned tex, int x, int y, unsigned w, unsigned hgt);
int  vkr_readback_ready(vkr_renderer *r, vkr_readback *rb, int wait);
/* Lent until the next copy into @rb. */
const void *vkr_readback_data(vkr_readback *rb);

/* ---- shaders ---- */
vkr_program *vkr_combiner_new(vkr_renderer *r, const vkr_combiner_desc *desc, int rect);
vkr_program *vkr_special_new(vkr_renderer *r, int vs_kind, int fs_kind);
void vkr_program_delete(vkr_renderer *r, vkr_program *p);
void vkr_use(vkr_renderer *r, vkr_program *p);
/* The final values of the program's uniform block. */
void vkr_values(vkr_renderer *r, vkr_program *p, const CombinerValues *v);
void vkr_special_params(vkr_renderer *r, vkr_program *p, float a, float b, float c, float d);

/* ---- drawing: write the vertices where they will be read ---- */
vkr_tri_vertex  *vkr_tri_reserve(vkr_renderer *r, unsigned count);
vkr_rect_vertex *vkr_rect_reserve(vkr_renderer *r, unsigned count);
uint16_t        *vkr_index_reserve(vkr_renderer *r, unsigned count);
void vkr_draw_tris(vkr_renderer *r, int topology, unsigned vertices, unsigned indices);
void vkr_draw_rects(vkr_renderer *r, int topology, unsigned vertices);
void vkr_draw_line(vkr_renderer *r, unsigned vertices, float width);

#ifdef __cplusplus
}
#endif

#endif
