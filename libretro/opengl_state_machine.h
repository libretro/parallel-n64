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

void sglBlendFunc(GLenum sfactor, GLenum dfactor);
void sglClearColor(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha);
void sglClearDepth(GLdouble value);
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

#ifndef NO_TRANSLATE
#define glEnable(T) sglEnable(S##T)
#define glDisable(T) sglDisable(S##T)
#define glIsEnabled(T) sglIsEnabled(S##T)

#define glEnableVertexAttribArray sglEnableVertexAttribArray
#define glDisableVertexAttribArray sglDisableVertexAttribArray
#define glVertexAttribPointer sglVertexAttribPointer

#define glBlendFunc sglBlendFunc
#define glClearColor sglClearColor
#define glClearDepth sglClearDepth
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
#define glBindTexture sglBindTexture

#endif

#ifdef __cplusplus
}
#endif

#endif