#include <assert.h>
#include <Types.h>
#include "opengl_Utils.h"
#include "GLFunctions.h"
#include <cstring>

using namespace opengl;

bool Utils::isExtensionSupported(const opengl::GLInfo & _glinfo, const char *extension) {
	if (_glinfo.majorVersion >= 3) {
		GLint count = 0;
		glGetIntegerv(GL_NUM_EXTENSIONS, &count);
		assert(count >= 0);
		for (GLuint i = 0; i < (GLuint)count; ++i) {
			const char* name = (const char*)glGetStringi(GL_EXTENSIONS, i);
			if (name == nullptr)
				continue;
			if (strcmp(extension, name) == 0)
				return true;
		}
		return false;
	}

	GLubyte *where = (GLubyte *)strchr(extension, ' ');
	if (where || *extension == '\0')
		return false;

	const GLubyte *extensions = glGetString(GL_EXTENSIONS);
	if (extensions == nullptr) {
		return false;
	}

	const GLubyte *start = extensions;
	for (;;) {
		where = (GLubyte *)strstr((const char *)start, extension);
		if (where == nullptr)
			break;

		GLubyte *terminator = where + strlen(extension);
		if (where == start || *(where - 1) == ' ') if (*terminator == ' ' || *terminator == '\0')
			return true;

		start = terminator;
	}

	return false;
}


bool Utils::isGLError()
{
#ifdef GL_DEBUG
	if (glGetError() != GL_NO_ERROR)
		return true;
#endif
	return false;
}

bool Utils::isFramebufferError()
{
#ifdef GL_DEBUG
	GLenum e = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	return e != GL_FRAMEBUFFER_COMPLETE;
#else
	return false;
#endif
}
