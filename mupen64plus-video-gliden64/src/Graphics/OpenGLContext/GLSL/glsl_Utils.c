/* GLSL program helpers: attribute locations, compile and link checks, a
 * rect program from its two sources. C89. */
#include <assert.h>
#include <stddef.h>
#include <Graphics/OpenGLContext/opengl_Attributes.h>
#include "glsl_Utils.h"

void gli_glsl_locate_attributes(GLuint _program, int _rect, int _textures)
{
	if (_rect) {
		glBindAttribLocation(_program, GLI_RECT_ATTRIB_POSITION, "aRectPosition");
		if (_textures) {
			glBindAttribLocation(_program, GLI_RECT_ATTRIB_TEXCOORD0, "aTexCoord0");
			glBindAttribLocation(_program, GLI_RECT_ATTRIB_TEXCOORD1, "aTexCoord1");
		}
		return;
	}

	glBindAttribLocation(_program, GLI_TRI_ATTRIB_POSITION, "aPosition");
	glBindAttribLocation(_program, GLI_TRI_ATTRIB_COLOR, "aColor");
	glBindAttribLocation(_program, GLI_TRI_ATTRIB_NUMLIGHTS, "aNumLights");
	glBindAttribLocation(_program, GLI_TRI_ATTRIB_MODIFY, "aModify");
	if (_textures)
		glBindAttribLocation(_program, GLI_TRI_ATTRIB_TEXCOORD, "aTexCoord");
}

int gli_glsl_compile_ok(GLuint _shader)
{
#ifdef GL_DEBUG
	GLint status;
	glGetShaderiv(_shader, GL_COMPILE_STATUS, &status);
	if (status == GL_FALSE)
		return 0;
#else
	(void)_shader;
#endif
	return 1;
}

static int link_status(GLuint _program)
{
	GLint status;
	glGetProgramiv(_program, GL_LINK_STATUS, &status);
	return status != GL_FALSE;
}

int gli_glsl_link_ok(GLuint _program, int _force)
{
#ifdef GL_DEBUG
	(void)_force;
	return link_status(_program);
#else
	if (_force)
		return link_status(_program);
	return 1;
#endif
}

GLuint gli_glsl_rect_program(const char *_strVertex, const char *_strFragment)
{
	GLuint vertex_shader_object = glCreateShader(GL_VERTEX_SHADER);
	GLuint fragment_shader_object;
	GLuint program;
	glShaderSource(vertex_shader_object, 1, &_strVertex, NULL);
	glCompileShader(vertex_shader_object);
	assert(gli_glsl_compile_ok(vertex_shader_object));

	fragment_shader_object = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(fragment_shader_object, 1, &_strFragment, NULL);
	glCompileShader(fragment_shader_object);
	assert(gli_glsl_compile_ok(fragment_shader_object));

	program = glCreateProgram();
	gli_glsl_locate_attributes(program, 1, 1);
	glAttachShader(program, vertex_shader_object);
	glAttachShader(program, fragment_shader_object);
	glLinkProgram(program);
	glDeleteShader(vertex_shader_object);
	glDeleteShader(fragment_shader_object);
	assert(gli_glsl_link_ok(program, 0));
	return program;
}
