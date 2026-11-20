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

#include <temporal_upscaler.h>

#include "../CombinerValues.h"
#include "vk_shaders.h"

#ifdef __cplusplus
extern "C" {
#endif

struct retro_hw_render_interface_vulkan;
struct retro_hw_render_upscaler_interface;
typedef struct vkr_renderer vkr_renderer;
typedef struct vkr_program vkr_program;
typedef struct vkr_readback vkr_readback;

enum vkr_format
{
   VKR_FMT_NONE = 0, VKR_FMT_RGBA8, VKR_FMT_RGB8, VKR_FMT_RGBA4, VKR_FMT_RGB5A1,
   VKR_FMT_R8, VKR_FMT_R16F, VKR_FMT_R32F, VKR_FMT_R32UI, VKR_FMT_DEPTH, VKR_FMT_R16UI,
   /* Glide's texel formats, taken as they lie in memory (16-bit ones native
    * endian), each read as GL reads its own: luminance (L 1), intensity
    * (I I I I), luminance and alpha in two bytes, 565, 1555 and 4444 with
    * the alpha at the top, and BGRA */
   VKR_FMT_L8, VKR_FMT_I8, VKR_FMT_LA8, VKR_FMT_RGB565, VKR_FMT_ARGB1555, VKR_FMT_ARGB4444, VKR_FMT_BGRA8,
   VKR_FMT_COUNT
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

/* ---- a client's own programs: its vertices, its SPIR-V ---- */
#define VKR_CLIENT_ATTRS 6
typedef struct vkr_client_attr
{
   uint32_t location, format, offset; /* format: a VkFormat */
} vkr_client_attr;

typedef struct vkr_client_layout
{
   uint32_t stride, count;
   vkr_client_attr attrs[VKR_CLIENT_ATTRS];
} vkr_client_layout;

#define VKR_CLIENT_KEY 20
/* Writes a client's fragment shader from its key, on the worker. */
typedef const uint32_t *(*vkr_client_writer)(spv_module *m, const int *key, unsigned *count);

/* The shaders read the renderer's descriptor set and push block (vk_shaders.h):
 * the values from VKR_BIND_VALUES, the textures bound, and the ops, flags
 * and special words vkr_client_push gives. */
typedef struct vkr_client_desc
{
   const uint32_t *vs, *fs;
   unsigned vs_words, fs_words;
   vkr_client_layout layout;
   /* Or, with no fs: the fragment shader @write_fs writes from @key on the
    * worker, the pipelines built behind the draws while @fallback (a
    * prewarmed program of the same layout and values) draws in its place.
    * No vs: the fallback's. */
   vkr_program *fallback;
   vkr_client_writer write_fs;
   int key[VKR_CLIENT_KEY];
} vkr_client_desc;

/* NULL on a device that cannot do it, or out of memory. The driver's
 * pipeline cache of an earlier session lent, @cache_size bytes at @cache,
 * for the call (NULL, 0: none); one of another GPU or driver, or a
 * damaged one, is a miss. */
vkr_renderer *vkr_create_lent(const struct retro_hw_render_interface_vulkan *vk,
      const vkr_shader_config *cfg, const void *cache, size_t cache_size);
/* Destroyed, its pipeline cache let go */
void vkr_destroy(vkr_renderer *r);
/* Destroyed, its pipeline cache handed back in a buffer the caller frees
 * (NULL where there is none), @changed set where it is not what the
 * renderer was lent. */
void *vkr_destroy_taking_cache(vkr_renderer *r, size_t *size, int *changed);
const vkr_caps *vkr_get_caps(const vkr_renderer *r);

/* the window: what framebuffer 0 is and how it is presented */
void vkr_set_window(vkr_renderer *r, unsigned w, unsigned h);
void vkr_present(vkr_renderer *r);
/* The size of the image last presented */
void vkr_presented_size(const vkr_renderer *r, unsigned *w, unsigned *h);

/* ---- a frontend's temporal upscaler ---- */
/* What the frame presented brings for it: the frame's triangles again
 * (rdp_upscaler_resolve), in pixels of the framebuffer shown, and where
 * the window has those: x * scale + offset. */
typedef struct vkr_upscaler_frame
{
   const struct retro_hw_render_upscaler_interface *iface;
   const rdp_upscaler_vertex_t *vertices;
   const rdp_upscaler_draw_t *draws;
   unsigned num_vertices, num_draws;
   float scale[2], offset[2];
   /* as the window has it */
   float jitter[2];
   /* the window's pixel the vertices' planes are from */
   float origin[2];
   float farthest;
   int reset;
} vkr_upscaler_frame;

/* At load: the pass's pipelines. Zero when the device cannot. */
int  vkr_upscaler_init(vkr_renderer *r);
/* vkr_present, the frame's motion vectors, translucency mask and depth
 * handed over first. Without @u or its iface, vkr_present. */
void vkr_present_upscaled(vkr_renderer *r, const vkr_upscaler_frame *u);
/* The triangles of the next draw are moved by this many pixels (x right,
 * y down the image) of the framebuffer drawn into. */
void vkr_jitter(vkr_renderer *r, float x, float y);
void vkr_get_viewport(const vkr_renderer *r, float *xywh);
unsigned vkr_draw_fbo(const vkr_renderer *r);
/* What the next draw does: bit 0 tests depth, bit 1 writes it, bit 2
 * blends; @cull the faces it drops, VKR_CULL_*. */
unsigned vkr_draw_state(const vkr_renderer *r, int *cull);

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
/* GL's glClear on a framebuffer of its own: the scissor and the colour mask
 * hold; @rgba NULL leaves the colour, @depth clears depth to @z. */
void vkr_clear_masked(vkr_renderer *r, const float *rgba, int depth, float z);
/* The next draws write the colour, the alpha. */
void vkr_color_mask(vkr_renderer *r, int rgb, int alpha);

/* ---- textures: handles, 0 is none ---- */
unsigned vkr_texture_new(vkr_renderer *r);
void vkr_texture_delete(vkr_renderer *r, unsigned h);
void vkr_texture_init(vkr_renderer *r, unsigned h, int fmt, unsigned samples,
      unsigned w, unsigned hgt, unsigned level, unsigned levels, const void *data, size_t data_bytes);
/* Memory in the ring to decode one texture level into, passed back as its
 * data with no copy; NULL if it does not fit. Write only. */
void *vkr_texture_lend(vkr_renderer *r, size_t bytes);
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
/* A client's program: its pipelines are built when a state first draws. */
vkr_program *vkr_client_new(vkr_renderer *r, const vkr_client_desc *desc);
/* At load, the pipeline of each state the client can draw with: set the
 * state (the framebuffer bound to draw into among it), then ask. A state
 * it draws with that none of these were for leaves the draw out. */
void vkr_client_prewarm(vkr_renderer *r, vkr_program *p);
void vkr_client_prewarm_wait(vkr_renderer *r);
/* Or ahead, waited for by nothing: until it is built the client's
 * fallback draws in its place. 0 where the queue is full and it was left
 * to its first draw. */
int vkr_client_prewarm_ahead(vkr_renderer *r, vkr_program *p);
/* Test hosts (PARALLEL_RDP_TEST_WAIT_PIPELINES) wait for those asked for
 * ahead, to count them; nothing else does */
void vkr_client_prewarm_ahead_wait_for_test(vkr_renderer *r);
/* What its next draws push. */
void vkr_client_push(vkr_renderer *r, vkr_program *p, const int ops[16], int flags, const float special[4]);
/* Its uniform block: @bytes of it, at most a CombinerValues. */
void vkr_values_raw(vkr_renderer *r, vkr_program *p, const void *v, size_t bytes);

/* ---- drawing: write the vertices where they will be read ---- */
vkr_tri_vertex  *vkr_tri_reserve(vkr_renderer *r, unsigned count);
vkr_rect_vertex *vkr_rect_reserve(vkr_renderer *r, unsigned count);
uint16_t        *vkr_index_reserve(vkr_renderer *r, unsigned count);
/* a client's vertices, drawn by vkr_draw_tris */
void            *vkr_vertex_reserve(vkr_renderer *r, size_t bytes);
/* Room for up to @bytes of a client's vertices, written straight into it as
 * they come, bound for vkr_draw_tris; vkr_vertex_close says how much was
 * used, and the rest is given back if nothing was reserved since. Closed
 * and drawn before the frame is presented: the room is that frame's. */
void            *vkr_vertex_open(vkr_renderer *r, size_t bytes);
void             vkr_vertex_close(vkr_renderer *r, size_t used);
void vkr_draw_tris(vkr_renderer *r, int topology, unsigned vertices, unsigned indices);
void vkr_draw_rects(vkr_renderer *r, int topology, unsigned vertices);
void vkr_draw_line(vkr_renderer *r, unsigned vertices, float width);

#ifdef __cplusplus
}
#endif

#endif
