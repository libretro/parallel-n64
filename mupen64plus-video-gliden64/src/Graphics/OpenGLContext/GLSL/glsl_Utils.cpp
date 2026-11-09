#include <assert.h>
#include <Graphics/OpenGLContext/opengl_Attributes.h>
#include "glsl_Utils.h"

using namespace glsl;

void Utils::locateAttributes(GLuint _program, bool _rect, bool _textures)
{
	if (_rect) {
		glBindAttribLocation(_program, opengl::rectAttrib::position, "aRectPosition");
		if (_textures) {
			glBindAttribLocation(_program, opengl::rectAttrib::texcoord0, "aTexCoord0");
			glBindAttribLocation(_program, opengl::rectAttrib::texcoord1, "aTexCoord1");
		}
		return;
	}

	glBindAttribLocation(_program, opengl::triangleAttrib::position, "aPosition");
	glBindAttribLocation(_program, opengl::triangleAttrib::color, "aColor");
	glBindAttribLocation(_program, opengl::triangleAttrib::numlights, "aNumLights");
	glBindAttribLocation(_program, opengl::triangleAttrib::modify, "aModify");
	if (_textures)
		glBindAttribLocation(_program, opengl::triangleAttrib::texcoord, "aTexCoord");
}


bool Utils::checkShaderCompileStatus(GLuint obj)
{
#ifdef GL_DEBUG
	GLint status;
	glGetShaderiv(obj, GL_COMPILE_STATUS, &status);
	if (status == GL_FALSE)
		return false;
#endif
	return true;
}

static
bool _checkProgramLinkStatus(GLuint obj)
{
	GLint status;
	glGetProgramiv(obj, GL_LINK_STATUS, &status);
	return status != GL_FALSE;
}

bool Utils::checkProgramLinkStatus(GLuint obj, bool _force)
{
#ifdef GL_DEBUG
	return _checkProgramLinkStatus(obj);
#else
	if (_force)
		return _checkProgramLinkStatus(obj);
	return true;
#endif
}

GLuint Utils::createRectShaderProgram(const char * _strVertex, const char * _strFragment)
{
	GLuint vertex_shader_object = glCreateShader(GL_VERTEX_SHADER);
	glShaderSource(vertex_shader_object, 1, &_strVertex, nullptr);
	glCompileShader(vertex_shader_object);
	assert(checkShaderCompileStatus(vertex_shader_object));

	GLuint fragment_shader_object = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(fragment_shader_object, 1, &_strFragment, nullptr);
	glCompileShader(fragment_shader_object);
	assert(checkShaderCompileStatus(fragment_shader_object));

	GLuint program = glCreateProgram();
	locateAttributes(program, true, true);
	glAttachShader(program, vertex_shader_object);
	glAttachShader(program, fragment_shader_object);
	glLinkProgram(program);
	glDeleteShader(vertex_shader_object);
	glDeleteShader(fragment_shader_object);
	assert(checkProgramLinkStatus(program));
	return program;
}
