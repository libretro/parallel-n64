/* test_glitch64_lfb_write -- Glide64's linear frame buffer write.
 *
 * grLfbWriteRegion puts a 555 image the CPU made back on screen (the
 * Pokemon Puzzle League hack). It used to draw nothing: it gave
 * glTexSubImage2D the argument list of glTexImage2D, into a texture with
 * no storage; it drew with the default program, whose vertexOffset is
 * never set, so every vertex divided by zero; it addressed the texture
 * in texels while telling the shader the texture was 1x1; its strip
 * folded on itself; and the draw was clipped by whatever clip window and
 * culled by whatever cull mode the frame had left.
 *
 * This runs Glitch64 on Mesa through headless EGL, leaves a clip window
 * and a cull mode set as a frame would, writes an image with odd sizes
 * at an offset, and holds every pixel of the window to it: the image
 * exactly where it was written, the clear colour elsewhere. It then
 * checks the clip window is in force again afterwards.
 *
 *   EGL_PLATFORM=surfaceless ./test_glitch64_lfb_write
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

static int scissor_is(GLint w, GLint h)
{
   GLint box[4];
   glGetIntegerv(GL_SCISSOR_BOX, box);
   return glIsEnabled(GL_SCISSOR_TEST) && box[2] == w && box[3] == h;
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

static uint16_t pixel_at(unsigned x, unsigned y)
{
   return (uint16_t)(0x8000 | ((x * 7 + y * 13 + x * y) & 0x7FFF));
}

static int run(unsigned dst_x, unsigned dst_y, unsigned w, unsigned h)
{
   uint16_t *image = (uint16_t*)malloc(w * h * 2);
   uint8_t *window = (uint8_t*)malloc(WIN_W * WIN_H * 4);
   unsigned x, y, bad = 0;

   for (y = 0; y < h; y++)
      for (x = 0; x < w; x++)
         image[y * w + x] = pixel_at(x, y);

   /* what a frame leaves behind */
   grClipWindow(0, 0, 8, 8);
   glEnable(GL_CULL_FACE);
   glCullFace(GL_FRONT);
   glDisable(GL_SCISSOR_TEST);
   glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
   glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
   glEnable(GL_SCISSOR_TEST);

   grLfbWriteRegion(GR_BUFFER_BACKBUFFER, dst_x, dst_y, GR_LFB_SRC_FMT_555,
         w, h, FXFALSE, (int32_t)(w * 2), image);

   /* the clip window is the frame's again */
   assert(scissor_is(8, 8));
   assert(glGetError() == GL_NO_ERROR);
   read_window(window);

   for (y = 0; y < WIN_H; y++)
   {
      for (x = 0; x < WIN_W; x++)
      {
         /* GL rows run bottom up; the image's rows top down */
         const uint8_t *p = window + ((WIN_H - 1 - y) * WIN_W + x) * 4;
         uint8_t want[3] = { 0, 0, 0 };
         if (x >= dst_x && x < dst_x + w && y >= dst_y && y < dst_y + h)
         {
            const uint16_t c = image[(y - dst_y) * w + (x - dst_x)];
            want[0] = (uint8_t)(((c >> 10) & 0x1F) << 3);
            want[1] = (uint8_t)(((c >> 5) & 0x1F) << 3);
            want[2] = (uint8_t)((c & 0x1F) << 3);
         }
         if (p[0] != want[0] || p[1] != want[1] || p[2] != want[2])
         {
            if (bad < 5)
               fprintf(stderr, "%ux%u at %u,%u: pixel %u,%u is %02x%02x%02x, want %02x%02x%02x\n",
                     w, h, dst_x, dst_y, x, y, p[0], p[1], p[2], want[0], want[1], want[2]);
            bad++;
         }
      }
   }
   free(image);
   free(window);
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

   failures += run(0, 0, 320, 240);
   failures += run(101, 57, 37, 23);
   failures += run(600, 470, 40, 10);

   grSstWinClose(0);
   if (failures)
   {
      fprintf(stderr, "%d of 3 writes wrong\n", failures);
      return 1;
   }
   printf("test_glitch64_lfb_write: ok\n");
   return 0;
}
