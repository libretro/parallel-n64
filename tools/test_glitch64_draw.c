/* test_glitch64_draw -- Glide64's draws each with its own state.
 *
 * Glitch64 kept three vertices drawn as triangles for the next draw, to
 * send together. Nothing drew them before the state changed, so they took
 * the next draw's colour, texture and combiner, and with a strip or fan
 * after them were drawn as part of it. Glide64's own draws never sent
 * triangles, so this lay unseen until it drew GoldenEye's water cut into
 * triangles: what came after took the water's state and drew clouds over
 * it, or a black wedge.
 *
 * This draws a red triangle and a green square beside it as a fan, and
 * holds the window to them: each where it was drawn, nothing between.
 *
 * The RDP steps the shade evenly across the screen, whatever the depth.
 * Glitch64 let the GPU correct the shade for perspective, so a polygon
 * reaching into the distance took its near corners' colour across most of
 * it: GoldenEye's sea, one polygon from the boat to the horizon, came out
 * teal instead of dark blue. This then draws a triangle whose corners lie
 * at very different depths and holds pixels inside it to the shade stepped
 * evenly between its corners, and the same for the fog, the shade's
 * alpha to the RDP, which was corrected for perspective too.
 *
 *   EGL_PLATFORM=surfaceless ./test_glitch64_draw
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

#define WIN_W 640
#define WIN_H 480

static GLuint target;

static uintptr_t framebuffer(void) { return target; }

static retro_proc_address_t resolve(const char *name)
{
   return (retro_proc_address_t)eglGetProcAddress(name);
}

/* the frontend: a context, and a window-sized FBO with depth for it */
static void frontend(void)
{
   const EGLint config_attrs[] = { EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
      EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT, EGL_NONE };
   const EGLint surface_attrs[] = { EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE };
   EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
   EGLConfig config;
   EGLint count;
   EGLContext context;
   EGLSurface surface;
   GLuint colour, depth;

   assert(eglInitialize(display, NULL, NULL));
   assert(eglBindAPI(EGL_OPENGL_API));
   assert(eglChooseConfig(display, config_attrs, &config, 1, &count) && count);
   context = eglCreateContext(display, config, EGL_NO_CONTEXT, NULL);
   surface = eglCreatePbufferSurface(display, config, surface_attrs);
   assert(context != EGL_NO_CONTEXT && surface != EGL_NO_SURFACE);
   assert(eglMakeCurrent(display, surface, surface, context));

   rglgen_resolve_symbols(resolve);

   glGenTextures(1, &colour);
   glBindTexture(GL_TEXTURE_2D, colour);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, WIN_W, WIN_H, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
   glGenRenderbuffers(1, &depth);
   glBindRenderbuffer(GL_RENDERBUFFER, depth);
   glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, WIN_W, WIN_H);
   glGenFramebuffers(1, &target);
   glBindFramebuffer(GL_FRAMEBUFFER, target);
   glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colour, 0);
   glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depth);
   assert(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
   while (glGetError() != GL_NO_ERROR) {}
}

/* the frontend's answer to the core asking for a GL context */
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

/* glsm set up as the core sets it up, the context made and bound */
static void core_context(void)
{
   glsm_ctx_params_t params;
   memset(&params, 0, sizeof(params));
   params.environ_cb = environment;
   params.stencil = true;
   assert(glsm_ctl(GLSM_CTL_STATE_CONTEXT_INIT, &params));
   assert(glsm_ctl(GLSM_CTL_STATE_CONTEXT_RESET, NULL));
   assert(glsm_ctl(GLSM_CTL_STATE_SETUP, NULL));
   assert(glsm_ctl(GLSM_CTL_STATE_BIND, NULL));
   while (glGetError() != GL_NO_ERROR) {}
}

/* the window as it stands, read past glsm, bottom row first */
static void read_window(uint8_t *window)
{
   glitch64_flush();
   GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
   glDisable(GL_SCISSOR_TEST);
   glReadPixels(0, 0, WIN_W, WIN_H, GL_RGBA, GL_UNSIGNED_BYTE, window);
   if (scissor)
      glEnable(GL_SCISSOR_TEST);
}

#include "../glide2gl/src/Glitch64/glide.h"
#include "../glide2gl/src/Glide64/rdp.h"

/* what Glitch64 takes from the rest of the core */
retro_environment_t environ_cb;
retro_log_printf_t log_cb;
SETTINGS settings;
struct gdp_global g_gdp;
float polygonOffsetFactor;
uint16_t *glide64_frameBuffer;
void retro_return(bool just_flipping) { (void)just_flipping; }

static void log_stub(enum retro_log_level level, const char *fmt, ...)
{
   (void)level;
   (void)fmt;
}

static void vtx(VERTEX *v, float x, float y, uint8_t r, uint8_t g)
{
   memset(v, 0, sizeof(*v));
   v->r = r;
   v->g = g;
   v->a = 0xFF;
   v->x = x;
   v->y = y;
   v->z = 1.0f;
   v->q = 1.0f;
}

/* the colour at window pixel @x, @y, top row first */
static uint32_t at(const uint8_t *window, unsigned x, unsigned y)
{
   const uint8_t *p = window + ((WIN_H - 1 - y) * WIN_W + x) * 4;
   return ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
}

/* A triangle red at one corner, green at another, blue at the third,
 * the blue corner forty times as deep as the others: each pixel's shade is
 * its barycentric weights on the screen, within rounding. */
static int check_shade_screen_linear(uint8_t *window)
{
   static const float px[3] = { 40.0f, 600.0f, 320.0f }, py[3] = { 40.0f, 40.0f, 440.0f };
   static const float q[3] = { 1.0f, 1.0f, 0.025f };
   static const unsigned probes[][2] = { { 320, 180 }, { 200, 100 }, { 440, 100 }, { 320, 380 }, { 250, 250 } };
   VERTEX tri[3];
   unsigned i, bad = 0;
   const float area = (px[1] - px[0]) * (py[2] - py[0]) - (px[2] - px[0]) * (py[1] - py[0]);

   for (i = 0; i < 3; i++)
   {
      vtx(&tri[i], px[i], py[i], i == 0 ? 0xFF : 0, i == 1 ? 0xFF : 0);
      tri[i].b = i == 2 ? 0xFF : 0;
      tri[i].q = q[i];
   }
   glDisable(GL_SCISSOR_TEST);
   glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
   glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
   glEnable(GL_SCISSOR_TEST);
   grDrawVertexArrayContiguous(GR_TRIANGLES, 3, tri);
   read_window(window);

   for (i = 0; i < sizeof(probes) / sizeof(probes[0]); i++)
   {
      /* the pixel's centre */
      const float x = (float)probes[i][0] + 0.5f, y = (float)probes[i][1] + 0.5f;
      const float l1 = ((x - px[0]) * (py[2] - py[0]) - (px[2] - px[0]) * (y - py[0])) / area;
      const float l2 = ((px[1] - px[0]) * (y - py[0]) - (x - px[0]) * (py[1] - py[0])) / area;
      const float l0 = 1.0f - l1 - l2;
      const int want[3] = { (int)(l0 * 255.0f + 0.5f), (int)(l1 * 255.0f + 0.5f), (int)(l2 * 255.0f + 0.5f) };
      const uint32_t c = at(window, probes[i][0], probes[i][1]);
      const int got[3] = { (int)(c >> 16), (int)((c >> 8) & 0xFF), (int)(c & 0xFF) };
      unsigned k;
      for (k = 0; k < 3; k++)
         if (got[k] - want[k] > 2 || want[k] - got[k] > 2)
            break;
      if (k < 3)
      {
         fprintf(stderr, "shade at %u,%u is %d,%d,%d, stepped evenly %d,%d,%d\n", probes[i][0], probes[i][1],
               got[0], got[1], got[2], want[0], want[1], want[2]);
         bad++;
      }
   }
   return bad != 0;
}

/* The same triangle white, fogged to black by a fog alpha of 0 at one
 * corner, 255 at the far one: each pixel is white less the far corner's
 * barycentric weight on the screen, within rounding. */
static int check_fog_screen_linear(uint8_t *window)
{
   static const float px[3] = { 40.0f, 600.0f, 320.0f }, py[3] = { 40.0f, 40.0f, 440.0f };
   static const float q[3] = { 1.0f, 1.0f, 0.025f }, fog[3] = { 0.0f, 0.0f, 255.0f };
   static const unsigned probes[][2] = { { 320, 180 }, { 200, 100 }, { 320, 380 }, { 250, 250 } };
   VERTEX tri[3];
   unsigned i, bad = 0;
   const float area = (px[1] - px[0]) * (py[2] - py[0]) - (px[2] - px[0]) * (py[1] - py[0]);

   for (i = 0; i < 3; i++)
   {
      vtx(&tri[i], px[i], py[i], 0xFF, 0xFF);
      tri[i].b = 0xFF;
      tri[i].q = q[i];
      /* what Glide64 hands over for FOG_MODE_ENABLED */
      tri[i].f = 1.0f / (fog[i] > 4.0f ? fog[i] : 4.0f);
   }
   g_gdp.fog_color.r = g_gdp.fog_color.g = g_gdp.fog_color.b = 0;
   guFogGenerateLinear(0.0f, 255.0f);
   grFogMode(GR_FOG_WITH_TABLE_ON_FOGCOORD_EXT);
   glDisable(GL_SCISSOR_TEST);
   glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
   glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
   glEnable(GL_SCISSOR_TEST);
   grDrawVertexArrayContiguous(GR_TRIANGLES, 3, tri);
   grFogMode(GR_FOG_DISABLE);
   read_window(window);

   for (i = 0; i < sizeof(probes) / sizeof(probes[0]); i++)
   {
      const float x = (float)probes[i][0] + 0.5f, y = (float)probes[i][1] + 0.5f;
      const float l1 = ((x - px[0]) * (py[2] - py[0]) - (px[2] - px[0]) * (y - py[0])) / area;
      const float l2 = ((px[1] - px[0]) * (y - py[0]) - (x - px[0]) * (py[1] - py[0])) / area;
      const float l0 = 1.0f - l1 - l2;
      /* the fog alpha stepped evenly, 4 its least */
      const float a = (l0 * 4.0f + l1 * 4.0f + l2 * 255.0f) / 255.0f;
      const int want = (int)((1.0f - a) * 255.0f + 0.5f);
      const int got = (int)(at(window, probes[i][0], probes[i][1]) >> 16);
      if (got - want > 2 || want - got > 2)
      {
         fprintf(stderr, "fogged at %u,%u is %d, stepped evenly %d\n", probes[i][0], probes[i][1], got, want);
         bad++;
      }
   }
   return bad != 0;
}

int main(void)
{
   VERTEX tri[3], fan[4];
   uint8_t *window = (uint8_t*)malloc(WIN_W * WIN_H * 4);
   int failures = 0;

   environ_cb = environment;
   log_cb = log_stub;
   frontend();
   core_context();

   assert(grSstWinOpen());
   while (glGetError() != GL_NO_ERROR) {}

   grClipWindow(0, 0, WIN_W, WIN_H);
   grCullMode(GR_CULL_DISABLE);
   grDepthBufferFunction(GR_CMP_ALWAYS);
   grDepthMask(false);
   {
      /* the shade: (0 - 0) * 0 + SHADE, alpha 1 */
      glitch64_n64_combiner c;
      memset(&c, 0, sizeof(c));
      c.rgb[1][0] = 15; c.rgb[1][1] = 15; c.rgb[1][2] = 31; c.rgb[1][3] = 4;
      c.alpha[1][0] = 7; c.alpha[1][1] = 7; c.alpha[1][2] = 7; c.alpha[1][3] = 6;
      grN64Combine(&c);
   }
   glDisable(GL_SCISSOR_TEST);
   glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
   glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
   glEnable(GL_SCISSOR_TEST);

   /* a red triangle on the left */
   vtx(&tri[0], 20.0f, 20.0f, 0xFF, 0);
   vtx(&tri[1], 300.0f, 20.0f, 0xFF, 0);
   vtx(&tri[2], 20.0f, 460.0f, 0xFF, 0);
   grDrawVertexArrayContiguous(GR_TRIANGLES, 3, tri);

   /* then a green square on the right */
   vtx(&fan[0], 340.0f, 20.0f, 0, 0xFF);
   vtx(&fan[1], 620.0f, 20.0f, 0, 0xFF);
   vtx(&fan[2], 620.0f, 460.0f, 0, 0xFF);
   vtx(&fan[3], 340.0f, 460.0f, 0, 0xFF);
   grDrawVertexArrayContiguous(GR_TRIANGLE_FAN, 4, fan);

   read_window(window);
   if (at(window, 60, 60) != 0xFF0000)
   {
      fprintf(stderr, "the triangle is %06x, not red\n", (unsigned)at(window, 60, 60));
      failures++;
   }
   if (at(window, 480, 240) != 0x00FF00)
   {
      fprintf(stderr, "the square is %06x, not green\n", (unsigned)at(window, 480, 240));
      failures++;
   }
   if (at(window, 320, 400) != 0)
   {
      fprintf(stderr, "between them is %06x, not the clear colour\n", (unsigned)at(window, 320, 400));
      failures++;
   }

   failures += check_shade_screen_linear(window);
   failures += check_fog_screen_linear(window);

   grSstWinClose();
   free(window);
   if (failures)
      return 1;
   printf("test_glitch64_draw: ok\n");
   return 0;
}
