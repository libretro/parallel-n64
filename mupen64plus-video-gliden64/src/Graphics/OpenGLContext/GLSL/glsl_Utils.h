#ifndef GLI_GLSL_UTILS_H
#define GLI_GLSL_UTILS_H
/* GLSL program helpers (glsl_Utils.c). C89. */
#include <Graphics/OpenGLContext/GLFunctions.h>

#ifdef __cplusplus
extern "C" {
#endif

/* a triangle's or a rect's attributes bound to their locations, the
 * texture coordinates only for a program that reads a texture */
void gli_glsl_locate_attributes(GLuint _program, int _rect, int _textures);
/* whether a shader compiled: asked under GL_DEBUG, else taken as so */
int gli_glsl_compile_ok(GLuint _shader);
/* whether a program linked: asked under GL_DEBUG or when @_force, else
 * taken as so, as asking waits for the driver */
int gli_glsl_link_ok(GLuint _program, int _force);
/* a rect program, texture coordinates bound, from its two sources, each
 * given as the @_vertexCount and @_fragmentCount strings it is made of */
GLuint gli_glsl_rect_program(const GLchar *const *_vertex, GLsizei _vertexCount,
	const GLchar *const *_fragment, GLsizei _fragmentCount);

#ifdef __cplusplus
}
#endif

#endif
