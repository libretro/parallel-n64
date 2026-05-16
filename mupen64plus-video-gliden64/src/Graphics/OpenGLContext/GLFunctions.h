#ifndef GLFUNCTIONS_H
#define GLFUNCTIONS_H

#ifdef OS_WINDOWS
#include <windows.h>
//#define FORCE_UNBUFFERED_DRAWER // Debug option.
#elif defined(OS_LINUX)
#include <winlnxdefs.h>
#endif

#ifdef GL_GLEXT_PROTOTYPES
#undef GL_GLEXT_PROTOTYPES
#endif // GL_GLEXT_PROTOTYPES

#ifdef EGL
#include <GL/glcorearb.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#elif defined(OS_MAC_OS_X)
#include <OpenGL/OpenGL.h>
#include <stddef.h>
#include <OpenGL/gl3.h>

// This is going to blow up so badly
typedef void (APIENTRYP PFNGLBUFFERSTORAGEPROC) (GLenum target, GLsizeiptr size, const void *data, GLbitfield flags);
typedef void (APIENTRYP PFNGLTEXTURESTORAGE2DPROC) (GLuint texture, GLsizei levels, GLenum internalformat, GLsizei width, GLsizei height);
typedef void (APIENTRYP PFNGLTEXTURESUBIMAGE2DPROC) (GLuint texture, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const void *pixels);
typedef void (APIENTRYP PFNGLTEXTUREPARAMETERIPROC) (GLuint texture, GLenum pname, GLint param);
typedef void (APIENTRYP PFNGLTEXTUREPARAMETERFPROC) (GLuint texture, GLenum pname, GLfloat param);
typedef void (APIENTRYP PFNGLCREATETEXTURESPROC) (GLenum target, GLsizei n, GLuint *textures);
typedef void (APIENTRYP PFNGLCREATEFRAMEBUFFERSPROC) (GLsizei n, GLuint *framebuffers);
typedef void (APIENTRYP PFNGLNAMEDFRAMEBUFFERTEXTUREPROC) (GLuint framebuffer, GLenum attachment, GLuint texture, GLint level);
typedef void (APIENTRYP PFNGLTEXTUREBARRIERPROC) (void);
typedef void (APIENTRYP PFNGLCREATEBUFFERSPROC) (GLsizei n, GLuint *buffers);

#define GL_MAP_PERSISTENT_BIT             0x0040
#define GL_MAP_COHERENT_BIT               0x0080
#define GL_CLIENT_STORAGE_BIT             0x0200
#elif defined(OS_IOS)
#include <OpenGLES/ES3/gl.h>
#include <OpenGLES/ES3/glext.h>
// Add missing type defintions for iOS
typedef double GLclampd;
typedef double GLdouble;
// These will get redefined by other GL headers.
#undef GL_DRAW_FRAMEBUFFER_BINDING
#undef GL_COPY_READ_BUFFER_BINDING
#undef GL_COPY_WRITE_BUFFER_BINDING
#include <GL/glcorearb.h>
#else
#include <GL/gl.h>
#include <GL/glcorearb.h>
#endif

#define GL_LUMINANCE 0x1909
#include <GL/glext.h>
#include <stdexcept>
#include <sstream>
#include "Log.h"

#include <string.h>

#include <glsm/glsm_caps.h>

#if !defined(EGL) && !defined(OS_IOS)
typedef void (APIENTRYP PFNGLPOLYGONOFFSETPROC) (GLfloat factor, GLfloat units);
typedef void (APIENTRYP PFNGLBINDTEXTUREPROC) (GLenum target, GLuint texture);
typedef void (APIENTRYP PFNGLTEXSUBIMAGE2DPROC) (GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const void *pixels);
typedef void (APIENTRYP PFNGLDRAWARRAYSPROC) (GLenum mode, GLint first, GLsizei count);
typedef void (APIENTRYP PFNGLDRAWELEMENTSPROC) (GLenum mode, GLsizei count, GLenum type, const void *indices);
typedef void (APIENTRYP PFNGLDELETETEXTURESPROC) (GLsizei n, const GLuint *textures);
typedef void (APIENTRYP PFNGLGENTEXTURESPROC) (GLsizei n, GLuint *textures);
typedef void (APIENTRYP PFNGLCOPYTEXIMAGE2DPROC) (GLenum target, GLint level, GLenum internalformat, GLint x, GLint y, GLsizei width, GLsizei height, GLint border);
#endif

typedef void (APIENTRYP PFNGLBLENDCOLORPROC) (GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha);

// #define GL_ERROR_DEBUG
#ifdef GL_ERROR_DEBUG
#define CHECKED_GL_FUNCTION(proc_name, ...) checked([&]() { proc_name(__VA_ARGS__);}, #proc_name)
#define CHECKED_GL_FUNCTION_WITH_RETURN(proc_name, ReturnType, ...) checkedWithReturn<ReturnType>([&]() { return proc_name(__VA_ARGS__);}, #proc_name)
#else
#define CHECKED_GL_FUNCTION(proc_name, ...) proc_name(__VA_ARGS__)
#define CHECKED_GL_FUNCTION_WITH_RETURN(proc_name, ReturnType, ...) proc_name(__VA_ARGS__)
#endif

#define CHECKED_GL_FUNCTION_UNCHECKED(proc_name, ...) proc_name(__VA_ARGS__)
#define CHECKED_GL_FUNCTION_UNCHECKED_WITH_RETURN(proc_name, ReturnType, ...) proc_name(__VA_ARGS__)

#define IS_GL_FUNCTION_VALID(proc_name) g_##proc_name != nullptr
#define GET_GL_FUNCTION(proc_name) g_##proc_name

#undef glGetError
#define glGetError g_glGetError
#undef glBlendFunc
#define glBlendFunc(...) CHECKED_GL_FUNCTION(g_glBlendFunc, __VA_ARGS__)
#undef glPixelStorei
#define glPixelStorei(...) CHECKED_GL_FUNCTION(g_glPixelStorei, __VA_ARGS__)
#undef glClearColor
#define glClearColor(...) CHECKED_GL_FUNCTION(g_glClearColor, __VA_ARGS__)
#undef glCullFace
#define glCullFace(...) CHECKED_GL_FUNCTION(g_glCullFace, __VA_ARGS__)
#undef glDepthFunc
#define glDepthFunc(...) CHECKED_GL_FUNCTION(g_glDepthFunc, __VA_ARGS__)
#undef glDepthMask
#define glDepthMask(...) CHECKED_GL_FUNCTION(g_glDepthMask, __VA_ARGS__)
#undef glDisable
#define glDisable(v) CHECKED_GL_FUNCTION(g_glDisable, S ## v)
#undef glEnable
#define glEnable(v) CHECKED_GL_FUNCTION(g_glEnable, S ## v)
#undef glPolygonOffset
#define glPolygonOffset(...) CHECKED_GL_FUNCTION(g_glPolygonOffset, __VA_ARGS__)
#undef glScissor
#define glScissor(...) CHECKED_GL_FUNCTION(g_glScissor, __VA_ARGS__)
#undef glViewport
#define glViewport(...) CHECKED_GL_FUNCTION(g_glViewport, __VA_ARGS__)
#undef glBindTexture
#define glBindTexture(...) CHECKED_GL_FUNCTION(g_glBindTexture, __VA_ARGS__)
#undef glTexImage2D
#define glTexImage2D(...) CHECKED_GL_FUNCTION(g_glTexImage2D, __VA_ARGS__)
#undef glTexParameteri
#define glTexParameteri(...) CHECKED_GL_FUNCTION(g_glTexParameteri, __VA_ARGS__)
#undef glGetIntegerv
#define glGetIntegerv(...) CHECKED_GL_FUNCTION_UNCHECKED(g_glGetIntegerv, __VA_ARGS__)
#undef glGetString
#define glGetString(...) CHECKED_GL_FUNCTION_UNCHECKED_WITH_RETURN(g_glGetString, const GLubyte*, __VA_ARGS__)
#undef glReadPixels
#define glReadPixels(...) CHECKED_GL_FUNCTION(g_glReadPixels, __VA_ARGS__)
#undef glTexSubImage2D
#define glTexSubImage2D(...) CHECKED_GL_FUNCTION(g_glTexSubImage2D, __VA_ARGS__)
#undef glDrawArrays
#define glDrawArrays(...) CHECKED_GL_FUNCTION(g_glDrawArrays, __VA_ARGS__)
#undef glDrawElements
#define glDrawElements(...) CHECKED_GL_FUNCTION(g_glDrawElements, __VA_ARGS__)
#undef glLineWidth
#define glLineWidth(...) CHECKED_GL_FUNCTION(g_glLineWidth, __VA_ARGS__)
#undef glClear
#define glClear(...) CHECKED_GL_FUNCTION(g_glClear, __VA_ARGS__)
#undef glGetFloatv
#define glGetFloatv(...) CHECKED_GL_FUNCTION(g_glGetFloatv, __VA_ARGS__)
#undef glDeleteTextures
#define glDeleteTextures(...) CHECKED_GL_FUNCTION(g_glDeleteTextures, __VA_ARGS__)
#undef glGenTextures
#define glGenTextures(...) CHECKED_GL_FUNCTION(g_glGenTextures, __VA_ARGS__)
#undef glTexParameterf
#define glTexParameterf(...) CHECKED_GL_FUNCTION(g_glTexParameterf, __VA_ARGS__)
#undef glActiveTexture
#define glActiveTexture(...) CHECKED_GL_FUNCTION(g_glActiveTexture, __VA_ARGS__)
#undef glBlendColor
#define glBlendColor(...) CHECKED_GL_FUNCTION(g_glBlendColor, __VA_ARGS__)
#undef glReadBuffer
#define glReadBuffer(...) CHECKED_GL_FUNCTION(g_glReadBuffer, __VA_ARGS__)
#undef glFinish
#define glFinish(...) CHECKED_GL_FUNCTION(g_glFinish, __VA_ARGS__)
#if defined(OS_ANDROID)
#define eglGetNativeClientBufferANDROID(...) CHECKED_GL_FUNCTION_WITH_RETURN(g_eglGetNativeClientBufferANDROID, EGLClientBuffer, __VA_ARGS__)
#endif

extern PFNGLBLENDFUNCPROC g_glBlendFunc;
extern PFNGLPIXELSTOREIPROC g_glPixelStorei;
extern PFNGLCLEARCOLORPROC g_glClearColor;
extern PFNGLCULLFACEPROC g_glCullFace;
extern PFNGLDEPTHFUNCPROC g_glDepthFunc;
extern PFNGLDEPTHMASKPROC g_glDepthMask;
extern PFNGLDISABLEPROC g_glDisable;
extern PFNGLENABLEPROC g_glEnable;
extern PFNGLPOLYGONOFFSETPROC g_glPolygonOffset;
extern PFNGLSCISSORPROC g_glScissor;
extern PFNGLVIEWPORTPROC g_glViewport;
extern PFNGLBINDTEXTUREPROC g_glBindTexture;
extern PFNGLTEXIMAGE2DPROC g_glTexImage2D;
extern PFNGLTEXPARAMETERIPROC g_glTexParameteri;
extern PFNGLGETINTEGERVPROC g_glGetIntegerv;
extern PFNGLGETSTRINGPROC g_glGetString;
extern PFNGLREADPIXELSPROC g_glReadPixels;
extern PFNGLTEXSUBIMAGE2DPROC g_glTexSubImage2D;
extern PFNGLDRAWARRAYSPROC g_glDrawArrays;
extern PFNGLGETERRORPROC g_glGetError;
extern PFNGLDRAWELEMENTSPROC g_glDrawElements;
extern PFNGLLINEWIDTHPROC g_glLineWidth;
extern PFNGLCLEARPROC g_glClear;
extern PFNGLGETFLOATVPROC g_glGetFloatv;
extern PFNGLDELETETEXTURESPROC g_glDeleteTextures;
extern PFNGLGENTEXTURESPROC g_glGenTextures;
extern PFNGLTEXPARAMETERFPROC g_glTexParameterf;
extern PFNGLACTIVETEXTUREPROC g_glActiveTexture;
extern PFNGLBLENDCOLORPROC g_glBlendColor;
extern PFNGLREADBUFFERPROC g_glReadBuffer;
extern PFNGLFINISHPROC g_glFinish;
#if defined(OS_ANDROID)
extern PFNEGLGETNATIVECLIENTBUFFERANDROIDPROC g_eglGetNativeClientBufferANDROID;
#endif

#undef glCreateShader
#define glCreateShader(...) CHECKED_GL_FUNCTION_WITH_RETURN(g_glCreateShader, GLuint, __VA_ARGS__)
#undef glCompileShader
#define glCompileShader(...) CHECKED_GL_FUNCTION(g_glCompileShader, __VA_ARGS__)
#undef glShaderSource
#define glShaderSource(...) CHECKED_GL_FUNCTION(g_glShaderSource, __VA_ARGS__)
#undef glCreateProgram
#define glCreateProgram(...) CHECKED_GL_FUNCTION_WITH_RETURN(g_glCreateProgram, GLuint, __VA_ARGS__)
#undef glAttachShader
#define glAttachShader(...) CHECKED_GL_FUNCTION(g_glAttachShader, __VA_ARGS__)
#undef glLinkProgram
#define glLinkProgram(...) CHECKED_GL_FUNCTION(g_glLinkProgram, __VA_ARGS__)
#undef glUseProgram
#define glUseProgram(...) CHECKED_GL_FUNCTION(g_glUseProgram, __VA_ARGS__)
#undef glGetUniformLocation
#define glGetUniformLocation(...) CHECKED_GL_FUNCTION_WITH_RETURN(g_glGetUniformLocation, GLint, __VA_ARGS__)
#undef glUniform1i
#define glUniform1i(...) CHECKED_GL_FUNCTION(g_glUniform1i, __VA_ARGS__)
#undef glUniform1f
#define glUniform1f(...) CHECKED_GL_FUNCTION(g_glUniform1f, __VA_ARGS__)
#undef glUniform2f
#define glUniform2f(...) CHECKED_GL_FUNCTION(g_glUniform2f, __VA_ARGS__)
#undef glUniform2i
#define glUniform2i(...) CHECKED_GL_FUNCTION(g_glUniform2i, __VA_ARGS__)
#undef glUniform4i
#define glUniform4i(...) CHECKED_GL_FUNCTION(g_glUniform4i, __VA_ARGS__)

#undef glUniform4f
#define glUniform4f(...) CHECKED_GL_FUNCTION(g_glUniform4f, __VA_ARGS__)
#undef glUniform3fv
#define glUniform3fv(...) CHECKED_GL_FUNCTION(g_glUniform3fv, __VA_ARGS__)
#undef glUniform4fv
#define glUniform4fv(...) CHECKED_GL_FUNCTION(g_glUniform4fv, __VA_ARGS__)
#undef glDetachShader
#define glDetachShader(...) CHECKED_GL_FUNCTION(g_glDetachShader, __VA_ARGS__)
#undef glDeleteShader
#define glDeleteShader(...) CHECKED_GL_FUNCTION(g_glDeleteShader, __VA_ARGS__)
#undef glDeleteProgram
#define glDeleteProgram(...) CHECKED_GL_FUNCTION(g_glDeleteProgram, __VA_ARGS__)
#undef glGetProgramInfoLog
#define glGetProgramInfoLog(...) CHECKED_GL_FUNCTION(g_glGetProgramInfoLog, __VA_ARGS__)
#undef glGetShaderInfoLog
#define glGetShaderInfoLog(...) CHECKED_GL_FUNCTION(g_glGetShaderInfoLog, __VA_ARGS__)
#undef glGetShaderiv
#define glGetShaderiv(...) CHECKED_GL_FUNCTION(g_glGetShaderiv, __VA_ARGS__)
#undef glGetProgramiv
#define glGetProgramiv(...) CHECKED_GL_FUNCTION(g_glGetProgramiv, __VA_ARGS__)

#undef glEnableVertexAttribArray
#define glEnableVertexAttribArray(...) CHECKED_GL_FUNCTION(g_glEnableVertexAttribArray, __VA_ARGS__)
#undef glDisableVertexAttribArray
#define glDisableVertexAttribArray(...) CHECKED_GL_FUNCTION(g_glDisableVertexAttribArray, __VA_ARGS__)
#undef glVertexAttribPointer
#define glVertexAttribPointer(...) CHECKED_GL_FUNCTION(g_glVertexAttribPointer, __VA_ARGS__)
#undef glBindAttribLocation
#define glBindAttribLocation(...) CHECKED_GL_FUNCTION(g_glBindAttribLocation, __VA_ARGS__)
#undef glVertexAttrib1f
#define glVertexAttrib1f(...) CHECKED_GL_FUNCTION(g_glVertexAttrib1f, __VA_ARGS__)
#undef glVertexAttrib4f
#define glVertexAttrib4f(...) CHECKED_GL_FUNCTION(g_glVertexAttrib4f, __VA_ARGS__)
#undef glVertexAttrib4fv
#define glVertexAttrib4fv(...) CHECKED_GL_FUNCTION(g_glVertexAttrib4fv, __VA_ARGS__)

#undef glDepthRangef
#define glDepthRangef(...) CHECKED_GL_FUNCTION(g_glDepthRangef, __VA_ARGS__)
#undef glClearDepthf
#define glClearDepthf(...) CHECKED_GL_FUNCTION(g_glClearDepthf, __VA_ARGS__)

#undef glBindBuffer
#define glBindBuffer(...) CHECKED_GL_FUNCTION(g_glBindBuffer, __VA_ARGS__)
#undef glBindFramebuffer
#define glBindFramebuffer(...) CHECKED_GL_FUNCTION(g_glBindFramebuffer, __VA_ARGS__)
#undef glBindRenderbuffer
#define glBindRenderbuffer(...) CHECKED_GL_FUNCTION(g_glBindRenderbuffer, __VA_ARGS__)
#undef glDrawBuffers
#define glDrawBuffers(...) CHECKED_GL_FUNCTION(g_glDrawBuffers, __VA_ARGS__)
#undef glGenFramebuffers
#define glGenFramebuffers(...) CHECKED_GL_FUNCTION(g_glGenFramebuffers, __VA_ARGS__)
#undef glDeleteFramebuffers
#define glDeleteFramebuffers(...) CHECKED_GL_FUNCTION(g_glDeleteFramebuffers, __VA_ARGS__)
#undef glFramebufferTexture2D
#define glFramebufferTexture2D(...) CHECKED_GL_FUNCTION(g_glFramebufferTexture2D, __VA_ARGS__)
#undef glTexImage2DMultisample
#define glTexImage2DMultisample(...) CHECKED_GL_FUNCTION(g_glTexImage2DMultisample, __VA_ARGS__)
#undef glTexStorage2DMultisample
#define glTexStorage2DMultisample(...) CHECKED_GL_FUNCTION(g_glTexStorage2DMultisample, __VA_ARGS__)
#undef glGenRenderbuffers
#define glGenRenderbuffers(...) CHECKED_GL_FUNCTION(g_glGenRenderbuffers, __VA_ARGS__)
#undef glRenderbufferStorage
#define glRenderbufferStorage(...) CHECKED_GL_FUNCTION(g_glRenderbufferStorage, __VA_ARGS__)
#undef glDeleteRenderbuffers
#define glDeleteRenderbuffers(...) CHECKED_GL_FUNCTION(g_glDeleteRenderbuffers, __VA_ARGS__)
#undef glFramebufferRenderbuffer
#define glFramebufferRenderbuffer(...) CHECKED_GL_FUNCTION(g_glFramebufferRenderbuffer, __VA_ARGS__)
#undef glCheckFramebufferStatus
#define glCheckFramebufferStatus(...) CHECKED_GL_FUNCTION_WITH_RETURN(g_glCheckFramebufferStatus, GLenum, __VA_ARGS__)
#undef glBlitFramebuffer
#define glBlitFramebuffer(...) CHECKED_GL_FUNCTION(g_glBlitFramebuffer, __VA_ARGS__)
#undef glGenVertexArrays
#define glGenVertexArrays(...) CHECKED_GL_FUNCTION(g_glGenVertexArrays, __VA_ARGS__)
#undef glBindVertexArray
#define glBindVertexArray(...) CHECKED_GL_FUNCTION(g_glBindVertexArray, __VA_ARGS__)
#undef glDeleteVertexArrays
#define glDeleteVertexArrays(...) CHECKED_GL_FUNCTION(g_glDeleteVertexArrays, __VA_ARGS__);
#undef glGenBuffers
#define glGenBuffers(...) CHECKED_GL_FUNCTION(g_glGenBuffers, __VA_ARGS__)
#undef glBufferData
#define glBufferData(...) CHECKED_GL_FUNCTION(g_glBufferData, __VA_ARGS__)
#undef glMapBuffer
#define glMapBuffer(...) CHECKED_GL_FUNCTION(g_glMapBuffer, __VA_ARGS__)
#undef glMapBufferRange
#define glMapBufferRange(...) CHECKED_GL_FUNCTION_WITH_RETURN(g_glMapBufferRange, void*, __VA_ARGS__)
#undef glUnmapBuffer
#define glUnmapBuffer(...) CHECKED_GL_FUNCTION(g_glUnmapBuffer, __VA_ARGS__)
#undef glDeleteBuffers
#define glDeleteBuffers(...) CHECKED_GL_FUNCTION(g_glDeleteBuffers, __VA_ARGS__)
#undef glBindImageTexture
#define glBindImageTexture(...) CHECKED_GL_FUNCTION(g_glBindImageTexture, __VA_ARGS__)
#undef glMemoryBarrier
#define glMemoryBarrier(...) CHECKED_GL_FUNCTION(g_glMemoryBarrier, __VA_ARGS__)
#undef glGetStringi
#define glGetStringi(...) CHECKED_GL_FUNCTION_WITH_RETURN(g_glGetStringi, const GLubyte*, __VA_ARGS__)
#undef glInvalidateFramebuffer
#define glInvalidateFramebuffer(...) CHECKED_GL_FUNCTION(g_glInvalidateFramebuffer, __VA_ARGS__)
#undef glBufferStorage
#define glBufferStorage(...) CHECKED_GL_FUNCTION(g_glBufferStorage, __VA_ARGS__)
#undef glFenceSync
#define glFenceSync(...) CHECKED_GL_FUNCTION_WITH_RETURN(g_glFenceSync, GLsync, __VA_ARGS__)
#undef glClientWaitSync
#define glClientWaitSync(...) CHECKED_GL_FUNCTION(g_glClientWaitSync, __VA_ARGS__)
#undef glDeleteSync
#define glDeleteSync(...) CHECKED_GL_FUNCTION(g_glDeleteSync, __VA_ARGS__)

#undef glGetUniformBlockIndex
#define glGetUniformBlockIndex(...) CHECKED_GL_FUNCTION(g_glGetUniformBlockIndex, __VA_ARGS__)
#undef glUniformBlockBinding
#define glUniformBlockBinding(...) CHECKED_GL_FUNCTION(g_glUniformBlockBinding, __VA_ARGS__)
#undef glGetActiveUniformBlockiv
#define glGetActiveUniformBlockiv(...) CHECKED_GL_FUNCTION(g_glGetActiveUniformBlockiv, __VA_ARGS__)
#undef glGetUniformIndices
#define glGetUniformIndices(...) CHECKED_GL_FUNCTION(g_glGetUniformIndices, __VA_ARGS__)
#undef glGetActiveUniformsiv
#define glGetActiveUniformsiv(...) CHECKED_GL_FUNCTION(g_glGetActiveUniformsiv, __VA_ARGS__)
#undef glBindBufferBase
#define glBindBufferBase(...) CHECKED_GL_FUNCTION(g_glBindBufferBase, __VA_ARGS__)
#undef glBufferSubData
#define glBufferSubData(...) CHECKED_GL_FUNCTION(g_glBufferSubData, __VA_ARGS__)

#undef glGetProgramBinary
#define glGetProgramBinary(...) CHECKED_GL_FUNCTION(g_glGetProgramBinary, __VA_ARGS__)
#undef glProgramBinary
#define glProgramBinary(...) CHECKED_GL_FUNCTION(g_glProgramBinary, __VA_ARGS__)
#undef glProgramParameteri
#define glProgramParameteri(...) CHECKED_GL_FUNCTION(g_glProgramParameteri, __VA_ARGS__)

#undef glTexStorage2D
#define glTexStorage2D(...) CHECKED_GL_FUNCTION(g_glTexStorage2D, __VA_ARGS__)
#undef glTextureStorage2D
#define glTextureStorage2D(...) CHECKED_GL_FUNCTION(g_glTextureStorage2D, __VA_ARGS__)
#undef glTextureSubImage2D
#define glTextureSubImage2D(...) CHECKED_GL_FUNCTION(g_glTextureSubImage2D, __VA_ARGS__)
#undef glTextureStorage2DMultisample
#define glTextureStorage2DMultisample(...) CHECKED_GL_FUNCTION(g_glTextureStorage2DMultisample, __VA_ARGS__)
#undef glTextureParameteri
#define glTextureParameteri(...) CHECKED_GL_FUNCTION(g_glTextureParameteri, __VA_ARGS__)
#undef glTextureParameterf
#define glTextureParameterf(...) CHECKED_GL_FUNCTION(g_glTextureParameterf, __VA_ARGS__)
#undef glCreateTextures
#define glCreateTextures(...) CHECKED_GL_FUNCTION(g_glCreateTextures, __VA_ARGS__)
#undef glCreateBuffers
#define glCreateBuffers(...) CHECKED_GL_FUNCTION(g_glCreateBuffers, __VA_ARGS__)
#undef glCreateFramebuffers
#define glCreateFramebuffers(...) CHECKED_GL_FUNCTION(g_glCreateFramebuffers, __VA_ARGS__)
#undef glNamedFramebufferTexture
#define glNamedFramebufferTexture(...) CHECKED_GL_FUNCTION(g_glNamedFramebufferTexture, __VA_ARGS__)
#undef glDrawRangeElementsBaseVertex
#define glDrawRangeElementsBaseVertex(...) CHECKED_GL_FUNCTION(g_glDrawRangeElementsBaseVertex, __VA_ARGS__)
#undef glFlushMappedBufferRange
#define glFlushMappedBufferRange(...) CHECKED_GL_FUNCTION(g_glFlushMappedBufferRange, __VA_ARGS__)
#undef glTextureBarrier
#define glTextureBarrier(...) CHECKED_GL_FUNCTION(g_glTextureBarrier, __VA_ARGS__)
#undef glTextureBarrierNV
#define glTextureBarrierNV(...) CHECKED_GL_FUNCTION(g_glTextureBarrierNV, __VA_ARGS__)
#undef glClearBufferfv
#define glClearBufferfv(...) CHECKED_GL_FUNCTION(g_glClearBufferfv, __VA_ARGS__)
#undef glEnablei
#define glEnablei(...) CHECKED_GL_FUNCTION(g_glEnablei, __VA_ARGS__)
#undef glDisablei
#define glDisablei(...) CHECKED_GL_FUNCTION(g_glDisablei, __VA_ARGS__)
#undef glEGLImageTargetTexture2DOES
#define glEGLImageTargetTexture2DOES(...) CHECKED_GL_FUNCTION(g_glEGLImageTargetTexture2DOES, __VA_ARGS__)

extern PFNGLCREATESHADERPROC g_glCreateShader;
extern PFNGLCOMPILESHADERPROC g_glCompileShader;
extern PFNGLSHADERSOURCEPROC g_glShaderSource;
extern PFNGLCREATEPROGRAMPROC g_glCreateProgram;
extern PFNGLATTACHSHADERPROC g_glAttachShader;
extern PFNGLLINKPROGRAMPROC g_glLinkProgram;
extern PFNGLUSEPROGRAMPROC g_glUseProgram;
extern PFNGLGETUNIFORMLOCATIONPROC g_glGetUniformLocation;
extern PFNGLUNIFORM1IPROC g_glUniform1i;
extern PFNGLUNIFORM1FPROC g_glUniform1f;
extern PFNGLUNIFORM2FPROC g_glUniform2f;
extern PFNGLUNIFORM2IPROC g_glUniform2i;
extern PFNGLUNIFORM4IPROC g_glUniform4i;

extern PFNGLUNIFORM4FPROC g_glUniform4f;
extern PFNGLUNIFORM3FVPROC g_glUniform3fv;
extern PFNGLUNIFORM4FVPROC g_glUniform4fv;
extern PFNGLDETACHSHADERPROC g_glDetachShader;
extern PFNGLDELETESHADERPROC g_glDeleteShader;
extern PFNGLDELETEPROGRAMPROC g_glDeleteProgram;
extern PFNGLGETPROGRAMINFOLOGPROC g_glGetProgramInfoLog;
extern PFNGLGETSHADERINFOLOGPROC g_glGetShaderInfoLog;
extern PFNGLGETSHADERIVPROC g_glGetShaderiv;
extern PFNGLGETPROGRAMIVPROC g_glGetProgramiv;

extern PFNGLENABLEVERTEXATTRIBARRAYPROC g_glEnableVertexAttribArray;
extern PFNGLDISABLEVERTEXATTRIBARRAYPROC g_glDisableVertexAttribArray;
extern PFNGLVERTEXATTRIBPOINTERPROC g_glVertexAttribPointer;
extern PFNGLBINDATTRIBLOCATIONPROC g_glBindAttribLocation;
extern PFNGLVERTEXATTRIB1FPROC g_glVertexAttrib1f;
extern PFNGLVERTEXATTRIB4FPROC g_glVertexAttrib4f;
extern PFNGLVERTEXATTRIB4FVPROC g_glVertexAttrib4fv;

extern PFNGLDEPTHRANGEFPROC g_glDepthRangef;
extern PFNGLCLEARDEPTHFPROC g_glClearDepthf;

extern PFNGLDRAWBUFFERSPROC g_glDrawBuffers;
extern PFNGLGENFRAMEBUFFERSPROC g_glGenFramebuffers;
extern PFNGLBINDFRAMEBUFFERPROC g_glBindFramebuffer;
extern PFNGLDELETEFRAMEBUFFERSPROC g_glDeleteFramebuffers;
extern PFNGLFRAMEBUFFERTEXTURE2DPROC g_glFramebufferTexture2D;
extern PFNGLTEXIMAGE2DMULTISAMPLEPROC g_glTexImage2DMultisample;
extern PFNGLTEXSTORAGE2DMULTISAMPLEPROC g_glTexStorage2DMultisample;
extern PFNGLGENRENDERBUFFERSPROC g_glGenRenderbuffers;
extern PFNGLBINDRENDERBUFFERPROC g_glBindRenderbuffer;
extern PFNGLRENDERBUFFERSTORAGEPROC g_glRenderbufferStorage;
extern PFNGLDELETERENDERBUFFERSPROC g_glDeleteRenderbuffers;
extern PFNGLFRAMEBUFFERRENDERBUFFERPROC g_glFramebufferRenderbuffer;
extern PFNGLCHECKFRAMEBUFFERSTATUSPROC g_glCheckFramebufferStatus;
extern PFNGLBLITFRAMEBUFFERPROC g_glBlitFramebuffer;
extern PFNGLGENVERTEXARRAYSPROC g_glGenVertexArrays;
extern PFNGLBINDVERTEXARRAYPROC g_glBindVertexArray;
extern PFNGLDELETEVERTEXARRAYSPROC g_glDeleteVertexArrays;
extern PFNGLGENBUFFERSPROC g_glGenBuffers;
extern PFNGLBINDBUFFERPROC g_glBindBuffer;
extern PFNGLBUFFERDATAPROC g_glBufferData;
extern PFNGLMAPBUFFERPROC g_glMapBuffer;
extern PFNGLMAPBUFFERRANGEPROC g_glMapBufferRange;
extern PFNGLUNMAPBUFFERPROC g_glUnmapBuffer;
extern PFNGLDELETEBUFFERSPROC g_glDeleteBuffers;
extern PFNGLBINDIMAGETEXTUREPROC g_glBindImageTexture;
extern PFNGLMEMORYBARRIERPROC g_glMemoryBarrier;
extern PFNGLGETSTRINGIPROC g_glGetStringi;
extern PFNGLINVALIDATEFRAMEBUFFERPROC g_glInvalidateFramebuffer;
extern PFNGLBUFFERSTORAGEPROC g_glBufferStorage;
extern PFNGLFENCESYNCPROC g_glFenceSync;
extern PFNGLCLIENTWAITSYNCPROC g_glClientWaitSync;
extern PFNGLDELETESYNCPROC g_glDeleteSync;

extern PFNGLGETUNIFORMBLOCKINDEXPROC g_glGetUniformBlockIndex;
extern PFNGLUNIFORMBLOCKBINDINGPROC g_glUniformBlockBinding;
extern PFNGLGETACTIVEUNIFORMBLOCKIVPROC g_glGetActiveUniformBlockiv;
extern PFNGLGETUNIFORMINDICESPROC g_glGetUniformIndices;
extern PFNGLGETACTIVEUNIFORMSIVPROC g_glGetActiveUniformsiv;
extern PFNGLBINDBUFFERBASEPROC g_glBindBufferBase;
extern PFNGLBUFFERSUBDATAPROC g_glBufferSubData;

extern PFNGLGETPROGRAMBINARYPROC g_glGetProgramBinary;
extern PFNGLPROGRAMBINARYPROC g_glProgramBinary;
extern PFNGLPROGRAMPARAMETERIPROC g_glProgramParameteri;

extern PFNGLTEXSTORAGE2DPROC g_glTexStorage2D;
extern PFNGLTEXTURESTORAGE2DPROC g_glTextureStorage2D;
extern PFNGLTEXTURESUBIMAGE2DPROC g_glTextureSubImage2D;
extern PFNGLTEXTURESTORAGE2DMULTISAMPLEEXTPROC g_glTextureStorage2DMultisample;
extern PFNGLTEXTUREPARAMETERIPROC g_glTextureParameteri;
extern PFNGLTEXTUREPARAMETERFPROC g_glTextureParameterf;
extern PFNGLCREATETEXTURESPROC g_glCreateTextures;
extern PFNGLCREATEBUFFERSPROC g_glCreateBuffers;
extern PFNGLCREATEFRAMEBUFFERSPROC g_glCreateFramebuffers;
extern PFNGLNAMEDFRAMEBUFFERTEXTUREPROC g_glNamedFramebufferTexture;
extern PFNGLDRAWRANGEELEMENTSBASEVERTEXPROC g_glDrawRangeElementsBaseVertex;
extern PFNGLFLUSHMAPPEDBUFFERRANGEPROC g_glFlushMappedBufferRange;
extern PFNGLTEXTUREBARRIERPROC g_glTextureBarrier;
extern PFNGLTEXTUREBARRIERNVPROC g_glTextureBarrierNV;
extern PFNGLCLEARBUFFERFVPROC g_glClearBufferfv;
extern PFNGLENABLEIPROC g_glEnablei;
extern PFNGLDISABLEIPROC g_glDisablei;

typedef void (APIENTRYP PFNGLEGLIMAGETARGETTEXTURE2DOESPROC) (GLenum target, void* image);
extern PFNGLEGLIMAGETARGETTEXTURE2DOESPROC g_glEGLImageTargetTexture2DOES;

extern "C" void initGLFunctions();

template<typename F> void checked(F fn, const char* _functionName)
{
	fn();
	auto error = glGetError();
	if (error != GL_NO_ERROR) {
		std::stringstream errorString;
		errorString << _functionName << " OpenGL error: 0x" << std::hex << error;
		printf("%s\n", errorString.str().c_str());
		throw std::runtime_error(errorString.str().c_str());
	}
}

template<typename R, typename F> R checkedWithReturn(F fn, const char* _functionName)
{
	R returnValue = fn();
	auto error = glGetError();
	if (error != GL_NO_ERROR) {
		std::stringstream errorString;
		errorString << _functionName << " OpenGL error: 0x" << std::hex << error;
		printf("%s\n", errorString.str().c_str());
		throw std::runtime_error(errorString.str().c_str());
	}

	return returnValue;
}

#endif // GLFUNCTIONS_H
