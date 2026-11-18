#pragma once

#include "GLFunctions.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Whether the context has the extension @_extension: by its list on GL 3
 * and later (@_majorVersion), else by its extension string */
int gli_gl_has_extension(GLint _majorVersion, const char *_extension);

/* Whether GL has an error, or the bound frame buffer is incomplete: on
 * GL_DEBUG only, else never */
int gli_gl_has_error(void);
int gli_gl_framebuffer_incomplete(void);

#ifdef __cplusplus
}

#include "opengl_GLInfo.h"

namespace opengl {

	struct Utils
	{
		static bool isExtensionSupported(const opengl::GLInfo & _glinfo, const char * extension)
		{
			return gli_gl_has_extension(_glinfo.majorVersion, extension) != 0;
		}
		static bool isGLError() { return gli_gl_has_error() != 0; }
		static bool isFramebufferError() { return gli_gl_framebuffer_incomplete() != 0; }
	};

}
#endif
