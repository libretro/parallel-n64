/* Copyright (C) 2010-2017 The RetroArch team
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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <glsym/glsym.h>
#include <glsm/glsm.h>

#if 0
#define HAVE_GLIDEN64
#endif

#define MAX_FRAMEBUFFERS 128000
#define MAX_UNIFORMS 1024

#ifdef HAVE_OPENGLES
#include <EGL/egl.h>
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
#endif

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

#ifndef HAVE_OPENGLES
   GLenum colorlogicop;
#endif

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
      GLuint r;
      GLuint g;
      GLuint b;
      GLuint a;
   } clear_color;

   struct
   {
      bool used;
      GLint x;
      GLint y;
      GLsizei w;
      GLsizei h;
   } scissor;

   struct
   {
      GLint x;
      GLint y;
      GLsizei w;
      GLsizei h;
   } viewport;

   struct
   {
      bool used;
      GLenum sfactor;
      GLenum dfactor;
   } blendfunc;

   struct
   {
      bool used;
      GLenum srcRGB;
      GLenum dstRGB;
      GLenum srcAlpha;
      GLenum dstAlpha;
   } blendfunc_separate;

   struct
   {
      bool used;
      GLboolean red;
      GLboolean green;
      GLboolean blue;
      GLboolean alpha;
   } colormask;

   struct
   {
      bool used;
      GLdouble depth;
   } cleardepth;

   struct
   {
      bool used;
      GLenum func;
   } depthfunc;

   struct
   {
      bool used;
      GLclampd zNear;
      GLclampd zFar;
   } depthrange;

   struct
   {
      bool used;
      GLfloat factor;
      GLfloat units;
   } polygonoffset;

   struct
   {
      bool used;
      GLenum func;
      GLint ref;
      GLuint mask;
   } stencilfunc;

   struct
   {
      bool used;
      GLenum sfail;
      GLenum dpfail;
      GLenum dppass;
   } stencilop;

   struct
   {
      bool used;
      GLenum mode;
   } frontface;

   struct
   {
      bool used;
      GLenum mode;
   } cullface;

   struct
   {
      bool used;
      GLuint mask;
   } stencilmask;

   struct
   {
      bool used;
      GLboolean mask;
   } depthmask;

   struct
   {
     GLint pack;
     GLint unpack;
   } pixelstore;

   struct
   {
      GLenum mode;
   } readbuffer;

   struct
   {
      GLuint location;
      GLuint desired_location;
   } framebuf[2];

   GLuint array_buffer;
   GLuint index_buffer;
   GLuint vao;
   GLuint program;
   int cap_state[SGL_CAP_MAX];
   int cap_translate[SGL_CAP_MAX];
};

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

struct gl_framebuffers
{
   int draw_buf_set;
   GLuint color_attachment;
   GLuint depth_attachment;
   GLenum target;
};

static struct gl_program_uniforms program_uniforms[MAX_UNIFORMS][MAX_UNIFORMS];
static struct gl_framebuffers* framebuffers[MAX_FRAMEBUFFERS];
static GLenum active_texture;
static GLuint default_framebuffer;
static GLint glsm_max_textures;
static bool copy_image_support = 0;
static struct retro_hw_render_callback hw_render;
static struct gl_cached_state gl_state;
static int window_first = 0;
static int resetting_context = 0;

static void on_gl_error(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const char* message, void *userParam)
{
   printf("%s\n", message);
}

void bindFBO(GLenum target)
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
   glPolygonMode(face, mode);
#endif
}

void rglTexImage2D(
	GLenum target,
 	GLint level,
 	GLint internalformat,
 	GLsizei width,
 	GLsizei height,
 	GLint border,
 	GLenum format,
 	GLenum type,
 	const GLvoid * data)
{
   glTexImage2D(target, level, internalformat, width, height, border, format, type, data);
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

/*
 *
 * Core in:
 * OpenGL    : 1.0
 */
void rglLineWidth(GLfloat width)
{
   glLineWidth(width);
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
   GLuint src_attachment;
   GLuint dst_attachment;
   const bool good_pointer = gl_state.framebuf[0].desired_location < MAX_FRAMEBUFFERS && gl_state.framebuf[1].desired_location < MAX_FRAMEBUFFERS;
   const bool good_target = framebuffers[gl_state.framebuf[0].desired_location]->target == framebuffers[gl_state.framebuf[1].desired_location]->target;
   const bool sameSize = dstX1 - dstX0 == srcX1 - srcX0 && dstY1 - dstY0 == srcY1 - srcY0;
   if (sameSize && copy_image_support && good_pointer && good_target) {
      if (mask == GL_COLOR_BUFFER_BIT) {
         src_attachment = framebuffers[gl_state.framebuf[1].desired_location]->color_attachment;
         dst_attachment = framebuffers[gl_state.framebuf[0].desired_location]->color_attachment;
      } else if (mask == GL_DEPTH_BUFFER_BIT) {
         src_attachment = framebuffers[gl_state.framebuf[1].desired_location]->depth_attachment;
         dst_attachment = framebuffers[gl_state.framebuf[0].desired_location]->depth_attachment;
      }
      rglCopyImageSubData(src_attachment, framebuffers[gl_state.framebuf[1].desired_location]->target, 0, srcX0, srcY0, 0,
         dst_attachment, framebuffers[gl_state.framebuf[0].desired_location]->target, 0, dstX0, dstY0, 0, srcX1 - srcX0, srcY1 - srcY0, 1);
   } else {
      bindFBO(GL_DRAW_FRAMEBUFFER);
      bindFBO(GL_READ_FRAMEBUFFER);
      glBlitFramebuffer(srcX0, srcY0, srcX1, srcY1,
         dstX0, dstY0, dstX1, dstY1,
         mask, filter);
   }
#endif
}

/*
 *
 * Core in:
 * OpenGLES  : 3.0
 */
void rglReadBuffer(GLenum mode)
{
#ifndef HAVE_OPENGLES2
   glReadBuffer(mode);
   gl_state.readbuffer.mode = mode;
#endif
}

/*
 *
 * Core in:
 * OpenGLES  : 2.0
 */
void rglClearDepth(GLdouble depth)
{
   glsm_ctl(GLSM_CTL_IMM_VBO_DRAW, NULL);
#ifdef HAVE_OPENGLES
   glClearDepthf(depth);
#else
   glClearDepth(depth);
#endif
   gl_state.cleardepth.used  = true;
   gl_state.cleardepth.depth = depth;
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
 * OpenGLES  : 2.0
 */

/*
 *
 * Core in:
 * OpenGLES  : 2.0
 */
void rglPixelStorei(GLenum pname, GLint param)
{
   if (pname == GL_UNPACK_ALIGNMENT)
   {
      if (param != gl_state.pixelstore.unpack)
      {
         glPixelStorei(pname, param);
         gl_state.pixelstore.unpack = param;
      }
   }
   else if (pname == GL_PACK_ALIGNMENT)
   {
      if (param != gl_state.pixelstore.pack)
      {
         glPixelStorei(pname, param);
         gl_state.pixelstore.pack = param;
      }
   }
   else
      glPixelStorei(pname, param);
}

/*
 *
 * Core in:
 * OpenGLES  : 2.0
 */
GLboolean rglUnmapBuffer(GLenum target)
{
#ifndef HAVE_OPENGLES2
   return glUnmapBuffer(target);
#endif
}

/*
 *
 * Core in:
 * OpenGLES  : 2.0
 */
void rglDepthRange(GLclampd zNear, GLclampd zFar)
{
#ifdef HAVE_OPENGLES
   glDepthRangef(zNear, zFar);
#else
   glDepthRange(zNear, zFar);
#endif
   gl_state.depthrange.used  = true;
   gl_state.depthrange.zNear = zNear;
   gl_state.depthrange.zFar  = zFar;
}

/*
 *
 * Core in:
 * OpenGLES  : 2.0
 */
void rglFrontFace(GLenum mode)
{
   gl_state.frontface.used = true;
   if (gl_state.frontface.mode != mode) {
      glFrontFace(mode);
      gl_state.frontface.mode = mode;
   }
}

/*
 *
 * Core in:
 * OpenGLES  : 2.0
 */
void rglDepthFunc(GLenum func)
{
   gl_state.depthfunc.used = true;
   if (gl_state.depthfunc.func != func) {
      glDepthFunc(func);
      gl_state.depthfunc.func = func;
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
   gl_state.colormask.used  = true;
   if (gl_state.colormask.red != red || gl_state.colormask.green != green || gl_state.colormask.blue != blue || gl_state.colormask.alpha != alpha) {
       glColorMask(red, green, blue, alpha);
       gl_state.colormask.red   = red;
       gl_state.colormask.green = green;
       gl_state.colormask.blue  = blue;
       gl_state.colormask.alpha = alpha;
   }
}

/*
 *
 * Core in:
 * OpenGLES  : 2.0
 */
void rglCullFace(GLenum mode)
{
   gl_state.cullface.used = true;
   if (gl_state.cullface.mode != mode)
   {
      glCullFace(mode);
      gl_state.cullface.mode = mode;
   }
}

/*
 *
 * Core in:
 * OpenGLES  : 2.0
 */
void rglStencilOp(GLenum sfail, GLenum dpfail, GLenum dppass)
{
   gl_state.stencilop.used   = true;
   if (gl_state.stencilop.sfail != sfail || gl_state.stencilop.dpfail != dpfail || gl_state.stencilop.dppass != dppass) {
      glStencilOp(sfail, dpfail, dppass);
      gl_state.stencilop.sfail  = sfail;
      gl_state.stencilop.dpfail = dpfail;
      gl_state.stencilop.dppass = dppass;
   }
}

/*
 *
 * Core in:
 * OpenGLES  : 2.0
 */
void rglStencilFunc(GLenum func, GLint ref, GLuint mask)
{
   gl_state.stencilfunc.used = true;
   if (gl_state.stencilfunc.func != func || gl_state.stencilfunc.ref != ref || gl_state.stencilfunc.mask != mask)
   {
      glStencilFunc(func, ref, mask);
      gl_state.stencilfunc.func = func;
      gl_state.stencilfunc.ref  = ref;
      gl_state.stencilfunc.mask = mask;
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
   if (gl_state.clear_color.r != red || gl_state.clear_color.g != green || gl_state.clear_color.b != blue || gl_state.clear_color.a != alpha)
   {
      glClearColor(red, green, blue, alpha);
      gl_state.clear_color.r = red;
      gl_state.clear_color.g = green;
      gl_state.clear_color.b = blue;
      gl_state.clear_color.a = alpha;
   }
}

/*
 *
 * Core in:
 * OpenGLES    : 2.0 (maybe earlier?)
 */
void rglScissor(GLint x, GLint y, GLsizei width, GLsizei height)
{
   gl_state.scissor.used = true;
   if (gl_state.scissor.x != x || gl_state.scissor.y != y || gl_state.scissor.w != width || gl_state.scissor.h != height)
   {
      glScissor(x, y, width, height);
      gl_state.scissor.x = x;
      gl_state.scissor.y = y;
      gl_state.scissor.w = width;
      gl_state.scissor.h = height;
   }
}

/*
 *
 * Core in:
 * OpenGL    : 1.0
 */
void rglViewport(GLint x, GLint y, GLsizei width, GLsizei height)
{
   if (gl_state.viewport.x != x || gl_state.viewport.y != y || gl_state.viewport.w != width || gl_state.viewport.h != height)
   {
      glViewport(x, y, width, height);
      gl_state.viewport.x = x;
      gl_state.viewport.y = y;
      gl_state.viewport.w = width;
      gl_state.viewport.h = height;
   }
}

void rglBlendFunc(GLenum sfactor, GLenum dfactor)
{
   gl_state.blendfunc.used = true;
   if (gl_state.blendfunc.sfactor != sfactor || gl_state.blendfunc.dfactor != dfactor)
   {
      glBlendFunc(sfactor, dfactor);
      gl_state.blendfunc.sfactor = sfactor;
      gl_state.blendfunc.dfactor = dfactor;
   }
}

/*
 * Category: Blending
 *
 * Core in:
 * OpenGL    : 1.4
 */
void rglBlendFuncSeparate(GLenum srcRGB, GLenum dstRGB, GLenum srcAlpha, GLenum dstAlpha)
{
   gl_state.blendfunc_separate.used     = true;
   if (gl_state.blendfunc_separate.srcRGB != srcRGB || gl_state.blendfunc_separate.dstRGB != dstRGB || gl_state.blendfunc_separate.srcAlpha != srcAlpha || gl_state.blendfunc_separate.dstAlpha != dstAlpha)
   {
      glBlendFuncSeparate(srcRGB, dstRGB, srcAlpha, dstAlpha);
      gl_state.blendfunc_separate.srcRGB   = srcRGB;
      gl_state.blendfunc_separate.dstRGB   = dstRGB;
      gl_state.blendfunc_separate.srcAlpha = srcAlpha;
      gl_state.blendfunc_separate.dstAlpha = dstAlpha;
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
   if (active_texture != texture - GL_TEXTURE0)
   {
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
   if (gl_state.bind_textures.ids[active_texture] != texture || gl_state.bind_textures.target[active_texture] != target)
   {
      glBindTexture(target, texture);
      gl_state.bind_textures.ids[active_texture] = texture;
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
   if (gl_state.program != program)
   {
      glUseProgram(program);
      gl_state.program = program;
   }
}

/*
 *
 * Core in:
 * OpenGL    : 1.0
 */
void rglDepthMask(GLboolean flag)
{
   gl_state.depthmask.used = true;
   if (gl_state.depthmask.mask != flag)
   {
      glDepthMask(flag);
      gl_state.depthmask.mask = flag;
   }
}

/*
 *
 * Core in:
 * OpenGL    : 1.0
 */
void rglStencilMask(GLenum mask)
{
   gl_state.stencilmask.used = true;
   if (gl_state.stencilmask.mask != mask)
   {
      glStencilMask(mask);
      gl_state.stencilmask.mask = mask;
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
   if (target == GL_ARRAY_BUFFER)
   {
      if (gl_state.array_buffer != buffer)
      {
         gl_state.array_buffer = buffer;
         glBindBuffer(target, buffer);
      }
   }
   else if (target == GL_ELEMENT_ARRAY_BUFFER)
   {
      if (gl_state.index_buffer != buffer)
      {
         gl_state.index_buffer = buffer;
         glBindBuffer(target, buffer);
      }
   }
   else
      glBindBuffer(target, buffer);
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
}

/*
 * Category: FBO
 *
 * Core in:
 * OpenGL    : 3.0
 * OpenGLES  : 2.0
 */
void rglFramebufferTexture2D(GLenum target, GLenum attachment,
      GLenum textarget, GLuint texture, GLint level)
{
   int type;
   if (target == GL_FRAMEBUFFER)
      type = 0;
#ifndef HAVE_OPENGLES2
   else if (target == GL_DRAW_FRAMEBUFFER)
      type = 0;
   else if (target == GL_READ_FRAMEBUFFER)
      type = 1;
#endif
   if (gl_state.framebuf[type].desired_location < MAX_FRAMEBUFFERS)
   {
      framebuffers[gl_state.framebuf[type].desired_location]->target = textarget;
      if (attachment == GL_COLOR_ATTACHMENT0)
      {
         if (framebuffers[gl_state.framebuf[type].desired_location]->color_attachment != texture)
         {
            bindFBO(target);
            glFramebufferTexture2D(target, attachment, textarget, texture, level);
            framebuffers[gl_state.framebuf[type].location]->color_attachment = texture;
         }
      }
      else if (attachment == GL_DEPTH_ATTACHMENT)
      {
         if (framebuffers[gl_state.framebuf[type].desired_location]->depth_attachment != texture)
         {
            bindFBO(target);
            glFramebufferTexture2D(target, attachment, textarget, texture, level);
            framebuffers[gl_state.framebuf[type].location]->depth_attachment = texture;
         }
      }
   }
   else
   {
      bindFBO(target);
      glFramebufferTexture2D(target, attachment, textarget, texture, level);
   }
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

void rglDrawRangeElementsBaseVertex(GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type, GLvoid *indices, GLint basevertex)
{
   bindFBO(GL_FRAMEBUFFER);
#ifdef HAVE_OPENGLES
   m_glDrawRangeElementsBaseVertex(mode, start, end, count, type, indices, basevertex);
#else
   glDrawRangeElementsBaseVertex(mode, start, end, count, type, indices, basevertex);
#endif
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
   for (i = 0; i < n; ++i)
   {
      if (_framebuffers[i] < MAX_FRAMEBUFFERS)
      {
         free(framebuffers[_framebuffers[i]]);
         framebuffers[_framebuffers[i]] = NULL;
      }
      for (p = 0; p < 2; ++p)
      {
         if (_framebuffers[i] == gl_state.framebuf[p].location)
            gl_state.framebuf[p].location = 0;
      }
   }
   glDeleteFramebuffers(n, _framebuffers);
}

void rglDeleteTextures(GLsizei n, const GLuint *textures)
{
   int i, p;
   for (i = 0; i < n; ++i)
   {
      if (textures[i] == gl_state.bind_textures.ids[active_texture])
      {
         gl_state.bind_textures.ids[active_texture] = 0;
         gl_state.bind_textures.target[active_texture] = GL_TEXTURE_2D;
      }
      for (p = 0; p < MAX_FRAMEBUFFERS; ++p)
      {
         if (framebuffers[p] != NULL)
         {
            if (framebuffers[p]->color_attachment == textures[i])
               framebuffers[p]->color_attachment = 0;
            if (framebuffers[p]->depth_attachment == textures[i])
               framebuffers[p]->depth_attachment = 0;
         }
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

void rglBufferStorage(GLenum target,
                       GLsizeiptr size,
                       const GLvoid * data,
                       GLbitfield flags)
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
   int type;
   if (target == GL_FRAMEBUFFER)
      type = 0;
#ifndef HAVE_OPENGLES2
   else if (target == GL_DRAW_FRAMEBUFFER)
      type = 0;
   else if (target == GL_READ_FRAMEBUFFER)
      type = 1;
#endif
   if (gl_state.framebuf[type].desired_location < MAX_FRAMEBUFFERS) {
      framebuffers[gl_state.framebuf[type].desired_location]->target = renderbuffertarget;
      if (attachment == GL_COLOR_ATTACHMENT0) {
         if (framebuffers[gl_state.framebuf[type].desired_location]->color_attachment != renderbuffer) {
            bindFBO(target);
            glFramebufferRenderbuffer(target, attachment, renderbuffertarget, renderbuffer);
            framebuffers[gl_state.framebuf[type].location]->color_attachment = renderbuffer;
         }
      }
      else if (attachment == GL_DEPTH_ATTACHMENT) {
         if (framebuffers[gl_state.framebuf[type].desired_location]->depth_attachment != renderbuffer) {
            bindFBO(target);
            glFramebufferRenderbuffer(target, attachment, renderbuffertarget, renderbuffer);
            framebuffers[gl_state.framebuf[type].location]->depth_attachment = renderbuffer;
         }
      }
   } else {
      bindFBO(target);
      glFramebufferRenderbuffer(target, attachment, renderbuffertarget, renderbuffer);
   }
}

/*
 * Category: Shaders
 *
 * Core in:
 * OpenGL    : 3.0
 */


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

void rglGetIntegerv(GLenum pname, GLint * data)
{
   glGetIntegerv(pname, data);
}

void rglGetFloatv(GLenum pname, GLfloat * params)
{
   glGetFloatv(pname, params);
}

const GLubyte* rglGetString(GLenum name)
{
   return glGetString(name);
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
#ifndef HAVE_OPENGLES2
   glProgramParameteri(program, pname, value);
#else
   printf("WARNING! Not implemented.\n");
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
#ifndef HAVE_OPENGLES2
   glGetActiveUniformBlockiv(program, uniformBlockIndex, pname, params);
#else
   printf("WARNING! Not implemented.\n");
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
#ifndef HAVE_OPENGLES2
   glGetActiveUniformsiv(program, uniformCount, uniformIndices, pname, params);
#else
   printf("WARNING! Not implemented.\n");
#endif
}

/*
 *
 * Core in:
 *
 * OpenGLES  : 3.0
 */
void rglGetUniformIndices(GLuint program,
  	GLsizei uniformCount,
  	const GLchar **uniformNames,
  	GLuint *uniformIndices)
{
#ifndef HAVE_OPENGLES2
   glGetUniformIndices(program, uniformCount, uniformNames, uniformIndices);
#else
   printf("WARNING! Not implemented.\n");
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
#ifndef HAVE_OPENGLES2
   glBindBufferBase(target, index, buffer);
#else
   printf("WARNING! Not implemented.\n");
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
#ifndef HAVE_OPENGLES2
   return glGetUniformBlockIndex(program, uniformBlockName);
#else
   printf("WARNING! Not implemented.\n");
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
#ifndef HAVE_OPENGLES2
   glUniformBlockBinding(program, uniformBlockIndex, uniformBlockBinding);
#else
   printf("WARNING! Not implemented.\n");
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
#ifndef HAVE_OPENGLES2
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
#ifndef HAVE_OPENGLES2
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
#ifndef HAVE_OPENGLES2
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
#ifndef HAVE_OPENGLES2
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

void rglShaderSource(GLuint shader, GLsizei count,
      const GLchar **string, const GLint *length)
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
   GLuint temp = glCreateProgram();
   int i;
   for (i = 0; i < MAX_UNIFORMS; ++i)
      memset(&program_uniforms[temp][i], 0, sizeof(struct gl_program_uniforms));
   return temp;
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

void rglTexParameteri(GLenum target, GLenum pname, GLint param)
{
   glTexParameteri(target, pname, param);
}

void rglTexParameterf(GLenum target, GLenum pname, GLfloat param)
{
   glTexParameterf(target, pname, param);
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
   if (gl_state.vertex_attrib_pointer.enabled[index] != 0) {
      gl_state.vertex_attrib_pointer.enabled[index] = 0;
      glDisableVertexAttribArray(index);
   }
}

/*
 * Category: Generic vertex attributes
 *
 * Core in:
 * OpenGL    : 2.0
 */
void rglEnableVertexAttribArray(GLuint index)
{
   if (gl_state.vertex_attrib_pointer.enabled[index] != 1) {
      gl_state.vertex_attrib_pointer.enabled[index] = 1;
      glEnableVertexAttribArray(index);
   }
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
#ifndef HAVE_OPENGLES2
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
   if (gl_state.attrib_pointer.size[name] != size || gl_state.attrib_pointer.type[name] != type || gl_state.attrib_pointer.normalized[name] != normalized || gl_state.attrib_pointer.stride[name] != stride || gl_state.attrib_pointer.pointer[name] != pointer || gl_state.attrib_pointer.buffer[name] != gl_state.array_buffer) {
      gl_state.attrib_pointer.used[name] = 1;
      gl_state.attrib_pointer.size[name] = size;
      gl_state.attrib_pointer.type[name] = type;
      gl_state.attrib_pointer.normalized[name] = normalized;
      gl_state.attrib_pointer.stride[name] = stride;
      gl_state.attrib_pointer.pointer[name] = pointer;
      gl_state.attrib_pointer.buffer[name] = gl_state.array_buffer;
      glVertexAttribPointer(name, size, type, normalized, stride, pointer);
   }
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
void rglVertexAttrib4fv(GLuint name, GLfloat* v)
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
   if (!resetting_context)
      glDeleteProgram(program);
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
   if (program_uniforms[gl_state.program][location].uniform1f != v0) {
      glUniform1f(location, v0);
      program_uniforms[gl_state.program][location].uniform1f = v0;
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
   if (program_uniforms[gl_state.program][location].uniform1f != value[0]) {
      glUniform1fv(location, count, value);
      program_uniforms[gl_state.program][location].uniform1f = value[0];
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
   if (program_uniforms[gl_state.program][location].uniform1i != value[0]) {
      glUniform1iv(location, count, value);
      program_uniforms[gl_state.program][location].uniform1i = value[0];
   }
}

void rglClearBufferfv( 	GLenum buffer,
  	GLint drawBuffer,
  	const GLfloat * value)
{
#ifndef HAVE_OPENGLES2
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
#ifndef HAVE_OPENGLES2
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
#ifndef HAVE_OPENGLES2
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
#ifndef HAVE_OPENGLES2
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
   if (program_uniforms[gl_state.program][location].uniform1i != v0) {
      glUniform1i(location, v0);
      program_uniforms[gl_state.program][location].uniform1i = v0;
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
   if (program_uniforms[gl_state.program][location].uniform2f[0] != v0 || program_uniforms[gl_state.program][location].uniform2f[1] != v1) {
      glUniform2f(location, v0, v1);
      program_uniforms[gl_state.program][location].uniform2f[0] = v0;
      program_uniforms[gl_state.program][location].uniform2f[1] = v1;
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
   if (program_uniforms[gl_state.program][location].uniform2i[0] != v0 || program_uniforms[gl_state.program][location].uniform2i[1] != v1) {
      glUniform2i(location, v0, v1);
      program_uniforms[gl_state.program][location].uniform2i[0] = v0;
      program_uniforms[gl_state.program][location].uniform2i[1] = v1;
   }
}

void rglUniform3i(GLint location, GLint v0, GLint v1, GLint v2)
{
   if (program_uniforms[gl_state.program][location].uniform3i[0] != v0 || program_uniforms[gl_state.program][location].uniform3i[1] != v1) {
      glUniform3i(location, v0, v1, v2);
      program_uniforms[gl_state.program][location].uniform3i[0] = v0;
      program_uniforms[gl_state.program][location].uniform3i[1] = v1;
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
   if (program_uniforms[gl_state.program][location].uniform2f[0] != value[0] || program_uniforms[gl_state.program][location].uniform2f[1] != value[1]) {
      glUniform2fv(location, count, value);
      program_uniforms[gl_state.program][location].uniform2f[0] = value[0];
      program_uniforms[gl_state.program][location].uniform2f[1] = value[1];
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
   if (program_uniforms[gl_state.program][location].uniform3f[0] != v0 || program_uniforms[gl_state.program][location].uniform3f[1] != v1 || program_uniforms[gl_state.program][location].uniform3f[2] != v2) {
      glUniform3f(location, v0, v1, v2);
      program_uniforms[gl_state.program][location].uniform3f[0] = v0;
      program_uniforms[gl_state.program][location].uniform3f[1] = v1;
      program_uniforms[gl_state.program][location].uniform3f[2] = v2;
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
   if (program_uniforms[gl_state.program][location].uniform3f[0] != value[0] || program_uniforms[gl_state.program][location].uniform3f[1] != value[1] || program_uniforms[gl_state.program][location].uniform3f[2] != value[2]) {
      glUniform3fv(location, count, value);
      program_uniforms[gl_state.program][location].uniform3f[0] = value[0];
      program_uniforms[gl_state.program][location].uniform3f[1] = value[1];
      program_uniforms[gl_state.program][location].uniform3f[2] = value[2];
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
   if (program_uniforms[gl_state.program][location].uniform4i[0] != v0 || program_uniforms[gl_state.program][location].uniform4i[1] != v1 || program_uniforms[gl_state.program][location].uniform4i[2] != v2 || program_uniforms[gl_state.program][location].uniform4i[3] != v3) {
      glUniform4i(location, v0, v1, v2, v3);
      program_uniforms[gl_state.program][location].uniform4i[0] = v0;
      program_uniforms[gl_state.program][location].uniform4i[1] = v1;
      program_uniforms[gl_state.program][location].uniform4i[2] = v2;
      program_uniforms[gl_state.program][location].uniform4i[3] = v3;
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
   if (program_uniforms[gl_state.program][location].uniform4f[0] != v0 || program_uniforms[gl_state.program][location].uniform4f[1] != v1 || program_uniforms[gl_state.program][location].uniform4f[2] != v2 || program_uniforms[gl_state.program][location].uniform4f[3] != v3) {
      glUniform4f(location, v0, v1, v2, v3);
      program_uniforms[gl_state.program][location].uniform4f[0] = v0;
      program_uniforms[gl_state.program][location].uniform4f[1] = v1;
      program_uniforms[gl_state.program][location].uniform4f[2] = v2;
      program_uniforms[gl_state.program][location].uniform4f[3] = v3;
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
   if (program_uniforms[gl_state.program][location].uniform4f[0] != value[0] || program_uniforms[gl_state.program][location].uniform4f[1] != value[1] || program_uniforms[gl_state.program][location].uniform4f[2] != value[2] || program_uniforms[gl_state.program][location].uniform4f[3] != value[3]) {
      glUniform4fv(location, count, value);
      program_uniforms[gl_state.program][location].uniform4f[0] = value[0];
      program_uniforms[gl_state.program][location].uniform4f[1] = value[1];
      program_uniforms[gl_state.program][location].uniform4f[2] = value[2];
      program_uniforms[gl_state.program][location].uniform4f[3] = value[3];
   }
}


/*
 *
 * Core in:
 * OpenGL    : 1.0
 */
void rglPolygonOffset(GLfloat factor, GLfloat units)
{
   gl_state.polygonoffset.used = true;
   if (gl_state.polygonoffset.factor != factor || gl_state.polygonoffset.units != units) {
      glPolygonOffset(factor, units);
      gl_state.polygonoffset.factor = factor;
      gl_state.polygonoffset.units  = units;
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
   glGenFramebuffers(n, ids);
   int i;
   for (i = 0; i < n; ++i) {
      if (ids[i] < MAX_FRAMEBUFFERS)
         framebuffers[ids[i]] = (struct gl_framebuffers*)calloc(1, sizeof(struct gl_framebuffers));
   }
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
   if (gl_state.framebuf[0].desired_location < MAX_FRAMEBUFFERS) {
      if (framebuffers[gl_state.framebuf[0].desired_location]->draw_buf_set == 0)
      {
         bindFBO(GL_DRAW_FRAMEBUFFER);
         glDrawBuffers(n, bufs);
         framebuffers[gl_state.framebuf[0].location]->draw_buf_set = 1;
      }
   } else {
      bindFBO(GL_DRAW_FRAMEBUFFER);
      glDrawBuffers(n, bufs);
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
void *rglMapBufferRange( 	GLenum target,
  	GLintptr offset,
  	GLsizeiptr length,
  	GLbitfield access)
{
#ifndef HAVE_OPENGLES2
   return glMapBufferRange(target, offset, length, access);
#endif
}

void rglFlushMappedBufferRange(GLenum target, GLintptr offset, GLsizeiptr length)
{
#ifndef HAVE_OPENGLES2
   glFlushMappedBufferRange(target, offset, length);
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
#ifndef HAVE_OPENGLES2
   glTexStorage2D(target, levels, internalFormat, width, height);
#else
   printf("WARNING! Not implemented.\n");
#endif
}

/*
 *
 * Core in:
 * OpenGL    : 4.2
 * OpenGLES  : 3.1
 */
void rglMemoryBarrier(GLbitfield barriers)
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
void rglBindImageTexture(GLuint unit,
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
#ifndef HAVE_OPENGLES2
   glGetProgramBinary(program, bufsize, length, binaryFormat, binary);
#else
   printf("WARNING! Not implemented.\n");
#endif
}

/*
 *
 * Core in:
 * OpenGL    : 4.1
 * OpenGLES  : 3.1
 */
void rglProgramBinary(GLuint program,
  	GLenum binaryFormat,
  	const void *binary,
  	GLsizei length)
{
#ifndef HAVE_OPENGLES2
   glProgramBinary(program, binaryFormat, binary, length);
#else
   printf("WARNING! Not implemented.\n");
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

/*
 *
 * Core in:
 * OpenGL    : 1.5
 */

/*
 *
 * Core in:
 * OpenGL    : 1.5
 */

void rglBlendEquation(GLenum mode)
{
   glBlendEquation(mode);
}

void rglBlendColor(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha)
{
   glBlendColor(red, green, blue, alpha);
}

/*
 * Category: Blending
 *
 * Core in:
 * OpenGL    : 2.0
 */
void rglBlendEquationSeparate(GLenum modeRGB, GLenum modeAlpha)
{
   glBlendEquationSeparate(modeRGB, modeAlpha);
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
#ifndef HAVE_OPENGLES2
   gl_state.bindvertex.array = array;
   glBindVertexArray(array);
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
#ifndef HAVE_OPENGLES2
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
#ifndef HAVE_OPENGLES2
   glDeleteVertexArrays(n, arrays);
#endif
}

/*
 *
 * Core in:
 * OpenGL    : 3.2
 * OpenGLES  : 3.0
 */
#ifndef HAVE_OPENGLES2
GLsync rglFenceSync(GLenum condition, GLbitfield flags)
{
   return glFenceSync(condition, flags);
}
/*
 *
 * Core in:
 * OpenGL    : 3.2
 * OpenGLES  : 3.0
 */
void rglWaitSync(void *sync, GLbitfield flags, uint64_t timeout)
{
   glWaitSync((GLsync)sync, flags, (GLuint64)timeout);
}

void rglDeleteSync(GLsync sync)
{
   glDeleteSync(sync);
}

GLenum rglClientWaitSync(GLsync sync, GLbitfield flags, GLuint64 timeout)
{
   return glClientWaitSync(sync, flags, timeout);
}
#endif
/* GLSM-side */

bool isExtensionSupported(const char *extension)
{
#ifdef GL_NUM_EXTENSIONS
   int i;
	GLint count = 0;
	glGetIntegerv(GL_NUM_EXTENSIONS, &count);

	for (i = 0; i < count; ++i)
   {
		const char* name = (const char*)glGetStringi(GL_EXTENSIONS, i);
		if (name == NULL)
			continue;
		if (strcmp(extension, name) == 0)
			return true;
	}
#else
	GLubyte *where = (GLubyte *)strchr(extension, ' ');
	if (where || *extension == '\0')
		return false;

	const GLubyte *extensions = glGetString(GL_EXTENSIONS);

	const GLubyte *start = extensions;
	for (;;)
   {
		where = (GLubyte *)strstr((const char *)start, extension);
		if (where == NULL)
			break;

		GLubyte *terminator = where + strlen(extension);
		if (where == start || *(where - 1) == ' ')
		if (*terminator == ' ' || *terminator == '\0')
			return true;

		start = terminator;
	}
#endif // GL_NUM_EXTENSIONS
	return false;
}

static void glsm_state_setup(void)
{
   glsm_ctx_proc_address_t proc_info;
   proc_info.addr = NULL;
   if (!glsm_ctl(GLSM_CTL_PROC_ADDRESS_GET, NULL))
      return;

   GLint majorVersion = 0;
   GLint minorVersion = 0;
   bool copy_image_support_version = 0;
#ifndef HAVE_OPENGLES2
   glGetIntegerv(GL_MAJOR_VERSION, &majorVersion);
   glGetIntegerv(GL_MINOR_VERSION, &minorVersion);
#endif
#ifdef HAVE_OPENGLES
   if (majorVersion >= 3 && minorVersion >= 2)
      copy_image_support_version = 1;
#else
   if (majorVersion >= 4 && minorVersion >= 3)
      copy_image_support_version = 1;
#endif
   copy_image_support = isExtensionSupported("GL_ARB_copy_image") || isExtensionSupported("GL_EXT_copy_image") || copy_image_support_version;
#ifdef HAVE_OPENGLES
   m_glDrawRangeElementsBaseVertex = (PFNGLDRAWRANGEELEMENTSBASEVERTEXPROC)proc_info.addr("glDrawRangeElementsBaseVertex");
   m_glBufferStorage = (PFNGLBUFFERSTORAGEEXTPROC)proc_info.addr("glBufferStorageEXT");
   m_glMemoryBarrier = (PFNGLMEMORYBARRIERPROC)proc_info.addr("glMemoryBarrier");
   m_glBindImageTexture = (PFNGLBINDIMAGETEXTUREPROC)proc_info.addr("glBindImageTexture");
   m_glTexStorage2DMultisample = (PFNGLTEXSTORAGE2DMULTISAMPLEPROC)proc_info.addr("glTexStorage2DMultisample");
   m_glCopyImageSubData = (PFNGLCOPYIMAGESUBDATAPROC)proc_info.addr("glCopyImageSubData");
   if (m_glCopyImageSubData == NULL)
      m_glCopyImageSubData = (PFNGLCOPYIMAGESUBDATAPROC)proc_info.addr("glCopyImageSubDataEXT");
#endif

#ifdef OPENGL_DEBUG
#ifdef HAVE_OPENGLES
   glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS_KHR);
   glDebugMessageCallbackKHR(on_gl_error, NULL);
#else
   glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
   glDebugMessageCallback(on_gl_error, NULL);
#endif
#endif

   memset(&gl_state, 0, sizeof(struct gl_cached_state));

   gl_state.cap_translate[SGL_DEPTH_TEST]               = GL_DEPTH_TEST;
   gl_state.cap_translate[SGL_BLEND]                    = GL_BLEND;
   gl_state.cap_translate[SGL_POLYGON_OFFSET_FILL]      = GL_POLYGON_OFFSET_FILL;
   gl_state.cap_translate[SGL_FOG]                      = GL_FOG;
   gl_state.cap_translate[SGL_CULL_FACE]                = GL_CULL_FACE;
   gl_state.cap_translate[SGL_ALPHA_TEST]               = GL_ALPHA_TEST;
   gl_state.cap_translate[SGL_SCISSOR_TEST]             = GL_SCISSOR_TEST;
   gl_state.cap_translate[SGL_STENCIL_TEST]             = GL_STENCIL_TEST;
   gl_state.cap_translate[SGL_DITHER]                   = GL_DITHER;
   gl_state.cap_translate[SGL_SAMPLE_ALPHA_TO_COVERAGE] = GL_SAMPLE_ALPHA_TO_COVERAGE;
   gl_state.cap_translate[SGL_SAMPLE_COVERAGE]          = GL_SAMPLE_COVERAGE;
#ifndef HAVE_OPENGLES
   gl_state.cap_translate[SGL_COLOR_LOGIC_OP]       = GL_COLOR_LOGIC_OP;
   gl_state.cap_translate[SGL_CLIP_DISTANCE0]       = GL_CLIP_DISTANCE0;
   gl_state.cap_translate[SGL_DEPTH_CLAMP]          = GL_DEPTH_CLAMP;
#endif

   glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &glsm_max_textures);
   if (glsm_max_textures > 32)
      glsm_max_textures = 32;

   int i;
   for (i = 0; i < glsm_max_textures; ++i) {
      gl_state.bind_textures.target[i] = GL_TEXTURE_2D;
      gl_state.bind_textures.ids[i] = 0;
   }

   gl_state.pixelstore.pack             = 4;
   gl_state.pixelstore.unpack           = 4;
   gl_state.array_buffer                = 0;
   gl_state.index_buffer                = 0;
   gl_state.bindvertex.array            = 0;
   default_framebuffer                  = hw_render.get_current_framebuffer();
   gl_state.framebuf[0].location        = default_framebuffer;
   gl_state.framebuf[1].location        = default_framebuffer;
   gl_state.framebuf[0].desired_location        = default_framebuffer;
   gl_state.framebuf[1].desired_location        = default_framebuffer;
   glBindFramebuffer(GL_FRAMEBUFFER, default_framebuffer);
   GLint params;
   if (!resetting_context)
      framebuffers[default_framebuffer] = (struct gl_framebuffers*)calloc(1, sizeof(struct gl_framebuffers));
   glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &params);
   framebuffers[default_framebuffer]->color_attachment = params;
   glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &params);
   framebuffers[default_framebuffer]->depth_attachment = params;
   framebuffers[default_framebuffer]->target = GL_TEXTURE_2D;
   gl_state.cullface.mode               = GL_BACK;
   gl_state.frontface.mode              = GL_CCW;

   gl_state.blendfunc_separate.used     = false;
   gl_state.blendfunc_separate.srcRGB   = GL_ONE;
   gl_state.blendfunc_separate.dstRGB   = GL_ZERO;
   gl_state.blendfunc_separate.srcAlpha = GL_ONE;
   gl_state.blendfunc_separate.dstAlpha = GL_ZERO;

   gl_state.depthfunc.used              = false;

   gl_state.colormask.used              = false;
   gl_state.colormask.red               = GL_TRUE;
   gl_state.colormask.green             = GL_TRUE;
   gl_state.colormask.blue              = GL_TRUE;
   gl_state.colormask.alpha             = GL_TRUE;

   gl_state.polygonoffset.used          = false;
   gl_state.polygonoffset.factor        = 0;
   gl_state.polygonoffset.units         = 0;

   gl_state.depthfunc.func              = GL_LESS;

#ifndef HAVE_OPENGLES
   gl_state.colorlogicop                = GL_COPY;
#endif
}

static void glsm_state_bind(void)
{
   unsigned i;
#ifndef HAVE_OPENGLES2
   if (gl_state.bindvertex.array != 0) {
      glBindVertexArray(gl_state.bindvertex.array);
      gl_state.array_buffer = 0;
   } else
#endif
   {
      for (i = 0; i < MAX_ATTRIB; i++)
      {
         if (gl_state.vertex_attrib_pointer.enabled[i])
            glEnableVertexAttribArray(i);

         if (gl_state.attrib_pointer.used[i]) {
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

   glPixelStorei(GL_UNPACK_ALIGNMENT, gl_state.pixelstore.unpack);
   glPixelStorei(GL_PACK_ALIGNMENT, gl_state.pixelstore.pack);

   glClearColor(
         gl_state.clear_color.r,
         gl_state.clear_color.g,
         gl_state.clear_color.b,
         gl_state.clear_color.a);

#ifdef HAVE_GLIDEN64
   if (EnableFBEmulation)
   {
      gl_state.framebuf[0].location = 0;
      gl_state.framebuf[1].location = 0;
   }
   else
#endif
   {
      glBindFramebuffer(GL_FRAMEBUFFER, default_framebuffer);
      gl_state.framebuf[0].location = default_framebuffer;
      gl_state.framebuf[1].location = default_framebuffer;
   }

   for(i = 0; i < SGL_CAP_MAX; i ++)
   {
      if (gl_state.cap_state[i])
         glEnable(gl_state.cap_translate[i]);
   }

   if (gl_state.blendfunc.used)
      glBlendFunc(
            gl_state.blendfunc.sfactor,
            gl_state.blendfunc.dfactor);

   glUseProgram(gl_state.program);

   glViewport(
         gl_state.viewport.x,
         gl_state.viewport.y,
         gl_state.viewport.w,
         gl_state.viewport.h);

   glActiveTexture(GL_TEXTURE0);
   glBindTexture(gl_state.bind_textures.target[0], gl_state.bind_textures.ids[0]);
   glActiveTexture(GL_TEXTURE0 + active_texture);
}

static void glsm_state_unbind(void)
{
   unsigned i;

   for (i = 0; i < SGL_CAP_MAX; i ++)
   {
      if (gl_state.cap_state[i])
         glDisable(gl_state.cap_translate[i]);
   }

#ifndef HAVE_OPENGLES2
   if (gl_state.bindvertex.array != 0)
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
   glActiveTexture(GL_TEXTURE0);
}

static bool glsm_state_ctx_destroy(void *data)
{
   return true;
}

static bool glsm_state_ctx_init(void *data)
{
   glsm_ctx_params_t *params = (glsm_ctx_params_t*)data;

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
#ifdef CORE
   hw_render.context_type       = RETRO_HW_CONTEXT_OPENGL_CORE;
   hw_render.version_major      = 3;
   hw_render.version_minor      = 3;
#else
   hw_render.context_type       = RETRO_HW_CONTEXT_OPENGL;
#endif
#endif
   hw_render.context_reset      = params->context_reset;
   hw_render.context_destroy    = params->context_destroy;
   hw_render.stencil            = params->stencil;
   hw_render.depth              = true;
   hw_render.bottom_left_origin = true;
   hw_render.cache_context      = true;
#ifdef OPENGL_DEBUG
   hw_render.debug_context      = true;
#endif

   if (!params->environ_cb(RETRO_ENVIRONMENT_SET_HW_RENDER, &hw_render))
      return false;

   return true;
}

GLuint glsm_get_current_framebuffer(void)
{
   return hw_render.get_current_framebuffer();
}

#ifdef HAVE_GLIDEN64
extern void retroChangeWindow(void);
#endif

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
         if (window_first > 0)
         {
            resetting_context = 1;
            glsm_state_setup();
#ifdef HAVE_GLIDEN64
            retroChangeWindow();
#endif
            glsm_state_unbind();
            resetting_context = 0;
         }
         else
            window_first = 1;
         break;
      case GLSM_CTL_STATE_CONTEXT_DESTROY:
         glsm_state_ctx_destroy(data);
         break;
      case GLSM_CTL_STATE_CONTEXT_INIT:
         return glsm_state_ctx_init(data);
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
