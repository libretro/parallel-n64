/* test_glitch64_depth_image -- Glide64 drawing a game's depth image.
 *
 * Resident Evil 2 draws the depth of its pre-rendered scenes into the
 * depth buffer as an image, and its characters go behind the scenery by
 * it. Glide64 cleared the depth buffer instead (the call it made was
 * given no image), so they were drawn over everything.
 *
 * grDepthImageWrite now takes the depth words as they lie in RDRAM, each
 * pair swapped on a little endian host, and draws them, scaled, to the
 * depths vertices with those words as z have. This writes an image whose
 * words change from pixel to pixel, at twice its size at an offset, and
 * then:
 *  - on desktop GL, holds every pixel's depth to its word, and the depth
 *    outside the image to the clear depth;
 *  - holds the colour to what it was: only depth is written;
 *  - holds the depth test, depth writes, clip window, culling and
 *    blending to what the frame had: Glide64 sets the depth test again
 *    only when the game changes it;
 *  - draws a fan over it at a depth between the words with the game's
 *    vertex arrays, and holds every pixel to it: the fan where the image is
 *    deeper, the clear colour where it is nearer;
 *  - writes it again with the depth test off, and holds the test to off.
 * It also draws a fan after an LFB colour write: both point the vertex
 * arrays at their own data, and the triangles after them drew from it.
 *
 *   EGL_PLATFORM=surfaceless ./test_glitch64_depth_image
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

/* the image, and where it goes: twice its size, at an offset; rows of odd
 * length, so a swapped pair can straddle two */
#define IMG_W 41
#define IMG_H 30
#define DST_X 100
#define DST_Y 50
#define SCALE 2

/* the fan's z, between the image's words */
#define FAN_Z 0x8000

static GLuint target;

static uintptr_t framebuffer(void) { return target; }

static retro_proc_address_t resolve(const char *name)
{
   return (retro_proc_address_t)eglGetProcAddress(name);
}

/* the frontend: a context, and a window-sized FBO with depth for it */
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
   GLuint colour, depth;

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
   glGenRenderbuffers(1, &depth);
   glBindRenderbuffer(GL_RENDERBUFFER, depth);
#ifdef HAVE_OPENGLES
   glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, WIN_W, WIN_H);
#else
   glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, WIN_W, WIN_H);
#endif
   glGenFramebuffers(1, &target);
   glBindFramebuffer(GL_FRAMEBUFFER, target);
   glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colour, 0);
#ifdef HAVE_OPENGLES
   glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth);
#else
   glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depth);
#endif
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
void retro_return(bool just_flipping) { (void)just_flipping; }

static void log_stub(enum retro_log_level level, const char *fmt, ...)
{
   (void)level;
   (void)fmt;
}

/* The depth word of image pixel @x, @y: some nearer than the fan, some
 * deeper, changing from one pixel to the next, so a pair drawn swapped
 * or a pixel drawn a texel off is seen. None is near the fan's. */
static uint16_t word_at(unsigned x, unsigned y)
{
   if ((x * 5 + y * 3 + x / 3) % 7 < 3)
      return (uint16_t)(FAN_Z - 0x100 * (1 + (x * 3 + y) % 11));
   return (uint16_t)(FAN_Z + 0x100 * (1 + (x + y * 7) % 13));
}

/* the image pixel window pixel @x, @y shows; 0 outside the image */
static int image_pixel(unsigned x, unsigned y, unsigned *ix, unsigned *iy)
{
   if (x < DST_X || y < DST_Y || x >= DST_X + IMG_W * SCALE || y >= DST_Y + IMG_H * SCALE)
      return 0;
   *ix = (x - DST_X) / SCALE;
   *iy = (y - DST_Y) / SCALE;
   return 1;
}

static void vtx(VERTEX *v, float x, float y, float z)
{
   memset(v, 0, sizeof(*v));
   v->r = 0xFF;
   v->a = 0xFF;
   v->x = x;
   v->y = y;
   v->z = z;
   v->q = 1.0f;
}

static void fan(float z)
{
   VERTEX f[4];
   vtx(&f[0], 0.0f, 0.0f, z);
   vtx(&f[1], (float)WIN_W, 0.0f, z);
   vtx(&f[2], (float)WIN_W, (float)WIN_H, z);
   vtx(&f[3], 0.0f, (float)WIN_H, z);
   grDrawVertexArrayContiguous(GR_TRIANGLE_FAN, 4, f);
}

/* the colour at window pixel @x, @y, top row first */
static uint32_t at(const uint8_t *window, unsigned x, unsigned y)
{
   const uint8_t *p = window + ((WIN_H - 1 - y) * WIN_W + x) * 4;
   return ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
}

/* the colour cleared to, read back */
#define CLEAR_RGB 0x4080C0u

/* a colour, the depth buffer at its farthest */
static void clear(void)
{
   glitch64_flush();
   glDisable(GL_SCISSOR_TEST);
   glClearColor(64.0f / 255.0f, 128.0f / 255.0f, 192.0f / 255.0f, 1.0f);
   /* through Glitch64, which keeps what it last set */
   grDepthMask(true);
   glClearDepth(1.0);
   glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

#ifndef HAVE_OPENGLES
static int check_depths(void)
{
   float *depth = (float*)malloc(WIN_W * WIN_H * sizeof(float));
   unsigned x, y, ix, iy, bad = 0;
   glitch64_flush();
   glDisable(GL_SCISSOR_TEST);
   glReadPixels(0, 0, WIN_W, WIN_H, GL_DEPTH_COMPONENT, GL_FLOAT, depth);
   glEnable(GL_SCISSOR_TEST);
   for (y = 0; y < WIN_H; y++)
      for (x = 0; x < WIN_W; x++)
      {
         const float d = depth[(WIN_H - 1 - y) * WIN_W + x];
         float want = 1.0f, diff;
         if (image_pixel(x, y, &ix, &iy))
            want = 0.5f + (float)word_at(ix, iy) / 131072.0f;
         diff = d > want ? d - want : want - d;
         if (diff > 1.0f / 262144.0f)
         {
            if (bad < 5)
               fprintf(stderr, "depth at %u,%u is %f, want %f\n", x, y, d, want);
            bad++;
         }
      }
   free(depth);
   return bad != 0;
}
#endif

int main(void)
{
   uint16_t *rdram = (uint16_t*)malloc(IMG_W * IMG_H * 2);
   uint8_t *window = (uint8_t*)malloc(WIN_W * WIN_H * 4);
   uint16_t lfb[16 * 16];
   unsigned x, y, ix, iy, bad;
   GLint func = 0, mask = 1;
   int failures = 0;

   environ_cb = environment;
   log_cb = log_stub;
   frontend();
   core_context();

   assert(grSstWinOpen());
   while (glGetError() != GL_NO_ERROR) {}

   /* the words as RDRAM has them on a little endian host */
   for (y = 0; y < IMG_H; y++)
      for (x = 0; x < IMG_W; x++)
         rdram[(y * IMG_W + x) ^ 1] = word_at(x, y);

   {
      /* the shade: (0 - 0) * 0 + SHADE, alpha 1 */
      glitch64_n64_combiner c;
      memset(&c, 0, sizeof(c));
      c.rgb[1][0] = 15; c.rgb[1][1] = 15; c.rgb[1][2] = 31; c.rgb[1][3] = 4;
      c.alpha[1][0] = 7; c.alpha[1][1] = 7; c.alpha[1][2] = 7; c.alpha[1][3] = 6;
      grN64Combine(&c);
   }
   clear();

   /* what a frame leaves behind: the game's own depth test, a clip window
    * over the whole window, culling, blending; and a draw before, so the
    * vertex arrays are the game's */
   grClipWindow(0, 0, WIN_W, WIN_H);
   grCullMode(GR_CULL_DISABLE);
   grDepthBufferFunction(GR_CMP_ALWAYS);
   grDepthMask(false);
   fan(0.0f);
   clear();
   grClipWindow(0, 0, WIN_W, WIN_H);
   grDepthBufferFunction(GR_CMP_LESS);
   grDepthMask(false);
   glEnable(GL_DEPTH_TEST);
   glCullFace(GL_FRONT);
   glEnable(GL_CULL_FACE);
   glEnable(GL_BLEND);
   glBlendFunc(GL_ONE, GL_ZERO);

   grDepthImageWrite(rdram, IMG_W, IMG_H, (float)DST_X, (float)DST_Y,
         (float)(DST_X + IMG_W * SCALE), (float)(DST_Y + IMG_H * SCALE));
   assert(glGetError() == GL_NO_ERROR);

   glGetIntegerv(GL_DEPTH_FUNC, &func);
   glGetIntegerv(GL_DEPTH_WRITEMASK, &mask);
   if (func != GL_LESS || mask != 0 || !glIsEnabled(GL_DEPTH_TEST)
         || !glIsEnabled(GL_SCISSOR_TEST) || !glIsEnabled(GL_CULL_FACE) || !glIsEnabled(GL_BLEND))
   {
      fprintf(stderr, "the frame's state is not put back: depth func %x, writes %d, test %d, "
            "clip %d, cull %d, blend %d\n", (unsigned)func, (int)mask, (int)glIsEnabled(GL_DEPTH_TEST),
            (int)glIsEnabled(GL_SCISSOR_TEST), (int)glIsEnabled(GL_CULL_FACE), (int)glIsEnabled(GL_BLEND));
      failures++;
   }

   /* colour untouched */
   read_window(window);
   for (bad = 0, y = 0; y < WIN_H; y++)
      for (x = 0; x < WIN_W; x++)
         if (at(window, x, y) != CLEAR_RGB)
            bad++;
   if (bad)
   {
      fprintf(stderr, "%u pixels of colour written\n", bad);
      failures++;
   }

#ifndef HAVE_OPENGLES
   failures += check_depths();
#endif

   /* the fan, behind what is nearer than it */
   glDisable(GL_CULL_FACE);
   glDisable(GL_BLEND);
   fan((float)FAN_Z);
   read_window(window);
   for (bad = 0, y = 0; y < WIN_H; y++)
      for (x = 0; x < WIN_W; x++)
      {
         const uint32_t want = !image_pixel(x, y, &ix, &iy) || word_at(ix, iy) > FAN_Z ? 0xFF0000u : CLEAR_RGB;
         if (at(window, x, y) != want)
         {
            if (bad < 5)
               fprintf(stderr, "fan at %u,%u is %06x, want %06x\n", x, y, (unsigned)at(window, x, y), (unsigned)want);
            bad++;
         }
      }
   if (bad)
      failures++;

   /* again, the depth test off: it stays off */
   glDisable(GL_DEPTH_TEST);
   grDepthImageWrite(rdram, IMG_W, IMG_H, (float)DST_X, (float)DST_Y,
         (float)(DST_X + IMG_W * SCALE), (float)(DST_Y + IMG_H * SCALE));
   if (glIsEnabled(GL_DEPTH_TEST))
   {
      fprintf(stderr, "the depth test is left on\n");
      failures++;
   }

   /* a fan after an LFB colour write */
   clear();
   glEnable(GL_SCISSOR_TEST);
   grDepthBufferFunction(GR_CMP_ALWAYS);
   for (x = 0; x < 16 * 16; x++)
      lfb[x] = 0x003F;
   grLfbWriteRegion(0, 0, 16, 16, 32, lfb);
   fan(0.0f);
   read_window(window);
   if (at(window, 8, 8) != 0xFF0000 || at(window, 320, 240) != 0xFF0000)
   {
      fprintf(stderr, "the fan after an LFB write is %06x and %06x, not red\n",
            (unsigned)at(window, 8, 8), (unsigned)at(window, 320, 240));
      failures++;
   }

   grSstWinClose();
   free(window);
   free(rdram);
   if (failures)
      return 1;
#ifdef HAVE_OPENGLES
   printf("test_glitch64_depth_image (GLES 2): ok\n");
#else
   printf("test_glitch64_depth_image (GL): ok\n");
#endif
   return 0;
}
