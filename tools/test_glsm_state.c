/* test_glsm_state -- the GL state glsm keeps for the core is the state GL
 * holds while the core draws.
 *
 * glsm caches the core's GL state and skips a call whose value it has
 * cached. Between slices the frontend draws with the same context, so
 * glsm_state_bind must put the core's state back over whatever the
 * frontend left, and the cache must follow GL wherever GL changes on its
 * own. It did not: a capability the frontend left enabled (blending, by
 * the statistics overlay among others) stayed enabled, as glsm only
 * re-enabled the core's; the clear colour was cached in integers, and
 * only for the default framebuffer; glBlendFunc's cache never saw
 * glBlendFuncSeparate; the cull face was never restored; a texture
 * deleted on another unit, or a deleted buffer, stayed cached as bound,
 * so the name GL gave again was never bound; a vertex array's element
 * buffer was cached across vertex arrays; glDrawBuffers was set once per
 * framebuffer and ignored after. Each made the core draw with state it
 * had not asked for: the frames of Glide64, Rice and glN64 blended into
 * the ones before them.
 *
 * Here the core sets its state through glsm, the frontend changes all of
 * it between slices, and after the bind every piece must read back as
 * the core set it, and after the unbind as GL's defaults. Then each
 * deletion and cache case above must leave GL holding what the core
 * asked for. Built for desktop GL and for GLES 2, on Mesa through
 * headless EGL:
 *   EGL_PLATFORM=surfaceless ./test_glsm_state
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <EGL/egl.h>
#include <glsym/glsym.h>
#include <glsm/glsm.h>
#include <libretro.h>

retro_log_printf_t log_cb;

static GLuint frontend_fbo, frontend_tex;
static int failures;

static uintptr_t framebuffer(void) { return frontend_fbo; }
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

/* GL as it stands, read past glsm */
static GLint gl_int(GLenum pname)
{
   GLint v = -1;
   glGetIntegerv(pname, &v);
   return v;
}

static float gl_float(GLenum pname, unsigned i)
{
   GLfloat v[4] = { -1, -1, -1, -1 };
   glGetFloatv(pname, v);
   return v[i];
}

static GLboolean gl_bool(GLenum pname, unsigned i)
{
   GLboolean v[4] = { 9, 9, 9, 9 };
   glGetBooleanv(pname, v);
   return v[i];
}

static int gl_enabled(GLenum cap) { return glIsEnabled(cap) ? 1 : 0; }

#define CHECK(what, cond) do { if (!(cond)) { \
   fprintf(stderr, "%s: %s does not hold (%s:%d)\n", what, #cond, __FILE__, __LINE__); \
   failures++; } } while (0)

/* the frontend's half between slices: every piece of state changed */
static void frontend_draws(void)
{
   glBindFramebuffer(GL_FRAMEBUFFER, frontend_fbo);
   glEnable(GL_BLEND);
   glEnable(GL_SCISSOR_TEST);
   glEnable(GL_CULL_FACE);
   glEnable(GL_STENCIL_TEST);
   glEnable(GL_POLYGON_OFFSET_FILL);
   glDisable(GL_DEPTH_TEST);
   glBlendFuncSeparate(GL_ONE, GL_ONE, GL_ZERO, GL_ONE);
   glBlendEquationSeparate(GL_FUNC_SUBTRACT, GL_FUNC_REVERSE_SUBTRACT);
   glBlendColor(0.5f, 0.5f, 0.5f, 0.5f);
   glColorMask(GL_FALSE, GL_TRUE, GL_FALSE, GL_TRUE);
   glDepthMask(GL_FALSE);
   glDepthFunc(GL_ALWAYS);
   glClearColor(1.0f, 0.0f, 1.0f, 1.0f);
   glCullFace(GL_FRONT);
   glFrontFace(GL_CW);
   glStencilFunc(GL_NEVER, 3, 7);
   glStencilOp(GL_ZERO, GL_INCR, GL_DECR);
   glStencilMask(5);
   glPolygonOffset(3.0f, 3.0f);
   glScissor(1, 2, 3, 4);
   glViewport(5, 6, 7, 8);
   glUseProgram(0);
}

/* the core's state, as GL must hold it while the core draws */
static void check_core_state(const char *when)
{
   CHECK(when, !gl_enabled(GL_BLEND));
   CHECK(when, !gl_enabled(GL_SCISSOR_TEST));
   CHECK(when, !gl_enabled(GL_STENCIL_TEST));
   CHECK(when, !gl_enabled(GL_POLYGON_OFFSET_FILL));
   CHECK(when, gl_enabled(GL_DEPTH_TEST));
   CHECK(when, gl_enabled(GL_CULL_FACE));
   CHECK(when, gl_int(GL_BLEND_SRC_RGB) == GL_SRC_ALPHA);
   CHECK(when, gl_int(GL_BLEND_DST_RGB) == GL_ONE_MINUS_SRC_ALPHA);
   CHECK(when, gl_int(GL_BLEND_SRC_ALPHA) == GL_SRC_ALPHA);
   CHECK(when, gl_int(GL_BLEND_DST_ALPHA) == GL_ONE_MINUS_SRC_ALPHA);
   CHECK(when, gl_int(GL_BLEND_EQUATION_RGB) == GL_FUNC_ADD);
   CHECK(when, gl_int(GL_BLEND_EQUATION_ALPHA) == GL_FUNC_ADD);
   CHECK(when, gl_float(GL_BLEND_COLOR, 0) == 0.0f);
   CHECK(when, gl_bool(GL_COLOR_WRITEMASK, 0) && gl_bool(GL_COLOR_WRITEMASK, 2));
   CHECK(when, gl_bool(GL_DEPTH_WRITEMASK, 0));
   CHECK(when, gl_int(GL_DEPTH_FUNC) == GL_LEQUAL);
   CHECK(when, fabsf(gl_float(GL_COLOR_CLEAR_VALUE, 0) - 0.25f) < 1e-6f);
   CHECK(when, fabsf(gl_float(GL_COLOR_CLEAR_VALUE, 1) - 0.5f) < 1e-6f);
   CHECK(when, fabsf(gl_float(GL_COLOR_CLEAR_VALUE, 2) - 0.75f) < 1e-6f);
   CHECK(when, gl_int(GL_CULL_FACE_MODE) == GL_BACK);
   CHECK(when, gl_int(GL_FRONT_FACE) == GL_CCW);
   CHECK(when, gl_int(GL_STENCIL_FUNC) == GL_ALWAYS);
   CHECK(when, gl_int(GL_STENCIL_FAIL) == GL_KEEP);
   CHECK(when, (gl_int(GL_STENCIL_WRITEMASK) & 0xff) == 0xff);
   CHECK(when, gl_float(GL_POLYGON_OFFSET_FACTOR, 0) == 0.0f);
   CHECK(when, gl_int(GL_VIEWPORT) == 0);
   CHECK(when, gl_int(GL_FRAMEBUFFER_BINDING) != 0); /* the core's target */
}

static void check_defaults(void)
{
   const char *when = "after the unbind";
   CHECK(when, !gl_enabled(GL_BLEND));
   CHECK(when, !gl_enabled(GL_DEPTH_TEST));
   CHECK(when, !gl_enabled(GL_CULL_FACE));
   CHECK(when, gl_int(GL_BLEND_SRC_RGB) == GL_ONE);
   CHECK(when, gl_int(GL_BLEND_DST_RGB) == GL_ZERO);
   CHECK(when, gl_int(GL_DEPTH_FUNC) == GL_LESS);
   CHECK(when, (gl_int(GL_STENCIL_WRITEMASK) & 0xff) == 0xff);
   CHECK(when, gl_int(GL_CURRENT_PROGRAM) == 0);
}

#include <glsm/glsmsym.h>

static void core_state(void)
{
   rglDisable(SGL_BLEND);
   rglDisable(SGL_SCISSOR_TEST);
   rglDisable(SGL_STENCIL_TEST);
   rglDisable(SGL_POLYGON_OFFSET_FILL);
   rglEnable(SGL_DEPTH_TEST);
   rglEnable(SGL_CULL_FACE);
   rglBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
   rglDepthFunc(GL_LEQUAL);
   rglClearColor(0.25f, 0.5f, 0.75f, 1.0f);
   rglViewport(0, 0, 64, 48);
}

int main(void)
{
   const EGLint config_attrs[] = { EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
#ifdef HAVE_OPENGLES
      EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
#else
      EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
#endif
      EGL_STENCIL_SIZE, 8, EGL_NONE };
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
   GLuint tex[2], buf, again, offscreen;
   unsigned slice;

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

   glGenTextures(1, &frontend_tex);
   glBindTexture(GL_TEXTURE_2D, frontend_tex);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 64, 48, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
   glGenFramebuffers(1, &frontend_fbo);
   glBindFramebuffer(GL_FRAMEBUFFER, frontend_fbo);
   glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, frontend_tex, 0);
   glBindTexture(GL_TEXTURE_2D, 0);

   memset(&params, 0, sizeof(params));
   params.environ_cb = environment;
   assert(glsm_ctl(GLSM_CTL_STATE_CONTEXT_INIT, &params));
   assert(glsm_ctl(GLSM_CTL_STATE_CONTEXT_RESET, NULL));
   assert(glsm_ctl(GLSM_CTL_STATE_SETUP, NULL));
   glsm_draw_target(64, 48);
   assert(glsm_ctl(GLSM_CTL_STATE_BIND, NULL));
   core_state();

   for (slice = 0; slice < 3; slice++)
   {
      assert(glsm_ctl(GLSM_CTL_STATE_UNBIND, NULL));
      check_defaults();
      frontend_draws();
      assert(glsm_ctl(GLSM_CTL_STATE_BIND, NULL));
      check_core_state("after the bind");
      /* what the core asks for again, cached, is what GL holds */
      core_state();
      check_core_state("after the core sets it again");
   }

   /* the clear colour, set while another framebuffer is bound, is the
    * colour GL holds after */
   glGenFramebuffers(1, &offscreen);
   rglBindFramebuffer(GL_FRAMEBUFFER, offscreen);
   rglClearColor(1.0f, 0.0f, 0.0f, 1.0f);
   rglBindFramebuffer(GL_FRAMEBUFFER, 0);
   rglClearColor(0.25f, 0.5f, 0.75f, 1.0f);
   CHECK("clear colour", fabsf(gl_float(GL_COLOR_CLEAR_VALUE, 0) - 0.25f) < 1e-6f);

   /* glBlendFunc after glBlendFuncSeparate */
   rglBlendFunc(GL_ONE, GL_ZERO);
   rglBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ZERO);
   rglBlendFunc(GL_ONE, GL_ZERO);
   CHECK("blend func", gl_int(GL_BLEND_SRC_RGB) == GL_ONE && gl_int(GL_BLEND_DST_ALPHA) == GL_ZERO);

   /* a texture deleted while another unit is active */
   rglGenTextures(2, tex);
   rglActiveTexture(GL_TEXTURE1);
   rglBindTexture(GL_TEXTURE_2D, tex[0]);
   rglActiveTexture(GL_TEXTURE0);
   rglDeleteTextures(1, &tex[0]);
   rglGenTextures(1, &again); /* GL gives the name again */
   rglActiveTexture(GL_TEXTURE1);
   rglBindTexture(GL_TEXTURE_2D, again);
   CHECK("texture name given again", (GLuint)gl_int(GL_TEXTURE_BINDING_2D) == again);
   rglActiveTexture(GL_TEXTURE0);

   /* a deleted buffer */
   rglGenBuffers(1, &buf);
   rglBindBuffer(GL_ARRAY_BUFFER, buf);
   rglDeleteBuffers(1, &buf);
   rglGenBuffers(1, &again);
   rglBindBuffer(GL_ARRAY_BUFFER, again);
   CHECK("buffer name given again", (GLuint)gl_int(GL_ARRAY_BUFFER_BINDING) == again);

   /* a deleted framebuffer that was to be bound */
   rglBindFramebuffer(GL_FRAMEBUFFER, offscreen);
   rglDeleteFramebuffers(1, &offscreen);
   rglClear(GL_COLOR_BUFFER_BIT);
   CHECK("deleted framebuffer", gl_int(GL_FRAMEBUFFER_BINDING) != 0 && glGetError() == GL_NO_ERROR);

#ifndef HAVE_OPENGLES
   {
      /* the element buffer belongs to the vertex array */
      GLuint vao[2], ebo;
      rglGenVertexArrays(2, vao);
      rglGenBuffers(1, &ebo);
      rglBindVertexArray(vao[0]);
      rglBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
      rglBindVertexArray(vao[1]);
      rglBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
      CHECK("element buffer of a vertex array", (GLuint)gl_int(GL_ELEMENT_ARRAY_BUFFER_BINDING) == ebo);
      rglBindVertexArray(0);
   }
   {
      /* the draw buffers asked for, each time */
      GLenum none = GL_NONE, colour = GL_COLOR_ATTACHMENT0;
      GLuint fbo, ctex;
      rglGenFramebuffers(1, &fbo);
      rglGenTextures(1, &ctex);
      rglBindTexture(GL_TEXTURE_2D, ctex);
      rglTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
      rglBindFramebuffer(GL_FRAMEBUFFER, fbo);
      rglFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, ctex, 0);
      rglDrawBuffers(1, &none);
      rglDrawBuffers(1, &colour);
      CHECK("draw buffers", gl_int(GL_DRAW_BUFFER0) == GL_COLOR_ATTACHMENT0);
      rglBindFramebuffer(GL_FRAMEBUFFER, 0);
   }
#endif
   CHECK("no GL error", glGetError() == GL_NO_ERROR);

   if (failures)
   {
      fprintf(stderr, "test_glsm_state: %d failures\n", failures);
      return 1;
   }
   puts("test_glsm_state: GL holds the core's state");
   eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
   eglDestroySurface(display, surface);
   eglDestroyContext(display, context);
   eglTerminate(display);
   return 0;
}
