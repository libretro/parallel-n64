/* test_glitch64_textures -- Glide64's texture uploads, as GL samples them.
 *
 * Glitch64 hands GL each Glide texture format either as it is (desktop GL,
 * with BGRA and the reversed packed types) or converted in place (GLES).
 * The conversions used to work on the window's size in texels rather than
 * the texture's, and to touch one texel past the end; on GLES every format
 * went up as 32-bit RGBA. Now the formats GLES takes as Glide lays them out
 * (565, intensity, intensity-alpha) go up as they are, and the rest are
 * converted texel by texel within the texture.
 *
 * This runs Glitch64 on Mesa through headless EGL - built once for desktop
 * GL and once for GLES 2 - uploads every format at a few sizes (1x1, a
 * row narrower than 4 bytes, wide and tall) from a buffer exactly as large
 * as the widest conversion needs, so that a write past it shows under the
 * address sanitizer, draws each texel to a pixel of its own and holds every
 * pixel to what the Glide format means.
 *
 *   EGL_PLATFORM=surfaceless ./test_glitch64_textures
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <EGL/egl.h>

#include <libretro.h>
#include <glsym/glsym.h>
#include <glsm/glsm.h>

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

   /* the frontend's framebuffer, as large as the largest texture */
   glGenTextures(1, &colour);
   glBindTexture(GL_TEXTURE_2D, colour);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 64, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
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

/* draws texture unit 0 to the framebuffer's w x h corner, a texel a pixel,
 * and reads it back, the top row first; GL calls straight to GL */
static GLuint blit_program;

static void make_blit_program(void)
{
   static const char *vs =
      "attribute vec2 p;\n"
      "varying vec2 uv;\n"
      "void main() { uv = p * 0.5 + 0.5; gl_Position = vec4(p, 0.0, 1.0); }\n";
   static const char *fs =
      "#ifdef GL_ES\nprecision mediump float;\n#endif\n"
      "uniform sampler2D t;\n"
      "varying vec2 uv;\n"
      "void main() { gl_FragColor = texture2D(t, uv); }\n";
   GLuint v = glCreateShader(GL_VERTEX_SHADER), f = glCreateShader(GL_FRAGMENT_SHADER);
   GLint ok;
   glShaderSource(v, 1, &vs, NULL);
   glCompileShader(v);
   glGetShaderiv(v, GL_COMPILE_STATUS, &ok);
   assert(ok);
   glShaderSource(f, 1, &fs, NULL);
   glCompileShader(f);
   glGetShaderiv(f, GL_COMPILE_STATUS, &ok);
   assert(ok);
   blit_program = glCreateProgram();
   glAttachShader(blit_program, v);
   glAttachShader(blit_program, f);
   glBindAttribLocation(blit_program, 0, "p");
   glLinkProgram(blit_program);
   glGetProgramiv(blit_program, GL_LINK_STATUS, &ok);
   assert(ok);
}

static void blit(int w, int h, uint8_t *out)
{
   static const GLfloat quad[] = { -1, -1, 1, -1, -1, 1, 1, 1 };
   uint8_t *rows = (uint8_t*)malloc((size_t)w * h * 4);
   int y;

   glBindFramebuffer(GL_FRAMEBUFFER, target);
   glViewport(0, 0, w, h);
   glDisable(GL_SCISSOR_TEST);
   glDisable(GL_BLEND);
   glDisable(GL_DEPTH_TEST);
   glDisable(GL_CULL_FACE);
   glUseProgram(blit_program);
   glUniform1i(glGetUniformLocation(blit_program, "t"), 0);
   glActiveTexture(GL_TEXTURE0);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
   glBindBuffer(GL_ARRAY_BUFFER, 0);
   glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, quad);
   glEnableVertexAttribArray(0);
   glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
   glPixelStorei(GL_PACK_ALIGNMENT, 1);
   glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rows);
   /* texture row 0 is at v = 0, the bottom of the viewport: GL row 0 */
   for (y = 0; y < h; y++)
      memcpy(out + (size_t)y * w * 4, rows + (size_t)y * w * 4, (size_t)w * 4);
   free(rows);
   assert(glGetError() == GL_NO_ERROR);
}

/* the frontend's half is done: what follows is the core's, through glsm */
#include "../glide2gl/src/Glitch64/glide.h"
#include "../glide2gl/src/Glide64/rdp.h"

#ifndef GR_TEXFMT_ARGB_8888
#define GR_TEXFMT_ARGB_8888 0x12
#endif

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

/* A unorm channel of n bits as GL reads it into 8: the exact value
 * rounded, or, as some drivers widen it, its bits repeated, which is at
 * most 1 off; 'alt' gets the second. */
static uint8_t alt[4];
static int channel;

static uint8_t unorm(unsigned v, unsigned bits)
{
   const unsigned max = (1u << bits) - 1;
   unsigned rep = v << (8 - bits);
   unsigned shift;
   for (shift = bits; shift < 8; shift += bits)
      rep |= (v << (8 - bits)) >> shift;
   alt[channel++ & 3] = (uint8_t)rep;
   return (uint8_t)((v * 255u * 2u + max) / (2u * max));
}

static const struct { int fmt; const char *name; unsigned bytes; } formats[] = {
   { GR_TEXFMT_ALPHA_8, "ALPHA_8", 1 },
   { GR_TEXFMT_INTENSITY_8, "INTENSITY_8", 1 },
   { GR_TEXFMT_ALPHA_INTENSITY_44, "ALPHA_INTENSITY_44", 1 },
   { GR_TEXFMT_RGB_565, "RGB_565", 2 },
   { GR_TEXFMT_ARGB_1555, "ARGB_1555", 2 },
   { GR_TEXFMT_ARGB_4444, "ARGB_4444", 2 },
   { GR_TEXFMT_ALPHA_INTENSITY_88, "ALPHA_INTENSITY_88", 2 },
   { GR_TEXFMT_ARGB_8888, "ARGB_8888", 4 },
};

/* what texel v of a Glide format reads as: want, or also (see unorm) */
#define UNORM(c, v, bits) do { want[c] = unorm((v), (bits)); also[c] = alt[(channel - 1) & 3]; } while (0)
#define EXACT(c, v) do { want[c] = also[c] = (uint8_t)(v); } while (0)
static void expect(int fmt, uint32_t v, uint8_t *want, uint8_t *also)
{
   switch (fmt)
   {
      case GR_TEXFMT_ALPHA_8:
         /* Glitch64 gives alpha textures their alpha as intensity too */
         EXACT(0, v & 255); EXACT(1, v & 255); EXACT(2, v & 255); EXACT(3, v & 255);
         break;
      case GR_TEXFMT_INTENSITY_8:
         EXACT(0, v & 255); EXACT(1, v & 255); EXACT(2, v & 255); EXACT(3, 255);
         break;
      case GR_TEXFMT_ALPHA_INTENSITY_44:
         UNORM(0, v & 15, 4); UNORM(1, v & 15, 4); UNORM(2, v & 15, 4); UNORM(3, (v >> 4) & 15, 4);
         break;
      case GR_TEXFMT_RGB_565:
         UNORM(0, (v >> 11) & 31, 5); UNORM(1, (v >> 5) & 63, 6); UNORM(2, v & 31, 5); EXACT(3, 255);
         break;
      case GR_TEXFMT_ARGB_1555:
         UNORM(0, (v >> 10) & 31, 5); UNORM(1, (v >> 5) & 31, 5); UNORM(2, v & 31, 5);
         EXACT(3, (v & 0x8000) ? 255 : 0);
         break;
      case GR_TEXFMT_ARGB_4444:
         UNORM(0, (v >> 8) & 15, 4); UNORM(1, (v >> 4) & 15, 4); UNORM(2, v & 15, 4); UNORM(3, (v >> 12) & 15, 4);
         break;
      case GR_TEXFMT_ALPHA_INTENSITY_88:
         EXACT(0, v & 255); EXACT(1, v & 255); EXACT(2, v & 255); EXACT(3, (v >> 8) & 255);
         break;
      case GR_TEXFMT_ARGB_8888:
         EXACT(0, (v >> 16) & 255); EXACT(1, (v >> 8) & 255); EXACT(2, v & 255); EXACT(3, v >> 24);
         break;
   }
}

static int run(int f, int large_log2, int aspect_log2)
{
   const int fmt = formats[f].fmt;
   const unsigned bytes = formats[f].bytes;
   int w = 1 << large_log2, h = 1 << large_log2, x, y, bad = 0;
   uint8_t *data, *out;
   uint32_t *texels;
   unsigned seed = 0x1234567u + (unsigned)(f * 77 + large_log2 * 7 + aspect_log2);
   GrTexInfo info;

   if (aspect_log2 < 0)
      w = h >> -aspect_log2;
   else
      h = w >> aspect_log2;

   /* room for the widest a texel becomes, and not a byte more */
   data = (uint8_t*)malloc((size_t)w * h * 4);
   out = (uint8_t*)malloc((size_t)w * h * 4);
   texels = (uint32_t*)malloc((size_t)w * h * sizeof(*texels));
   for (x = 0; x < w * h; x++)
   {
      seed = seed * 1103515245u + 12345u;
      texels[x] = seed ^ (seed >> 16);
      if (bytes == 1)
         data[x] = (uint8_t)(texels[x] &= 0xff);
      else if (bytes == 2)
         ((uint16_t*)data)[x] = (uint16_t)(texels[x] &= 0xffff);
      else
         ((uint32_t*)data)[x] = texels[x];
   }

   memset(&info, 0, sizeof(info));
   info.smallLodLog2 = large_log2;
   info.largeLodLog2 = large_log2;
   info.aspectRatioLog2 = aspect_log2;
   info.format = fmt;
   info.data = data;
   grTexSource(GR_TMU1, 0x1000 * (unsigned)(f + 1), GR_MIPMAPLEVELMASK_BOTH, &info, 1);
   assert(glGetError() == GL_NO_ERROR);
   assert(info.width == w && info.height == h);

   blit(w, h, out);
   for (y = 0; y < h; y++)
   {
      for (x = 0; x < w; x++)
      {
         uint8_t want[4], also[4];
         const uint8_t *got = out + ((size_t)y * w + x) * 4;
         int c, wrong = 0;
         expect(fmt, texels[y * w + x], want, also);
         for (c = 0; c < 4; c++)
            wrong |= got[c] != want[c] && got[c] != also[c];
         if (wrong)
         {
            if (bad < 3)
               fprintf(stderr, "%s %dx%d texel %d,%d (%08x): %02x%02x%02x%02x, want %02x%02x%02x%02x\n",
                     formats[f].name, w, h, x, y, texels[y * w + x], got[0], got[1], got[2], got[3],
                     want[0], want[1], want[2], want[3]);
            bad++;
         }
      }
   }
   free(data);
   free(out);
   free(texels);
   return bad != 0;
}

int main(void)
{
   /* (largeLodLog2, aspectRatioLog2): 1x1, 2x1, 8x8, 32x4, 2x16 */
   static const int sizes[][2] = { { 0, 0 }, { 1, 1 }, { 3, 0 }, { 5, 3 }, { 4, -3 } };
   unsigned f, s;
   int failures = 0;

   environ_cb = environment;
   log_cb = log_stub;
   frontend();
   make_blit_program();
   core_context();
   assert(grSstWinOpen());
   while (glGetError() != GL_NO_ERROR) {}

   for (f = 0; f < sizeof(formats) / sizeof(formats[0]); f++)
      for (s = 0; s < sizeof(sizes) / sizeof(sizes[0]); s++)
         failures += run((int)f, sizes[s][0], sizes[s][1]);

   grSstWinClose(0);
   if (failures)
   {
      fprintf(stderr, "%d uploads read back wrong\n", failures);
      return 1;
   }
#ifdef HAVE_OPENGLES
   printf("test_glitch64_textures (GLES 2): ok\n");
#else
   printf("test_glitch64_textures (GL): ok\n");
#endif
   return 0;
}
