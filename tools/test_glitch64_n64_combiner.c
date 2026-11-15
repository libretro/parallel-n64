/* test_glitch64_n64_combiner -- Glide64's combiner, the N64's own.
 *
 * Glide64 mapped each N64 combiner mode it knew onto a Voodoo's, by hand,
 * 15000 lines of it, and drew the rest as TEXEL0; Glitch64 then turned the
 * Voodoo's back into GLSL. It now writes the N64's (a - b) * c + d into
 * the shader directly (glitch64_n64_combiner.c).
 *
 * This draws with every input in every slot of the colour and the alpha
 * combiner, in one cycle and in two, and a run of mixed modes, from two
 * one-texel tiles, a shade, prim, env, K4, K5 and the LOD fractions all
 * different, and holds each pixel to the combiner worked out here in C:
 * one-cycle mode runs the second cycle's settings, and the second cycle of
 * two takes the first's result as COMBINED and reads the tiles swapped.
 * It also holds the shader key to a round trip, and checks the blender's
 * fog colour and alpha standing in for the combiner's.
 *
 * Glitch64 batches triangles while the state stays the same; a change of
 * the combiner's inputs, of the depth test, the cull mode, the blend or
 * the alpha test has to draw the triangles before it first. This draws
 * pairs of quads with each changed between them, and holds each quad to
 * its own state.
 *
 *   EGL_PLATFORM=surfaceless ./test_glitch64_n64_combiner
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <EGL/egl.h>

#include <libretro.h>
#include <glsym/glsym.h>
#include <glsm/glsm.h>
/* the frontend's half calls GL directly, so it comes before glsmsym.h
 * (through glide.h) turns the GL calls that follow into glsm's */

/* Glitch64 batches triangles until a change of state, or the frame is read
 * or shown: what reads the frame here past Glitch64 draws them first */
void glitch64_flush(void);

#define WIN_W 64
#define WIN_H 64

static GLuint target;

static uintptr_t framebuffer(void) { return target; }

static retro_proc_address_t resolve(const char *name)
{
   return (retro_proc_address_t)eglGetProcAddress(name);
}

static void frontend(void)
{
   const EGLint config_attrs[] = { EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE,
#ifdef HAVE_OPENGLES
      EGL_OPENGL_ES2_BIT,
#else
      EGL_OPENGL_BIT,
#endif
      EGL_NONE };
#ifdef HAVE_OPENGLES
   const EGLint context_attrs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
#else
   const EGLint *context_attrs = NULL;
#endif
   const EGLint surface_attrs[] = { EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE };
   EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
   EGLConfig config;
   EGLint count;
   EGLContext context;
   EGLSurface surface;
   GLuint colour;

   assert(eglInitialize(display, NULL, NULL));
#ifdef HAVE_OPENGLES
   assert(eglBindAPI(EGL_OPENGL_ES_API));
#else
   assert(eglBindAPI(EGL_OPENGL_API));
#endif
   assert(eglChooseConfig(display, config_attrs, &config, 1, &count) && count);
   context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attrs);
   surface = eglCreatePbufferSurface(display, config, surface_attrs);
   assert(context != EGL_NO_CONTEXT && surface != EGL_NO_SURFACE);
   assert(eglMakeCurrent(display, surface, surface, context));
   rglgen_resolve_symbols(resolve);

   glGenTextures(1, &colour);
   glBindTexture(GL_TEXTURE_2D, colour);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, WIN_W, WIN_H, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
   glGenFramebuffers(1, &target);
   glBindFramebuffer(GL_FRAMEBUFFER, target);
   glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colour, 0);
   assert(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
   while (glGetError() != GL_NO_ERROR) {}
}

static bool environment(unsigned cmd, void *data)
{
   if (cmd == RETRO_ENVIRONMENT_SET_HW_RENDER)
   {
      struct retro_hw_render_callback *hw = (struct retro_hw_render_callback*)data;
      hw->get_proc_address = resolve;
      hw->get_current_framebuffer = framebuffer;
      return true;
   }
   return false;
}

static void core_context(void)
{
   glsm_ctx_params_t params;
   memset(&params, 0, sizeof(params));
   params.environ_cb = environment;
   assert(glsm_ctl(GLSM_CTL_STATE_CONTEXT_INIT, &params));
   assert(glsm_ctl(GLSM_CTL_STATE_CONTEXT_RESET, NULL));
   assert(glsm_ctl(GLSM_CTL_STATE_SETUP, NULL));
   assert(glsm_ctl(GLSM_CTL_STATE_BIND, NULL));
   while (glGetError() != GL_NO_ERROR) {}
}

#include "../glide2gl/src/Glitch64/glide.h"
#include "../glide2gl/src/Glide64/rdp.h"
#include "../glide2gl/src/Glitch64/glitch64_vk.h"

#ifndef GR_TEXFMT_ARGB_8888
#define GR_TEXFMT_ARGB_8888 0x12
#endif

retro_environment_t environ_cb;
retro_log_printf_t log_cb;
SETTINGS settings;
struct gdp_global g_gdp;
float polygonOffsetFactor;
void retro_return(bool just_flipping) { (void)just_flipping; }

static void log_stub(enum retro_log_level level, const char *fmt, ...)
{
   (void)level;
   (void)fmt;
}

/* the inputs, 0 to 255: two tiles, shade, prim, env, and the constants */
static const unsigned char T0[4] = { 200, 40, 120, 180 }, T1[4] = { 30, 220, 90, 60 };
static const unsigned char SHADE[4] = { 100, 150, 250, 210 };
static const unsigned char PRIM[4] = { 240, 80, 20, 110 }, ENV[4] = { 60, 130, 190, 230 };
static const unsigned char CENTER[3] = { 70, 90, 140 }, SCALE[3] = { 160, 30, 250 };
static const float K4 = 50.0f / 255.0f, K5 = 170.0f / 255.0f;
static const float PRIM_LOD = 90.0f / 255.0f, LOD = 35.0f / 255.0f;

static void texture(int tmu, const unsigned char *c, unsigned addr)
{
   uint32_t texel = ((uint32_t)c[3] << 24) | ((uint32_t)c[0] << 16) | ((uint32_t)c[1] << 8) | c[2];
   GrTexInfo info;
   memset(&info, 0, sizeof(info));
   info.format = GR_TEXFMT_ARGB_8888;
   info.width = info.height = 1;
   info.data = &texel;
   grTexSource(tmu, addr, &info, 1);
}

/* one input of a cycle, as a colour (or alpha) 0 to 1 */
typedef struct { float c[4]; } col;

static col rgba(const unsigned char *v)
{
   col r;
   r.c[0] = v[0] / 255.0f; r.c[1] = v[1] / 255.0f; r.c[2] = v[2] / 255.0f; r.c[3] = v[3] / 255.0f;
   return r;
}

static col all(float v)
{
   col r;
   r.c[0] = r.c[1] = r.c[2] = r.c[3] = v;
   return r;
}

static float lod_frac;

/* the RDP's inputs to colour slot @slot (0 a, 1 b, 2 c, 3 d) */
static col rgb_in(unsigned slot, unsigned v, col comb, col t0, col t1)
{
   const col prim = rgba(PRIM), env = rgba(ENV), shade = rgba(SHADE);
   col r;
   switch (v)
   {
      case 0: return comb;
      case 1: return t0;
      case 2: return t1;
      case 3: return prim;
      case 4: return shade;
      case 5: return env;
   }
   if (slot == 0 && v == 6) return all(1.0f);
   if (slot == 1 && v == 6) { r.c[0] = CENTER[0] / 255.0f; r.c[1] = CENTER[1] / 255.0f; r.c[2] = CENTER[2] / 255.0f; return r; }
   if (slot == 1 && v == 7) return all(K4);
   if (slot == 2)
      switch (v)
      {
         case 6: r.c[0] = SCALE[0] / 255.0f; r.c[1] = SCALE[1] / 255.0f; r.c[2] = SCALE[2] / 255.0f; return r;
         case 7: return all(comb.c[3]);
         case 8: return all(t0.c[3]);
         case 9: return all(t1.c[3]);
         case 10: return all(prim.c[3]);
         case 11: return all(shade.c[3]);
         case 12: return all(env.c[3]);
         case 13: return all(lod_frac);
         case 14: return all(PRIM_LOD);
         case 15: return all(K5);
      }
   if (slot == 3 && v == 6) return all(1.0f);
   return all(0.0f);
}

static float alpha_in(unsigned slot, unsigned v, col comb, col t0, col t1)
{
   switch (v)
   {
      case 1: return t0.c[3];
      case 2: return t1.c[3];
      case 3: return PRIM[3] / 255.0f;
      case 4: return SHADE[3] / 255.0f;
      case 5: return ENV[3] / 255.0f;
   }
   if (slot == 2)
      return v == 0 ? lod_frac : v == 6 ? PRIM_LOD : 0.0f;
   if (v == 0) return comb.c[3];
   return v == 6 ? 1.0f : 0.0f;
}

static col reference(const glitch64_n64_combiner *m)
{
   col comb = all(0.0f), out;
   unsigned i, k;
   for (i = m->two_cycle ? 0 : 1; i < 2; i++)
   {
      const int second = m->two_cycle && i == 1;
      const col t0 = second ? rgba(T1) : rgba(T0), t1 = second ? rgba(T0) : rgba(T1);
      col in[4];
      float a[4];
      for (k = 0; k < 4; k++)
      {
         in[k] = rgb_in(k, m->rgb[i][k], comb, t0, t1);
         a[k] = alpha_in(k, m->alpha[i][k], comb, t0, t1);
      }
      for (k = 0; k < 3; k++)
         out.c[k] = (in[0].c[k] - in[1].c[k]) * in[2].c[k] + in[3].c[k];
      out.c[3] = (a[0] - a[1]) * a[2] + a[3];
      comb = out;
   }
   for (k = 0; k < 4; k++)
      comb.c[k] = comb.c[k] < 0.0f ? 0.0f : comb.c[k] > 1.0f ? 1.0f : comb.c[k];
   return comb;
}

static void vtx(VERTEX *v, float x, float y)
{
   memset(v, 0, sizeof(*v));
   v->x = x;
   v->y = y;
   v->z = 1.0f;
   v->q = 1.0f;
   v->r = SHADE[0]; v->g = SHADE[1]; v->b = SHADE[2]; v->a = SHADE[3];
   v->coord[0] = v->coord[1] = v->coord[2] = v->coord[3] = 0.5f;
}

static int failures, draws;
/* the programs the checks drew with, this window */
static GLint programs[64];
static int n_programs;

static void check(const glitch64_n64_combiner *m, const char *what)
{
   VERTEX quad[4];
   unsigned char px[4];
   const col want = reference(m);
   unsigned k;
   int bad = 0;

   grN64Combine(m);
   /* the whole of Glitch64's 640x480 window: the framebuffer is a corner */
   vtx(&quad[0], 0.0f, 0.0f);
   vtx(&quad[1], 640.0f, 0.0f);
   vtx(&quad[2], 640.0f, 480.0f);
   vtx(&quad[3], 0.0f, 480.0f);
   glDisable(GL_SCISSOR_TEST);
   glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
   glitch64_flush();
   glClear(GL_COLOR_BUFFER_BIT);
   grDrawVertexArrayContiguous(GR_TRIANGLE_FAN, 4, quad);
   glitch64_flush();
   glReadPixels(WIN_W / 2, WIN_H / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
   draws++;
   {
      GLint prog = 0;
      int i;
      glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
      for (i = 0; i < n_programs && programs[i] != prog; i++) {}
      if (i == n_programs && n_programs < 64)
         programs[n_programs++] = prog;
   }
   for (k = 0; k < 4; k++)
   {
      const int w = (int)(want.c[k] * 255.0f + 0.5f);
      if (px[k] - w > 2 || w - px[k] > 2)
         bad = 1;
   }
   if (bad)
   {
      if (failures < 12)
         fprintf(stderr, "%s, %s: rgb %u %u %u %u / %u %u %u %u, alpha %u %u %u %u / %u %u %u %u: "
               "%u %u %u %u, want %.0f %.0f %.0f %.0f\n", what, m->two_cycle ? "2-cycle" : "1-cycle",
               m->rgb[0][0], m->rgb[0][1], m->rgb[0][2], m->rgb[0][3],
               m->rgb[1][0], m->rgb[1][1], m->rgb[1][2], m->rgb[1][3],
               m->alpha[0][0], m->alpha[0][1], m->alpha[0][2], m->alpha[0][3],
               m->alpha[1][0], m->alpha[1][1], m->alpha[1][2], m->alpha[1][3],
               px[0], px[1], px[2], px[3],
               want.c[0] * 255.0f, want.c[1] * 255.0f, want.c[2] * 255.0f, want.c[3] * 255.0f);
      failures++;
   }
}

/* a mode, cycle by cycle: "(T0 - SHADE) * ENV + PRIM" in both */
static void base(glitch64_n64_combiner *m, int two)
{
   unsigned i;
   memset(m, 0, sizeof(*m));
   for (i = 0; i < 2; i++)
   {
      m->rgb[i][0] = 1; m->rgb[i][1] = 4; m->rgb[i][2] = 5; m->rgb[i][3] = 3;
      m->alpha[i][0] = 1; m->alpha[i][1] = 4; m->alpha[i][2] = 5; m->alpha[i][3] = 3;
   }
   m->two_cycle = (uint8_t)two;
}

static void check_keys(void)
{
   unsigned seed = 77, n;
   for (n = 0; n < 5000; n++)
   {
      glitch64_n64_combiner a, b;
      uint32_t key[2];
      unsigned i, k;
      for (i = 0; i < 2; i++)
         for (k = 0; k < 4; k++)
         {
            seed = seed * 1103515245u + 12345u;
            a.rgb[i][k] = (uint8_t)((seed >> 16) & (k == 2 ? 31 : k == 3 ? 7 : 15));
            a.alpha[i][k] = (uint8_t)((seed >> 24) & 7);
         }
      a.two_cycle = (uint8_t)(n & 1);
      a.flags = (uint8_t)((n >> 1) & 7);
      glitch64_n64_key(&a, key);
      memset(&b, 0xA5, sizeof(b));
      glitch64_n64_from_key(key, &b);
      if (memcmp(&a, &b, sizeof(a)) || !key[1])
      {
         fprintf(stderr, "key %08x %08x does not give its combiner back\n", (unsigned)key[0], (unsigned)key[1]);
         failures++;
         return;
      }
   }
}

static void quad_at(float x0, float x1, float z)
{
   VERTEX quad[4];
   vtx(&quad[0], x0, 0.0f);
   vtx(&quad[1], x1, 0.0f);
   vtx(&quad[2], x1, 480.0f);
   vtx(&quad[3], x0, 480.0f);
   quad[0].z = quad[1].z = quad[2].z = quad[3].z = z;
   grDrawVertexArrayContiguous(GR_TRIANGLE_FAN, 4, quad);
}

/* the window pixel at Glitch64's (x, 240): the framebuffer is its 64x64
 * bottom left corner, Glitch64's y down from the top */
static uint32_t pixel_at(unsigned x)
{
   unsigned char px[4];
   glitch64_flush();
   glReadPixels((GLint)x, WIN_H / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
   return ((uint32_t)px[0] << 16) | ((uint32_t)px[1] << 8) | px[2];
}

static int check_batches(float *prim, float *env, float *consts, const float *level,
      const float *center, const float *scale)
{
   glitch64_n64_combiner m;
   int bad = 0;
   /* PRIM, alpha 1 */
   memset(&m, 0, sizeof(m));
   m.rgb[1][0] = 15; m.rgb[1][1] = 15; m.rgb[1][2] = 31; m.rgb[1][3] = 3;
   m.alpha[1][0] = 7; m.alpha[1][1] = 7; m.alpha[1][2] = 7; m.alpha[1][3] = 6;
   grN64Combine(&m);
   glDisable(GL_SCISSOR_TEST);
   glitch64_flush();
   glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
   glClear(GL_COLOR_BUFFER_BIT);

   /* the combiner's inputs: red, then green */
   prim[0] = 1.0f; prim[1] = 0.0f; prim[2] = 0.0f; prim[3] = 1.0f;
   grN64Constants(prim, env, consts, level, center, scale);
   quad_at(0.0f, 32.0f, 1.0f);
   prim[0] = 0.0f; prim[1] = 1.0f;
   grN64Constants(prim, env, consts, level, center, scale);
   quad_at(32.0f, 64.0f, 1.0f);
   if (pixel_at(16) != 0xFF0000 || pixel_at(48) != 0x00FF00)
   {
      fprintf(stderr, "quads either side of a change of prim: %06x %06x\n",
            (unsigned)pixel_at(16), (unsigned)pixel_at(48));
      bad++;
   }

   /* the blend: the second quad adds, the first replaces */
   grAlphaBlendFunction(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ONE, GR_BLEND_ZERO);
   prim[0] = 0.0f; prim[1] = 0.0f; prim[2] = 1.0f;
   grN64Constants(prim, env, consts, level, center, scale);
   quad_at(0.0f, 32.0f, 1.0f);
   grAlphaBlendFunction(GR_BLEND_ONE, GR_BLEND_ONE, GR_BLEND_ONE, GR_BLEND_ZERO);
   quad_at(32.0f, 64.0f, 1.0f);
   if (pixel_at(16) != 0x0000FF || pixel_at(48) != 0x00FFFF)
   {
      fprintf(stderr, "quads either side of a change of blend: %06x %06x\n",
            (unsigned)pixel_at(16), (unsigned)pixel_at(48));
      bad++;
   }
   grAlphaBlendFunction(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ONE, GR_BLEND_ZERO);

   /* the alpha test: the second quad's alpha 0.5 fails a reference of 0.75 */
   prim[0] = 1.0f; prim[1] = 1.0f; prim[2] = 1.0f; prim[3] = 0.5f;
   m.alpha[1][3] = 3;
   grN64Combine(&m);
   grN64Constants(prim, env, consts, level, center, scale);
   grAlphaTestFunction(GR_CMP_ALWAYS, 0, 0);
   quad_at(0.0f, 32.0f, 1.0f);
   grAlphaTestFunction(GR_CMP_GEQUAL, 192, 1);
   quad_at(32.0f, 64.0f, 1.0f);
   if (pixel_at(16) != 0xFFFFFF || pixel_at(48) != 0x00FFFF)
   {
      fprintf(stderr, "quads either side of a change of alpha test: %06x %06x\n",
            (unsigned)pixel_at(16), (unsigned)pixel_at(48));
      bad++;
   }
   grAlphaTestFunction(GR_CMP_ALWAYS, 0, 0);
   glDisable(GL_BLEND);
   return bad;
}

/* every check, on the window open now */
static void run(const float *level)
{
   static const unsigned span[4] = { 16, 16, 32, 8 };
   float prim[4], env[4], consts[4], center[3], scale[3];
   glitch64_n64_combiner m;
   unsigned two, cyc, slot, v, k, n, seed = 1;

   check_keys();

   /* tile 0 in TMU 0, tile 1 in TMU 1, as TexCache puts them */
   texture(GR_TMU0, T0, 0x1000);
   texture(GR_TMU1, T1, 0x2000);
   for (k = 0; k < 4; k++)
   {
      prim[k] = PRIM[k] / 255.0f;
      env[k] = ENV[k] / 255.0f;
   }
   consts[0] = K4; consts[1] = K5; consts[2] = PRIM_LOD; consts[3] = LOD;
   for (k = 0; k < 3; k++)
   {
      center[k] = CENTER[k] / 255.0f;
      scale[k] = SCALE[k] / 255.0f;
   }
   grN64Constants(prim, env, consts, level, center, scale);
   grFogMode(GR_FOG_DISABLE);
   lod_frac = LOD;

   /* every input in every slot of the cycle run; the other slots of it
    * left as the base mode */
   for (two = 0; two < 2; two++)
      for (cyc = two ? 0 : 1; cyc < 2; cyc++)
         for (slot = 0; slot < 4; slot++)
            for (v = 0; v < span[slot]; v++)
            {
               base(&m, (int)two);
               /* noise is not a value to hold a pixel to */
               if (!(slot == 0 && v == 7))
               {
                  m.rgb[cyc][slot] = (uint8_t)v;
                  check(&m, "colour input");
               }
               if (v < 8)
               {
                  base(&m, (int)two);
                  m.alpha[cyc][slot] = (uint8_t)v;
                  check(&m, "alpha input");
               }
            }

   /* modes mixed at random */
   for (n = 0; n < 300; n++)
   {
      memset(&m, 0, sizeof(m));
      for (cyc = 0; cyc < 2; cyc++)
         for (slot = 0; slot < 4; slot++)
         {
            seed = seed * 1103515245u + 12345u;
            v = (seed >> 16) % span[slot];
            if (slot == 0 && v == 7)
               v = 4;
            m.rgb[cyc][slot] = (uint8_t)v;
            m.alpha[cyc][slot] = (uint8_t)((seed >> 24) & 7);
         }
      m.two_cycle = (uint8_t)(n & 1);
      check(&m, "mixed");
   }

   /* LOD_FRACTION from the derivatives: the tiles' one texel is magnified,
    * and with no level past the first the RDP gives 1 */
   base(&m, 0);
   m.rgb[1][2] = 13;
   m.alpha[1][2] = 0;
   m.flags = GLITCH64_N64_LOD;
   lod_frac = 1.0f;
   check(&m, "LOD_FRACTION per pixel");
   lod_frac = LOD;

   /* the blender's fog colour and alpha in place of the combiner's */
   {
      unsigned char px[4];
      VERTEX quad[4];
      base(&m, 0);
      m.flags = GLITCH64_N64_FOG_RGB | GLITCH64_N64_FOG_ALPHA;
      g_gdp.fog_color.r = 11; g_gdp.fog_color.g = 22; g_gdp.fog_color.b = 33; g_gdp.fog_color.a = 44;
      grN64Combine(&m);
      grN64Constants(prim, env, consts, level, center, scale);
      vtx(&quad[0], 0.0f, 0.0f);
      vtx(&quad[1], 640.0f, 0.0f);
      vtx(&quad[2], 640.0f, 480.0f);
      vtx(&quad[3], 0.0f, 480.0f);
      glitch64_flush();
      glClear(GL_COLOR_BUFFER_BIT);
      grDrawVertexArrayContiguous(GR_TRIANGLE_FAN, 4, quad);
      glitch64_flush();
      glReadPixels(WIN_W / 2, WIN_H / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
      if (px[0] != 11 || px[1] != 22 || px[2] != 33 || px[3] != 44)
      {
         fprintf(stderr, "fog in the combiner's place: %u %u %u %u\n", px[0], px[1], px[2], px[3]);
         failures++;
      }
   }

   failures += check_batches(prim, env, consts, level, center, scale);
}

int main(void)
{
   const float level[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
   int mode;

   environ_cb = environment;
   log_cb = log_stub;
   frontend();
   core_context();
   for (mode = 0; mode < 3; mode++)
   {
      /* 0: the shared program on, each combiner through it until its own
       * has linked; 1: on, everything through it (GLITCH64_COMBINER=uber);
       * 2: off (the default), each combiner through its own */
      static const char *modes[3] = { "", "uber", "" };
      glitch64_shared_shader = mode < 2;
      setenv("GLITCH64_COMBINER", modes[mode], 1);
      assert(grSstWinOpen());
      while (glGetError() != GL_NO_ERROR) {}
      n_programs = 0;
      run(level);
      assert(glGetError() == GL_NO_ERROR);
      /* the option does what it says: everything through the one shared
       * program, or each combiner through its own */
      if ((mode == 1 && n_programs != 1) || (mode == 2 && n_programs < 8))
      {
         fprintf(stderr, "mode %d: the checks drew with %d programs\n", mode, n_programs);
         failures++;
      }
      grSstWinClose();
   }
   if (failures)
   {
      fprintf(stderr, "%d of %d draws wrong\n", failures, draws);
      return 1;
   }
#ifdef HAVE_OPENGLES
   printf("test_glitch64_n64_combiner (GLES 2): ok, %d draws\n", draws);
#else
   printf("test_glitch64_n64_combiner (GL): ok, %d draws\n", draws);
#endif
   return 0;
}
