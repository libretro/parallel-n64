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
   grDepthMask(FXFALSE);
   grColorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
         GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_NONE, FXFALSE);
   grAlphaCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
         GR_COMBINE_LOCAL_CONSTANT, GR_COMBINE_OTHER_NONE, FXFALSE);
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

   grSstWinClose(0);
   free(window);
   if (failures)
      return 1;
   printf("test_glitch64_draw: ok\n");
   return 0;
}
