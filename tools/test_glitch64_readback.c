/* test_glitch64_readback -- Glide64's frame buffer copy scaled down on
 * the GPU.
 *
 * Perfect Dark blurs the frame just drawn for its pause screen: the CPU
 * reads the frame from RDRAM. Glide64 never copied it there, and the
 * pause screen's background was black; it now copies every frame, as it
 * already did for the games that read the frame back. A copy read the
 * whole window and picked the N64's pixels out of it on the CPU: at 9x
 * that is 100 MB a frame to read for 300 KB of pixels. The window is now
 * scaled down on the GPU and only the N64's pixels are read back.
 *
 * This paints the window in blocks of colour, reads it back scaled down,
 * and holds every pixel read to the block it is in, the bottom row first;
 * and a part of the window to the part read.
 *
 *   EGL_PLATFORM=surfaceless ./test_glitch64_readback
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

static const uint32_t colours[3][4] = {
   { 0xFF0000, 0x00FF00, 0x0000FF, 0xFFFF00 },
   { 0x00FFFF, 0xFF00FF, 0x808080, 0xFFFFFF },
   { 0x400000, 0x004000, 0x000040, 0x204060 }
};

/* the window in blocks of 160x160, the top row of blocks first */
static void paint(void)
{
   unsigned bx, by;
   glEnable(GL_SCISSOR_TEST);
   for (by = 0; by < 3; by++)
      for (bx = 0; bx < 4; bx++)
      {
         const uint32_t c = colours[by][bx];
         glScissor((GLint)(bx * 160), (GLint)(WIN_H - (by + 1) * 160), 160, 160);
         glClearColor(((c >> 16) & 0xFF) / 255.0f, ((c >> 8) & 0xFF) / 255.0f, (c & 0xFF) / 255.0f, 1.0f);
         glClear(GL_COLOR_BUFFER_BIT);
      }
}

/* (dw x dh) read of the window rect (x, y, w, h): pixel (px, py), the top
 * row first, is the block at the window point it scales from */
static int check(const char *name, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t dw, uint32_t dh)
{
   uint32_t stride, px, py;
   int bad = 0;
   const uint8_t *rows = grLfbReadScaled(x, y, w, h, dw, dh, &stride);
   if (!rows)
   {
      fprintf(stderr, "%s: not read\n", name);
      return 1;
   }
   for (py = 0; py < dh; py++)
      for (px = 0; px < dw; px++)
      {
         const uint8_t *p = rows + (size_t)(dh - 1 - py) * stride + px * 4;
         const unsigned wx = (unsigned)x + (unsigned)(((px + 0.5) * w) / dw);
         const unsigned wy = (unsigned)y + (unsigned)(((py + 0.5) * h) / dh);
         const uint32_t want = colours[wy / 160][wx / 160];
         const uint32_t got = ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
         if (got != want)
         {
            if (bad < 5)
               fprintf(stderr, "%s: pixel %u,%u is %06x, want %06x\n", name, px, py, (unsigned)got, (unsigned)want);
            bad++;
         }
      }
   return bad != 0;
}

int main(void)
{
   int failures = 0;

   environ_cb = environment;
   log_cb = log_stub;
   frontend();
   core_context();

   assert(grSstWinOpen());
   while (glGetError() != GL_NO_ERROR) {}
   if (!grLfbReadScaledInit())
   {
      fprintf(stderr, "no framebuffer blits\n");
      return 1;
   }

   paint();
   /* the whole window to a quarter */
   failures += check("whole", 0, 0, WIN_W, WIN_H, WIN_W / 4, WIN_H / 4);
   /* a part of it, to a size it does not divide into */
   failures += check("part", 80, 20, 480, 400, 150, 125);
   /* the scissor a frame left is in force again */
   glScissor(0, 0, 8, 8);
   paint();
   glScissor(1, 2, 3, 4);
   failures += check("again", 0, 0, WIN_W, WIN_H, 160, 120);
   {
      GLint box[4];
      glGetIntegerv(GL_SCISSOR_BOX, box);
      if (!glIsEnabled(GL_SCISSOR_TEST) || box[0] != 1 || box[1] != 2 || box[2] != 3 || box[3] != 4)
      {
         fprintf(stderr, "the scissor was not left as it was\n");
         failures++;
      }
   }
   assert(glGetError() == GL_NO_ERROR);

   grSstWinClose(0);
   if (failures)
      return 1;
   printf("test_glitch64_readback: ok\n");
   return 0;
}
