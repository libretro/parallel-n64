/* Rice's textures (gles2rice/src/Texture.h, OGLTexture.h), on a GL
 * context of EGL's (llvmpipe in CI):
 *
 *   - a texture made: the size asked for kept, the size made a power of 2
 *     each way, the scales between them, nothing scaled or clamped, its
 *     pixels there to write, 32 bits a texel;
 *   - an update: where the pixels are, the sizes, the pitch;
 *   - its end: the pixels on the GPU (read back), with the filters as the
 *     options say (mipmapped or not), and GL's format as they say;
 *   - held, an end leaves the upload for later, once, when let go; let go
 *     with nothing pending, nothing is uploaded;
 *   - deleted: the GL texture given back.
 *
 * A backend of its own (counting what it is asked) holds the base to the
 * same, without GL. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <EGL/egl.h>

#include <libretro.h>
#include <glsym/glsym.h>
#include <glsm/glsm.h>

#include "../gles2rice/src/OGLTexture.h"

static int failures;
#define CHECK(c) do { if (!(c)) { printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

/* the options, as RiceConfig.cpp gives them */
static uint32_t quality, colour, mipmap;
uint32_t RiceConfig_TextureQuality(void) { return quality; }
uint32_t RiceConfig_ColorQuality(void)   { return colour; }
uint32_t RiceConfig_Mipmapping(void)     { return mipmap; }

static GLuint target;
static uintptr_t framebuffer(void) { return target; }
static retro_proc_address_t resolve(const char *name)
{
   return (retro_proc_address_t)eglGetProcAddress(name);
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

static void context(void)
{
   const EGLint config_attrs[] = { EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
      EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT, EGL_NONE };
   const EGLint surface_attrs[] = { EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE };
   EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
   EGLConfig config;
   EGLint count;
   EGLContext ctx;
   EGLSurface surface;
   glsm_ctx_params_t params;

   assert(eglInitialize(display, NULL, NULL));
   assert(eglBindAPI(EGL_OPENGL_API));
   assert(eglChooseConfig(display, config_attrs, &config, 1, &count) && count);
   ctx = eglCreateContext(display, config, EGL_NO_CONTEXT, NULL);
   surface = eglCreatePbufferSurface(display, config, surface_attrs);
   assert(ctx != EGL_NO_CONTEXT && surface != EGL_NO_SURFACE);
   assert(eglMakeCurrent(display, surface, surface, ctx));
   rglgen_resolve_symbols(resolve);

   memset(&params, 0, sizeof(params));
   params.environ_cb = environment;
   assert(glsm_ctl(GLSM_CTL_STATE_CONTEXT_INIT, &params));
   assert(glsm_ctl(GLSM_CTL_STATE_CONTEXT_RESET, NULL));
   assert(glsm_ctl(GLSM_CTL_STATE_SETUP, NULL));
   assert(glsm_ctl(GLSM_CTL_STATE_BIND, NULL));
   while (glGetError() != GL_NO_ERROR) {}
}

/* ---- the base, with a backend that counts ---- */

static int uploads, releases;
static void count_upload(CTexture *tex) { (void)tex; uploads++; }
static void count_release(CTexture *tex) { (void)tex; releases++; }
static const CTextureOps counting = { count_upload, count_release };

static void base(void)
{
   CTexture *tex = (CTexture *)malloc(sizeof(*tex));
   DrawInfo di;

   /* what a reused block held: none of it may stay */
   memset(tex, 0xA5, sizeof(*tex));
   CTexture_Init(tex, &counting, 20, 14, AS_RENDER_TARGET);
   CHECK(tex->ops == &counting);
   CHECK(tex->m_dwWidth == 20 && tex->m_dwHeight == 14);
   CHECK(tex->m_dwCreatedTextureWidth == 20 && tex->m_dwCreatedTextureHeight == 14);
   CHECK(tex->m_fXScale == 1.0f && tex->m_fYScale == 1.0f);
   CHECK(!tex->m_bScaledS && !tex->m_bScaledT && !tex->m_bClampedS && !tex->m_bClampedT);
   CHECK(!tex->m_bIsEnhancedTexture && tex->m_Usage == AS_RENDER_TARGET);
   CHECK(!tex->m_bHoldUpload && !tex->m_bUploadPending);
   CHECK(tex->m_pTexture == NULL && tex->m_dwTextureFmt == TEXTURE_FMT_A8R8G8B8);
   CHECK(CTexture_GetSurfaceFormat(tex) == TEXTURE_FMT_UNKNOWN);
   CHECK(CTexture_GetTexture(tex) == NULL);
   /* no pixels: nothing to write */
   CHECK(!CTexture_StartUpdate(tex, &di));

   tex->m_dwCreatedTextureWidth  = 32;
   tex->m_dwCreatedTextureHeight = 16;
   tex->m_pTexture = malloc(32 * 16 * 4);
   CHECK(CTexture_GetSurfaceFormat(tex) == TEXTURE_FMT_A8R8G8B8);
   CHECK(CTexture_GetPixelSize(tex) == 4);
   memset(&di, 0, sizeof(di));
   CHECK(CTexture_StartUpdate(tex, &di));
   CHECK(di.dwWidth == 20 && di.dwHeight == 14 && di.dwCreatedWidth == 32 && di.dwCreatedHeight == 16);
   CHECK(di.lpSurface == tex->m_pTexture && di.lPitch == 32 * 4);
   tex->m_dwTextureFmt = TEXTURE_FMT_A4R4G4B4;
   CHECK(CTexture_GetPixelSize(tex) == 2 && CTexture_GetSurfaceFormat(tex) == TEXTURE_FMT_A4R4G4B4);
   CHECK(CTexture_StartUpdate(tex, &di) && di.lPitch == 32 * 2);
   tex->m_dwTextureFmt = TEXTURE_FMT_A8R8G8B8;

   /* scaled and clamped where the size made is the one asked */
   CTexture_SetOthersVariables(tex);
   CHECK(!tex->m_bScaledS && !tex->m_bClampedS && !tex->m_bScaledT && !tex->m_bClampedT);
   tex->m_dwWidth = 32;
   CTexture_SetOthersVariables(tex);
   CHECK(tex->m_bScaledS && tex->m_bClampedS && !tex->m_bScaledT && !tex->m_bClampedT);
   tex->m_dwHeight = 16;
   CTexture_SetOthersVariables(tex);
   CHECK(tex->m_bScaledT && tex->m_bClampedT);

   /* an end uploads; held, once at the release */
   uploads = 0;
   CTexture_EndUpdate(tex);
   CHECK(uploads == 1 && !tex->m_bUploadPending);
   CTexture_HoldUpload(tex, true);
   CHECK(tex->m_bHoldUpload);
   CTexture_EndUpdate(tex);
   CTexture_EndUpdate(tex);
   CHECK(uploads == 1 && tex->m_bUploadPending);
   CTexture_HoldUpload(tex, false);
   CHECK(uploads == 2 && !tex->m_bUploadPending && !tex->m_bHoldUpload);
   /* held and let go with nothing ended: nothing */
   CTexture_HoldUpload(tex, true);
   CTexture_HoldUpload(tex, false);
   CHECK(uploads == 2);
   CTexture_EndUpdate(tex);
   CHECK(uploads == 3);

   releases = 0;
   CTexture_Delete(tex);
   CHECK(releases == 1);
   CTexture_Delete(NULL);
   CHECK(releases == 1);
}

/* ---- GL's ---- */

static void gl(void)
{
   CTexture *tex;
   GLuint name;
   GLint fmt = 0, min = 0, mag = 0, w = 0, h = 0;
   DrawInfo di;
   uint32_t *px, got[8 * 4];
   unsigned i;

   quality = 0; colour = 0; mipmap = 0;
   tex = COGLTexture_New(5, 3, AS_NORMAL);
   CHECK(tex != NULL);
   CHECK(tex->m_dwWidth == 5 && tex->m_dwHeight == 3);
   CHECK(tex->m_dwCreatedTextureWidth == 8 && tex->m_dwCreatedTextureHeight == 4);
   /* within a step of float (the core is built -Ofast: a reciprocal) */
   CHECK(fabs(tex->m_fXScale - 1.6) < 1e-6 && fabs(tex->m_fYScale - 4.0 / 3.0) < 1e-6);
   CHECK(tex->m_dwTextureFmt == TEXTURE_FMT_A8R8G8B8 && tex->m_Usage == AS_NORMAL);
   CHECK(!tex->m_bHoldUpload && !tex->m_bUploadPending && !tex->m_bScaledS && !tex->m_bClampedT);
   name = COGLTexture_Name(tex);
   CHECK(glIsTexture(name));
   CHECK(((COGLTexture *)tex)->m_glFmt == GL_RGBA);
   glBindTexture(GL_TEXTURE_2D, name);
   glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &w);
   glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &h);
   CHECK(w == 8 && h == 4);

   /* the pixels written, ended: on the GPU, linear, no mipmaps */
   CHECK(CTexture_StartUpdate(tex, &di) && di.lPitch == 8 * 4 && di.dwCreatedWidth == 8);
   px = (uint32_t *)di.lpSurface;
   for (i = 0; i < 8 * 4; i++)
      px[i] = 0x01020304u * (i + 1);
   CTexture_EndUpdate(tex);
   glBindTexture(GL_TEXTURE_2D, name);
   glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, got);
   CHECK(!memcmp(got, px, sizeof(got)));
   glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &min);
   glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, &mag);
   CHECK(min == GL_LINEAR && mag == GL_LINEAR);

   /* held: on the GPU only when let go */
   CTexture_HoldUpload(tex, true);
   px[0] = 0xdeadbeefu;
   CTexture_EndUpdate(tex);
   glBindTexture(GL_TEXTURE_2D, name);
   glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, got);
   CHECK(got[0] != 0xdeadbeefu);
   CTexture_HoldUpload(tex, false);
   glBindTexture(GL_TEXTURE_2D, name);
   glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, got);
   CHECK(got[0] == 0xdeadbeefu);

   /* mipmapped where the option says, from the option as it is then */
   mipmap = 1;
   CTexture_EndUpdate(tex);
   glBindTexture(GL_TEXTURE_2D, name);
   glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &min);
   CHECK(min == GL_LINEAR_MIPMAP_NEAREST);
   glGetTexLevelParameteriv(GL_TEXTURE_2D, 2, GL_TEXTURE_WIDTH, &w);
   CHECK(w == 2);
   mipmap = 0;

   CTexture_Delete(tex);
   CHECK(!glIsTexture(name));

   /* GL's format as the options say: 16 bits where asked, or by default
    * where the colours are 16 bits */
   quality = 2;
   tex = COGLTexture_New(4, 4, AS_NORMAL);
   CHECK(((COGLTexture *)tex)->m_glFmt == GL_RGBA4);
   /* a power of 2 already: as it is */
   CHECK(tex->m_dwCreatedTextureWidth == 4 && tex->m_dwCreatedTextureHeight == 4);
   CHECK(fabs(tex->m_fXScale - 1.0) < 1e-6 && fabs(tex->m_fYScale - 1.0) < 1e-6);
   glBindTexture(GL_TEXTURE_2D, COGLTexture_Name(tex));
   glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &fmt);
   CHECK(fmt == GL_RGBA4);
   CTexture_Delete(tex);
   quality = 0; colour = TEXTURE_FMT_A4R4G4B4;
   tex = COGLTexture_New(4, 4, AS_NORMAL);
   CHECK(((COGLTexture *)tex)->m_glFmt == GL_RGBA4);
   CTexture_Delete(tex);
   quality = 1;
   tex = COGLTexture_New(4, 4, AS_NORMAL);
   CHECK(((COGLTexture *)tex)->m_glFmt == GL_RGBA);
   CTexture_Delete(tex);
   quality = 0; colour = TEXTURE_FMT_A8R8G8B8;
   tex = COGLTexture_New(1, 1, AS_BACK_BUFFER_SAVE);
   CHECK(((COGLTexture *)tex)->m_glFmt == GL_RGBA && tex->m_dwCreatedTextureWidth == 1);
   CHECK(tex->m_Usage == AS_BACK_BUFFER_SAVE);
   CTexture_Delete(tex);
}

int main(void)
{
   context();
   base();
   gl();
   printf("rice texture: %s\n", failures ? "FAILED" : "ok");
   return failures ? 1 : 0;
}
