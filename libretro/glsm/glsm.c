/* Copyright (C) 2010-2019 The RetroArch team
 *
 * ---------------------------------------------------------------------------------------
 * The following license statement only applies to this libretro SDK code part (glsm).
 * ---------------------------------------------------------------------------------------
 *
 * Permission is hereby granted, free of charge,
 * to any person obtaining a copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/* glsym is deliberately held back from upstream libretro-common.
 *
 * Everything else under libretro-common/ here tracks RetroArch master, but
 * glsym/ and include/glsym/ do not, and a wholesale resync will break the
 * link.  Upstream has dropped the rglgen indirection for 37 OpenGL 1.1 core
 * entry points - glBlendFunc, glClearColor, glDisable, glDrawArrays and the
 * rest are no longer #defined to __rglgen_* - because RetroArch itself links
 * against a GL library and can call them directly.
 *
 * A libretro core cannot.  It is a shared object loaded into a frontend that
 * has already chosen a context - GLX, EGL, WGL, a software wrapper - and the
 * whole point of resolving entry points through rglgen_resolve_symbols() and
 * the frontend's get_proc_address is that one core binary works against any
 * of them.  Linking a GL library into the core to satisfy those 37 symbols
 * would bind it to one implementation at build time, and the library to name
 * differs per platform (-lGL, -lGLESv2, opengl32, a framework), so it is not
 * one flag but a per-target matrix that would have to be kept correct
 * forever.
 *
 * This file is the coupling: it drives glsym and has no upstream counterpart,
 * so glsym and glsm move together or not at all.  Syncing only glsym leaves
 * roughly forty GL symbols undefined at link.  If glsm is ever reworked to
 * resolve those entry points itself, glsym can rejoin the resync; until then
 * the drift is intentional and this comment is the reason. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glsym/glsym.h>
#include <glsm/glsm.h>
#include <gfx/gl_capabilities.h>

#if defined(HAVE_OPENGLES)
#if !defined(IOS)
#include <EGL/egl.h>
#endif /* !defined(IOS) */
typedef void (GL_APIENTRYP PFNGLDRAWRANGEELEMENTSBASEVERTEXPROC) (GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type, const void *indices, GLint basevertex);
typedef void (GL_APIENTRYP PFNGLBUFFERSTORAGEEXTPROC) (GLenum target, GLsizeiptr size, const void *data, GLbitfield flags);
typedef void (GL_APIENTRYP PFNGLMEMORYBARRIERPROC) (GLbitfield barriers);
typedef void (GL_APIENTRYP PFNGLBINDIMAGETEXTUREPROC) (GLuint unit, GLuint texture, GLint level, GLboolean layered, GLint layer, GLenum access, GLenum format);
typedef void (GL_APIENTRYP PFNGLTEXSTORAGE2DMULTISAMPLEPROC) (GLenum target, GLsizei samples, GLenum internalformat, GLsizei width, GLsizei height, GLboolean fixedsamplelocations);
typedef void (GL_APIENTRYP PFNGLCOPYIMAGESUBDATAPROC) (GLuint srcName, GLenum srcTarget, GLint srcLevel, GLint srcX, GLint srcY, GLint srcZ, GLuint dstName, GLenum dstTarget, GLint dstLevel, GLint dstX, GLint dstY, GLint dstZ, GLsizei srcWidth, GLsizei srcHeight, GLsizei srcDepth);
PFNGLDRAWRANGEELEMENTSBASEVERTEXPROC m_glDrawRangeElementsBaseVertex;
PFNGLBUFFERSTORAGEEXTPROC m_glBufferStorage;
PFNGLMEMORYBARRIERPROC m_glMemoryBarrier;
PFNGLBINDIMAGETEXTUREPROC m_glBindImageTexture;
PFNGLTEXSTORAGE2DMULTISAMPLEPROC m_glTexStorage2DMultisample;
PFNGLCOPYIMAGESUBDATAPROC m_glCopyImageSubData;
#endif /* defined(HAVE_OPENGLES) */

#ifndef GL_DEPTH_CLAMP
#define GL_DEPTH_CLAMP                    0x864F
#define GL_RASTERIZER_DISCARD             0x8C89
#define GL_SAMPLE_MASK                    0x8E51
#endif

/* the uniform values cached: programs and locations past these go
 * straight to GL */
#define GLSM_UNIFORM_PROGRAMS  4096
#define GLSM_UNIFORM_LOCATIONS 64


/* The core's GL state as glsm knows it. Every field holds what the core
 * last set, or GL's default; glsm_state_bind puts all of it back over
 * whatever the frontend left, and glsm_state_unbind leaves GL's defaults
 * for the frontend. A setter skips a call only when the cache says GL
 * already holds the value, so the cache must never differ from GL while
 * the core's state is bound. */
struct gl_cached_state
{
   struct
   {
      GLuint ids[32];
      GLenum target[32];
   } bind_textures;

   struct
   {
      bool used[MAX_ATTRIB];
      GLint size[MAX_ATTRIB];
      GLenum type[MAX_ATTRIB];
      GLboolean normalized[MAX_ATTRIB];
      GLsizei stride[MAX_ATTRIB];
      const GLvoid *pointer[MAX_ATTRIB];
      GLuint buffer[MAX_ATTRIB];
   } attrib_pointer;

   struct
   {
      bool enabled[MAX_ATTRIB];
   } vertex_attrib_pointer;

   struct
   {
      GLuint array;
   } bindvertex;

   struct
   {
      GLint pack;
      GLint unpack;
   } pixelstore;

   GLfloat clear_color[4];
   GLdouble clear_depth;

   struct
   {
      bool used; /* the box is GL's window size until the core sets one */
      GLint x;
      GLint y;
      GLsizei w;
      GLsizei h;
   } scissor, viewport;

   GLenum blend_func[4];     /* src RGB, dst RGB, src alpha, dst alpha */
   GLenum blend_equation[2]; /* RGB, alpha */
   GLfloat blend_color[4];
   GLboolean color_mask[4];
   GLboolean depth_mask;
   GLenum depth_func;
   GLclampd depth_range[2];
   GLfloat polygon_offset[2];
   GLfloat line_width;
#ifndef HAVE_OPENGLES
   GLenum polygon_mode;
#endif
   GLenum cull_face;
   GLenum front_face;

   struct
   {
      GLenum func;
      GLint ref;
      GLuint mask;
   } stencil_func;

   GLenum stencil_op[3];
   GLuint stencil_mask;

   struct
   {
      bool used;
      GLenum mode;
   } readbuffer;

   struct
   {
      GLuint location;
      GLuint desired_location;
   } framebuf[2];

   GLuint vao;
   int fbo_cleared_once;
   GLuint array_buffer;
   GLuint index_buffer;
   GLuint program;
   int cap_state[SGL_CAP_MAX];
   int cap_translate[SGL_CAP_MAX]; /* 0: not a capability of this context */
};

void rglBlendFuncSeparate(GLenum srcRGB, GLenum dstRGB, GLenum srcAlpha, GLenum dstAlpha);
void rglBlendEquationSeparate(GLenum modeRGB, GLenum modeAlpha);

/* no element buffer is known to be bound: a VAO bind changed it */
#define GLSM_UNKNOWN_BUFFER 0xFFFFFFFFu

struct gl_program_uniforms
{
   GLfloat uniform1f;
   GLfloat uniform2f[2];
   GLfloat uniform3f[3];
   GLfloat uniform4f[4];
   GLint uniform1i;
   GLint uniform2i[2];
   GLint uniform3i[3];
   GLint uniform4i[4];
};

/* What glsm knows of a framebuffer object the core uses: the attachments
 * and draw buffers it last set, so the same ones are not set again. */
struct gl_framebuffers
{
   GLuint id;
   GLuint color_attachment;
   GLuint depth_attachment;
   GLsizei draw_buffers_n; /* 0: not set through glsm */
   GLenum draw_buffers[4];
};

static GLuint default_framebuffer;
static GLint glsm_max_textures;
struct retro_hw_render_callback hw_render;
static struct gl_cached_state gl_state;

/* each program's uniform values, allocated as it first sets one; all 0,
 * as GL leaves them when a program links */
static struct gl_program_uniforms *program_uniforms[GLSM_UNIFORM_PROGRAMS];

static void glsm_uniforms_forget(GLuint program)
{
   if (program < GLSM_UNIFORM_PROGRAMS && program_uniforms[program])
   {
      free(program_uniforms[program]);
      program_uniforms[program] = NULL;
   }
}

/* The cached values of a uniform of the program in use, or NULL where the
 * cache has no slot for it: location -1 (a uniform the program does not
 * have, which GL ignores), or a location or program past the cache. */
static struct gl_program_uniforms *glsm_uniform_slot(GLint location)
{
   const GLuint program = gl_state.program;
   if (location < 0 || location >= GLSM_UNIFORM_LOCATIONS
         || program == 0 || program >= GLSM_UNIFORM_PROGRAMS)
      return NULL;
   if (!program_uniforms[program])
      program_uniforms[program] = (struct gl_program_uniforms*)
         calloc(GLSM_UNIFORM_LOCATIONS, sizeof(struct gl_program_uniforms));
   return program_uniforms[program] ? &program_uniforms[program][location] : NULL;
}
/* a few framebuffers at a time: a list, searched */
static struct gl_framebuffers *fb_records;
static size_t fb_count, fb_cap;

static struct gl_framebuffers *glsm_fb_find(GLuint id)
{
   size_t i;
   for (i = 0; i < fb_count; i++)
      if (fb_records[i].id == id)
         return &fb_records[i];
   return NULL;
}

/* the record of framebuffer id, a new empty one if it has none; NULL
 * only when out of memory */
static struct gl_framebuffers *glsm_fb(GLuint id)
{
   struct gl_framebuffers *fb = glsm_fb_find(id);
   if (fb)
      return fb;
   if (fb_count == fb_cap)
   {
      size_t cap = fb_cap ? fb_cap * 2 : 16;
      struct gl_framebuffers *grown = (struct gl_framebuffers*)
         realloc(fb_records, cap * sizeof(*grown));
      if (!grown)
         return NULL;
      fb_records = grown;
      fb_cap     = cap;
   }
   fb = &fb_records[fb_count++];
   memset(fb, 0, sizeof(*fb));
   fb->id = id;
   return fb;
}

static void glsm_fb_forget(GLuint id)
{
   struct gl_framebuffers *fb = glsm_fb_find(id);
   if (fb)
      *fb = fb_records[--fb_count];
}

/* The draw target (see glsm_draw_target) */
static GLuint draw_fbo, draw_color, draw_depth;
static GLuint front_fbo, front_color;     /* the frame last finished */
static bool front_valid;
static unsigned draw_w, draw_h;           /* the target allocated */
static unsigned draw_want_w, draw_want_h; /* the one asked for: 0 for none */
static bool draw_failed;                  /* the frontend's can only be drawn into */
static GLuint frontend_framebuffer;

static GLenum active_texture;
static int window_first = 0;
/* the objects named are the lost context's: deleting them would delete
 * whatever the new context has under those names */
static int resetting_context = 0;

void glsm_abandon_objects(bool abandon)
{
   resetting_context = abandon ? 1 : 0;
}

static void bindFBO(GLenum target)
{
#ifdef HAVE_OPENGLES2
   if (target == GL_FRAMEBUFFER && (gl_state.framebuf[0].desired_location != gl_state.framebuf[0].location || gl_state.framebuf[1].desired_location != gl_state.framebuf[1].location))
   {
      glBindFramebuffer(GL_FRAMEBUFFER, gl_state.framebuf[0].desired_location);
      gl_state.framebuf[0].location = gl_state.framebuf[0].desired_location;
      gl_state.framebuf[1].location = gl_state.framebuf[1].desired_location;
   }
#else
   if ((target == GL_FRAMEBUFFER || target == GL_DRAW_FRAMEBUFFER) && gl_state.framebuf[0].desired_location != gl_state.framebuf[0].location)
   {
      glBindFramebuffer(GL_DRAW_FRAMEBUFFER, gl_state.framebuf[0].desired_location);
      gl_state.framebuf[0].location = gl_state.framebuf[0].desired_location;
   }
   else if (target == GL_READ_FRAMEBUFFER && gl_state.framebuf[1].desired_location != gl_state.framebuf[1].location)
   {
      glBindFramebuffer(GL_READ_FRAMEBUFFER, gl_state.framebuf[1].desired_location);
      gl_state.framebuf[1].location = gl_state.framebuf[1].desired_location;
   }
#endif
}

/* GL wrapper-side */

/*
 *
 * Core in:
 * OpenGL    : 1.0
 */
GLenum rglGetError(void)
{
   return glGetError();
}

/*
 *
 * Core in:
 * OpenGL    : 3.2
 * OpenGLES  : N/A
 */

void rglProvokingVertex(	GLenum provokeMode)
{
#if defined(HAVE_OPENGL)
   glProvokingVertex(provokeMode);
#endif
}

/*
 *
 * Core in:
 * OpenGL    : 3.2
 * OpenGLES  : 3.0
 */
void rglGetInteger64v(	GLenum pname, int64_t *data)
{
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES) && defined(HAVE_OPENGLES3)
   glGetInteger64v(pname, (GLint64*)data);
#endif
}

/*
 *
 * Core in:
 * OpenGL    : 3.2
 * OpenGLES  : 3.0
 */
void rglSamplerParameteri(	GLuint sampler,
 	GLenum pname,
 	GLint param)
{
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES) && defined(HAVE_OPENGLES3)
   glSamplerParameteri(sampler, pname, param);
#endif
}

void rglGenSamplers(	GLsizei n,
 	GLuint *samplers)
{
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES) && defined(HAVE_OPENGLES3)
   glGenSamplers(n, samplers);
#endif
}

void rglBindSampler(	GLuint unit,
 	GLuint sampler)
{
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES) && defined(HAVE_OPENGLES3)
   glBindSampler(unit, sampler);
#endif
}

/*
 *
 * Core in:
 * OpenGL    : 1.0
 */
void rglClear(GLbitfield mask)
{
   bindFBO(GL_FRAMEBUFFER);
   glClear(mask);
}

/*
 *
 * Core in:
 * OpenGL    : 2.0
 */
void rglValidateProgram(GLuint program)
{
   glValidateProgram(program);
}

/*
 *
 * Core in:
 * OpenGL    : 1.0
 * OpenGLES  : N/A
 */
void rglPolygonMode(GLenum face, GLenum mode)
{
#ifndef HAVE_OPENGLES
   if (gl_state.polygon_mode != mode)
   {
      glPolygonMode(GL_FRONT_AND_BACK, mode);
      gl_state.polygon_mode = mode;
   }
#endif
}

void rglTexSubImage2D(
      GLenum target,
  	GLint level,
  	GLint xoffset,
  	GLint yoffset,
  	GLsizei width,
  	GLsizei height,
  	GLenum format,
  	GLenum type,
  	const GLvoid * pixels)
{
   glTexSubImage2D(target, level, xoffset, yoffset,
         width, height, format, type, pixels);
}

void rglGetBufferSubData(	GLenum target,
 	GLintptr offset,
 	GLsizeiptr size,
 	GLvoid * data)
{
#if defined(HAVE_OPENGL)
   glGetBufferSubData(target, offset, size, data);
#endif
}

/*
 *
 * Core in:
 * OpenGL    : 1.0glGetError()
 */
void rglLineWidth(GLfloat width)
{
   if (gl_state.line_width != width)
   {
      glLineWidth(width);
      gl_state.line_width = width;
   }
}

void rglReadPixels(	GLint x,
 	GLint y,
 	GLsizei width,
 	GLsizei height,
 	GLenum format,
 	GLenum type,
 	GLvoid * data)
{
#ifdef HAVE_OPENGLES2
   bindFBO(GL_FRAMEBUFFER);
#else
   bindFBO(GL_READ_FRAMEBUFFER);
#endif
   glReadPixels(x, y, width, height, format, type, data);
}

/*
 *
 * Core in:
 * OpenGLES  : 3.0
 */
void rglReadBuffer(GLenum mode)
{
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES) && defined(HAVE_OPENGLES3)
   glReadBuffer(mode);
   if (gl_state.framebuf[1].desired_location == default_framebuffer)
   {
      gl_state.readbuffer.used = true;
      gl_state.readbuffer.mode = mode;
   }
#endif
}

/*
 *
 * Core in:
 * OpenGLES  : 2.0
 */
void rglClearDepth(GLdouble depth)
{
   if (gl_state.clear_depth == depth)
      return;
#ifdef HAVE_OPENGLES
   glClearDepthf(depth);
#else
   glClearDepth(depth);
#endif
   gl_state.clear_depth = depth;
}

/*
 *
 * Core in:
 * OpenGLES  : 2.0
 */
void rglPixelStorei(GLenum pname, GLint param)
{
   if (pname == GL_UNPACK_ALIGNMENT) {
      if (param != gl_state.pixelstore.unpack) {
         glPixelStorei(pname, param);
         gl_state.pixelstore.unpack = param;
      }
   } else if (pname == GL_PACK_ALIGNMENT) {
      if (param != gl_state.pixelstore.pack) {
         glPixelStorei(pname, param);
         gl_state.pixelstore.pack = param;
      }
   } else
      glPixelStorei(pname, param);
}

/*
 *
 * Core in:
 * OpenGLES  : 2.0
 */
void rglDepthRange(GLclampd zNear, GLclampd zFar)
{
   if (gl_state.depth_range[0] == zNear && gl_state.depth_range[1] == zFar)
      return;
#ifdef HAVE_OPENGLES
   glDepthRangef(zNear, zFar);
#else
   glDepthRange(zNear, zFar);
#endif
   gl_state.depth_range[0] = zNear;
   gl_state.depth_range[1] = zFar;
}

/*
 *
 * Core in:
 * OpenGLES  : 2.0
 */
void rglFrontFace(GLenum mode)
{
   if (gl_state.front_face != mode)
   {
      glFrontFace(mode);
      gl_state.front_face = mode;
   }
}

/*
 *
 * Core in:
 * OpenGLES  : 2.0
 */
void rglDepthFunc(GLenum func)
{
   if (gl_state.depth_func != func)
   {
      glDepthFunc(func);
      gl_state.depth_func = func;
   }
}

/*
 *
 * Core in:
 * OpenGLES  : 2.0
 */
void rglColorMask(GLboolean red, GLboolean green,
      GLboolean blue, GLboolean alpha)
{
   if (gl_state.color_mask[0] != red || gl_state.color_mask[1] != green
         || gl_state.color_mask[2] != blue || gl_state.color_mask[3] != alpha)
   {
      glColorMask(red, green, blue, alpha);
      gl_state.color_mask[0] = red;
      gl_state.color_mask[1] = green;
      gl_state.color_mask[2] = blue;
      gl_state.color_mask[3] = alpha;
   }
}

/*
 *
 * Core in:
 * OpenGLES  : 2.0
 */
void rglCullFace(GLenum mode)
{
   if (gl_state.cull_face != mode)
   {
      glCullFace(mode);
      gl_state.cull_face = mode;
   }
}

/*
 *
 * Core in:
 * OpenGLES  : 2.0
 */
void rglStencilOp(GLenum sfail, GLenum dpfail, GLenum dppass)
{
   if (gl_state.stencil_op[0] != sfail || gl_state.stencil_op[1] != dpfail
         || gl_state.stencil_op[2] != dppass)
   {
      glStencilOp(sfail, dpfail, dppass);
      gl_state.stencil_op[0] = sfail;
      gl_state.stencil_op[1] = dpfail;
      gl_state.stencil_op[2] = dppass;
   }
}

/*
 *
 * Core in:
 * OpenGLES  : 2.0
 */
void rglStencilFunc(GLenum func, GLint ref, GLuint mask)
{
   if (gl_state.stencil_func.func != func || gl_state.stencil_func.ref != ref
         || gl_state.stencil_func.mask != mask)
   {
      glStencilFunc(func, ref, mask);
      gl_state.stencil_func.func = func;
      gl_state.stencil_func.ref  = ref;
      gl_state.stencil_func.mask = mask;
   }
}

/*
 *
 * Core in:
 * OpenGL    : 1.0
 */
GLboolean rglIsEnabled(GLenum cap)
{
   return gl_state.cap_state[cap] ? GL_TRUE : GL_FALSE;
}

/*
 *
 * Core in:
 * OpenGL    : 1.0
 */
void rglClearColor(GLclampf red, GLclampf green,
      GLclampf blue, GLclampf alpha)
{
   if (gl_state.clear_color[0] != red || gl_state.clear_color[1] != green
         || gl_state.clear_color[2] != blue || gl_state.clear_color[3] != alpha)
   {
      glClearColor(red, green, blue, alpha);
      gl_state.clear_color[0] = red;
      gl_state.clear_color[1] = green;
      gl_state.clear_color[2] = blue;
      gl_state.clear_color[3] = alpha;
   }
}

/*
 *
 * Core in:
 * OpenGLES    : 2.0 (maybe earlier?)
 */
void rglScissor(GLint x, GLint y, GLsizei width, GLsizei height)
{
   if (!gl_state.scissor.used || gl_state.scissor.x != x || gl_state.scissor.y != y
         || gl_state.scissor.w != width || gl_state.scissor.h != height)
   {
      glScissor(x, y, width, height);
      gl_state.scissor.used = true;
      gl_state.scissor.x    = x;
      gl_state.scissor.y    = y;
      gl_state.scissor.w    = width;
      gl_state.scissor.h    = height;
   }
}

/*
 *
 * Core in:
 * OpenGL    : 1.0
 */
void rglViewport(GLint x, GLint y, GLsizei width, GLsizei height)
{
   if (!gl_state.viewport.used || gl_state.viewport.x != x || gl_state.viewport.y != y
         || gl_state.viewport.w != width || gl_state.viewport.h != height)
   {
      glViewport(x, y, width, height);
      gl_state.viewport.used = true;
      gl_state.viewport.x    = x;
      gl_state.viewport.y    = y;
      gl_state.viewport.w    = width;
      gl_state.viewport.h    = height;
   }
}

void rglBlendFunc(GLenum sfactor, GLenum dfactor)
{
   rglBlendFuncSeparate(sfactor, dfactor, sfactor, dfactor);
}

/*
 * Category: Blending
 *
 * Core in:
 * OpenGL    : 1.4
 */
void rglBlendFuncSeparate(GLenum srcRGB, GLenum dstRGB, GLenum srcAlpha, GLenum dstAlpha)
{
   /* one state behind both calls: glBlendFunc sets the alpha factors too */
   if (gl_state.blend_func[0] != srcRGB || gl_state.blend_func[1] != dstRGB
         || gl_state.blend_func[2] != srcAlpha || gl_state.blend_func[3] != dstAlpha)
   {
      glBlendFuncSeparate(srcRGB, dstRGB, srcAlpha, dstAlpha);
      gl_state.blend_func[0] = srcRGB;
      gl_state.blend_func[1] = dstRGB;
      gl_state.blend_func[2] = srcAlpha;
      gl_state.blend_func[3] = dstAlpha;
   }
}

/*
 * Category: Textures
 *
 * Core in:
 * OpenGL    : 1.3
 */
void rglActiveTexture(GLenum texture)
{
   if (active_texture != texture - GL_TEXTURE0) {
      bindFBO(GL_FRAMEBUFFER);
      glActiveTexture(texture);
      active_texture = texture - GL_TEXTURE0;
   }
}

/*
 *
 * Core in:
 * OpenGL    : 1.1
 */
void rglBindTexture(GLenum target, GLuint texture)
{
   if (active_texture >= 32)
   {
      /* past the units glsm keeps */
      bindFBO(GL_FRAMEBUFFER);
      glBindTexture(target, texture);
      return;
   }
   if (gl_state.bind_textures.ids[active_texture] != texture
         || gl_state.bind_textures.target[active_texture] != target)
   {
      bindFBO(GL_FRAMEBUFFER);
      glBindTexture(target, texture);
      gl_state.bind_textures.ids[active_texture]    = texture;
      gl_state.bind_textures.target[active_texture] = target;
   }
}

/*
 *
 * Core in:
 * OpenGL    : 1.0
 */
void rglDisable(GLenum cap)
{
   if (gl_state.cap_state[cap] != 0)
   {
      if (gl_state.cap_translate[cap])
         glDisable(gl_state.cap_translate[cap]);
      gl_state.cap_state[cap] = 0;
   }
}

/*
 *
 * Core in:
 * OpenGL    : 1.0
 */
void rglEnable(GLenum cap)
{
   if (gl_state.cap_state[cap] != 1)
   {
      if (gl_state.cap_translate[cap])
         glEnable(gl_state.cap_translate[cap]);
      gl_state.cap_state[cap] = 1;
   }
}

/*
 * Category: Shaders
 *
 * Core in:
 * OpenGL    : 2.0
 */
void rglUseProgram(GLuint program)
{

   if(gl_state.program != program)
   {
      gl_state.program = program;
      glUseProgram(program);
   }
}

/*
 *
 * Core in:
 * OpenGL    : 1.0
 */
void rglDepthMask(GLboolean flag)
{
   if (gl_state.depth_mask != flag)
   {
      glDepthMask(flag);
      gl_state.depth_mask = flag;
   }
}

/*
 *
 * Core in:
 * OpenGL    : 1.0
 */
void rglStencilMask(GLenum mask)
{
   if (gl_state.stencil_mask != mask)
   {
      glStencilMask(mask);
      gl_state.stencil_mask = mask;
   }
}

/*
 *
 * Core in:
 * OpenGL    : 1.5
 */
void rglBufferData(GLenum target, GLsizeiptr size,
      const GLvoid *data, GLenum usage)
{
   glBufferData(target, size, data, usage);
}

/*
 *
 * Core in:
 * OpenGL    : 1.5
 */
void rglBufferSubData(GLenum target, GLintptr offset,
      GLsizeiptr size, const GLvoid *data)
{
   glBufferSubData(target, offset, size, data);
}

/*
 *
 * Core in:
 * OpenGL    : 1.5
 */
void rglBindBuffer(GLenum target, GLuint buffer)
{
   if (target == GL_ARRAY_BUFFER) {
      if (gl_state.array_buffer != buffer) {
         gl_state.array_buffer = buffer;
         glBindBuffer(target, buffer);
      }
   }
   else if (target == GL_ELEMENT_ARRAY_BUFFER) {
      if (gl_state.index_buffer != buffer) {
         gl_state.index_buffer = buffer;
         glBindBuffer(target, buffer);
      }
   }
   else {
      glBindBuffer(target, buffer);
   }
}

/*
 * Category: Shaders
 *
 * Core in:
 * OpenGL    : 2.0
 */
void rglLinkProgram(GLuint program)
{
   glLinkProgram(program);
   /* linking sets every uniform back to 0 */
   glsm_uniforms_forget(program);
}

/*
 * Category: FBO
 *
 * Core in:
 * OpenGL    : 3.0
 * OpenGLES  : 2.0
 */
/* the framebuffer binding target names: 0 draw, 1 read */
static int glsm_fb_slot(GLenum target)
{
#ifndef HAVE_OPENGLES2
   if (target == GL_READ_FRAMEBUFFER)
      return 1;
#endif
   return 0;
}

void rglFramebufferTexture2D(GLenum target, GLenum attachment,
      GLenum textarget, GLuint texture, GLint level)
{
   struct gl_framebuffers *fb = glsm_fb(gl_state.framebuf[glsm_fb_slot(target)].desired_location);
   GLuint *cached = NULL;
   if (fb && level == 0)
   {
      if (attachment == GL_COLOR_ATTACHMENT0)
         cached = &fb->color_attachment;
      else if (attachment == GL_DEPTH_ATTACHMENT)
         cached = &fb->depth_attachment;
   }
   if (cached && *cached == texture)
      return;
   bindFBO(target);
   glFramebufferTexture2D(target, attachment, textarget, texture, level);
   if (cached)
      *cached = texture;
}

/*
 * Category: FBO
 *
 * Core in:
 * OpenGL    : 3.0
 * OpenGLES  : 3.2
 */
void rglFramebufferTexture(GLenum target, GLenum attachment,
  	GLuint texture, GLint level)
{
   bindFBO(target);
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES) && defined(HAVE_OPENGLES_3_2)
   glFramebufferTexture(target, attachment, texture, level);
#endif
}

/*
 *
 * Core in:
 * OpenGL    : 1.1
 */
void rglDrawArrays(GLenum mode, GLint first, GLsizei count)
{
   bindFBO(GL_FRAMEBUFFER);
   glDrawArrays(mode, first, count);
}

/*
 *
 * Core in:
 * OpenGL    : 1.1
 */
void rglDrawElements(GLenum mode, GLsizei count, GLenum type,
                           const GLvoid * indices)
{
   bindFBO(GL_FRAMEBUFFER);
   glDrawElements(mode, count, type, indices);
}

void rglCompressedTexImage2D(GLenum target, GLint level,
      GLenum internalformat, GLsizei width, GLsizei height,
      GLint border, GLsizei imageSize, const GLvoid *data)
{
   glCompressedTexImage2D(target, level, internalformat,
         width, height, border, imageSize, data);
}

void rglDeleteFramebuffers(GLsizei n, const GLuint *_framebuffers)
{
   int i, p;
   if (resetting_context)
      return;
   for (i = 0; i < n; ++i)
   {
      if (_framebuffers[i] == 0)
         continue;
      glsm_fb_forget(_framebuffers[i]);
      /* GL falls back to framebuffer 0 for a deleted bound one; glsm's 0 is
       * the default framebuffer */
      for (p = 0; p < 2; ++p)
      {
         if (_framebuffers[i] == gl_state.framebuf[p].location)
            gl_state.framebuf[p].location = 0;
         if (_framebuffers[i] == gl_state.framebuf[p].desired_location)
            gl_state.framebuf[p].desired_location = default_framebuffer;
      }
   }
   glDeleteFramebuffers(n, _framebuffers);
}

void rglDeleteTextures(GLsizei n, const GLuint *textures)
{
   int i;
   size_t f;
   unsigned u;
   if (resetting_context)
      return;
   for (i = 0; i < n; ++i)
   {
      if (textures[i] == 0)
         continue;
      /* GL unbinds a deleted texture from every unit; a name GL gives
       * again must not look bound */
      for (u = 0; u < 32; u++)
         if (gl_state.bind_textures.ids[u] == textures[i])
            gl_state.bind_textures.ids[u] = 0;
      for (f = 0; f < fb_count; f++)
      {
         if (fb_records[f].color_attachment == textures[i])
            fb_records[f].color_attachment = 0;
         if (fb_records[f].depth_attachment == textures[i])
            fb_records[f].depth_attachment = 0;
      }
   }
   glDeleteTextures(n, textures);
}

/*
 *
 * Core in:
 * OpenGLES    : 2.0
 */
void rglRenderbufferStorage(GLenum target, GLenum internalFormat,
      GLsizei width, GLsizei height)
{
   glRenderbufferStorage(target, internalFormat, width, height);
}

/*
 *
 * Core in:
 *
 * OpenGL      : 3.0
 * OpenGLES    : 2.0
 */
void rglBindRenderbuffer(GLenum target, GLuint renderbuffer)
{
   glBindRenderbuffer(target, renderbuffer);
}

/*
 *
 * Core in:
 *
 * OpenGLES    : 2.0
 */
void rglDeleteRenderbuffers(GLsizei n, GLuint *renderbuffers)
{
   if (resetting_context)
      return;
   glDeleteRenderbuffers(n, renderbuffers);
}

/*
 *
 * Core in:
 *
 * OpenGL      : 3.0
 * OpenGLES    : 2.0
 */
void rglGenRenderbuffers(GLsizei n, GLuint *renderbuffers)
{
   glGenRenderbuffers(n, renderbuffers);
}

/*
 *
 * Core in:
 *
 * OpenGL      : 3.0
 * OpenGLES    : 2.0
 */
void rglGenerateMipmap(GLenum target)
{
   glGenerateMipmap(target);
}

/*
 * Category: FBO
 *
 * Core in:
 * OpenGL    : 3.0
 */
GLenum rglCheckFramebufferStatus(GLenum target)
{
   bindFBO(target);
   return glCheckFramebufferStatus(target);
}

/*
 * Category: FBO
 *
 * Core in:
 * OpenGL    : 3.0
 * OpenGLES  : 2.0
 */
void rglFramebufferRenderbuffer(GLenum target, GLenum attachment,
      GLenum renderbuffertarget, GLuint renderbuffer)
{
   struct gl_framebuffers *fb = glsm_fb(gl_state.framebuf[glsm_fb_slot(target)].desired_location);
   /* the cache does not tell a renderbuffer from a texture of the same
    * name: what a renderbuffer attach replaces is not known */
   if (fb)
   {
      if (attachment == GL_COLOR_ATTACHMENT0)
         fb->color_attachment = 0;
      else if (attachment == GL_DEPTH_ATTACHMENT)
         fb->depth_attachment = 0;
   }
   bindFBO(target);
   glFramebufferRenderbuffer(target, attachment, renderbuffertarget, renderbuffer);
}

/*
 * Category: Shaders
 *
 * Core in:
 * OpenGL    : 3.0
 */
void rglBindFragDataLocation(GLuint program, GLuint colorNumber,
                                   const char * name)
{
#if !defined(HAVE_OPENGLES2) && !defined(HAVE_OPENGLES3)
   glBindFragDataLocation(program, colorNumber, name);
#endif
}

/*
 * Category: Shaders
 *
 * Core in:
 * OpenGL    : 2.0
 */
void rglGetProgramiv(GLuint shader, GLenum pname, GLint *params)
{
   glGetProgramiv(shader, pname, params);
}

/*
 * Category: Shaders
 *
 * Core in:
 * OpenGL    : 4.1
 * OpenGLES  : 3.0
 */
void rglProgramParameteri( 	GLuint program,
  	GLenum pname,
  	GLint value)
{
#if !defined(HAVE_OPENGLES) || defined(HAVE_OPENGLES) && (defined(HAVE_OPENGLES3) || defined(HAVE_OPENGLES_3_1))
   glProgramParameteri(program, pname, value);
#endif
}

/*
 *
 * Core in:
 * OpenGL    : 2.0
 */
void rglGetActiveUniform(GLuint program, GLuint index, GLsizei bufsize,
      GLsizei *length, GLint *size, GLenum *type, GLchar *name)
{
   glGetActiveUniform(program, index, bufsize, length, size, type, name);
}

void rglGenQueries(	GLsizei n,
 	GLuint * ids)
{
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES) && defined(HAVE_OPENGLES3)
   glGenQueries(n, ids);
#endif
}

void rglGetQueryObjectuiv(	GLuint id,
 	GLenum pname,
 	GLuint * params)
{
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES) && defined(HAVE_OPENGLES3)
   glGetQueryObjectuiv(id, pname, params);
#endif
}

void rglDeleteQueries(	GLsizei n,
 	const GLuint * ids)
{
   if (resetting_context)
      return;
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES) && defined(HAVE_OPENGLES3)
   glDeleteQueries(n, ids);
#endif
}

void rglBeginQuery(	GLenum target,
 	GLuint id)
{
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES) && defined(HAVE_OPENGLES3)
   glBeginQuery(target, id);
#endif
}

void rglEndQuery(	GLenum target)
{
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES) && defined(HAVE_OPENGLES3)
   glEndQuery(target);
#endif
}

/*
 * Category: UBO
 *
 * Core in:
 *
 * OpenGL    : 2.0
 * OpenGLES  : 3.0
 */
void rglGetActiveUniformBlockiv(GLuint program,
  	GLuint uniformBlockIndex,
  	GLenum pname,
  	GLint *params)
{
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES) && defined(HAVE_OPENGLES3)
   glGetActiveUniformBlockiv(program, uniformBlockIndex,
         pname, params);
#endif
}

/*
 *
 * Core in:
 *
 * OpenGLES  : 3.0
 */
void rglGetActiveUniformsiv( 	GLuint program,
  	GLsizei uniformCount,
  	const GLuint *uniformIndices,
  	GLenum pname,
  	GLint *params)
{
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES) && defined(HAVE_OPENGLES3)
   glGetActiveUniformsiv(program, uniformCount,
         uniformIndices, pname, params);
#endif
}

/*
 *
 * Core in:
 *
 * OpenGLES  : 3.0
 */
void rglGetUniformIndices(GLuint program, GLsizei uniformCount, const GLchar *const*uniformNames, GLuint *uniformIndices)
{
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES) && defined(HAVE_OPENGLES3)
   glGetUniformIndices(program, uniformCount,
         uniformNames, uniformIndices);
#endif
}

/*
 * Category: UBO
 *
 * Core in:
 *
 * OpenGLES  : 3.0
 */
void rglBindBufferBase( 	GLenum target,
  	GLuint index,
  	GLuint buffer)
{
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES) && defined(HAVE_OPENGLES3)
   glBindBufferBase(target, index, buffer);
#endif
}

/*
 *
 * Category: UBO
 *
 * Core in:
 *
 * OpenGLES  : 3.0
 */
GLuint rglGetUniformBlockIndex( 	GLuint program,
  	const GLchar *uniformBlockName)
{
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES) && defined(HAVE_OPENGLES3)
   return glGetUniformBlockIndex(program, uniformBlockName);
#else
   return 0;
#endif
}

/*
 * Category: UBO
 *
 * Core in:
 *
 * OpenGLES  : 3.0
 */
void rglUniformBlockBinding( 	GLuint program,
  	GLuint uniformBlockIndex,
  	GLuint uniformBlockBinding)
{
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES) && defined(HAVE_OPENGLES3)
   glUniformBlockBinding(program, uniformBlockIndex,
         uniformBlockBinding);
#endif
}

/*
 *
 * Core in:
 * OpenGL    : 2.0
 * OpenGLES  : 3.0
 */
void rglUniform1ui(GLint location, GLuint v)
{
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES) && defined(HAVE_OPENGLES3)
   glUniform1ui(location ,v);
#endif
}

/*
 *
 * Core in:
 * OpenGL    : 2.0
 * OpenGLES  : 3.0
 */
void rglUniform2ui(GLint location, GLuint v0, GLuint v1)
{
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES) && defined(HAVE_OPENGLES3)
   glUniform2ui(location, v0, v1);
#endif
}

/*
 *
 * Core in:
 * OpenGL    : 2.0
 * OpenGLES  : 3.0
 */
void rglUniform3ui(GLint location, GLuint v0, GLuint v1, GLuint v2)
{
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES) && defined(HAVE_OPENGLES3)
   glUniform3ui(location, v0, v1, v2);
#endif
}

/*
 *
 * Core in:
 * OpenGL    : 2.0
 * OpenGLES  : 3.0
 */
void rglUniform4ui(GLint location, GLuint v0, GLuint v1, GLuint v2, GLuint v3)
{
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES) && defined(HAVE_OPENGLES3)
   glUniform4ui(location, v0, v1, v2, v3);
#endif
}

/*
 *
 * Core in:
 * OpenGL    : 2.0
 */
void rglUniformMatrix4fv(GLint location, GLsizei count, GLboolean transpose,
      const GLfloat *value)
{
   glUniformMatrix4fv(location, count, transpose, value);
}

/*
 * Category: Shaders
 *
 * Core in:
 * OpenGL    : 2.0
 */
void rglDetachShader(GLuint program, GLuint shader)
{
   glDetachShader(program, shader);
}

/*
 * Category: Shaders
 *
 * Core in:
 * OpenGL    : 2.0
 */
void rglGetShaderiv(GLuint shader, GLenum pname, GLint *params)
{
   glGetShaderiv(shader, pname, params);
}

/*
 * Category: Shaders
 *
 * Core in:
 * OpenGL    : 2.0
 */
void rglAttachShader(GLuint program, GLuint shader)
{
   glAttachShader(program, shader);
}

/*
 *
 * Core in:
 * OpenGL    : 2.0
 */
GLint rglGetAttribLocation(GLuint program, const GLchar *name)
{
   return glGetAttribLocation(program, name);
}

/*
 * Category: Shaders
 *
 * Core in:
 * OpenGL    : 2.0
 */
void rglShaderSource(GLuint shader, GLsizei count, const GLchar *const*string, const GLint *length)
{
   glShaderSource(shader, count, string, length);
}

/*
 * Category: Shaders
 *
 * Core in:
 * OpenGL    : 2.0
 */
void rglCompileShader(GLuint shader)
{
   glCompileShader(shader);
}

/*
 * Category: Shaders
 *
 * Core in:
 * OpenGL    : 2.0
 */
GLuint rglCreateProgram(void)
{
   GLuint program = glCreateProgram();
   glsm_uniforms_forget(program);
   return program;
}

/*
 *
 * Core in:
 * OpenGL    : 1.1
 */
void rglGenTextures(GLsizei n, GLuint *textures)
{
   glGenTextures(n, textures);
}

/*
 *
 * Core in:
 * OpenGL    : 2.0
 */
void rglGetShaderInfoLog(GLuint shader, GLsizei maxLength,
      GLsizei *length, GLchar *infoLog)
{
   glGetShaderInfoLog(shader, maxLength, length, infoLog);
}

/*
 *
 * Core in:
 * OpenGL    : 2.0
 */
void rglGetProgramInfoLog(GLuint shader, GLsizei maxLength,
      GLsizei *length, GLchar *infoLog)
{
   glGetProgramInfoLog(shader, maxLength, length, infoLog);
}

/*
 *
 * Core in:
 * OpenGL    : 2.0
 */
GLboolean rglIsProgram(GLuint program)
{
   return glIsProgram(program);
}

void rglTexCoord2f(GLfloat s, GLfloat t)
{
#ifdef HAVE_LEGACY_GL
   glTexCoord2f(s, t);
#endif
}

/*
 * Category: Generic vertex attributes
 *
 * Core in:
 * OpenGL    : 2.0
 *
 */
void rglDisableVertexAttribArray(GLuint index)
{
   if (index < MAX_ATTRIB)
      gl_state.vertex_attrib_pointer.enabled[index] = 0;
   glDisableVertexAttribArray(index);
}

/*
 * Category: Generic vertex attributes
 *
 * Core in:
 * OpenGL    : 2.0
 */
void rglEnableVertexAttribArray(GLuint index)
{
   if (index < MAX_ATTRIB)
      gl_state.vertex_attrib_pointer.enabled[index] = 1;
   glEnableVertexAttribArray(index);
}

/*
 * Category: Shaders
 *
 * Core in:
 * OpenGL    : 2.0
 */
void rglVertexAttribIPointer(
      GLuint index,
      GLint size,
      GLenum type,
      GLsizei stride,
      const GLvoid * pointer)
{
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES) && defined(HAVE_OPENGLES3)
   glVertexAttribIPointer(index, size, type, stride, pointer);
#endif
}

void rglVertexAttribLPointer(
      GLuint index,
      GLint size,
      GLenum type,
      GLsizei stride,
      const GLvoid * pointer)
{
#if defined(HAVE_OPENGL)
   glVertexAttribLPointer(index, size, type, stride, pointer);
#endif
}

/*
 * Category: Generic vertex attributes
 *
 * Core in:
 * OpenGL    : 2.0
 */
void rglVertexAttribPointer(GLuint name, GLint size,
      GLenum type, GLboolean normalized, GLsizei stride,
      const GLvoid* pointer)
{
   if (name < MAX_ATTRIB)
   {
      gl_state.attrib_pointer.used[name]       = 1;
      gl_state.attrib_pointer.size[name]       = size;
      gl_state.attrib_pointer.type[name]       = type;
      gl_state.attrib_pointer.normalized[name] = normalized;
      gl_state.attrib_pointer.stride[name]     = stride;
      gl_state.attrib_pointer.pointer[name]    = pointer;
      gl_state.attrib_pointer.buffer[name]     = gl_state.array_buffer;
   }
   glVertexAttribPointer(name, size, type, normalized, stride, pointer);
}

/*
 * Category: Generic vertex attributes
 *
 * Core in:
 * OpenGL    : 2.0
 */
void rglBindAttribLocation(GLuint program, GLuint index, const GLchar *name)
{
   glBindAttribLocation(program, index, name);
}

/*
 *
 * Core in:
 * OpenGL    : 2.0
 */
void rglVertexAttrib4f(GLuint name, GLfloat x, GLfloat y,
      GLfloat z, GLfloat w)
{
   glVertexAttrib4f(name, x, y, z, w);
}

/*
 *
 * Core in:
 * OpenGL    : 2.0
 */
void rglVertexAttrib4fv(GLuint name, const GLfloat* v)
{
   glVertexAttrib4fv(name, v);
}

/*
 * Category: Shaders
 *
 * Core in:
 * OpenGL    : 2.0
 */
GLuint rglCreateShader(GLenum shaderType)
{
   return glCreateShader(shaderType);
}

/*
 * Category: Shaders
 *
 * Core in:
 * OpenGL    : 2.0
 */
void rglDeleteProgram(GLuint program)
{
   if (resetting_context)
      return;
   glDeleteProgram(program);
   /* GL keeps a deleted program in use, uniforms and all, until another
    * is used */
   if (program != gl_state.program)
      glsm_uniforms_forget(program);
}

/*
 * Category: Shaders
 *
 * Core in:
 * OpenGL    : 2.0
 */
void rglDeleteShader(GLuint shader)
{
   if (!resetting_context)
      glDeleteShader(shader);
}

/*
 * Category: Shaders
 *
 * Core in:
 * OpenGL    : 2.0
 */
GLint rglGetUniformLocation(GLuint program, const GLchar *name)
{
   return glGetUniformLocation(program, name);
}

/*
 * Category: VBO and PBO
 *
 * Core in:
 * OpenGL    : 1.5
 */
void rglDeleteBuffers(GLsizei n, const GLuint *buffers)
{
   int i;
   unsigned a;
   if (resetting_context)
      return;
   for (i = 0; i < n; ++i)
   {
      if (buffers[i] == 0)
         continue;
      /* GL unbinds a deleted buffer; a name GL gives again must not look
       * bound */
      if (gl_state.array_buffer == buffers[i])
         gl_state.array_buffer = 0;
      if (gl_state.index_buffer == buffers[i])
         gl_state.index_buffer = 0;
      for (a = 0; a < MAX_ATTRIB; a++)
         if (gl_state.attrib_pointer.buffer[a] == buffers[i])
            gl_state.attrib_pointer.used[a] = 0;
   }
   glDeleteBuffers(n, buffers);
}

/*
 * Category: VBO and PBO
 *
 * Core in:
 * OpenGL    : 1.5
 */
void rglGenBuffers(GLsizei n, GLuint *buffers)
{
   glGenBuffers(n, buffers);
}

/*
 * Category: Shaders
 *
 * Core in:
 * OpenGL    : 2.0
 */
void rglUniform1f(GLint location, GLfloat v0)
{
   struct gl_program_uniforms *u = glsm_uniform_slot(location);
   if (!u || (u->uniform1f != v0)) {
      glUniform1f(location, v0);
      if (u) u->uniform1f = v0;
   }
}

/*
 * Category: Shaders
 *
 * Core in:
 * OpenGL    : 2.0
 */
void rglUniform1fv(GLint location,  GLsizei count,  const GLfloat *value)
{
   struct gl_program_uniforms *u = glsm_uniform_slot(location);
   if (!u || (u->uniform1f != value[0])) {
      glUniform1fv(location, count, value);
      if (u) u->uniform1f = value[0];
   }
}

/*
 * Category: Shaders
 *
 * Core in:
 * OpenGL    : 2.0
 */
void rglUniform1iv(GLint location,  GLsizei count,  const GLint *value)
{
   struct gl_program_uniforms *u = glsm_uniform_slot(location);
   if (!u || (u->uniform1i != value[0])) {
      glUniform1iv(location, count, value);
      if (u) u->uniform1i = value[0];
   }
}

void rglClearBufferfv( 	GLenum buffer,
  	GLint drawBuffer,
  	const GLfloat * value)
{
   bindFBO(GL_FRAMEBUFFER);
#if defined(HAVE_OPENGL) || (defined(HAVE_OPENGLES) && defined(HAVE_OPENGLES3))
   glClearBufferfv(buffer, drawBuffer, value);
#endif
}

void rglTexBuffer(GLenum target, GLenum internalFormat, GLuint buffer)
{
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES) && defined(HAVE_OPENGLES_3_2)
   glTexBuffer(target, internalFormat, buffer);
#endif
}

/*
 *
 * Core in:
 * OpenGL    : 2.0
 * OpenGLES  : 3.0
 */
const GLubyte* rglGetStringi(GLenum name, GLuint index)
{
#if defined(HAVE_OPENGL) || (defined(HAVE_OPENGLES) && defined(HAVE_OPENGLES3))
   return glGetStringi(name, index);
#else
   return NULL;
#endif
}

void rglClearBufferfi( 	GLenum buffer,
  	GLint drawBuffer,
  	GLfloat depth,
  	GLint stencil)
{
#if defined(HAVE_OPENGL) || (defined(HAVE_OPENGLES) && defined(HAVE_OPENGLES3))
   glClearBufferfi(buffer, drawBuffer, depth, stencil);
#endif
}

/*
 *
 * Core in:
 * OpenGL    : 3.0
 * OpenGLES  : 3.0
 */
void rglRenderbufferStorageMultisample( 	GLenum target,
  	GLsizei samples,
  	GLenum internalformat,
  	GLsizei width,
  	GLsizei height)
{
#if defined(HAVE_OPENGL) || (defined(HAVE_OPENGLES) && defined(HAVE_OPENGLES3))
   glRenderbufferStorageMultisample(target, samples, internalformat, width, height);
#endif
}

/*
 * Category: Shaders
 *
 * Core in:
 * OpenGL    : 2.0
 */
void rglUniform1i(GLint location, GLint v0)
{
   struct gl_program_uniforms *u = glsm_uniform_slot(location);
   if (!u || (u->uniform1i != v0)) {
      glUniform1i(location, v0);
      if (u) u->uniform1i = v0;
   }
}

/*
 * Category: Shaders
 *
 * Core in:
 * OpenGL    : 2.0
 */
void rglUniform2f(GLint location, GLfloat v0, GLfloat v1)
{
   struct gl_program_uniforms *u = glsm_uniform_slot(location);
   if (!u || (u->uniform2f[0] != v0 || u->uniform2f[1] != v1)) {
      glUniform2f(location, v0, v1);
      if (u) {
         u->uniform2f[0] = v0;
         u->uniform2f[1] = v1;
      }
   }
}

/*
 * Category: Shaders
 *
 * Core in:
 * OpenGL    : 2.0
 */
void rglUniform2i(GLint location, GLint v0, GLint v1)
{
   struct gl_program_uniforms *u = glsm_uniform_slot(location);
   if (!u || (u->uniform2i[0] != v0 || u->uniform2i[1] != v1)) {
      glUniform2i(location, v0, v1);
      if (u) {
         u->uniform2i[0] = v0;
         u->uniform2i[1] = v1;
      }
   }
}

/*
 * Category: Shaders
 *
 * Core in:
 * OpenGL    : 2.0
 */
void rglUniform2fv(GLint location, GLsizei count, const GLfloat *value)
{
   struct gl_program_uniforms *u = glsm_uniform_slot(location);
   if (!u || (u->uniform2f[0] != value[0] || u->uniform2f[1] != value[1])) {
      glUniform2fv(location, count, value);
      if (u) {
         u->uniform2f[0] = value[0];
         u->uniform2f[1] = value[1];
      }
   }
}

/*
 * Category: Shaders
 *
 * Core in:
 * OpenGL    : 2.0
 */
void rglUniform3f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2)
{
   struct gl_program_uniforms *u = glsm_uniform_slot(location);
   if (!u || (u->uniform3f[0] != v0 || u->uniform3f[1] != v1 || u->uniform3f[2] != v2)) {
      glUniform3f(location, v0, v1, v2);
      if (u) {
         u->uniform3f[0] = v0;
         u->uniform3f[1] = v1;
         u->uniform3f[2] = v2;
      }
   }
}

/*
 * Category: Shaders
 *
 * Core in:
 * OpenGL    : 2.0
 */
void rglUniform3fv(GLint location, GLsizei count, const GLfloat *value)
{
   struct gl_program_uniforms *u = glsm_uniform_slot(location);
   if (!u || (u->uniform3f[0] != value[0] || u->uniform3f[1] != value[1] || u->uniform3f[2] != value[2])) {
      glUniform3fv(location, count, value);
      if (u) {
         u->uniform3f[0] = value[0];
         u->uniform3f[1] = value[1];
         u->uniform3f[2] = value[2];
      }
   }
}

/*
 * Category: Shaders
 *
 * Core in:
 * OpenGL    : 2.0
 */
void rglUniform4i(GLint location, GLint v0, GLint v1, GLint v2, GLint v3)
{
   struct gl_program_uniforms *u = glsm_uniform_slot(location);
   if (!u || (u->uniform4i[0] != v0 || u->uniform4i[1] != v1 || u->uniform4i[2] != v2 || u->uniform4i[3] != v3)) {
      glUniform4i(location, v0, v1, v2, v3);
      if (u) {
         u->uniform4i[0] = v0;
         u->uniform4i[1] = v1;
         u->uniform4i[2] = v2;
         u->uniform4i[3] = v3;
      }
   }
}

/*
 * Category: Shaders
 *
 * Core in:
 * OpenGL    : 2.0
 */
void rglUniform4f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3)
{
   struct gl_program_uniforms *u = glsm_uniform_slot(location);
   if (!u || (u->uniform4f[0] != v0 || u->uniform4f[1] != v1 || u->uniform4f[2] != v2 || u->uniform4f[3] != v3)) {
      glUniform4f(location, v0, v1, v2, v3);
      if (u) {
         u->uniform4f[0] = v0;
         u->uniform4f[1] = v1;
         u->uniform4f[2] = v2;
         u->uniform4f[3] = v3;
      }
   }
}

/*
 * Category: Shaders
 *
 * Core in:
 * OpenGL    : 2.0
 */
void rglUniform4fv(GLint location, GLsizei count, const GLfloat *value)
{
   struct gl_program_uniforms *u = glsm_uniform_slot(location);
   if (!u || (u->uniform4f[0] != value[0] || u->uniform4f[1] != value[1] || u->uniform4f[2] != value[2] || u->uniform4f[3] != value[3])) {
      glUniform4fv(location, count, value);
      if (u) {
         u->uniform4f[0] = value[0];
         u->uniform4f[1] = value[1];
         u->uniform4f[2] = value[2];
         u->uniform4f[3] = value[3];
      }
   }
}

/*
 *
 * Core in:
 * OpenGL    : 1.0
 */
void rglPolygonOffset(GLfloat factor, GLfloat units)
{
   if (gl_state.polygon_offset[0] != factor || gl_state.polygon_offset[1] != units)
   {
      glPolygonOffset(factor, units);
      gl_state.polygon_offset[0] = factor;
      gl_state.polygon_offset[1] = units;
   }
}

/*
 * Category: FBO
 *
 * Core in:
 * OpenGL    : 3.0
 */
void rglGenFramebuffers(GLsizei n, GLuint *ids)
{
   int i;
   glGenFramebuffers(n, ids);
   /* a name GL gives again starts with nothing attached */
   for (i = 0; i < n; ++i)
      glsm_fb_forget(ids[i]);
}

/*
 * Category: FBO
 *
 * Core in:
 * OpenGL    : 3.0
 */
void rglBindFramebuffer(GLenum target, GLuint framebuffer)
{
   if (framebuffer == 0)
      framebuffer = default_framebuffer;

   if (target == GL_FRAMEBUFFER) {
         gl_state.framebuf[0].desired_location = framebuffer;
         gl_state.framebuf[1].desired_location = framebuffer;
   }
#ifndef HAVE_OPENGLES2
   else if (target == GL_DRAW_FRAMEBUFFER) {
         gl_state.framebuf[0].desired_location = framebuffer;
   }
   else if (target == GL_READ_FRAMEBUFFER) {
         gl_state.framebuf[1].desired_location = framebuffer;
   }
#endif
}

/*
 * Category: FBO
 *
 * Core in:
 * OpenGL    : 2.0
 * OpenGLES  : 3.0
 */
void rglDrawBuffers(GLsizei n, const GLenum *bufs)
{
#ifndef HAVE_OPENGLES2
   struct gl_framebuffers *fb = glsm_fb(gl_state.framebuf[0].desired_location);
   if (fb && n > 0 && n <= 4)
   {
      if (fb->draw_buffers_n == n
            && !memcmp(fb->draw_buffers, bufs, (size_t)n * sizeof(*bufs)))
         return;
      fb->draw_buffers_n = n;
      memcpy(fb->draw_buffers, bufs, (size_t)n * sizeof(*bufs));
   }
   else if (fb)
      fb->draw_buffers_n = 0;
   bindFBO(GL_DRAW_FRAMEBUFFER);
   glDrawBuffers(n, bufs);
#endif
}

/*
 * Category: FBO
 *
 * Core in:
 * OpenGL    : 2.0
 * OpenGLES  : 3.0
 */
void *rglMapBufferRange( 	GLenum target,
  	GLintptr offset,
  	GLsizeiptr length,
  	GLbitfield access)
{
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES) && defined(HAVE_OPENGLES3)
   return glMapBufferRange(target, offset, length, access);
#else
   return NULL;
#endif
}

/*
 *
 * Core in:
 * OpenGL    : 4.3
 * OpenGLES  : 3.1
 */
void rglTexStorage2DMultisample(GLenum target, GLsizei samples,
      GLenum internalformat, GLsizei width, GLsizei height,
      GLboolean fixedsamplelocations)
{
#ifndef HAVE_OPENGLES
   glTexStorage2DMultisample(target, samples, internalformat,
         width, height, fixedsamplelocations);
#else
   m_glTexStorage2DMultisample(target, samples, internalformat,
         width, height, fixedsamplelocations);
#endif
}

/*
 *
 * Core in:
 * OpenGLES  : 3.0
 */
void rglTexStorage2D(GLenum target, GLsizei levels, GLenum internalFormat,
      GLsizei width, GLsizei height)
{
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES) && defined(HAVE_OPENGLES3)
   glTexStorage2D(target, levels, internalFormat, width, height);
#endif
}
/*
 *
 * Core in:
 * OpenGL    : 3.2
 * OpenGLES  : 3.2
 */
void rglDrawRangeElementsBaseVertex(GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type, const void *indices, GLint basevertex)
{
#ifdef HAVE_OPENGLES
   bindFBO(GL_FRAMEBUFFER);
   m_glDrawRangeElementsBaseVertex(mode, start, end, count, type, indices, basevertex);
#else
   bindFBO(GL_DRAW_FRAMEBUFFER);
   glDrawRangeElementsBaseVertex(mode, start, end, count, type, indices, basevertex);
#endif
}

/*
 *
 * Core in:
 * OpenGL    : 4.2
 * OpenGLES  : 3.1
 */
void rglMemoryBarrier( 	GLbitfield barriers)
{
#ifndef HAVE_OPENGLES
   glMemoryBarrier(barriers);
#else
   m_glMemoryBarrier(barriers);
#endif
}

/*
 *
 * Core in:
 * OpenGL    : 4.2
 * OpenGLES  : 3.1
 */
void rglBindImageTexture( 	GLuint unit,
  	GLuint texture,
  	GLint level,
  	GLboolean layered,
  	GLint layer,
  	GLenum access,
  	GLenum format)
{
#ifndef HAVE_OPENGLES
   glBindImageTexture(unit, texture, level, layered, layer, access, format);
#else
   m_glBindImageTexture(unit, texture, level, layered, layer, access, format);
#endif
}

/*
 *
 * Core in:
 * OpenGL    : 4.1
 * OpenGLES  : 3.1
 */
void rglGetProgramBinary( 	GLuint program,
  	GLsizei bufsize,
  	GLsizei *length,
  	GLenum *binaryFormat,
  	void *binary)
{
#if !defined(HAVE_OPENGLES) || defined(HAVE_OPENGLES) && defined(HAVE_OPENGLES3)
   glGetProgramBinary(program, bufsize, length, binaryFormat, binary);
#endif
}

/*
 *
 * Core in:
 * OpenGL    : 4.1
 * OpenGLES  : 3.0
 */
void rglProgramBinary(GLuint program,
  	GLenum binaryFormat,
  	const void *binary,
  	GLsizei length)
{
#if !defined(HAVE_OPENGLES) || defined(HAVE_OPENGLES) && (defined(HAVE_OPENGLES3) || defined(HAVE_OPENGLES_3_1))
   glProgramBinary(program, binaryFormat, binary, length);
#endif
}

void rglTexImage2DMultisample( 	GLenum target,
  	GLsizei samples,
  	GLenum internalformat,
  	GLsizei width,
  	GLsizei height,
  	GLboolean fixedsamplelocations)
{
#ifndef HAVE_OPENGLES
   glTexImage2DMultisample(target, samples, internalformat, width, height, fixedsamplelocations);
#endif
}

void rglTexImage3D(	GLenum target,
 	GLint level,
 	GLint internalFormat,
 	GLsizei width,
 	GLsizei height,
 	GLsizei depth,
 	GLint border,
 	GLenum format,
 	GLenum type,
 	const GLvoid * data)
{
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES) && defined(HAVE_OPENGLES3)
   glTexImage3D(target, level, internalFormat, width, height, depth, border, format, type, data);
#endif
}

/*
 *
 * Core in:
 * OpenGL    : 1.5
 */
void * rglMapBuffer(	GLenum target, GLenum access)
{
#if defined(HAVE_OPENGLES)
   return glMapBufferOES(target, access);
#else
   return glMapBuffer(target, access);
#endif
}

/*
 *
 * Core in:
 * OpenGL    : 1.5
 */
GLboolean rglUnmapBuffer( 	GLenum target)
{
   if (resetting_context)
      return GL_TRUE;
#if defined(HAVE_OPENGLES)
   return glUnmapBufferOES(target);
#else
   return glUnmapBuffer(target);
#endif
}

void rglBlendEquation(GLenum mode)
{
   rglBlendEquationSeparate(mode, mode);
}

void rglBlendColor(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha)
{
   if (gl_state.blend_color[0] != red || gl_state.blend_color[1] != green
         || gl_state.blend_color[2] != blue || gl_state.blend_color[3] != alpha)
   {
      glBlendColor(red, green, blue, alpha);
      gl_state.blend_color[0] = red;
      gl_state.blend_color[1] = green;
      gl_state.blend_color[2] = blue;
      gl_state.blend_color[3] = alpha;
   }
}

/*
 * Category: Blending
 *
 * Core in:
 * OpenGL    : 2.0
 */
void rglBlendEquationSeparate(GLenum modeRGB, GLenum modeAlpha)
{
   if (gl_state.blend_equation[0] != modeRGB || gl_state.blend_equation[1] != modeAlpha)
   {
      glBlendEquationSeparate(modeRGB, modeAlpha);
      gl_state.blend_equation[0] = modeRGB;
      gl_state.blend_equation[1] = modeAlpha;
   }
}

/*
 *
 * Core in:
 * OpenGL    : 2.0
 * OpenGLES  : 3.2
 */
void rglCopyImageSubData( 	GLuint srcName,
  	GLenum srcTarget,
  	GLint srcLevel,
  	GLint srcX,
  	GLint srcY,
  	GLint srcZ,
  	GLuint dstName,
  	GLenum dstTarget,
  	GLint dstLevel,
  	GLint dstX,
  	GLint dstY,
  	GLint dstZ,
  	GLsizei srcWidth,
  	GLsizei srcHeight,
  	GLsizei srcDepth)
{
#ifndef HAVE_OPENGLES
   glCopyImageSubData(srcName,
         srcTarget,
         srcLevel,
         srcX,
         srcY,
         srcZ,
         dstName,
         dstTarget,
         dstLevel,
         dstX,
         dstY,
         dstZ,
         srcWidth,
         srcHeight,
         srcDepth);
#else
   m_glCopyImageSubData(srcName,
         srcTarget,
         srcLevel,
         srcX,
         srcY,
         srcZ,
         dstName,
         dstTarget,
         dstLevel,
         dstX,
         dstY,
         dstZ,
         srcWidth,
         srcHeight,
         srcDepth);
#endif
}

/*
 * Category: FBO
 *
 * Core in:
 * OpenGL    : 3.0
 * OpenGLES  : 3.0
 */
void rglBlitFramebuffer(
      GLint srcX0, GLint srcY0,
      GLint srcX1, GLint srcY1,
      GLint dstX0, GLint dstY0,
      GLint dstX1, GLint dstY1,
      GLbitfield mask, GLenum filter)
{
#ifndef HAVE_OPENGLES2
   bindFBO(GL_DRAW_FRAMEBUFFER);
   bindFBO(GL_READ_FRAMEBUFFER);
   glBlitFramebuffer(srcX0, srcY0, srcX1, srcY1,
         dstX0, dstY0, dstX1, dstY1, mask, filter);
#endif
}

/*
 * Category: VAO
 *
 * Core in:
 * OpenGL    : 3.0
 * OpenGLES  : 3.0
 */
void rglBindVertexArray(GLuint array)
{
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES) && defined(HAVE_OPENGLES3)
   if (array == 0)
      array = gl_state.vao; /* glsm's own, where it needs one */
   if (gl_state.bindvertex.array == array)
      return;
   gl_state.bindvertex.array = array;
   glBindVertexArray(array);
   /* the element buffer binding is the vertex array's */
   gl_state.index_buffer = GLSM_UNKNOWN_BUFFER;
#endif
}

/*
 * Category: VAO
 *
 * Core in:
 * OpenGL    : 3.0
 * OpenGLES  : 3.0
 */
void rglGenVertexArrays(GLsizei n, GLuint *arrays)
{
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES) && defined(HAVE_OPENGLES3)
   glGenVertexArrays(n, arrays);
#endif
}

/*
 * Category: VAO
 *
 * Core in:
 * OpenGL    : 3.0
 * OpenGLES  : 3.0
 */
void rglDeleteVertexArrays(GLsizei n, const GLuint *arrays)
{
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES) && defined(HAVE_OPENGLES3)
   int i;
   if (resetting_context)
      return;
   for (i = 0; i < n; ++i)
   {
      if (arrays[i] != 0 && arrays[i] == gl_state.bindvertex.array)
      {
         /* GL falls back to 0: glsm's own, where it needs one */
         gl_state.bindvertex.array = 0;
         gl_state.index_buffer     = GLSM_UNKNOWN_BUFFER;
      }
   }
   glDeleteVertexArrays(n, arrays);
   if (gl_state.bindvertex.array == 0 && gl_state.vao)
   {
      glBindVertexArray(gl_state.vao);
      gl_state.bindvertex.array = gl_state.vao;
   }
#endif
}

/*
 *
 * Core in:
 * OpenGL    : 3.2
 * OpenGLES  : 3.0
 */
void *rglFenceSync(GLenum condition, GLbitfield flags)
{
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES) && defined(HAVE_OPENGLES3)
   return (GLsync)glFenceSync(condition, flags);
#else
   return NULL;
#endif
}

/*
 *
 * Core in:
 * OpenGL    : 3.2
 * OpenGLES  : 3.0
 */
void rglDeleteSync(GLsync sync)
{
   if (resetting_context)
      return;
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES) && defined(HAVE_OPENGLES3)
  glDeleteSync((GLsync)sync);
#endif
}

/*
 *
 * Core in:
 * OpenGL    : 3.2
 * OpenGLES  : 3.0
 */
void rglWaitSync(void *sync, GLbitfield flags, uint64_t timeout)
{
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES) && defined(HAVE_OPENGLES3)
   glWaitSync((GLsync)sync, flags, (GLuint64)timeout);
#endif
}

/*
 *
 * Core in:
 * OpenGL    : 4.4
 * OpenGLES  : Not available
 */
void rglBufferStorage(GLenum target, GLsizeiptr size, const GLvoid *data, GLbitfield flags)
{
#ifndef HAVE_OPENGLES
   glBufferStorage(target, size, data, flags);
#else
   m_glBufferStorage(target, size, data, flags);
#endif
}

/*
 *
 * Core in:
 * OpenGL    : 2.0
 * OpenGLES  : 2.0
 */

void rglUniform2iv(	GLint location,
 	GLsizei count,
 	const GLint *value)
{
   glUniform2iv(location, count, value);
}

/*
 *
 * Core in:
 * OpenGL    : 3.0
 * OpenGLES  : ?.?
 */

void rglUniform2uiv(	GLint location,
 	GLsizei count,
 	const GLuint *value)
{
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES) && defined(HAVE_OPENGLES3)
   glUniform2uiv(location, count, value);
#endif
}

/*
 *
 * Core in:
 * OpenGL    : 4.3
 * OpenGLES  : ?.?
 */
void rglTextureView(	GLuint texture,
 	GLenum target,
 	GLuint origtexture,
 	GLenum internalformat,
 	GLuint minlevel,
 	GLuint numlevels,
 	GLuint minlayer,
 	GLuint numlayers)
{
#if defined(HAVE_OPENGL)
   glTextureView(texture, target, origtexture, internalformat, minlevel, numlevels, minlayer, numlayers);
#endif
}

/*
 *
 * Core in:
 * OpenGL    : 3.0
 * OpenGLES  : 3.0
 */
void rglFlushMappedBufferRange(GLenum target, GLintptr offset, GLsizeiptr length)
{
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES) && defined(HAVE_OPENGLES3)
  glFlushMappedBufferRange(target, offset, length);
#endif
}

#ifndef GL_WAIT_FAILED
#define GL_WAIT_FAILED                                   0x911D
#endif

/*
 *
 * Core in:
 * OpenGL    : 3.2
 * OpenGLES  : 3.0
 */
GLenum rglClientWaitSync(GLsync sync, GLbitfield flags, GLuint64 timeout)
{
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES) && defined(HAVE_OPENGLES3)
  return glClientWaitSync((GLsync)sync, flags, (GLuint64)timeout);
#else
  return GL_WAIT_FAILED;
#endif
}

/*
 *
 * Core in:
 * OpenGL    : 3.2
 * OpenGLES  : Not available
 */
void rglDrawElementsBaseVertex(GLenum mode, GLsizei count, GLenum type,
			       GLvoid *indices, GLint basevertex)
{
#if defined(HAVE_OPENGL)
   bindFBO(GL_DRAW_FRAMEBUFFER);
   glDrawElementsBaseVertex(mode, count, type, indices, basevertex);
#endif
}

void rglTexParameteri( GLenum target, GLenum pname, GLint param )
{
   glTexParameteri(target, pname, param);
}

void rglTexImage2D( GLenum target, GLint level,
                    GLint internalFormat,
                    GLsizei width, GLsizei height,
                    GLint border, GLenum format, GLenum type,
                    const GLvoid *pixels )
{
   glTexImage2D(target, level, internalFormat, width, height, border, format, type, pixels);
}

void rglGetIntegerv (GLenum pname, GLint *data)
{
   glGetIntegerv(pname, data);
}

void rglFlush(void)
{
   glFlush();
}

const GLubyte* rglGetString (GLenum name)
{
   return glGetString(name);
}

void rglGetTexParameteriv (GLenum target, GLenum pname, GLint *params)
{
   glGetTexParameteriv(target, pname, params);
}

void rglGetFloatv (GLenum pname, GLfloat *data)
{
   glGetFloatv(pname, data);
}

/* GLSM-side */


static void glsm_state_setup(void)
{
#ifdef HAVE_OPENGLES
   m_glDrawRangeElementsBaseVertex = (PFNGLDRAWRANGEELEMENTSBASEVERTEXPROC)glsm_get_proc_address("glDrawRangeElementsBaseVertex");
   m_glBufferStorage = (PFNGLBUFFERSTORAGEEXTPROC)glsm_get_proc_address("glBufferStorageEXT");
   m_glMemoryBarrier = (PFNGLMEMORYBARRIERPROC)glsm_get_proc_address("glMemoryBarrier");
   m_glBindImageTexture = (PFNGLBINDIMAGETEXTUREPROC)glsm_get_proc_address("glBindImageTexture");
   m_glTexStorage2DMultisample = (PFNGLTEXSTORAGE2DMULTISAMPLEPROC)glsm_get_proc_address("glTexStorage2DMultisample");
   m_glCopyImageSubData = (PFNGLCOPYIMAGESUBDATAPROC)glsm_get_proc_address("glCopyImageSubData");
   if (m_glCopyImageSubData == NULL)
      m_glCopyImageSubData = (PFNGLCOPYIMAGESUBDATAPROC)glsm_get_proc_address("glCopyImageSubDataEXT");
#endif

   unsigned i;
#ifdef HAVE_OPENGLES
   const bool core_or_es = true;
#else
   const bool core_or_es = hw_render.context_type == RETRO_HW_CONTEXT_OPENGL_CORE;
#endif

   /* the programs named were the old context's */
   for (i = 0; i < GLSM_UNIFORM_PROGRAMS; i++)
      glsm_uniforms_forget(i);

   /* GL's defaults: what a new context holds */
   memset(&gl_state, 0, sizeof(struct gl_cached_state));
   gl_state.cap_translate[SGL_DEPTH_TEST]               = GL_DEPTH_TEST;
   gl_state.cap_translate[SGL_BLEND]                    = GL_BLEND;
   gl_state.cap_translate[SGL_POLYGON_OFFSET_FILL]      = GL_POLYGON_OFFSET_FILL;
   gl_state.cap_translate[SGL_CULL_FACE]                = GL_CULL_FACE;
   gl_state.cap_translate[SGL_SCISSOR_TEST]             = GL_SCISSOR_TEST;
   gl_state.cap_translate[SGL_STENCIL_TEST]             = GL_STENCIL_TEST;
   gl_state.cap_translate[SGL_DITHER]                   = GL_DITHER;
   gl_state.cap_translate[SGL_SAMPLE_ALPHA_TO_COVERAGE] = GL_SAMPLE_ALPHA_TO_COVERAGE;
   gl_state.cap_translate[SGL_SAMPLE_COVERAGE]          = GL_SAMPLE_COVERAGE;
   /* fog and alpha test are no capabilities of core or ES contexts: the
    * cache keeps what the core asks for, and GL is not told */
   if (!core_or_es)
   {
      gl_state.cap_translate[SGL_FOG]                   = GL_FOG;
      gl_state.cap_translate[SGL_ALPHA_TEST]            = GL_ALPHA_TEST;
   }
#ifndef HAVE_OPENGLES
   gl_state.cap_translate[SGL_COLOR_LOGIC_OP]           = GL_COLOR_LOGIC_OP;
   gl_state.cap_translate[SGL_CLIP_DISTANCE0]           = GL_CLIP_DISTANCE0;
   gl_state.cap_translate[SGL_DEPTH_CLAMP]              = GL_DEPTH_CLAMP;
#endif
   gl_state.cap_state[SGL_DITHER]                       = 1;

   glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &glsm_max_textures);
   if (glsm_max_textures > 32)
      glsm_max_textures = 32;
   for (i = 0; i < 32; ++i)
      gl_state.bind_textures.target[i] = GL_TEXTURE_2D;
   active_texture                       = 0;

   gl_state.pixelstore.pack             = 4;
   gl_state.pixelstore.unpack           = 4;
   gl_state.clear_depth                 = 1.0;
   gl_state.blend_func[0]               = GL_ONE;
   gl_state.blend_func[1]               = GL_ZERO;
   gl_state.blend_func[2]               = GL_ONE;
   gl_state.blend_func[3]               = GL_ZERO;
   gl_state.blend_equation[0]           = GL_FUNC_ADD;
   gl_state.blend_equation[1]           = GL_FUNC_ADD;
   for (i = 0; i < 4; ++i)
      gl_state.color_mask[i]            = GL_TRUE;
   gl_state.depth_mask                  = GL_TRUE;
   gl_state.depth_func                  = GL_LESS;
   gl_state.depth_range[1]              = 1.0;
   gl_state.line_width                  = 1.0f;
#ifndef HAVE_OPENGLES
   gl_state.polygon_mode                = GL_FILL;
#endif
   gl_state.cull_face                   = GL_BACK;
   gl_state.front_face                  = GL_CCW;
   gl_state.stencil_func.func           = GL_ALWAYS;
   gl_state.stencil_func.mask           = ~0u;
   gl_state.stencil_op[0]               = GL_KEEP;
   gl_state.stencil_op[1]               = GL_KEEP;
   gl_state.stencil_op[2]               = GL_KEEP;
   gl_state.stencil_mask                = ~0u;

   /* a new context: the draw target's names are gone with the old one */
   fb_count    = 0; /* the framebuffers named were the old context's */
   draw_fbo    = 0;
   draw_color  = 0;
   draw_depth  = 0;
   front_fbo   = 0;
   front_color = 0;
   front_valid = false;
   draw_w      = 0;
   draw_h      = 0;
   draw_failed = false;

   default_framebuffer                  = glsm_get_current_framebuffer();
   frontend_framebuffer                 = default_framebuffer;

   gl_state.framebuf[0].location        = default_framebuffer;
   gl_state.framebuf[1].location        = default_framebuffer;
   gl_state.framebuf[0].desired_location        = default_framebuffer;
   gl_state.framebuf[1].desired_location        = default_framebuffer;

   glBindFramebuffer(GL_FRAMEBUFFER, default_framebuffer);


   /* A vertex array object must be bound for any draw to be valid under a
    * core-profile GL context.  Whether we got one is a runtime property of the
    * context the frontend actually created -- not a compile-time choice, and
    * not something to do unconditionally.
    *
    * RetroArch's "glcore" driver creates a core-profile context: plugins that
    * draw from client-side vertex arrays with no VAO of their own (rice, gln64)
    * render nothing there -- a black screen with audio and input still working
    * -- so a VAO must be bound.  RetroArch's "gl" (gl2) driver creates a
    * compatibility context, where client arrays are valid and a VAO is neither
    * required nor wanted; binding one there only perturbs the GL state the gl2
    * driver tracks (corrupting its menu/OSD).  So bind a VAO only on a real
    * core-profile context. */
#ifndef HAVE_OPENGLES
   {
      /* Use the context type RetroArch actually requested rather than the
       * queried profile mask. The profile mask is unreliable -- NVIDIA reports
       * the core-profile bit even on a compatibility context -- so querying it
       * made glsm bind a VAO on RA's "gl" (gl2) driver, which uses no VAO and
       * assumes default vertex-array state; the core's VAO capture then diverged
       * from what RA composited and the screen went black. context_type is set
       * from params.core when the context is requested, so it reflects exactly
       * whether RA asked for a core (glcore) or compatibility (gl) context. */
      if (hw_render.context_type == RETRO_HW_CONTEXT_OPENGL_CORE
            && glGenVertexArrays && glBindVertexArray)
      {
         if (gl_state.vao == 0)
            glGenVertexArrays(1, &gl_state.vao);
         glBindVertexArray(gl_state.vao);
      }
   }
#endif
}

/* The draw target: a framebuffer of the core's own that the renderers
 * which draw straight on from one frame into the next (Glide64, Rice,
 * glN64) draw into instead of the frontend's. glsm_draw_target_present()
 * copies each frame they finish into the front buffer at the swap, and
 * glsm_draw_target_show() puts it in the frontend's framebuffer at the
 * end of every slice, so the frontend presents that frame, not the next
 * one drawn over it, and is handed a frame every slice. */

/* GLES 3 has the blit; desktop GL from 3.0 or ARB_framebuffer_object */
#if defined(HAVE_OPENGLES3)
#define GLSM_HAVE_BLIT
#define GLSM_BLIT_AVAILABLE 1
#elif !defined(HAVE_OPENGLES)
#define GLSM_HAVE_BLIT
#define GLSM_BLIT_AVAILABLE (glBlitFramebuffer != NULL)
#endif

static void glsm_draw_target_free(void)
{
   if (draw_fbo)
      glsm_fb_forget(draw_fbo);
   if (draw_fbo)
      glDeleteFramebuffers(1, &draw_fbo);
   if (front_fbo)
      glDeleteFramebuffers(1, &front_fbo);
   if (draw_color)
      glDeleteTextures(1, &draw_color);
   if (front_color)
      glDeleteTextures(1, &front_color);
   if (draw_depth)
      glDeleteRenderbuffers(1, &draw_depth);
   draw_fbo    = 0;
   draw_color  = 0;
   draw_depth  = 0;
   front_fbo   = 0;
   front_color = 0;
   front_valid = false;
   draw_w      = 0;
   draw_h      = 0;
}

/* (Re)allocates the target at the size asked for. Bindings it changes
 * are put back as the frontend's: glsm_state_bind binds the core's after. */
static GLuint glsm_colour_texture(unsigned w, unsigned h)
{
   GLuint tex = 0;
   glGenTextures(1, &tex);
   glBindTexture(GL_TEXTURE_2D, tex);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, (GLsizei)w, (GLsizei)h,
         0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
   return tex;
}

static void glsm_draw_target_alloc(void)
{
   GLint prev_tex = 0, prev_rb = 0;
   GLenum depth_format = GL_DEPTH_COMPONENT16;
   bool complete;

   glsm_draw_target_free();
#ifdef GLSM_HAVE_BLIT
   if (!GLSM_BLIT_AVAILABLE)
   {
      /* nothing to put the frames in the frontend's with */
      draw_failed = true;
      return;
   }
#endif
   glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev_tex);
   glGetIntegerv(GL_RENDERBUFFER_BINDING, &prev_rb);

   draw_color  = glsm_colour_texture(draw_want_w, draw_want_h);
   front_color = glsm_colour_texture(draw_want_w, draw_want_h);

#if defined(HAVE_OPENGLES) && !defined(HAVE_OPENGLES3)
   if (gl_query_extension("GL_OES_depth24"))
      depth_format = 0x81A6; /* GL_DEPTH_COMPONENT24_OES */
#else
   depth_format = GL_DEPTH_COMPONENT24;
#endif
   glGenRenderbuffers(1, &draw_depth);
   glBindRenderbuffer(GL_RENDERBUFFER, draw_depth);
   glRenderbufferStorage(GL_RENDERBUFFER, depth_format,
         (GLsizei)draw_want_w, (GLsizei)draw_want_h);

   glGenFramebuffers(1, &front_fbo);
   glBindFramebuffer(GL_FRAMEBUFFER, front_fbo);
   glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, front_color, 0);
   complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;

   glGenFramebuffers(1, &draw_fbo);
   glBindFramebuffer(GL_FRAMEBUFFER, draw_fbo);
   glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, draw_color, 0);
   glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, draw_depth);
   complete = complete && glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;

   if (!complete)
   {
      glsm_draw_target_free();
      draw_failed = true;
   }
   else
   {
      struct gl_framebuffers *fb = glsm_fb(draw_fbo);
      draw_w = draw_want_w;
      draw_h = draw_want_h;
      if (fb)
      {
         fb->color_attachment = draw_color;
         fb->depth_attachment = 0; /* a renderbuffer */
      }
      /* both start black, as the frontend's is cleared once; the masks
       * and scissor are the frontend's here, and glsm_state_bind puts
       * the core's in place after */
      glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
      glDepthMask(GL_TRUE);
      glDisable(GL_SCISSOR_TEST);
      glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
      glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
      glBindFramebuffer(GL_FRAMEBUFFER, front_fbo);
      glClear(GL_COLOR_BUFFER_BIT);
   }

   glBindTexture(GL_TEXTURE_2D, (GLuint)prev_tex);
   glBindRenderbuffer(GL_RENDERBUFFER, (GLuint)prev_rb);
   glBindFramebuffer(GL_FRAMEBUFFER, frontend_framebuffer);
}

/* Draw into a target of the core's own, width x height, from the next
 * glsm_state_bind on (0, 0: into the frontend's framebuffer). */
void glsm_draw_target(unsigned width, unsigned height)
{
   draw_want_w = width;
   draw_want_h = height;
}

/* A frame is finished: put it in the frontend's framebuffer. Called with
 * the core's state bound, as the renderers swap; the frame is copied in
 * full, whatever the renderer's scissor, and the core's bindings are
 * left as they were. */
void glsm_draw_target_present(void)
{
#ifdef GLSM_HAVE_BLIT
   int scissor_on;
#else
   GLint prev_tex = 0;
#endif
   if (!draw_fbo)
      return;
#ifdef GLSM_HAVE_BLIT
   scissor_on = gl_state.cap_state[SGL_SCISSOR_TEST];
   if (scissor_on)
      glDisable(gl_state.cap_translate[SGL_SCISSOR_TEST]);
   glBindFramebuffer(GL_READ_FRAMEBUFFER, draw_fbo);
   glBindFramebuffer(GL_DRAW_FRAMEBUFFER, front_fbo);
   glBlitFramebuffer(0, 0, (GLint)draw_w, (GLint)draw_h,
         0, 0, (GLint)draw_w, (GLint)draw_h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
   glBindFramebuffer(GL_READ_FRAMEBUFFER, gl_state.framebuf[1].location);
   glBindFramebuffer(GL_DRAW_FRAMEBUFFER, gl_state.framebuf[0].location);
   if (scissor_on)
      glEnable(gl_state.cap_translate[SGL_SCISSOR_TEST]);
#else
   /* GLES 2 has no blit: a copy into the front texture */
   glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev_tex);
   glBindFramebuffer(GL_FRAMEBUFFER, draw_fbo);
   glBindTexture(GL_TEXTURE_2D, front_color);
   glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, (GLsizei)draw_w, (GLsizei)draw_h);
   glBindTexture(GL_TEXTURE_2D, (GLuint)prev_tex);
   glBindFramebuffer(GL_FRAMEBUFFER, gl_state.framebuf[0].location);
#endif
   front_valid = true;
}

/* After GLSM_CTL_STATE_UNBIND, with GL's defaults in place: puts the
 * frame last finished in the framebuffer the frontend gave for this
 * slice, whether one was finished in it or not. Returns whether there
 * is a frame to present: the frontend gets one every slice, as from
 * GLideN64, and never has to present one again from a framebuffer it
 * has since handed out for another. */
bool glsm_draw_target_show(void)
{
#ifndef GLSM_HAVE_BLIT
   GLint type = 0, tex = 0;
#endif
   if (!draw_fbo || !front_valid)
      return false;
#ifdef GLSM_HAVE_BLIT
   glBindFramebuffer(GL_READ_FRAMEBUFFER, front_fbo);
   glBindFramebuffer(GL_DRAW_FRAMEBUFFER, frontend_framebuffer);
   glBlitFramebuffer(0, 0, (GLint)draw_w, (GLint)draw_h,
         0, 0, (GLint)draw_w, (GLint)draw_h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
#else
   /* GLES 2: a copy into the texture the frontend's framebuffer draws to */
   glBindFramebuffer(GL_FRAMEBUFFER, frontend_framebuffer);
   glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
         GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &type);
   if (type == GL_TEXTURE)
      glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
            GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &tex);
   if (!tex)
   {
      /* nothing to copy into: draw into it from the next bind */
      draw_failed = true;
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      return false;
   }
   glBindFramebuffer(GL_FRAMEBUFFER, front_fbo);
   glBindTexture(GL_TEXTURE_2D, (GLuint)tex);
   glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, (GLsizei)draw_w, (GLsizei)draw_h);
   glBindTexture(GL_TEXTURE_2D, 0);
#endif
   glBindFramebuffer(GL_FRAMEBUFFER, 0);
   return true;
}

/* GL's defaults for the render state glsm keeps (not the bindings) */
static void glsm_default_render_state(struct gl_cached_state *st)
{
   unsigned i;
   for (i = 0; i < SGL_CAP_MAX; i++)
      st->cap_state[i]     = 0;
   st->cap_state[SGL_DITHER] = 1;
   st->clear_color[0]      = 0.0f;
   st->clear_color[1]      = 0.0f;
   st->clear_color[2]      = 0.0f;
   st->clear_color[3]      = 0.0f;
   st->clear_depth         = 1.0;
   st->blend_func[0]       = GL_ONE;
   st->blend_func[1]       = GL_ZERO;
   st->blend_func[2]       = GL_ONE;
   st->blend_func[3]       = GL_ZERO;
   st->blend_equation[0]   = GL_FUNC_ADD;
   st->blend_equation[1]   = GL_FUNC_ADD;
   for (i = 0; i < 4; i++)
   {
      st->blend_color[i]   = 0.0f;
      st->color_mask[i]    = GL_TRUE;
   }
   st->depth_mask          = GL_TRUE;
   st->depth_func          = GL_LESS;
   st->depth_range[0]      = 0.0;
   st->depth_range[1]      = 1.0;
   st->polygon_offset[0]   = 0.0f;
   st->polygon_offset[1]   = 0.0f;
   st->line_width          = 1.0f;
#ifndef HAVE_OPENGLES
   st->polygon_mode        = GL_FILL;
#endif
   st->cull_face           = GL_BACK;
   st->front_face          = GL_CCW;
   st->stencil_func.func   = GL_ALWAYS;
   st->stencil_func.ref    = 0;
   st->stencil_func.mask   = ~0u;
   st->stencil_op[0]       = GL_KEEP;
   st->stencil_op[1]       = GL_KEEP;
   st->stencil_op[2]       = GL_KEEP;
   st->stencil_mask        = ~0u;
}

/* Puts all of st's render state in GL, whatever GL held: the frontend
 * changes any of it between the core's slices, and a setter that finds
 * its value cached calls nothing. The scissor and viewport boxes only
 * once set. */
static void glsm_apply_render_state(const struct gl_cached_state *st)
{
   unsigned i;
   for (i = 0; i < SGL_CAP_MAX; i++)
   {
      if (!gl_state.cap_translate[i])
         continue;
      if (st->cap_state[i])
         glEnable(gl_state.cap_translate[i]);
      else
         glDisable(gl_state.cap_translate[i]);
   }
   glBlendFuncSeparate(st->blend_func[0], st->blend_func[1],
         st->blend_func[2], st->blend_func[3]);
   glBlendEquationSeparate(st->blend_equation[0], st->blend_equation[1]);
   glBlendColor(st->blend_color[0], st->blend_color[1],
         st->blend_color[2], st->blend_color[3]);
   glColorMask(st->color_mask[0], st->color_mask[1],
         st->color_mask[2], st->color_mask[3]);
   glDepthMask(st->depth_mask);
   glDepthFunc(st->depth_func);
#ifdef HAVE_OPENGLES
   glDepthRangef(st->depth_range[0], st->depth_range[1]);
   glClearDepthf(st->clear_depth);
#else
   glDepthRange(st->depth_range[0], st->depth_range[1]);
   glClearDepth(st->clear_depth);
   glPolygonMode(GL_FRONT_AND_BACK, st->polygon_mode);
#endif
   glPolygonOffset(st->polygon_offset[0], st->polygon_offset[1]);
   glLineWidth(st->line_width);
   glCullFace(st->cull_face);
   glFrontFace(st->front_face);
   glStencilFunc(st->stencil_func.func, st->stencil_func.ref, st->stencil_func.mask);
   glStencilOp(st->stencil_op[0], st->stencil_op[1], st->stencil_op[2]);
   glStencilMask(st->stencil_mask);
   glClearColor(st->clear_color[0], st->clear_color[1],
         st->clear_color[2], st->clear_color[3]);
   if (st->scissor.used)
      glScissor(st->scissor.x, st->scissor.y, st->scissor.w, st->scissor.h);
   if (st->viewport.used)
      glViewport(st->viewport.x, st->viewport.y, st->viewport.w, st->viewport.h);
}

static void glsm_state_bind(void)
{
   unsigned i;
   const GLuint previous_default_framebuffer = default_framebuffer;
   /* The frontend can hand back a different target framebuffer between frames
    * (RetroArch's "gl"/gl2 driver recreates or rotates its render target on
    * resize, shader-pass changes, and similar). default_framebuffer is captured
    * once at context reset, so re-query the live framebuffer here and render
    * into the one the frontend is actually compositing this frame -- otherwise
    * the core draws into a stale FBO and the screen stays black on the gl
    * driver. No VAO is involved; this is plain framebuffer-binding state. */
   frontend_framebuffer = glsm_get_current_framebuffer();
   if (draw_want_w && draw_want_h && !draw_failed)
   {
      if (draw_w != draw_want_w || draw_h != draw_want_h)
         glsm_draw_target_alloc();
   }
   else if (draw_fbo)
      glsm_draw_target_free();
   default_framebuffer = draw_fbo ? draw_fbo : frontend_framebuffer;
#ifndef HAVE_OPENGLES2
   if (gl_state.bindvertex.array != 0) {
      glBindVertexArray(gl_state.bindvertex.array);
   } else
#endif
   {
#ifndef HAVE_OPENGLES2
      /* Select glsm's VAO (zero for GLES/compatibility contexts) rather
       * than the frontend's. Each pointer captures ARRAY_BUFFER below. */
      if (glBindVertexArray)
         glBindVertexArray(gl_state.vao);
#endif
      for (i = 0; i < MAX_ATTRIB; i++)
      {
         if (gl_state.vertex_attrib_pointer.enabled[i])
            glEnableVertexAttribArray(i);
         else
            glDisableVertexAttribArray(i);

         if (gl_state.attrib_pointer.used[i]) {
            glBindBuffer(GL_ARRAY_BUFFER, gl_state.attrib_pointer.buffer[i]);
            glVertexAttribPointer(
               i,
               gl_state.attrib_pointer.size[i],
               gl_state.attrib_pointer.type[i],
               gl_state.attrib_pointer.normalized[i],
               gl_state.attrib_pointer.stride[i],
               gl_state.attrib_pointer.pointer[i]);
         }
      }
    }
   /* Restore actual bindings without changing the cached desired bindings. */
   glBindBuffer(GL_ARRAY_BUFFER, gl_state.array_buffer);
   if (gl_state.index_buffer != GLSM_UNKNOWN_BUFFER)
      glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gl_state.index_buffer);

   glPixelStorei(GL_UNPACK_ALIGNMENT, gl_state.pixelstore.unpack);
   glPixelStorei(GL_PACK_ALIGNMENT, gl_state.pixelstore.pack);

   /* Preserve offscreen targets selected by GLideN64's framebuffer cache.
    * Only bindings that named the frontend target follow a rotated FBO. */
   for (i = 0; i < 2; ++i)
      if (gl_state.framebuf[i].desired_location == previous_default_framebuffer)
         gl_state.framebuf[i].desired_location = default_framebuffer;

   glBindFramebuffer(GL_FRAMEBUFFER, default_framebuffer);
   gl_state.framebuf[0].location = default_framebuffer;
   gl_state.framebuf[1].location = default_framebuffer;

   /* The first time the hardware-render framebuffer is bound, clear it once in
    * full. RetroArch's gl driver allocates the FBO at the next power of two of
    * the content size (e.g. 1024x1024 for 640x480); the N64 renderers only ever
    * touch their content region, so the rest would otherwise show whatever the
    * frontend left there (corruption around the image). The untouched region is
    * never written again, so a single clear makes it permanently black --
    * clearing every frame would wipe content for renderers that rely on
    * framebuffer persistence. Scissor is taken from glsm's tracked cap. */
   if (!gl_state.fbo_cleared_once)
   {
      int scissor_on = gl_state.cap_state[SGL_SCISSOR_TEST];
      if (scissor_on)
         glDisable(gl_state.cap_translate[SGL_SCISSOR_TEST]);
      if (draw_fbo)
         glBindFramebuffer(GL_FRAMEBUFFER, frontend_framebuffer);
      glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
      glClear(GL_COLOR_BUFFER_BIT);
      if (draw_fbo)
         glBindFramebuffer(GL_FRAMEBUFFER, default_framebuffer);
      if (scissor_on)
         glEnable(gl_state.cap_translate[SGL_SCISSOR_TEST]);
      gl_state.fbo_cleared_once = 1;
   }
   /* Restore the core's read buffer for the default framebuffer. rglReadBuffer
    * only tracks it for the default FB, and the read FB has just been bound to
    * the default above, so this targets the right framebuffer. Without this the
    * core's glReadPixels/glBlitFramebuffer read from whatever buffer the
    * frontend last selected. */
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES3)
   if (gl_state.readbuffer.used)
      glReadBuffer(gl_state.readbuffer.mode);
#endif

   glsm_apply_render_state(&gl_state);
   glUseProgram(gl_state.program);

   glActiveTexture(GL_TEXTURE0 + active_texture);
   /* Restore the texture binding on every unit the core uses, not just the
    * currently-active one. The N64 renderers bind textures on units 0 and 1
    * (multitexturing); restoring only the active unit left unit 1 pointing at
    * whatever the frontend last bound, so the core sampled the wrong texture. */
   {
      int u;
      for (u = 0; u < (int)glsm_max_textures && u < 32; u++)
      {
         glActiveTexture(GL_TEXTURE0 + u);
         glBindTexture(gl_state.bind_textures.target[u]
               ? gl_state.bind_textures.target[u] : GL_TEXTURE_2D,
               gl_state.bind_textures.ids[u]);
      }
      glActiveTexture(GL_TEXTURE0 + active_texture);
   }

   /* The draw/read bindings can differ after framebuffer presentation. */
   bindFBO(GL_FRAMEBUFFER);
#ifndef HAVE_OPENGLES2
   bindFBO(GL_READ_FRAMEBUFFER);
#endif
}

static void glsm_state_unbind(void)
{
   unsigned i;
   int t;
   struct gl_cached_state defaults;

   /* GL's defaults, as the frontend expects to find them */
   memset(&defaults, 0, sizeof(defaults));
   glsm_default_render_state(&defaults);
   glsm_apply_render_state(&defaults);
   glUseProgram(0);

   /* Unbind any textures the core left bound on each unit, then leave unit 0
    * active. RetroArch's gl2 driver does not defensively rebind every unit, so
    * a leftover core texture corrupts its own rendering on the compatibility
    * path (no VAO isolates this). */
   for (t = (int)glsm_max_textures - 1; t >= 0; t--)
   {
      if (t < 32 && gl_state.bind_textures.ids[t])
      {
         glActiveTexture(GL_TEXTURE0 + t);
         glBindTexture(gl_state.bind_textures.target[t], 0);
      }
   }
   glActiveTexture(GL_TEXTURE0);

   /* Detach the core VAO before resetting element-buffer state: the
    * element binding is part of the VAO and must survive presentation. */
#ifndef HAVE_OPENGLES2
   if (gl_state.bindvertex.array != 0 || gl_state.vao != 0)
      glBindVertexArray(0);
   else
#endif
   {
      for (i = 0; i < MAX_ATTRIB; i++)
      {
         if (gl_state.vertex_attrib_pointer.enabled[i])
            glDisableVertexAttribArray(i);
      }
   }

   glBindBuffer(GL_ARRAY_BUFFER, 0);
   /* A core-profile context has no element binding without a VAO. The
    * frontend selects its own VAO, so there is nothing to reset there. */
   if (hw_render.context_type != RETRO_HW_CONTEXT_OPENGL_CORE)
      glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

   /* Restore the default pixel-store alignment the frontend expects. */
   glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
   glPixelStorei(GL_PACK_ALIGNMENT, 4);

   glBindFramebuffer(RARCH_GL_FRAMEBUFFER, 0);
}

static bool glsm_state_ctx_destroy(void *data)
{
   unsigned i;
   /* the context's objects are gone: so is what glsm knew of them */
   for (i = 0; i < GLSM_UNIFORM_PROGRAMS; i++)
      glsm_uniforms_forget(i);
   free(fb_records);
   fb_records = NULL;
   fb_count   = 0;
   fb_cap     = 0;
   draw_fbo    = 0;
   draw_color  = 0;
   draw_depth  = 0;
   front_fbo   = 0;
   front_color = 0;
   front_valid = false;
   draw_w      = 0;
   draw_h      = 0;
   return true;
}

static bool glsm_state_ctx_init(glsm_ctx_params_t *params)
{
   if (!params || !params->environ_cb)
      return false;

#ifdef HAVE_OPENGLES
#if defined(HAVE_OPENGLES_3_1)
   hw_render.context_type       = RETRO_HW_CONTEXT_OPENGLES_VERSION;
   hw_render.version_major      = 3;
   hw_render.version_minor      = 1;
#elif defined(HAVE_OPENGLES3)
   hw_render.context_type       = RETRO_HW_CONTEXT_OPENGLES3;
#else
   hw_render.context_type       = RETRO_HW_CONTEXT_OPENGLES2;
#endif
#else
#if !defined(HAVE_LIBNX)
   if (params->core)
   {
      hw_render.context_type       = RETRO_HW_CONTEXT_OPENGL_CORE;
      hw_render.version_major      = params->major;
      hw_render.version_minor      = params->minor;
   }
   else
#endif /* HAVE_LIBNX */
   {
      hw_render.context_type       = RETRO_HW_CONTEXT_OPENGL;
      if (params->major != 0)
         hw_render.version_major   = params->major;
      if (params->minor != 0)
         hw_render.version_minor   = params->minor;
   }
#endif

   hw_render.context_reset      = params->context_reset;
   hw_render.context_destroy    = params->context_destroy;
   hw_render.stencil            = params->stencil;
   hw_render.depth              = true;
   hw_render.bottom_left_origin = true;
   hw_render.cache_context      = true;

   if (!params->environ_cb(RETRO_ENVIRONMENT_SET_HW_RENDER, &hw_render))
      return false;

   return true;
}

GLuint glsm_get_current_framebuffer(void)
{
   return hw_render.get_current_framebuffer();
}

/* Whether RetroArch requested a core-profile context (its "glcore" driver), as
 * opposed to a compatibility context (its "gl" driver). Based on the context
 * type glsm itself recorded when the context was requested, which is reliable;
 * GL_CONTEXT_PROFILE_MASK is not, since NVIDIA reports the core bit even on a
 * compatibility context. */
bool glsm_context_is_core(void)
{
   return hw_render.context_type == RETRO_HW_CONTEXT_OPENGL_CORE;
}

void* glsm_get_proc_address(const char* sym)
{
   if (!hw_render.get_proc_address)
      return NULL;
   
   return hw_render.get_proc_address(sym);
}

extern void initGLFunctions();
bool glsm_ctl(enum glsm_state_ctl state, void *data)
{
   switch (state)
   {
      case GLSM_CTL_IMM_VBO_DRAW:
         return false;
      case GLSM_CTL_IMM_VBO_DISABLE:
         return false;
      case GLSM_CTL_IS_IMM_VBO:
         return false;
      case GLSM_CTL_SET_IMM_VBO:
         break;
      case GLSM_CTL_UNSET_IMM_VBO:
         break;
      case GLSM_CTL_PROC_ADDRESS_GET:
         {
            glsm_ctx_proc_address_t *proc = (glsm_ctx_proc_address_t*)data;
            if (!hw_render.get_proc_address)
               return false;
            proc->addr = hw_render.get_proc_address;
         }
         break;
      case GLSM_CTL_STATE_CONTEXT_RESET:
         rglgen_resolve_symbols(hw_render.get_proc_address);
#ifdef HAVE_GLIDEN64
         initGLFunctions();
#endif

         /* a new context: what glsm knew of the old one is dropped */
         if (window_first > 0)
         {
            glsm_state_setup();
            glsm_state_unbind();
         }
         else
            window_first = 1;
         break;
      case GLSM_CTL_STATE_CONTEXT_DESTROY:
         glsm_state_ctx_destroy(data);
         break;
      case GLSM_CTL_STATE_CONTEXT_INIT:
         return glsm_state_ctx_init((glsm_ctx_params_t*)data);
      case GLSM_CTL_STATE_SETUP:
         glsm_state_setup();
         break;
      case GLSM_CTL_STATE_UNBIND:
         glsm_state_unbind();
         break;
      case GLSM_CTL_STATE_BIND:
         glsm_state_bind();
         break;
      case GLSM_CTL_NONE:
      default:
         break;
   }

   return true;
}
