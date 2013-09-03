#ifndef OPENGL_STATE_MACHINE_H__
#define OPENGL_STATE_MACHINE_H__

#ifdef __cplusplus
extern "C" {
#endif

void sglEnter();
void sglExit();

enum
{
    SGL_TEXTURE_2D, SGL_DEPTH_TEST, SGL_BLEND, SGL_POLYGON_OFFSET_FILL, SGL_CULL_FACE, SGL_SCISSOR_TEST, SGL_CAP_MAX
};

void sglEnable(GLenum cap);
void sglDisable(GLenum cap);
GLboolean sglIsEnabled(GLenum cap);

void sglEnableVertexAttribArray(GLuint index);
void sglDisableVertexAttribArray(GLuint index);
void sglVertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalize, GLsizei stride, const GLvoid* pointer);
void sglVertexAttrib4f(GLuint index, GLfloat x, GLfloat y, GLfloat z, GLfloat w);
void sglVertexAttrib4fv(GLuint index, GLfloat* v);

void sglBindFramebuffer(GLenum target, GLuint framebuffer);
void sglBlendFunc(GLenum sfactor, GLenum dfactor);
void sglBlendFuncSeparate(GLenum srcRGB, GLenum dstRGB, GLenum srcAlpha, GLenum dstAlpha);
void sglClearColor(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha);
void sglClearDepth(GLdouble value);
void sglColorMask(GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha);
void sglCullFace(GLenum mode);
void sglDepthFunc(GLenum func);
void sglDepthMask(GLboolean flag);
void sglDepthRange(GLclampd nearVal, GLclampd farVal);
void sglFrontFace(GLenum mode);
void sglPolygonOffset(GLfloat factor, GLfloat units);
void sglScissor(GLint x, GLint y, GLsizei width, GLsizei height);
void sglUseProgram(GLuint program);
void sglViewport(GLint x, GLint y, GLsizei width, GLsizei height);

void sglActiveTexture(GLenum texture);
void sglBindTexture(GLenum target, GLuint texture);

// For gles2glide64
void sglBindTextureGlide(GLenum target, GLuint texture);
void sglDeleteTexturesGlide(GLuint n, const GLuint* ids);
GLuint sglAddTextureMap(unsigned address);

#ifndef NO_TRANSLATE
#define glEnable(T) sglEnable(S##T)
#define glDisable(T) sglDisable(S##T)
#define glIsEnabled(T) sglIsEnabled(S##T)

#define glEnableVertexAttribArray sglEnableVertexAttribArray
#define glDisableVertexAttribArray sglDisableVertexAttribArray
#define glVertexAttribPointer sglVertexAttribPointer
#define glVertexAttrib4f sglVertexAttrib4f
#define glVertexAttrib4fv sglVertexAttrib4fv

#define glBindFramebuffer sglBindFramebuffer
#define glBlendFunc sglBlendFunc
#define glBlendFuncSeparate sglBlendFuncSeparate
#define glClearColor sglClearColor
#define glClearDepth sglClearDepth
#define glColorMask sglColorMask
#define glCullFace sglCullFace
#define glDepthFunc sglDepthFunc
#define glDepthMask sglDepthMask
#define glDepthRange sglDepthRange
#define glFrontFace sglFrontFace
#define glPolygonOffset sglPolygonOffset
#define glScissor sglScissor
#define glUseProgram sglUseProgram
#define glViewport sglViewport

#define glActiveTexture sglActiveTexture

#ifdef GLIDE64
#define glBindTexture sglBindTextureGlide
#define glDeleteTextures sglDeleteTexturesGlide
#else
#define glBindTexture sglBindTexture
#endif

#endif

#ifdef __cplusplus
}
#endif

#endif
