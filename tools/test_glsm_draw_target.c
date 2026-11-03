/* test_glsm_draw_target -- the frontend presents the frame a renderer
 * finished, not the next one drawn over it.
 *
 * Glide64, Rice and glN64 drew straight into the frontend's framebuffer
 * and went on into the next frame after a swap, before the VI ended the
 * slice: the frontend presented, and with threaded video composited, a
 * framebuffer the next frame was being drawn into, frames interleaved.
 * They draw into a target of the core's own now (glsm_draw_target),
 * glsm_draw_target_present() keeps each frame they finish, and
 * glsm_draw_target_show() puts it in the frontend's framebuffer at the
 * end of every slice. A slice in which they finished no frame handed
 * the frontend none, and with threaded video's three framebuffers it
 * presented one it had since handed out again: frames out of order,
 * ghosting.
 *
 * Here a renderer clears a frame to one colour, swaps with a scissor
 * set, and draws part of the next frame in another, every third slice
 * without a swap, for frames into three frontend framebuffers taken in
 * turn: every slice the frontend must be handed the frame last swapped,
 * in full, the renderer's own target must keep
 * what it drew across the slice boundary, and the core's bindings must
 * be what they were around the swap. Then the target is resized, and
 * glsm set up again as on a context reset. Drawing straight into the
 * frontend's framebuffer, as before, must fail the check.
 *
 * Built for desktop GL (the blit) and for GLES 2 (the copy into the
 * frontend's texture), on Mesa through headless EGL:
 *   EGL_PLATFORM=surfaceless ./test_glsm_draw_target
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <EGL/egl.h>
#include <glsym/glsym.h>
#include <glsm/glsm.h>
#include <libretro.h>

#define FB_W 64
#define FB_H 48

retro_log_printf_t log_cb;

#define FRONTEND_FBS 3 /* RetroArch's threaded video rotates three */
static GLuint frontend_fbo[FRONTEND_FBS], frontend_tex[FRONTEND_FBS];
static unsigned frontend_cur;

static uintptr_t framebuffer(void) { return frontend_fbo[frontend_cur]; }
static retro_proc_address_t resolve(const char *name)
{
   return (retro_proc_address_t)eglGetProcAddress(name);
}

static bool environment(unsigned cmd, void *data)
{
   if (cmd == RETRO_ENVIRONMENT_SET_HW_RENDER)
   {
      struct retro_hw_render_callback *hw = (struct retro_hw_render_callback*)data;
      hw->get_proc_address        = resolve;
      hw->get_current_framebuffer = framebuffer;
      return true;
   }
   return false;
}

/* the frontend's half: its framebuffers, each drawing to a texture, as
 * RetroArch's do */
static void frontend_setup(void)
{
   unsigned i;
   glGenTextures(FRONTEND_FBS, frontend_tex);
   glGenFramebuffers(FRONTEND_FBS, frontend_fbo);
   for (i = 0; i < FRONTEND_FBS; i++)
   {
      glBindTexture(GL_TEXTURE_2D, frontend_tex[i]);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, FB_W, FB_H, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
      glBindFramebuffer(GL_FRAMEBUFFER, frontend_fbo[i]);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, frontend_tex[i], 0);
      assert(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
   }
   glBindTexture(GL_TEXTURE_2D, 0);
   glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

/* what the frontend presents: its framebuffer at (x, y), read as it
 * reads it, with its own bindings */
static uint32_t presented(unsigned x, unsigned y)
{
   GLubyte px[4];
   glBindFramebuffer(GL_FRAMEBUFFER, frontend_fbo[frontend_cur]);
   glPixelStorei(GL_PACK_ALIGNMENT, 4);
   glReadPixels((GLint)x, (GLint)y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
   glBindFramebuffer(GL_FRAMEBUFFER, 0);
   return ((uint32_t)px[0] << 16) | ((uint32_t)px[1] << 8) | px[2];
}

/* the GL state as it stands, read past glsm's caches */
static GLint gl_int(GLenum pname)
{
   GLint actual = -1;
   glGetIntegerv(pname, &actual);
   return actual;
}

static int scissor_enabled(void) { return glIsEnabled(GL_SCISSOR_TEST); }
static GLenum gl_error(void) { return glGetError(); }

/* the core's half, through glsm */
#include <glsm/glsmsym.h>

static uint32_t core_pixel(unsigned x, unsigned y)
{
   GLubyte px[4];
   rglReadPixels((GLint)x, (GLint)y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
   return ((uint32_t)px[0] << 16) | ((uint32_t)px[1] << 8) | px[2];
}

static void clear_to(uint32_t rgb)
{
   rglClearColor(((rgb >> 16) & 0xff) / 255.0f, ((rgb >> 8) & 0xff) / 255.0f,
         (rgb & 0xff) / 255.0f, 1.0f);
   rglClear(GL_COLOR_BUFFER_BIT);
}

/* frames drawn as Glide64 draws them; returns how many the frontend
 * presented as they were swapped, in full */
static unsigned run_frames(unsigned w, unsigned h, unsigned frames, unsigned first)
{
   const unsigned pw = w ? w : FB_W, ph = h ? h : FB_H;
   unsigned k, good = 0;
   uint32_t last = 0, drawn = 0;
   GLuint core_tex;
   glGenTextures(1, &core_tex);
   for (k = first; k < first + frames; k++)
   {
      /* every third slice the renderer finishes no frame, as a 20 or
       * 30 fps game does on the VIs between its frames */
      const bool swaps       = (k - first) % 3 != 2;
      const uint32_t swapped = 0x102030u + 0x0b0000u * (k % 8);
      const uint32_t next    = 0x40c080u + 0x000b00u * (k % 8);
      GLint draw_fb = 0;
      frontend_cur = k % FRONTEND_FBS;
      glsm_draw_target(w, h);
      assert(glsm_ctl(GLSM_CTL_STATE_BIND, NULL));

      /* what the renderer drew into before the boundary is still there */
      if (w && k != first)
         assert(core_pixel(4, 4) == drawn);

      rglBindFramebuffer(GL_FRAMEBUFFER, 0);
      rglBindTexture(GL_TEXTURE_2D, core_tex);
      if (swaps)
      {
         rglDisable(SGL_SCISSOR_TEST);
         clear_to(swapped);
         /* the swap, with the renderer's scissor round a corner */
         rglEnable(SGL_SCISSOR_TEST);
         rglScissor(0, 0, 16, 16);
         rglDrawArrays(GL_POINTS, 0, 0); /* glsm binds what is pending */
         draw_fb = gl_int(GL_FRAMEBUFFER_BINDING);
         glsm_draw_target_present();
         assert(gl_int(GL_FRAMEBUFFER_BINDING) == draw_fb);
         assert(gl_int(GL_TEXTURE_BINDING_2D) == (GLint)core_tex);
         assert(scissor_enabled());
         last = swapped;
      }
      /* the next frame begins in the corner */
      rglEnable(SGL_SCISSOR_TEST);
      rglScissor(0, 0, 16, 16);
      clear_to(next);
      drawn = next;
      assert(core_pixel(4, 4) == next);
      assert(gl_error() == GL_NO_ERROR);
      assert(glsm_ctl(GLSM_CTL_STATE_UNBIND, NULL));

      /* the end of the slice: the frontend is handed the frame last
       * finished, in the framebuffer it gave for this slice */
      if (glsm_draw_target_show())
         assert(gl_error() == GL_NO_ERROR);

      if (last && presented(4, 4) == last && presented(pw - 1, ph - 1) == last
            && presented(pw / 2, ph / 2) == last)
         good++;
   }
   glDeleteTextures(1, &core_tex);
   return good;
}

int main(void)
{
   const EGLint config_attrs[] = { EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
#ifdef HAVE_OPENGLES
      EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
#else
      EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
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
   glsm_ctx_params_t params;

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
   frontend_setup();

   memset(&params, 0, sizeof(params));
   params.environ_cb = environment;
   assert(glsm_ctl(GLSM_CTL_STATE_CONTEXT_INIT, &params));
   assert(glsm_ctl(GLSM_CTL_STATE_CONTEXT_RESET, NULL));
   assert(glsm_ctl(GLSM_CTL_STATE_SETUP, NULL));
   assert(glsm_ctl(GLSM_CTL_STATE_UNBIND, NULL));
   while (gl_error() != GL_NO_ERROR) {}

   assert(run_frames(FB_W, FB_H, 9, 0) == 9);
   /* a smaller frame: the target follows it */
   assert(run_frames(40, 30, 6, 9) == 6);
   /* set up again, as on a context reset */
   assert(glsm_ctl(GLSM_CTL_STATE_SETUP, NULL));
   assert(glsm_ctl(GLSM_CTL_STATE_UNBIND, NULL));
   assert(run_frames(FB_W, FB_H, 6, 15) == 6);

   /* drawing straight into the frontend's framebuffer, as before, shows
    * the next frame in the corner of every one */
   if (run_frames(0, 0, 6, 21) != 0)
   {
      fprintf(stderr, "the check passes drawing into the frontend's framebuffer\n");
      return 1;
   }

   puts("test_glsm_draw_target: every presented frame is the swapped one");
   eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
   eglDestroySurface(display, surface);
   eglDestroyContext(display, context);
   eglTerminate(display);
   return 0;
}
