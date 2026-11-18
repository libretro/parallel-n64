/* test_gliden64_gl_utils -- whether GLideN64's GL context has an extension
 * (Graphics/OpenGLContext/opengl_Utils.c).
 *
 * On GL 3 and later the context lists its extensions one by one, and a
 * name is had when one of them is it whole. Before, it gives one string of
 * names, a space apart, and a name is had when it is there whole: at the
 * start, between two spaces or at the end, not as the start of a longer
 * name or the end of one. A name with a space in it, or no name, is never
 * had. The GL entry points are stubs here.
 *
 *   ./test_gliden64_gl_utils
 */
#include <stdio.h>
#include <string.h>

#include "opengl_Utils.h"

static int failures;

static void check(int _ok, const char *_what)
{
	if (!_ok) {
		printf("FAIL: %s\n", _what);
		failures++;
	}
}

/* ---- the GL entry points opengl_Utils.c calls ---- */

static const char *list[4];
static GLint listCount;
static const char *string;
static int stringCalls;

static void APIENTRY get_integerv(GLenum _pname, GLint *_data)
{
	if (_pname == GL_NUM_EXTENSIONS)
		*_data = listCount;
}

static const GLubyte *APIENTRY get_stringi(GLenum _name, GLuint _index)
{
	return _name == GL_EXTENSIONS && _index < (GLuint)listCount ? (const GLubyte *)list[_index] : NULL;
}

static const GLubyte *APIENTRY get_string(GLenum _name)
{
	stringCalls++;
	return _name == GL_EXTENSIONS ? (const GLubyte *)string : NULL;
}

PFNGLGETINTEGERVPROC g_glGetIntegerv = get_integerv;
PFNGLGETSTRINGIPROC g_glGetStringi = get_stringi;
PFNGLGETSTRINGPROC g_glGetString = get_string;
PFNGLGETERRORPROC g_glGetError;
PFNGLCHECKFRAMEBUFFERSTATUSPROC g_glCheckFramebufferStatus;

static void test_list(void)
{
	list[0] = "GL_ARB_buffer_storage";
	list[1] = NULL;
	list[2] = "GL_EXT_shader_texture_lod";
	listCount = 3;
	stringCalls = 0;
	check(gli_gl_has_extension(3, "GL_ARB_buffer_storage"), "GL 3: the first name of the list");
	check(gli_gl_has_extension(4, "GL_EXT_shader_texture_lod"), "the last, past a name the context did not give");
	check(!gli_gl_has_extension(3, "GL_ARB_buffer"), "not the start of a name");
	check(!gli_gl_has_extension(3, "GL_ARB_buffer_storage_x"), "not a longer name");
	check(!gli_gl_has_extension(3, "GL_OES_depth_texture"), "not one not listed");
	check(stringCalls == 0, "nor the old string asked for");
	listCount = 0;
	check(!gli_gl_has_extension(3, "GL_ARB_buffer_storage"), "nothing in an empty list");
}

static void test_string(void)
{
	string = "GL_OES_depth_texture GL_OES_rgb8_rgba8 GL_EXT_texture_lod_bias_x GL_KHR_debug";
	check(gli_gl_has_extension(2, "GL_OES_depth_texture"), "GL 2: a name at the start");
	check(gli_gl_has_extension(2, "GL_OES_rgb8_rgba8"), "one between two spaces");
	check(gli_gl_has_extension(2, "GL_KHR_debug"), "one at the end");
	check(!gli_gl_has_extension(2, "GL_OES_depth"), "not the start of a name");
	check(!gli_gl_has_extension(2, "depth_texture"), "nor its end");
	check(gli_gl_has_extension(2, "GL_EXT_texture_lod_bias_x") && !gli_gl_has_extension(2, "GL_EXT_texture_lod_bias"),
		"a name past the start of a longer one is not it");
	check(!gli_gl_has_extension(2, "GL_OES_depth_texture GL_OES_rgb8_rgba8"), "a name with a space is never had");
	check(!gli_gl_has_extension(2, ""), "nor no name");
	string = "GL_EXT_texture_lod_bias_x GL_EXT_texture_lod_bias";
	check(gli_gl_has_extension(2, "GL_EXT_texture_lod_bias"), "but is found whole after it");
	string = NULL;
	check(!gli_gl_has_extension(2, "GL_KHR_debug"), "nor any, with no string");
}

int main(void)
{
	test_list();
	test_string();
	check(!gli_gl_has_error() && !gli_gl_framebuffer_incomplete(), "no GL error or incomplete frame buffer off GL_DEBUG");
	if (failures) {
		printf("%d failed\n", failures);
		return 1;
	}
	printf("gl utils: ok\n");
	return 0;
}
