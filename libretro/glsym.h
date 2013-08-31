#ifndef GLSYM_H__
#define GLSYM_H__

#include "libretro.h"

#define GL_GLEXT_PROTOTYPES
#if defined(GLES)
#ifdef IOS
#include <OpenGLES/ES2/gl.h>
#else
#include <GLES2/gl2.h>
#endif
#elif defined(__APPLE__)
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#else
#include <GL/gl.h>
#include <GL/glext.h>
#endif

#if !defined(GLES) && !defined(__APPLE__)
// Overridden by SGL.
#ifndef glVertexAttribPointer
#define glVertexAttribPointer pglVertexAttribPointer
#endif
#ifndef glEnableVertexAttribArray
#define glEnableVertexAttribArray pglEnableVertexAttribArray
#endif
#ifndef glDisableVertexAttribArray
#define glDisableVertexAttribArray pglDisableVertexAttribArray
#endif
#ifndef glUseProgram
#define glUseProgram pglUseProgram
#endif
#ifndef glActiveTexture
#define glActiveTexture pglActiveTexture
#endif
#ifndef glBindFramebuffer
#define glBindFramebuffer pglBindFramebuffer
#endif

#define glCreateProgram pglCreateProgram
#define glCreateShader pglCreateShader
#define glCompileShader pglCompileShader
#define glShaderSource pglShaderSource
#define glAttachShader pglAttachShader
#define glLinkProgram pglLinkProgram
#define glBindFramebuffer pglBindFramebuffer
#define glGetUniformLocation pglGetUniformLocation
#define glUniformMatrix4fv pglUniformMatrix4fv
#define glUniform1i pglUniform1i
#define glUniform1f pglUniform1f
#define glGetAttribLocation pglGetAttribLocation
#define glGenBuffers pglGenBuffers
#define glBufferData pglBufferData
#define glBindBuffer pglBindBuffer
#define glGetShaderiv pglGetShaderiv
#define glGetShaderInfoLog pglGetShaderInfoLog
#define glBindAttribLocation pglBindAttribLocation
#define glGetProgramiv pglGetProgramiv
#define glGetProgramInfoLog pglGetProgramInfoLog
#define glDeleteShader pglDeleteShader
#define glGetShaderInfoLog pglGetShaderInfoLog
#define glDeleteProgram pglDeleteProgram
#define glUniform3f pglUniform3f
#define glUniform4f pglUniform4f
#define glUniform4fv pglUniform4fv
#define glUniform2f pglUniform2f
#define glGenerateMipmap pglGenerateMipmap

#define glGenFramebuffers pglGenFramebuffers
#define glGenRenderbuffers pglGenRenderbuffers
#define glBindRenderbuffer pglBindRenderbuffer
#define glRenderbufferStorage pglRenderbufferStorage
#define glFramebufferTexture2D pglFramebufferTexture2D
#define glFramebufferRenderbuffer pglFramebufferRenderbuffer
#define glCheckFramebufferStatus pglCheckFramebufferStatus
#define glDeleteFramebuffers pglDeleteFramebuffers
#define glDeleteRenderbuffers pglDeleteRenderbuffers
#define glFramebufferRenderbuffer pglFramebufferRenderbuffer
#define glVertexAttrib4f pglVertexAttrib4f
#define glBlendFuncSeparate pglBlendFuncSeparate
#define glVertexAttrib4fv pglVertexAttrib4fv

extern PFNGLCREATEPROGRAMPROC pglCreateProgram;
extern PFNGLCREATESHADERPROC pglCreateShader;
extern PFNGLCREATESHADERPROC pglCompileShader;
extern PFNGLCREATESHADERPROC pglUseProgram;
extern PFNGLSHADERSOURCEPROC pglShaderSource;
extern PFNGLATTACHSHADERPROC pglAttachShader;
extern PFNGLLINKPROGRAMPROC pglLinkProgram;
extern PFNGLBINDFRAMEBUFFERPROC pglBindFramebuffer;
extern PFNGLGETUNIFORMLOCATIONPROC pglGetUniformLocation;
extern PFNGLUNIFORMMATRIX4FVPROC pglUniformMatrix4fv;
extern PFNGLUNIFORM1IPROC pglUniform1i;
extern PFNGLUNIFORM1FPROC pglUniform1f;
extern PFNGLGETATTRIBLOCATIONPROC pglGetAttribLocation;
extern PFNGLVERTEXATTRIBPOINTERPROC pglVertexAttribPointer;
extern PFNGLENABLEVERTEXATTRIBARRAYPROC pglEnableVertexAttribArray;
extern PFNGLDISABLEVERTEXATTRIBARRAYPROC pglDisableVertexAttribArray;
extern PFNGLGENBUFFERSPROC pglGenBuffers;
extern PFNGLBUFFERDATAPROC pglBufferData;
extern PFNGLBINDBUFFERPROC pglBindBuffer;
extern PFNGLMAPBUFFERRANGEPROC pglMapBufferRange;
extern PFNGLACTIVETEXTUREPROC pglActiveTexture;

extern PFNGLGETSHADERIVPROC pglGetShaderiv;
extern PFNGLGETSHADERINFOLOGPROC pglGetShaderInfoLog;
extern PFNGLBINDATTRIBLOCATIONPROC pglBindAttribLocation;
extern PFNGLGETPROGRAMIVPROC pglGetProgramiv;
extern PFNGLGETPROGRAMINFOLOGPROC pglGetProgramInfoLog;
extern PFNGLDELETESHADERPROC pglDeleteShader;
extern PFNGLGETSHADERINFOLOGPROC pglGetShaderInfoLog;
extern PFNGLDELETEPROGRAMPROC pglDeleteProgram;
extern PFNGLUNIFORM3FPROC pglUniform3f;
extern PFNGLUNIFORM4FPROC pglUniform4f;
extern PFNGLUNIFORM4FVPROC pglUniform4fv;
extern PFNGLUNIFORM2FPROC pglUniform2f;
extern PFNGLGENERATEMIPMAPPROC pglGenerateMipmap;

extern PFNGLGENFRAMEBUFFERSPROC pglGenFramebuffers;
extern PFNGLGENRENDERBUFFERSPROC pglGenRenderbuffers;
extern PFNGLBINDRENDERBUFFERPROC pglBindRenderbuffer;
extern PFNGLRENDERBUFFERSTORAGEPROC pglRenderbufferStorage;
extern PFNGLFRAMEBUFFERTEXTURE2DPROC pglFramebufferTexture2D;
extern PFNGLFRAMEBUFFERRENDERBUFFERPROC pglFramebufferRenderbuffer;
extern PFNGLCHECKFRAMEBUFFERSTATUSPROC pglCheckFramebufferStatus;
extern PFNGLDELETEFRAMEBUFFERSPROC pglDeleteFramebuffers;
extern PFNGLDELETERENDERBUFFERSPROC pglDeleteRenderbuffers;
extern PFNGLFRAMEBUFFERRENDERBUFFERPROC pglFramebufferRenderbuffer;
extern PFNGLVERTEXATTRIB4FPROC pglVertexAttrib4f;
extern PFNGLBLENDFUNCSEPARATEPROC pglBlendFuncSeparate;
extern PFNGLVERTEXATTRIB4FVPROC pglVertexAttrib4fv;

#endif

void glsym_init_procs(retro_hw_get_proc_address_t cb);

#endif

