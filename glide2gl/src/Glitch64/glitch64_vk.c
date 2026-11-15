#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "glide.h"
#include "glitchmain.h"
#include "glitch64_vk.h"

int glitch64_vulkan = 0;

#ifdef HAVE_GLIDEN64

#include "vk_device.h"
#include "vk_renderer.h"
#include "glitch64_vk_shaders.h"
#include "glitch64_vk_textures.h"
#include "../../../Graphics/RDP/RDP_state.h"
#include "../Glide64/rdp.h"
#include <streams/file_stream.h>
#include "../../../mupen64plus-core/src/plugin/plugin.h"
#include "../../../mupen64plus-video-gliden64/src/CombinerKeyPath.h"

/* Napalm's, as glitch64_textures.c has it */
#ifndef GR_TEXFMT_ARGB_8888
#define GR_TEXFMT_ARGB_8888 0x12
#endif

extern int width, height;
extern uint16_t *glide64_frameBuffer;
extern float fogStart, fogEnd;
extern float polygonOffsetFactor;
extern float polygonOffsetUnits;

/* the vertices of a batch, written straight into the renderer's ring: room
 * for this many is opened at its first triangle, what is unused given
 * back when it is drawn */
#define VK_VERTICES  (3 * 4096)
#define VK_TEXTURES  4096
/* textures written over, kept to take the next of their format and size */
#define VK_SPARE     1024
/* the combiners with a shader of their own; past that, the shared one */
#define VK_OWN       2048
#define VK_OWN_SLOTS 4096
#define LFB_SCALED_MAX 1024
/* no depth bias level set yet */
#define NO_BIAS 0x7fffffff

/* Glitch64's vertex as it batches it: the GL backend's VBufVertex */
typedef struct vk_vertex
{
   float x, y, z, q;
   uint8_t b, g, r, a;
   float coord[4];
   float fog;
} vk_vertex;

/* a texture of TMU memory: Glide64's address, plus one */
/* a combiner's own program: its ops and flags */
typedef struct vk_own
{
   int key[G64VK_KEY_WORDS];
   vkr_program *p;
   /* the blend functions it drew with (vk_blends' places) */
   uint32_t blends;
} vk_own;

/* Every blend function Glide64 sets, as the renderer has them: the colour's
 * and the alpha's. They are those of Combine.c's A_BLEND and the blend
 * functions of glide64_gDP.c, TexCache.c and Framebuffer_glide64.c; the
 * first is blending off. */
#define VK_BLENDS 11
static const unsigned char vk_blends[VK_BLENDS][4] = {
   { 0, 0, 0, 0 },
   { VKR_BLEND_ONE, VKR_BLEND_ZERO, VKR_BLEND_ZERO, VKR_BLEND_ZERO },
   { VKR_BLEND_ONE, VKR_BLEND_ONE, VKR_BLEND_ZERO, VKR_BLEND_ZERO },
   { VKR_BLEND_ZERO, VKR_BLEND_ONE, VKR_BLEND_ZERO, VKR_BLEND_ZERO },
   { VKR_BLEND_ZERO, VKR_BLEND_INV_SRC_ALPHA, VKR_BLEND_ZERO, VKR_BLEND_ZERO },
   { VKR_BLEND_SRC_ALPHA, VKR_BLEND_INV_SRC_ALPHA, VKR_BLEND_ZERO, VKR_BLEND_ZERO },
   { VKR_BLEND_SRC_ALPHA, VKR_BLEND_ONE, VKR_BLEND_ZERO, VKR_BLEND_ZERO },
   { VKR_BLEND_INV_SRC_ALPHA, VKR_BLEND_SRC_ALPHA, VKR_BLEND_ZERO, VKR_BLEND_ZERO },
   { VKR_BLEND_ZERO, VKR_BLEND_SRC_ALPHA, VKR_BLEND_ZERO, VKR_BLEND_ZERO },
   { VKR_BLEND_ONE, VKR_BLEND_ZERO, VKR_BLEND_ONE, VKR_BLEND_ZERO },
   { VKR_BLEND_SRC_ALPHA, VKR_BLEND_INV_SRC_ALPHA, VKR_BLEND_ONE, VKR_BLEND_ZERO }
};

/* the combiners a game met, kept for its next session: each with the blend
 * functions it drew with */
#define VK_JOURNAL_MAGIC   0x47364b4au
#define VK_JOURNAL_VERSION 1u

static struct
{
   vkr_renderer *r;
   spv_module spv;
   int spv_ready;
   vkr_program *combiner, *depth;
   unsigned color, zbuf, fbo;
   unsigned lfb_color, lfb_fbo;
   unsigned lfb_tex, depth_tex;
   vkr_readback *rb;
   uint8_t *buf;          /* the window's size in RGBA8 */
   uint8_t *staging;      /* a texture's texels, when the ring lends none */

   vk_vertex *verts;
   unsigned n_verts;

   glitch64_vk_values values;
   int ops[16];
   int flags;
   uint32_t key[2];
   uint32_t fog_rgba;

   int alpha_test, alpha_ref, alpha_func;
   int fog_mode;
   int dither;
   int three_point[2];
   int cull, depth_func, depth_mask, depth_on, bias;
   uint32_t clip[4];
   unsigned blend[4];

   vk_tex textures[VK_TEXTURES];
   unsigned n_textures;
   vk_tex spare[VK_SPARE];
   unsigned n_spare;
   unsigned bound[2];
   int wrap_s[2], wrap_t[2], min_f[2], mag_f[2];

   vk_own own[VK_OWN_SLOTS];
   unsigned n_own;
   int own_dirty;
   vkr_client_layout layout;
   int blend_index;       /* vk_blends' place of the blend set now, -1 none */
   char journal[PATH_MAX_LENGTH];
} vk;

/* ---- the combiner's inputs, as the shader indexes them ---- */

static int texel_in(int second, unsigned which, int alpha)
{
   if ((which ^ (unsigned)second) & 1)
      return alpha ? G64VK_IN_TEXEL1_A : G64VK_IN_TEXEL1;
   return alpha ? G64VK_IN_TEXEL0_A : G64VK_IN_TEXEL0;
}

static int rgb_in(int second, int slot, unsigned v)
{
   if (v <= 5)
   {
      static const int common[6] = { G64VK_IN_COMBINED, -1, -1, G64VK_IN_PRIM, G64VK_IN_SHADE, G64VK_IN_ENV };
      if (v == 1 || v == 2)
         return texel_in(second, v - 1, 0);
      return common[v];
   }
   switch (slot)
   {
      case 0:
         return v == 6 ? G64VK_IN_ONE : v == 7 ? G64VK_IN_NOISE : G64VK_IN_ZERO;
      case 1:
         return v == 6 ? G64VK_IN_CENTER : v == 7 ? G64VK_IN_K4 : G64VK_IN_ZERO;
      case 2:
         switch (v)
         {
            case 6:  return G64VK_IN_SCALE;
            case 7:  return G64VK_IN_COMBINED_A;
            case 8:  case 9: return texel_in(second, v - 8, 1);
            case 10: return G64VK_IN_PRIM_A;
            case 11: return G64VK_IN_SHADE_A;
            case 12: return G64VK_IN_ENV_A;
            case 13: return G64VK_IN_LOD;
            case 14: return G64VK_IN_PRIM_LOD;
            case 15: return G64VK_IN_K5;
         }
         return G64VK_IN_ZERO;
      default:
         return v == 6 ? G64VK_IN_ONE : G64VK_IN_ZERO;
   }
}

/* the alpha's: the shader takes .w, where the colour inputs have it */
static int alpha_in(int second, int slot, unsigned v)
{
   if (slot == 2)
   {
      switch (v)
      {
         case 0: return G64VK_IN_LOD;
         case 1: case 2: return texel_in(second, v - 1, 0);
         case 3: return G64VK_IN_PRIM;
         case 4: return G64VK_IN_SHADE;
         case 5: return G64VK_IN_ENV;
         case 6: return G64VK_IN_PRIM_LOD;
      }
      return G64VK_IN_ZERO;
   }
   switch (v)
   {
      case 0: return G64VK_IN_COMBINED;
      case 1: case 2: return texel_in(second, v - 1, 0);
      case 3: return G64VK_IN_PRIM;
      case 4: return G64VK_IN_SHADE;
      case 5: return G64VK_IN_ENV;
      case 6: return G64VK_IN_ONE;
   }
   return G64VK_IN_ZERO;
}

static void set_combiner(const glitch64_n64_combiner *c)
{
   int i, k;
   vk.flags &= ~(G64VK_TWO_CYCLE | G64VK_FOG_RGB | G64VK_FOG_ALPHA | G64VK_LOD_PIXEL | G64VK_NOISE);
   for (i = 0; i < 2; i++)
   {
      const int second = c->two_cycle && i == 1;
      for (k = 0; k < 4; k++)
      {
         vk.ops[8 * i + k] = rgb_in(second, k, c->rgb[i][k]);
         vk.ops[8 * i + 4 + k] = alpha_in(second, k, c->alpha[i][k]);
      }
      if ((c->two_cycle || i == 1) && c->rgb[i][0] == 7)
         vk.flags |= G64VK_NOISE;
   }
   if (c->two_cycle)
      vk.flags |= G64VK_TWO_CYCLE;
   if (c->flags & GLITCH64_N64_FOG_RGB)
      vk.flags |= G64VK_FOG_RGB;
   if (c->flags & GLITCH64_N64_FOG_ALPHA)
      vk.flags |= G64VK_FOG_ALPHA;
   if (c->flags & GLITCH64_N64_LOD)
      vk.flags |= G64VK_LOD_PIXEL;
}

/* ---- drawing ---- */

/* The program of these ops and flags: its own, written and built by the
 * renderer's worker while the shared one draws for it; nothing compiles
 * here. A table of fixed size, made at load. */
static vk_own *own_find(const int *key)
{
   uint32_t h = 2166136261u;
   unsigned i;

   for (i = 0; i < G64VK_KEY_WORDS; i++)
      h = (h ^ (uint32_t)key[i]) * 16777619u;
   for (i = 0; i < VK_OWN_SLOTS; i++)
   {
      vk_own *o = &vk.own[(h + i) & (VK_OWN_SLOTS - 1)];
      if (!o->p)
      {
         vkr_client_desc d;
         if (vk.n_own >= VK_OWN)
            return NULL;
         memset(&d, 0, sizeof(d));
         d.layout = vk.layout;
         d.fallback = vk.combiner;
         d.write_fs = glitch64_vk_write_own;
         memcpy(d.key, key, G64VK_KEY_WORDS * sizeof(int));
         o->p = vkr_client_new(vk.r, &d);
         if (!o->p)
            return NULL;
         memcpy(o->key, key, G64VK_KEY_WORDS * sizeof(int));
         o->blends = 0;
         vk.n_own++;
         return o;
      }
      if (!memcmp(o->key, key, G64VK_KEY_WORDS * sizeof(int)))
         return o;
   }
   return NULL;
}

static vkr_program *own_program(int flags)
{
   int key[G64VK_KEY_WORDS];
   vk_own *o;

   if (flags & G64VK_COPY)
      return vk.combiner;
   memcpy(key, vk.ops, sizeof(vk.ops));
   key[16] = flags;
   o = own_find(key);
   if (!o)
      return vk.combiner;
   if (vk.blend_index >= 0 && !(o->blends & (1u << vk.blend_index)))
   {
      o->blends |= 1u << vk.blend_index;
      vk.own_dirty = 1;
   }
   return o->p;
}

/* @n vertices bound already: the program, its values, the draw */
static void draw_bound(unsigned n, int flags)
{
   static const float none[4] = { 0.f, 0.f, 0.f, 0.f };
   vkr_program *p = own_program(flags);
   vkr_use(vk.r, p);
   vkr_values_raw(vk.r, p, &vk.values, sizeof(vk.values));
   vkr_client_push(vk.r, p, vk.ops, flags, none);
   vkr_draw_tris(vk.r, VKR_TOPO_TRIANGLES, n, 0);
}

/* Glitch64's own few vertices */
static void draw_vertices(const void *data, unsigned n, int flags)
{
   void *dst;
   if (!n)
      return;
   dst = vkr_vertex_reserve(vk.r, n * sizeof(vk_vertex));
   if (!dst)
      return;
   memcpy(dst, data, n * sizeof(vk_vertex));
   draw_bound(n, flags);
}

void vk_flush(void)
{
   unsigned n = vk.n_verts;
   int flags = vk.flags;
   if (!vk.verts || !vk.r)
      return;
   vkr_vertex_close(vk.r, n * sizeof(vk_vertex));
   vk.verts = NULL;
   vk.n_verts = 0;
   if (!n)
      return;
   if (vk.fog_mode)
      flags |= G64VK_FOG;
   if (vk.dither)
      flags |= G64VK_DITHER;
   if (vk.three_point[0])
      flags |= G64VK_3POINT0;
   if (vk.three_point[1])
      flags |= G64VK_3POINT1;
   vk.values.fog[0] = vk.fog_mode != 2 ? 0.f : 1.f;
   vk.values.fog[1] = fogEnd;
   vk.values.fog[2] = 1.f / (fogEnd - fogStart);
   vk.values.fog[3] = vk.alpha_test ? vk.alpha_ref / 255.f : -1.f;
   draw_bound(n, flags);
}

#define VK_VERTEX(i) ((const char*)pointers + (size_t)(i) * sizeof(VERTEX))

void vk_grDrawVertexArrayContiguous(uint32_t mode, uint32_t count, void *pointers)
{
   const unsigned tris = count < 3 ? 0 : (mode == GR_TRIANGLES ? count / 3 : count - 2);
   unsigned i;

   if (vk.n_verts + tris * 3 > VK_VERTICES)
      vk_flush();
   if (!tris || tris * 3 > VK_VERTICES || !vk.r)
      return;
   if (!vk.verts)
   {
      vk.verts = (vk_vertex*)vkr_vertex_open(vk.r, VK_VERTICES * sizeof(vk_vertex));
      if (!vk.verts)
         return;
   }
   for (i = 0; i < tris; i++)
   {
      unsigned a, b, c;
      if (mode == GR_TRIANGLES)
      {
         a = 3 * i; b = a + 1; c = a + 2;
      }
      else if (mode == GR_TRIANGLE_FAN)
      {
         a = 0; b = i + 1; c = i + 2;
      }
      else if (i & 1)
      {
         a = i + 1; b = i; c = i + 2;
      }
      else
      {
         a = i; b = i + 1; c = i + 2;
      }
      memcpy(&vk.verts[vk.n_verts++], VK_VERTEX(a), sizeof(vk_vertex));
      memcpy(&vk.verts[vk.n_verts++], VK_VERTEX(b), sizeof(vk_vertex));
      memcpy(&vk.verts[vk.n_verts++], VK_VERTEX(c), sizeof(vk_vertex));
   }
}

/* ---- state: each call that changes something draws what came before ---- */

void vk_grCullMode(int32_t mode)
{
   if (mode == vk.cull)
      return;
   vk_flush();
   vk.cull = mode;
   /* the frame is kept as GL has it, rows bottom up: Vulkan sees each
    * triangle the other way round */
   vkr_enable(vk.r, VKR_CAP_CULL, mode != GR_CULL_DISABLE);
   vkr_cull(vk.r, mode == GR_CULL_NEGATIVE ? VKR_CULL_BACK : VKR_CULL_FRONT);
}

static int vk_cmp(unsigned func)
{
   switch (func)
   {
      case GL_LESS:   return VKR_CMP_LESS;
      case GL_ALWAYS: return VKR_CMP_ALWAYS;
   }
   return VKR_CMP_LEQUAL;
}

void vk_grDepthBufferMode(int32_t mode)
{
   const int on = mode != GR_DEPTHBUFFER_DISABLE;
   if (on == vk.depth_on)
      return;
   vk_flush();
   vk.depth_on = on;
   vkr_enable(vk.r, VKR_CAP_DEPTH_TEST, on);
}

void vk_grDepthBufferFunction(unsigned func)
{
   if ((int)func == vk.depth_func)
      return;
   vk_flush();
   vk.depth_func = (int)func;
   vkr_depth_compare(vk.r, vk_cmp(func));
}

void vk_grDepthMask(bool mask)
{
   if ((int)mask == vk.depth_mask)
      return;
   vk_flush();
   vk.depth_mask = mask;
   vkr_depth_write(vk.r, mask);
}

void vk_grDepthBiasLevel(int32_t level)
{
   if (level == vk.bias)
      return;
   vk_flush();
   vk.bias = level;
   vkr_enable(vk.r, VKR_CAP_POLY_OFFSET, level != 0);
   vkr_polygon_offset(vk.r, polygonOffsetFactor, (float)level * settings.depth_bias * 0.01f);
}

void vk_grClipWindow(uint32_t minx, uint32_t miny, uint32_t maxx, uint32_t maxy)
{
   if (minx == vk.clip[0] && miny == vk.clip[1] && maxx == vk.clip[2] && maxy == vk.clip[3])
      return;
   vk_flush();
   vk.clip[0] = minx; vk.clip[1] = miny; vk.clip[2] = maxx; vk.clip[3] = maxy;
   vkr_scissor(vk.r, (int)minx, height - (int)maxy, (int)(maxx - minx), (int)(maxy - miny));
   vkr_enable(vk.r, VKR_CAP_SCISSOR, 1);
}

void vk_grColorMask(bool rgb, bool a)
{
   vk_flush();
   vkr_color_mask(vk.r, rgb, a);
}

static int vk_blend(unsigned f)
{
   switch (f)
   {
      case GL_ZERO:                return VKR_BLEND_ZERO;
      case GL_SRC_ALPHA:           return VKR_BLEND_SRC_ALPHA;
      case GL_ONE_MINUS_SRC_ALPHA: return VKR_BLEND_INV_SRC_ALPHA;
      case GL_DST_ALPHA:           return VKR_BLEND_DST_ALPHA;
   }
   return VKR_BLEND_ONE;
}

void vk_grAlphaBlendFunction(unsigned rgb_sf, unsigned rgb_df, unsigned alpha_sf, unsigned alpha_df)
{
   if (vk.blend[0] == rgb_sf && vk.blend[1] == rgb_df && vk.blend[2] == alpha_sf && vk.blend[3] == alpha_df)
      return;
   vk_flush();
   vk.blend[0] = rgb_sf; vk.blend[1] = rgb_df; vk.blend[2] = alpha_sf; vk.blend[3] = alpha_df;
   vkr_enable(vk.r, VKR_CAP_BLEND, 1);
   vkr_blend(vk.r, vk_blend(rgb_sf), vk_blend(rgb_df), vk_blend(alpha_sf), vk_blend(alpha_df));
   {
      unsigned b;
      vk.blend_index = -1;
      for (b = 1; b < VK_BLENDS; b++)
         if (vk_blends[b][0] == vk_blend(rgb_sf) && vk_blends[b][1] == vk_blend(rgb_df)
               && vk_blends[b][2] == vk_blend(alpha_sf) && vk_blends[b][3] == vk_blend(alpha_df))
            vk.blend_index = (int)b;
   }
}

void vk_grAlphaTestReferenceValue(uint8_t value)
{
   if (vk.alpha_ref == value)
      return;
   vk_flush();
   vk.alpha_ref = value;
}

void vk_grAlphaTestFunction(int32_t function, uint8_t value, int set_alpha_ref)
{
   const int test = function != GR_CMP_ALWAYS;
   const int ref = set_alpha_ref ? value : vk.alpha_ref;
   if (vk.alpha_func == function && vk.alpha_test == test && vk.alpha_ref == ref)
      return;
   vk_flush();
   vk.alpha_func = function;
   vk.alpha_test = test;
   vk.alpha_ref = ref;
}

void vk_grFogMode(int32_t mode)
{
   if (vk.fog_mode == mode)
      return;
   vk_flush();
   vk.fog_mode = mode;
}

void vk_grStippleMode(int32_t mode)
{
   const int d = mode != GR_STIPPLE_DISABLE;
   if (d == vk.dither)
      return;
   vk_flush();
   vk.dither = d;
}

void vk_grN64Combine(const glitch64_n64_combiner *c)
{
   uint32_t key[2];
   glitch64_n64_key(c, key);
   if (key[0] == vk.key[0] && key[1] == vk.key[1])
      return;
   vk_flush();
   vk.key[0] = key[0];
   vk.key[1] = key[1];
   set_combiner(c);
}

void vk_grN64Constants(const float prim[4], const float env[4], const float consts[4],
      const float level[4], const float key_center[3], const float key_scale[3])
{
   if (!memcmp(vk.values.prim, prim, 16) && !memcmp(vk.values.env, env, 16)
         && !memcmp(vk.values.consts, consts, 16) && !memcmp(vk.values.level, level, 16)
         && !memcmp(vk.values.key_center, key_center, 12) && !memcmp(vk.values.key_scale, key_scale, 12)
         && vk.fog_rgba == g_gdp.fog_color.total)
      return;
   vk_flush();
   vk.fog_rgba = g_gdp.fog_color.total;
   vk.values.fog_color[0] = g_gdp.fog_color.r / 255.f;
   vk.values.fog_color[1] = g_gdp.fog_color.g / 255.f;
   vk.values.fog_color[2] = g_gdp.fog_color.b / 255.f;
   vk.values.fog_color[3] = g_gdp.fog_color.a / 255.f;
   memcpy(vk.values.prim, prim, 16);
   memcpy(vk.values.env, env, 16);
   memcpy(vk.values.consts, consts, 16);
   memcpy(vk.values.level, level, 16);
   memcpy(vk.values.key_center, key_center, 12);
   memcpy(vk.values.key_scale, key_scale, 12);
}

/* ---- textures ---- */

static vk_tex *tex_find(unsigned id)
{
   return vk_tex_find(vk.textures, vk.n_textures, id);
}

/* A texture of TMU memory written over: its image kept, as long as there
 * is room, for the next of its format and size, which then takes it with
 * no new image made (the renderer orders the new texels after what read
 * the old) */
static void tex_retire(const vk_tex *t)
{
   if (vk.n_spare < VK_SPARE)
      vk.spare[vk.n_spare++] = *t;
   else
      vkr_texture_delete(vk.r, t->handle);
}

static unsigned tex_image(int fmt, unsigned w, unsigned h)
{
   unsigned i = vk.n_spare;
   while (i--)
      if (vk.spare[i].fmt == fmt && vk.spare[i].w == w && vk.spare[i].h == h)
      {
         const unsigned handle = vk.spare[i].handle;
         vk.spare[i] = vk.spare[--vk.n_spare];
         return handle;
      }
   return vkr_texture_new(vk.r);
}

/* TMU memory written over: the textures it held are gone; where a texture
 * at @lo now goes */
static unsigned tex_remove(unsigned lo, unsigned hi)
{
   unsigned a, b, i;
   vk_tex_range(vk.textures, vk.n_textures, lo, hi, &a, &b);
   for (i = a; i < b; i++)
   {
      const vk_tex *t = &vk.textures[i];
      if (vk.bound[0] == t->handle)
         vk.bound[0] = 0;
      if (vk.bound[1] == t->handle)
         vk.bound[1] = 0;
      tex_retire(t);
   }
   vk_tex_close(vk.textures, &vk.n_textures, a, b);
   return a;
}

/* The renderer's format for Glide's texels as they lie: nothing is
 * converted but AI44, widened to a byte each; 0 for a format Glide64 never
 * gives */
static int vk_format(int fmt, unsigned *bpp)
{
   *bpp = 2;
   switch (fmt)
   {
      case GR_TEXFMT_ALPHA_8:            *bpp = 1; return VKR_FMT_I8; /* GL's INTENSITY8 */
      case GR_TEXFMT_INTENSITY_8:        *bpp = 1; return VKR_FMT_L8;
      case GR_TEXFMT_ALPHA_INTENSITY_44: return VKR_FMT_LA8;
      case GR_TEXFMT_ALPHA_INTENSITY_88: return VKR_FMT_LA8;
      case GR_TEXFMT_RGB_565:            return VKR_FMT_RGB565;
      case GR_TEXFMT_ARGB_1555:          return VKR_FMT_ARGB1555;
      case GR_TEXFMT_ARGB_4444:          return VKR_FMT_ARGB4444;
      case GR_TEXFMT_ARGB_8888:          *bpp = 4; return VKR_FMT_BGRA8;
   }
   return 0;
}

static int vk_wrap(int gl)
{
   if (gl == GL_CLAMP_TO_EDGE)
      return VKR_WRAP_CLAMP;
   if (gl == GL_MIRRORED_REPEAT)
      return VKR_WRAP_MIRROR;
   return VKR_WRAP_REPEAT;
}

static void tex_params(unsigned index)
{
   if (vk.bound[index])
      vkr_texture_params(vk.r, vk.bound[index], vk.mag_f[index], vk.min_f[index], 0,
            vk.wrap_s[index], vk.wrap_t[index], 0.f, 1.f);
}

void vk_grTexSource(int32_t tmu, uint32_t start, void *info_, int do_download)
{
   GrTexInfo *info = (GrTexInfo*)info_;
   /* GR_TMU0 holds tile 0 */
   const unsigned index = tmu == GR_TMU1 ? 1 : 0;
   const unsigned id = start + 1;
   int w, h;
   vk_tex *t;

   vk_flush();
   h = 1 << info->largeLodLog2;
   w = 1 << info->largeLodLog2;
   if (info->aspectRatioLog2 < 0)
      w = h >> -info->aspectRatioLog2;
   else
      h = w >> info->aspectRatioLog2;

   if (do_download)
   {
      unsigned bpp;
      const int fmt = vk_format(info->format, &bpp);
      const unsigned n = (unsigned)(w * h);
      const size_t bytes = (size_t)n * bpp;
      unsigned at = tex_remove(id, id + (unsigned)(w * h * (int)grTexCalcMemRequired(0, 0, info->format)));
      if (vk.n_textures >= VK_TEXTURES)
         at = tex_remove(0, ~0u);
      t = vk_tex_open(vk.textures, &vk.n_textures, at);
      t->id = id;
      t->fmt = fmt;
      t->w = (uint16_t)w;
      t->h = (uint16_t)h;
      t->handle = tex_image(fmt, (unsigned)w, (unsigned)h);
      if (info->format == GR_TEXFMT_ALPHA_INTENSITY_44)
      {
         /* a nibble each to a byte each, as GL's LUMINANCE_ALPHA had them:
          * written straight where the GPU reads them */
         const uint8_t *src = (const uint8_t*)info->data;
         uint8_t *dst = (uint8_t*)vkr_texture_lend(vk.r, bytes);
         unsigned k;
         if (!dst)
            dst = bytes <= 2048u * 2048u * 4u ? vk.staging : NULL;
         if (dst)
         {
            for (k = 0; k < n; k++)
            {
               dst[2 * k]     = (uint8_t)((src[k] & 15) * 17);
               dst[2 * k + 1] = (uint8_t)((src[k] >> 4) * 17);
            }
            vkr_texture_init(vk.r, t->handle, fmt, 1, (unsigned)w, (unsigned)h, 0, 1, dst, bytes);
         }
      }
      else if (fmt)
         /* the texels as Glide64 decoded them: the one copy is into the
          * ring, from where the GPU takes them */
         vkr_texture_init(vk.r, t->handle, fmt, 1, (unsigned)w, (unsigned)h, 0, 1, info->data, bytes);
      info->width = w;
      info->height = h;
   }
   else
      t = tex_find(id);

   vk.bound[index] = t ? t->handle : 0;
   vkr_bind(vk.r, index ? VKR_BIND_TEX1 : VKR_BIND_TEX0, vk.bound[index]);
   tex_params(index);

   {
      int sw = 256, sh = 256;
      if (info->aspectRatioLog2 < 0)
         sw = sh >> -info->aspectRatioLog2;
      else
         sh = sw >> info->aspectRatioLog2;
      vk.values.tex_sizes[2 * index] = (float)sw;
      vk.values.tex_sizes[2 * index + 1] = (float)sh;
      vk.values.exact_sizes[2 * index] = (float)info->width;
      vk.values.exact_sizes[2 * index + 1] = (float)info->height;
   }
}

void vk_grTexFilterClampMode(int32_t tmu, int32_t s_clamp, int32_t t_clamp, int32_t minf, int32_t magf)
{
   const unsigned index = tmu == GR_TMU1 ? 1 : 0;
   vk_flush();
   vk.wrap_s[index] = vk_wrap(s_clamp);
   vk.wrap_t[index] = vk_wrap(t_clamp);
   vk.min_f[index] = minf == GR_TEXTUREFILTER_POINT_SAMPLED ? VKR_FILTER_NEAREST : VKR_FILTER_LINEAR;
   vk.mag_f[index] = magf == GR_TEXTUREFILTER_POINT_SAMPLED ? VKR_FILTER_NEAREST : VKR_FILTER_LINEAR;
#ifdef DISABLE_3POINT
   vk.three_point[index] = 0;
#else
   vk.three_point[index] = magf == GR_TEXTUREFILTER_3POINT_LINEAR;
#endif
   tex_params(index);
}

/* ---- the frame ---- */

static void reset_state(void)
{
   unsigned i;
   vk.n_verts = 0;
   vk.verts = NULL;
   vk.cull = -1;
   vk.depth_func = -1;
   vk.depth_mask = -1;
   vk.depth_on = -1;
   vk.bias = NO_BIAS;
   vk.blend_index = 0;
   vk.fog_rgba = ~g_gdp.fog_color.total;
   for (i = 0; i < 4; i++)
   {
      vk.clip[i] = ~0u;
      vk.blend[i] = ~0u;
   }
   vk.key[0] = vk.key[1] = 0;
   vk.alpha_test = 0;
   vk.alpha_func = GR_CMP_ALWAYS;
   vk.fog_mode = 0;
   vk.dither = 0;
   for (i = 0; i < 2; i++)
   {
      vk.bound[i] = 0;
      vk.wrap_s[i] = vk.wrap_t[i] = VKR_WRAP_REPEAT;
      vk.min_f[i] = vk.mag_f[i] = VKR_FILTER_NEAREST;
      vk.three_point[i] = 0;
   }
   memset(&vk.values, 0, sizeof(vk.values));
   vk.values.tex_sizes[0] = vk.values.tex_sizes[1] = vk.values.tex_sizes[2] = vk.values.tex_sizes[3] = 256.f;
   vk.values.exact_sizes[0] = vk.values.exact_sizes[1] = vk.values.exact_sizes[2] = vk.values.exact_sizes[3] = 1.f;
   vk.values.window[0] = (float)width / 2.f;
   vk.values.window[1] = (float)height / 2.f;
   vk.values.window[2] = 1.f;

   /* until the first Combine: TEXEL0 */
   {
      glitch64_n64_combiner c;
      memset(&c, 0, sizeof(c));
      c.rgb[1][0] = 15; c.rgb[1][1] = 15; c.rgb[1][2] = 31; c.rgb[1][3] = 1;
      c.alpha[1][0] = 7; c.alpha[1][1] = 7; c.alpha[1][2] = 7; c.alpha[1][3] = 1;
      vk.flags = 0;
      set_combiner(&c);
      glitch64_n64_key(&c, vk.key);
   }

   /* GL's defaults */
   vkr_enable(vk.r, VKR_CAP_DEPTH_TEST, 0);
   vkr_enable(vk.r, VKR_CAP_CULL, 0);
   vkr_enable(vk.r, VKR_CAP_BLEND, 0);
   vkr_enable(vk.r, VKR_CAP_SCISSOR, 0);
   vkr_enable(vk.r, VKR_CAP_POLY_OFFSET, 0);
   vkr_depth_compare(vk.r, VKR_CMP_LESS);
   vkr_depth_write(vk.r, 1);
   vkr_color_mask(vk.r, 1, 1);
   vkr_viewport(vk.r, 0, 0, width, height);
}

static vkr_program *make_program(int depth)
{
   static const uint32_t formats[5] = {
      VK_FORMAT_R32G32B32A32_SFLOAT, VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R32G32_SFLOAT,
      VK_FORMAT_R32G32_SFLOAT, VK_FORMAT_R32_SFLOAT
   };
   static const uint32_t offsets[5] = { G64VK_OFF_POS, G64VK_OFF_COLOR, G64VK_OFF_TC0, G64VK_OFF_TC1, G64VK_OFF_FOG };
   vkr_client_desc d;
   uint32_t *vs;
   const uint32_t *words;
   unsigned n = 0, i;
   vkr_program *p;

   memset(&d, 0, sizeof(d));
   words = glitch64_vk_vertex(&vk.spv, &n);
   if (!words)
      return NULL;
   /* the module's words last until it is written again */
   vs = (uint32_t*)malloc(n * sizeof(uint32_t));
   if (!vs)
      return NULL;
   memcpy(vs, words, n * sizeof(uint32_t));
   d.vs = vs;
   d.vs_words = n;
   d.fs = glitch64_vk_fragment(&vk.spv, depth, NULL, &d.fs_words);
   d.layout.stride = G64VK_VERTEX_STRIDE;
   d.layout.count = 5;
   for (i = 0; i < 5; i++)
   {
      d.layout.attrs[i].location = i;
      d.layout.attrs[i].format = formats[i];
      d.layout.attrs[i].offset = offsets[i];
   }
   vk.layout = d.layout;
   p = d.fs ? vkr_client_new(vk.r, &d) : NULL;
   free(vs);
   return p;
}

/* Every pipeline Glide64 can draw with, at load (AGENTS.md: never one on
 * the draw path). The blend pairs are those of Combine.c's A_BLEND and the
 * blend functions of glide64_gDP.c, TexCache.c and Framebuffer_glide64.c:
 * the colour's, and the alpha's. */
/* The combiners an earlier session met, with their own pipelines built at
 * load for the blend functions they drew with (their depth and cull state
 * is dynamic where the device has it, the shared shader stands in where
 * not): what a game draws again starts on its own shaders. */
static void journal_load(void)
{
   void *data = NULL;
   int64_t size = 0;
   const uint32_t *w;
   size_t n, i;

   if (!vk.journal[0] || !filestream_exists(vk.journal)
         || !filestream_read_file(vk.journal, &data, &size) || !data)
      return;
   w = (const uint32_t*)data;
   n = size >= 8 ? (size_t)(size - 8) / ((G64VK_KEY_WORDS + 1) * 4) : 0;
   if (size >= 8 && w[0] == VK_JOURNAL_MAGIC && w[1] == VK_JOURNAL_VERSION)
   {
      vkr_enable(vk.r, VKR_CAP_DEPTH_TEST, 1);
      vkr_depth_write(vk.r, 1);
      vkr_depth_compare(vk.r, VKR_CMP_LEQUAL);
      vkr_enable(vk.r, VKR_CAP_CULL, 0);
      for (i = 0; i < n; i++)
      {
         const uint32_t *rec = w + 2 + i * (G64VK_KEY_WORDS + 1);
         int key[G64VK_KEY_WORDS];
         unsigned b, k;
         vk_own *o;
         for (k = 0; k < G64VK_KEY_WORDS; k++)
            key[k] = (int)rec[k];
         if (key[16] & G64VK_COPY)
            continue;
         for (k = 0; k < 16 && key[k] >= 0 && key[k] < G64VK_IN_COUNT; k++)
            ;
         if (k < 16 || !(o = own_find(key)))
            continue;
         o->blends |= rec[G64VK_KEY_WORDS] & ((1u << VK_BLENDS) - 1);
         for (b = 0; b < VK_BLENDS; b++)
            if (o->blends & (1u << b))
            {
               vkr_enable(vk.r, VKR_CAP_BLEND, b != 0);
               vkr_blend(vk.r, vk_blends[b][0], vk_blends[b][1], vk_blends[b][2], vk_blends[b][3]);
               vkr_client_prewarm(vk.r, o->p);
            }
      }
   }
   free(data);
   vk.own_dirty = 0;
}

/* At close, when this session met a combiner or a blend function more */
static void journal_save(void)
{
   RFILE *f;
   uint32_t head[2];
   unsigned i, k;

   if (!vk.journal[0] || !vk.own_dirty)
      return;
   f = filestream_open(vk.journal, RETRO_VFS_FILE_ACCESS_WRITE, RETRO_VFS_FILE_ACCESS_HINT_NONE);
   if (!f)
      return;
   head[0] = VK_JOURNAL_MAGIC;
   head[1] = VK_JOURNAL_VERSION;
   filestream_write(f, head, sizeof(head));
   for (i = 0; i < VK_OWN_SLOTS; i++)
      if (vk.own[i].p)
      {
         uint32_t rec[G64VK_KEY_WORDS + 1];
         for (k = 0; k < G64VK_KEY_WORDS; k++)
            rec[k] = (uint32_t)vk.own[i].key[k];
         rec[G64VK_KEY_WORDS] = vk.own[i].blends;
         filestream_write(f, rec, sizeof(rec));
      }
   filestream_close(f);
   vk.own_dirty = 0;
}

static void prewarm(void)
{
   static const int cmps[3] = { VKR_CMP_LESS, VKR_CMP_LEQUAL, VKR_CMP_ALWAYS };
   unsigned b, d, c;

   vkr_color_mask(vk.r, 1, 1);
   for (b = 0; b < VK_BLENDS; b++)
   {
      vkr_enable(vk.r, VKR_CAP_BLEND, b != 0);
      vkr_blend(vk.r, vk_blends[b][0], vk_blends[b][1], vk_blends[b][2], vk_blends[b][3]);
      /* depth off, or on with each write and test */
      for (d = 0; d < 7; d++)
      {
         vkr_enable(vk.r, VKR_CAP_DEPTH_TEST, d != 0);
         vkr_depth_write(vk.r, d ? (int)((d - 1) & 1) : 1);
         vkr_depth_compare(vk.r, d ? cmps[(d - 1) >> 1] : VKR_CMP_LESS);
         for (c = 0; c < 3; c++)
         {
            vkr_enable(vk.r, VKR_CAP_CULL, c != 0);
            vkr_cull(vk.r, c == 1 ? VKR_CULL_FRONT : VKR_CULL_BACK);
            vkr_client_prewarm(vk.r, vk.combiner);
         }
      }
   }
   /* the depth image's own state */
   vkr_enable(vk.r, VKR_CAP_BLEND, 0);
   vkr_enable(vk.r, VKR_CAP_CULL, 0);
   vkr_enable(vk.r, VKR_CAP_DEPTH_TEST, 1);
   vkr_depth_write(vk.r, 1);
   vkr_depth_compare(vk.r, VKR_CMP_ALWAYS);
   vkr_color_mask(vk.r, 0, 0);
   vkr_client_prewarm(vk.r, vk.depth);
   vkr_color_mask(vk.r, 1, 1);
   journal_load();
   vkr_client_prewarm_wait(vk.r);
}

uint32_t vk_grSstWinOpen(void)
{
   vkr_shader_config cfg;

   if (vk.r)
      vk_grSstWinClose();
   if (!vk.spv_ready)
   {
      if (!spv_init(&vk.spv))
         return 0;
      vk.spv_ready = 1;
   }
   memset(&cfg, 0, sizeof(cfg));
   /* the game's combiners, and the driver's pipeline cache beside them */
   combiner_key_path(vk.journal, sizeof(vk.journal), "glide64-vulkan", gfx_info.HEADER);
   {
      char cache[PATH_MAX_LENGTH + 16];
      snprintf(cache, sizeof(cache), "%s.pipelines", vk.journal);
      vk.r = vkr_create(gliden64_vulkan_interface(), &cfg, cache);
   }
   if (!vk.r)
      return 0;
   vkr_set_window(vk.r, (unsigned)width, (unsigned)height);

   vk.buf = (uint8_t*)malloc((size_t)width * height * 4);
   vk.staging = (uint8_t*)malloc(2048u * 2048u * 4u);
   vk.rb = vkr_readback_new(vk.r, (size_t)width * height * 4);
   vk.combiner = make_program(0);
   vk.depth = make_program(1);
   if (!vk.buf || !vk.staging || !vk.rb || !vk.combiner || !vk.depth)
   {
      vk_grSstWinClose();
      return 0;
   }

   vk.color = vkr_texture_new(vk.r);
   vk.zbuf = vkr_texture_new(vk.r);
   vk.fbo = vkr_fbo_new(vk.r);
   vkr_texture_init(vk.r, vk.color, VKR_FMT_RGBA8, 1, (unsigned)width, (unsigned)height, 0, 1, NULL, 0);
   vkr_texture_init(vk.r, vk.zbuf, VKR_FMT_DEPTH, 1, (unsigned)width, (unsigned)height, 0, 1, NULL, 0);
   vkr_fbo_color(vk.r, vk.fbo, vk.color);
   vkr_fbo_depth(vk.r, vk.fbo, vk.zbuf);
   vkr_fbo_bind(vk.r, 1, 1, vk.fbo);
   vk.lfb_tex = vkr_texture_new(vk.r);
   vk.depth_tex = vkr_texture_new(vk.r);
   vk.n_textures = 0;
   vk.n_spare = 0;

   prewarm();
   reset_state();
   {
      static const float black[4] = { 0.f, 0.f, 0.f, 1.f };
      vkr_clear_masked(vk.r, black, 1, 1.f);
   }
   return 1;
}

void vk_grSstWinClose(void)
{
   unsigned i;
   if (!vk.r)
      return;
   for (i = 0; i < vk.n_textures; i++)
      vkr_texture_delete(vk.r, vk.textures[i].handle);
   vk.n_textures = 0;
   for (i = 0; i < vk.n_spare; i++)
      vkr_texture_delete(vk.r, vk.spare[i].handle);
   vk.n_spare = 0;
   journal_save();
   for (i = 0; i < VK_OWN_SLOTS; i++)
      if (vk.own[i].p)
      {
         vkr_program_delete(vk.r, vk.own[i].p);
         vk.own[i].p = NULL;
      }
   vk.n_own = 0;
   if (vk.combiner)
      vkr_program_delete(vk.r, vk.combiner);
   if (vk.depth)
      vkr_program_delete(vk.r, vk.depth);
   if (vk.rb)
      vkr_readback_delete(vk.r, vk.rb);
   vkr_destroy(vk.r);
   vk.r = NULL;
   vk.verts = NULL;
   vk.n_verts = 0;
   vk.combiner = vk.depth = NULL;
   vk.rb = NULL;
   vk.color = vk.zbuf = vk.fbo = vk.lfb_color = vk.lfb_fbo = vk.lfb_tex = vk.depth_tex = 0;
   free(vk.buf);
   free(vk.staging);
   vk.buf = vk.staging = NULL;
}

void glitch64_vk_context_destroy(void)
{
   vk_grSstWinClose();
}

void vk_grBufferClear(uint32_t color, uint32_t alpha, uint32_t depth)
{
   float rgba[4];
   vk_flush();
   rgba[0] = ((color >> 24) & 0xFF) / 255.f;
   rgba[1] = ((color >> 16) & 0xFF) / 255.f;
   rgba[2] = (color & 0xFF) / 255.f;
   rgba[3] = alpha / 255.f;
   /* the depth mask holds for glClear too */
   vkr_clear_masked(vk.r, rgba, vk.depth_mask != 0, depth / 65535.f);
}

void vk_grBufferSwap(void)
{
   vk_flush();
   if (!vk.r)
      return;
   /* the window: what is presented, the frame as it is now */
   vkr_blit(vk.r, vk.fbo, 0, 0, 0, width, height, 0, 0, width, height, 1, 0, 0);
}

void glitch64_vk_present(void)
{
   if (!vk.r)
      return;
   /* what the next frame drew since the swap goes in this frame's commands:
    * its vertices are in this frame's ring */
   vk_flush();
   vkr_present(vk.r);
}

/* ---- the frame read back ---- */

static const uint8_t *read_rect(unsigned fbo, int x, int y, unsigned w, unsigned h)
{
   const uint8_t *src;
   vkr_fbo_bind(vk.r, 1, 0, fbo);
   vkr_readback_fbo(vk.r, vk.rb, x, y, w, h, 0);
   vkr_fbo_bind(vk.r, 1, 0, vk.fbo);
   vkr_readback_ready(vk.r, vk.rb, 1);
   src = (const uint8_t*)vkr_readback_data(vk.rb);
   if (!src)
      return NULL;
   memcpy(vk.buf, src, (size_t)w * h * 4);
   return vk.buf;
}

int32_t vk_grLfbLock(int32_t writeMode, void *info_)
{
   GrLfbInfo_t *info = (GrLfbInfo_t*)info_;
   vk_flush();
   info->strideInBytes = width * ((writeMode == GR_LFBWRITEMODE_888) ? 4 : 2);
   info->lfbPtr = glide64_frameBuffer;
   info->writeMode = writeMode;
   if (writeMode == GR_LFBWRITEMODE_565)
   {
      const uint8_t *b = read_rect(vk.fbo, 0, 0, (unsigned)width, (unsigned)height);
      int i, j;
      if (!b)
         return FXTRUE;
      for (j = 0; j < height; j++)
         for (i = 0; i < width; i++)
         {
            const uint8_t *p = &b[(j * width + i) * 4];
            glide64_frameBuffer[(height - j - 1) * width + i] =
               (uint16_t)(((p[0] >> 3) << 11) | ((p[1] >> 2) << 5) | (p[2] >> 3));
         }
   }
   return FXTRUE;
}

int vk_grLfbReadScaledInit(void)
{
   if (!vk.r)
      return 0;
   if (vk.lfb_fbo)
      return 1;
   vk.lfb_color = vkr_texture_new(vk.r);
   vk.lfb_fbo = vkr_fbo_new(vk.r);
   if (!vk.lfb_color || !vk.lfb_fbo)
      return 0;
   vkr_texture_init(vk.r, vk.lfb_color, VKR_FMT_RGBA8, 1, LFB_SCALED_MAX, LFB_SCALED_MAX, 0, 1, NULL, 0);
   vkr_fbo_color(vk.r, vk.lfb_fbo, vk.lfb_color);
   return 1;
}

const uint8_t *vk_grLfbReadScaled(int32_t x, int32_t y, int32_t w, int32_t h,
      uint32_t dw, uint32_t dh, uint32_t *stride)
{
   vk_flush();
   if (!vk.lfb_fbo || !vk.buf || w <= 0 || h <= 0 || dw == 0 || dh == 0
         || dw > LFB_SCALED_MAX || dh > LFB_SCALED_MAX
         || (size_t)dw * dh > (size_t)width * height)
      return NULL;
   /* each pixel the window's at its centre */
   vkr_blit(vk.r, vk.fbo, vk.lfb_fbo, x, height - (y + h), x + w, height - y,
         0, 0, (int)dw, (int)dh, 1, 0, 0);
   *stride = dw * 4;
   return read_rect(vk.lfb_fbo, 0, 0, dw, dh);
}

const uint8_t *vk_grLfbReadRect(int32_t x, int32_t y, int32_t w, int32_t h)
{
   vk_flush();
   return read_rect(vk.fbo, x, height - (y + h), (unsigned)w, (unsigned)h);
}

/* a quad of window pixels, as two triangles, with texel coordinates for tile 0 */
static void quad(vk_vertex *v, float x0, float y0, float x1, float y1, float u1, float v1)
{
   static const unsigned order[6] = { 0, 1, 2, 2, 1, 3 };
   vk_vertex c[4];
   unsigned i;
   memset(c, 0, sizeof(c));
   for (i = 0; i < 4; i++)
   {
      c[i].x = (i & 2) ? x1 : x0;
      c[i].y = (i & 1) ? y1 : y0;
      c[i].q = 1.f;
      c[i].coord[0] = (i & 2) ? u1 : 0.f;
      c[i].coord[1] = (i & 1) ? v1 : 0.f;
      c[i].a = 255;
   }
   for (i = 0; i < 6; i++)
      v[i] = c[order[i]];
}

/* Glitch64's own draw past the pixel pipeline: no clip window, culling,
 * depth test or blending; what the game set comes back after */
static void own_draw_begin(int depth)
{
   vkr_enable(vk.r, VKR_CAP_SCISSOR, 0);
   vkr_enable(vk.r, VKR_CAP_CULL, 0);
   vkr_enable(vk.r, VKR_CAP_BLEND, 0);
   vkr_enable(vk.r, VKR_CAP_POLY_OFFSET, 0);
   vkr_enable(vk.r, VKR_CAP_DEPTH_TEST, depth);
   if (depth)
   {
      vkr_depth_compare(vk.r, VKR_CMP_ALWAYS);
      vkr_depth_write(vk.r, 1);
      vkr_color_mask(vk.r, 0, 0);
   }
}

static void own_draw_end(void)
{
   vkr_enable(vk.r, VKR_CAP_SCISSOR, vk.clip[0] != ~0u);
   vkr_enable(vk.r, VKR_CAP_CULL, vk.cull > 0);
   vkr_enable(vk.r, VKR_CAP_BLEND, vk.blend[0] != ~0u);
   vkr_enable(vk.r, VKR_CAP_POLY_OFFSET, vk.bias != 0 && vk.bias != NO_BIAS);
   vkr_enable(vk.r, VKR_CAP_DEPTH_TEST, vk.depth_on > 0);
   vkr_depth_compare(vk.r, vk.depth_func < 0 ? VKR_CMP_LESS : vk_cmp((unsigned)vk.depth_func));
   vkr_depth_write(vk.r, vk.depth_mask != 0);
   vkr_color_mask(vk.r, 1, 1);
   vkr_bind(vk.r, VKR_BIND_TEX0, vk.bound[0]);
}

void vk_grLfbWriteRegion(uint32_t dst_x, uint32_t dst_y, uint32_t src_width, uint32_t src_height,
      int32_t src_stride, const void *src_data)
{
   const uint16_t *fb = (const uint16_t*)src_data;
   const unsigned half = (unsigned)src_stride / 2;
   const size_t bytes = (size_t)src_width * src_height * 4;
   uint8_t *dst;
   unsigned i, j;
   float sizes[4], exact[4];
   vk_vertex v[6];

   vk_flush();
   if (!src_width || !src_height)
      return;
   dst = (uint8_t*)vkr_texture_lend(vk.r, bytes);
   if (!dst)
      dst = bytes <= 2048u * 2048u * 4u ? vk.staging : NULL;
   if (!dst)
      return;
   /* 555: five bits to eight, the top bits repeated in the low ones */
   for (j = 0; j < src_height; j++)
      for (i = 0; i < src_width; i++)
      {
         const unsigned col = fb[j * half + i];
         const unsigned r = (col >> 10) & 0x1F, g = (col >> 5) & 0x1F, b = col & 0x1F;
         uint8_t *p = &dst[(j * src_width + i) * 4];
         p[0] = (uint8_t)((r << 3) | (r >> 2));
         p[1] = (uint8_t)((g << 3) | (g >> 2));
         p[2] = (uint8_t)((b << 3) | (b >> 2));
         p[3] = 0xFF;
      }
   vkr_texture_init(vk.r, vk.lfb_tex, VKR_FMT_RGBA8, 1, src_width, src_height, 0, 1, dst, bytes);
   vkr_texture_params(vk.r, vk.lfb_tex, VKR_FILTER_NEAREST, VKR_FILTER_NEAREST, 0,
         VKR_WRAP_CLAMP, VKR_WRAP_CLAMP, 0.f, 1.f);
   vkr_bind(vk.r, VKR_BIND_TEX0, vk.lfb_tex);

   memcpy(sizes, vk.values.tex_sizes, sizeof(sizes));
   memcpy(exact, vk.values.exact_sizes, sizeof(exact));
   vk.values.tex_sizes[0] = (float)src_width;
   vk.values.tex_sizes[1] = (float)src_height;
   /* the alpha test holds, as the GL copy shader has it */
   vk.values.fog[3] = vk.alpha_test ? vk.alpha_ref / 255.f : -1.f;
   own_draw_begin(0);
   quad(v, (float)dst_x, (float)dst_y, (float)(dst_x + src_width), (float)(dst_y + src_height),
         (float)src_width, (float)src_height);
   draw_vertices(v, 6, G64VK_COPY);
   own_draw_end();
   memcpy(vk.values.tex_sizes, sizes, sizeof(sizes));
   memcpy(vk.values.exact_sizes, exact, sizeof(exact));
}

void vk_grDepthImageWrite(const void *words, uint32_t w, uint32_t h,
      float x0, float y0, float x1, float y1)
{
   const uint16_t *src = (const uint16_t*)words;
   const size_t bytes = (size_t)w * h * 4;
   static const float none[4] = { 0.f, 0.f, 0.f, 0.f };
   float *dst, sizes[4];
   vk_vertex v[6];
   unsigned i, n = w * h;
   void *vd;

   vk_flush();
   if (!n)
      return;
   dst = (float*)vkr_texture_lend(vk.r, bytes);
   if (!dst)
      dst = bytes <= 2048u * 2048u * 4u ? (float*)vk.staging : NULL;
   if (!dst)
      return;
   /* the words as RDRAM has them, each pair swapped on a little endian
    * host; a word of z lands at z / 131072 + 0.5 in the window */
   for (i = 0; i < n; i++)
   {
#ifdef MSB_FIRST
      const unsigned k = i;
#else
      const unsigned k = (i ^ 1u) < n ? (i ^ 1u) : i;
#endif
      dst[i] = src[k] / 131072.f + 0.5f;
   }
   vkr_texture_init(vk.r, vk.depth_tex, VKR_FMT_R32F, 1, w, h, 0, 1, dst, bytes);
   vkr_texture_params(vk.r, vk.depth_tex, VKR_FILTER_NEAREST, VKR_FILTER_NEAREST, 0,
         VKR_WRAP_CLAMP, VKR_WRAP_CLAMP, 0.f, 1.f);
   vkr_bind(vk.r, VKR_BIND_TEX0, vk.depth_tex);

   memcpy(sizes, vk.values.tex_sizes, sizeof(sizes));
   vk.values.tex_sizes[0] = vk.values.tex_sizes[1] = 1.f;
   own_draw_begin(1);
   quad(v, x0, y0, x1, y1, (float)w, (float)h);
   vkr_use(vk.r, vk.depth);
   vkr_values_raw(vk.r, vk.depth, &vk.values, sizeof(vk.values));
   vkr_client_push(vk.r, vk.depth, vk.ops, 0, none);
   vd = vkr_vertex_reserve(vk.r, sizeof(v));
   if (vd)
   {
      memcpy(vd, v, sizeof(v));
      vkr_draw_tris(vk.r, VKR_TOPO_TRIANGLES, 6, 0);
   }
   own_draw_end();
   memcpy(vk.values.tex_sizes, sizes, sizeof(sizes));
}

#else

/* without GLideN64's renderer glitch64_vulkan stays 0: nothing reaches these */
void glitch64_vk_present(void) { }
void glitch64_vk_context_destroy(void) { }
uint32_t vk_grSstWinOpen(void) { return 0; }
void vk_grSstWinClose(void) { }
void vk_grBufferSwap(void) { }
void vk_grLfbWriteRegion(uint32_t dst_x, uint32_t dst_y, uint32_t src_width, uint32_t src_height,
      int32_t src_stride, const void *src_data)
{
   (void)dst_x; (void)dst_y; (void)src_width; (void)src_height; (void)src_stride; (void)src_data;
}

#endif
