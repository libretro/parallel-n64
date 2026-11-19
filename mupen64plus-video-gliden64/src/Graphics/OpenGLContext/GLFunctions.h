#ifndef GLFUNCTIONS_H
#define GLFUNCTIONS_H

#ifdef OS_WINDOWS
#include <windows.h>
/* #define FORCE_UNBUFFERED_DRAWER: a debug option */
#elif defined(OS_LINUX)
#include <winlnxdefs.h>
#endif

#ifdef GL_GLEXT_PROTOTYPES
#undef GL_GLEXT_PROTOTYPES
#endif /* GL_GLEXT_PROTOTYPES */

#if defined(HAVE_OPENGLES3)
/* Desktop <GL/gl.h> must stay out: it declares glEnable and friends as real
 * functions, and glsmsym.h has already turned those names into macros.
 * glcorearb.h has PFNGL*PROC typedefs this file needs and declares
 * nothing itself.  The GLES headers have to arrive here, before the
 * glX -> g_glX macros below, or their prototypes expand into bogus
 * declarations. */
#include <glsym/rglgen_headers.h>
#include <GL/glcorearb.h>
#elif defined(OS_MAC_OS_X)
#include <OpenGL/OpenGL.h>
#include <stddef.h>
#include <OpenGL/gl3.h>

/* This is going to blow up so badly */
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
/* Add missing type defintions for iOS */
typedef double GLclampd;
typedef double GLdouble;
/* These will get redefined by other GL headers. */
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
#include <string.h>

#include <glsm/glsm_caps.h>

#if !defined(OS_IOS)
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

#define IS_GL_FUNCTION_VALID(proc_name) (g_##proc_name != NULL)
#define GET_GL_FUNCTION(proc_name) g_##proc_name

/* Each GL call is its function pointer's, called */
#ifdef __cplusplus
extern "C" {
#endif

#undef glGetError
#define glGetError g_glGetError
#undef glBlendFunc
#define glBlendFunc g_glBlendFunc
#undef glBlendFuncSeparate
#define glBlendFuncSeparate g_glBlendFuncSeparate
#undef glPixelStorei
#define glPixelStorei g_glPixelStorei
#undef glClearColor
#define glClearColor g_glClearColor
#undef glCullFace
#define glCullFace g_glCullFace
#undef glDepthFunc
#define glDepthFunc g_glDepthFunc
#undef glDepthMask
#define glDepthMask g_glDepthMask
#undef glDisable
#define glDisable(v) g_glDisable(S ## v)
#undef glEnable
#define glEnable(v) g_glEnable(S ## v)
#undef glPolygonOffset
#define glPolygonOffset g_glPolygonOffset
#undef glScissor
#define glScissor g_glScissor
#undef glViewport
#define glViewport g_glViewport
#undef glBindTexture
#define glBindTexture g_glBindTexture
#undef glTexImage2D
#define glTexImage2D g_glTexImage2D
#undef glTexParameteri
#define glTexParameteri g_glTexParameteri
#undef glGetIntegerv
#define glGetIntegerv g_glGetIntegerv
#undef glGetString
#define glGetString g_glGetString
#undef glReadPixels
#define glReadPixels g_glReadPixels
#undef glTexSubImage2D
#define glTexSubImage2D g_glTexSubImage2D
#undef glDrawArrays
#define glDrawArrays g_glDrawArrays
#undef glDrawElements
#define glDrawElements g_glDrawElements
#undef glLineWidth
#define glLineWidth g_glLineWidth
#undef glClear
#define glClear g_glClear
#undef glGetFloatv
#define glGetFloatv g_glGetFloatv
#undef glDeleteTextures
#define glDeleteTextures g_glDeleteTextures
#undef glGenTextures
#define glGenTextures g_glGenTextures
#undef glTexParameterf
#define glTexParameterf g_glTexParameterf
#undef glActiveTexture
#define glActiveTexture g_glActiveTexture
#undef glBlendColor
#define glBlendColor g_glBlendColor
#undef glReadBuffer
#define glReadBuffer g_glReadBuffer
#undef glFinish
#define glFinish g_glFinish
#if defined(OS_ANDROID)
#endif

extern PFNGLBLENDFUNCPROC g_glBlendFunc;
extern PFNGLBLENDFUNCSEPARATEPROC g_glBlendFuncSeparate;
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
#endif

#undef glCreateShader
#define glCreateShader g_glCreateShader
#undef glCompileShader
#define glCompileShader g_glCompileShader
#undef glShaderSource
#define glShaderSource g_glShaderSource
#undef glCreateProgram
#define glCreateProgram g_glCreateProgram
#undef glAttachShader
#define glAttachShader g_glAttachShader
#undef glLinkProgram
#define glLinkProgram g_glLinkProgram
#undef glUseProgram
#define glUseProgram g_glUseProgram
#undef glGetUniformLocation
#define glGetUniformLocation g_glGetUniformLocation
#undef glUniform1i
#define glUniform1i g_glUniform1i
#undef glUniform1f
#define glUniform1f g_glUniform1f
#undef glUniform2f
#define glUniform2f g_glUniform2f
#undef glUniform2i
#define glUniform2i g_glUniform2i
#undef glUniform4i
#define glUniform4i g_glUniform4i

#undef glUniform4f
#define glUniform4f g_glUniform4f
#undef glUniform3fv
#define glUniform3fv g_glUniform3fv
#undef glUniform4fv
#define glUniform4fv g_glUniform4fv
#undef glDetachShader
#define glDetachShader g_glDetachShader
#undef glDeleteShader
#define glDeleteShader g_glDeleteShader
#undef glDeleteProgram
#define glDeleteProgram g_glDeleteProgram
#undef glGetProgramInfoLog
#define glGetProgramInfoLog g_glGetProgramInfoLog
#undef glGetShaderInfoLog
#define glGetShaderInfoLog g_glGetShaderInfoLog
#undef glGetShaderiv
#define glGetShaderiv g_glGetShaderiv
#undef glGetProgramiv
#define glGetProgramiv g_glGetProgramiv

#undef glEnableVertexAttribArray
#define glEnableVertexAttribArray g_glEnableVertexAttribArray
#undef glDisableVertexAttribArray
#define glDisableVertexAttribArray g_glDisableVertexAttribArray
#undef glVertexAttribPointer
#define glVertexAttribPointer g_glVertexAttribPointer
#undef glBindAttribLocation
#define glBindAttribLocation g_glBindAttribLocation
#undef glVertexAttrib1f
#define glVertexAttrib1f g_glVertexAttrib1f
#undef glVertexAttrib4f
#define glVertexAttrib4f g_glVertexAttrib4f
#undef glVertexAttrib4fv
#define glVertexAttrib4fv g_glVertexAttrib4fv

#undef glDepthRangef
#define glDepthRangef g_glDepthRangef
#undef glClearDepthf
#define glClearDepthf g_glClearDepthf

#undef glBindBuffer
#define glBindBuffer g_glBindBuffer
#undef glBindFramebuffer
#define glBindFramebuffer g_glBindFramebuffer
#undef glBindRenderbuffer
#define glBindRenderbuffer g_glBindRenderbuffer
#undef glDrawBuffers
#define glDrawBuffers g_glDrawBuffers
#undef glGenFramebuffers
#define glGenFramebuffers g_glGenFramebuffers
#undef glDeleteFramebuffers
#define glDeleteFramebuffers g_glDeleteFramebuffers
#undef glFramebufferTexture2D
#define glFramebufferTexture2D g_glFramebufferTexture2D
#undef glTexImage2DMultisample
#define glTexImage2DMultisample g_glTexImage2DMultisample
#undef glTexStorage2DMultisample
#define glTexStorage2DMultisample g_glTexStorage2DMultisample
#undef glGenRenderbuffers
#define glGenRenderbuffers g_glGenRenderbuffers
#undef glRenderbufferStorage
#define glRenderbufferStorage g_glRenderbufferStorage
#undef glDeleteRenderbuffers
#define glDeleteRenderbuffers g_glDeleteRenderbuffers
#undef glFramebufferRenderbuffer
#define glFramebufferRenderbuffer g_glFramebufferRenderbuffer
#undef glCheckFramebufferStatus
#define glCheckFramebufferStatus g_glCheckFramebufferStatus
#undef glBlitFramebuffer
#define glBlitFramebuffer g_glBlitFramebuffer
#undef glGenVertexArrays
#define glGenVertexArrays g_glGenVertexArrays
#undef glBindVertexArray
#define glBindVertexArray g_glBindVertexArray
#undef glDeleteVertexArrays
#define glDeleteVertexArrays g_glDeleteVertexArrays
#undef glGenBuffers
#define glGenBuffers g_glGenBuffers
#undef glBufferData
#define glBufferData g_glBufferData
#undef glMapBuffer
#define glMapBuffer g_glMapBuffer
#undef glMapBufferRange
#define glMapBufferRange g_glMapBufferRange
#undef glUnmapBuffer
#define glUnmapBuffer g_glUnmapBuffer
#undef glDeleteBuffers
#define glDeleteBuffers g_glDeleteBuffers
#undef glBindImageTexture
#define glBindImageTexture g_glBindImageTexture
#undef glMemoryBarrier
#define glMemoryBarrier g_glMemoryBarrier
#undef glGetStringi
#define glGetStringi g_glGetStringi
#undef glInvalidateFramebuffer
#define glInvalidateFramebuffer g_glInvalidateFramebuffer
#undef glBufferStorage
#define glBufferStorage g_glBufferStorage
#undef glFenceSync
#define glFenceSync g_glFenceSync
#undef glClientWaitSync
#define glClientWaitSync g_glClientWaitSync
#undef glDeleteSync
#define glDeleteSync g_glDeleteSync

#undef glGetUniformBlockIndex
#define glGetUniformBlockIndex g_glGetUniformBlockIndex
#undef glUniformBlockBinding
#define glUniformBlockBinding g_glUniformBlockBinding
#undef glGetActiveUniformBlockiv
#define glGetActiveUniformBlockiv g_glGetActiveUniformBlockiv
#undef glGetUniformIndices
#define glGetUniformIndices g_glGetUniformIndices
#undef glGetActiveUniformsiv
#define glGetActiveUniformsiv g_glGetActiveUniformsiv
#undef glBindBufferBase
#define glBindBufferBase g_glBindBufferBase
#undef glBufferSubData
#define glBufferSubData g_glBufferSubData

#undef glGetProgramBinary
#define glGetProgramBinary g_glGetProgramBinary
#undef glProgramBinary
#define glProgramBinary g_glProgramBinary
#undef glProgramParameteri
#define glProgramParameteri g_glProgramParameteri

#undef glTexStorage2D
#define glTexStorage2D g_glTexStorage2D
#undef glTextureStorage2D
#define glTextureStorage2D g_glTextureStorage2D
#undef glTextureSubImage2D
#define glTextureSubImage2D g_glTextureSubImage2D
#undef glTextureStorage2DMultisample
#define glTextureStorage2DMultisample g_glTextureStorage2DMultisample
#undef glTextureParameteri
#define glTextureParameteri g_glTextureParameteri
#undef glTextureParameterf
#define glTextureParameterf g_glTextureParameterf
#undef glCreateTextures
#define glCreateTextures g_glCreateTextures
#undef glCreateBuffers
#define glCreateBuffers g_glCreateBuffers
#undef glCreateFramebuffers
#define glCreateFramebuffers g_glCreateFramebuffers
#undef glNamedFramebufferTexture
#define glNamedFramebufferTexture g_glNamedFramebufferTexture
#undef glDrawRangeElementsBaseVertex
#define glDrawRangeElementsBaseVertex g_glDrawRangeElementsBaseVertex
#undef glFlushMappedBufferRange
#define glFlushMappedBufferRange g_glFlushMappedBufferRange
#undef glTextureBarrier
#define glTextureBarrier g_glTextureBarrier
#undef glTextureBarrierNV
#define glTextureBarrierNV g_glTextureBarrierNV
#undef glClearBufferfv
#define glClearBufferfv g_glClearBufferfv
#undef glEnablei
#define glEnablei g_glEnablei
#undef glDisablei
#define glDisablei g_glDisablei

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


void initGLFunctions(void);

#ifdef __cplusplus
}
#endif


#endif /* GLFUNCTIONS_H */
