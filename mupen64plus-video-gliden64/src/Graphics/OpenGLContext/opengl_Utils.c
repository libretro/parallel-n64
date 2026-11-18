/* GL's extensions and errors (opengl_Utils.h). C89. */
#include <assert.h>
#include <string.h>
#include "opengl_Utils.h"

int gli_gl_has_extension(GLint _majorVersion, const char *_extension)
{
	const GLubyte *extensions;
	const GLubyte *start;
	if (_majorVersion >= 3) {
		GLint count = 0;
		GLuint i;
		glGetIntegerv(GL_NUM_EXTENSIONS, &count);
		assert(count >= 0);
		for (i = 0; i < (GLuint)count; ++i) {
			const char *name = (const char*)glGetStringi(GL_EXTENSIONS, i);
			if (name == NULL)
				continue;
			if (strcmp(_extension, name) == 0)
				return 1;
		}
		return 0;
	}

	/* one name, not a list of them */
	if (strchr(_extension, ' ') != NULL || *_extension == '\0')
		return 0;

	extensions = glGetString(GL_EXTENSIONS);
	if (extensions == NULL)
		return 0;

	start = extensions;
	for (;;) {
		const GLubyte *terminator;
		const GLubyte *where = (const GLubyte *)strstr((const char *)start, _extension);
		if (where == NULL)
			break;

		terminator = where + strlen(_extension);
		if ((where == start || *(where - 1) == ' ') && (*terminator == ' ' || *terminator == '\0'))
			return 1;

		start = terminator;
	}

	return 0;
}

int gli_gl_has_error(void)
{
#ifdef GL_DEBUG
	if (glGetError() != GL_NO_ERROR)
		return 1;
#endif
	return 0;
}

int gli_gl_framebuffer_incomplete(void)
{
#ifdef GL_DEBUG
	return glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE;
#else
	return 0;
#endif
}
