/* test_gliden64_glsl_utils: GLideN64's GLSL program helpers (glsl_Utils.c)
 * and FXAA's shader text (glsl_FXAA.c) on stub GL calls.
 *
 * The attributes a triangle program and a rect program bind, with and
 * without texture coordinates; compile and link checks, which ask the
 * driver only when forced; a rect program made from its two sources, every
 * call in its order; and FXAA's two shaders, line by line, the same text
 * the C++ held. C89. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Graphics/OpenGLContext/GLFunctions.h"
#include "Graphics/OpenGLContext/opengl_Attributes.h"
#include "Graphics/OpenGLContext/GLSL/glsl_Utils.h"
#include "Graphics/OpenGLContext/GLSL/glsl_FXAA.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

/* every call, as text, one after the other */
static char s_log[4096];
static void logf_(const char *s)
{
	if (strlen(s_log) + strlen(s) + 2 < sizeof(s_log)) {
		strcat(s_log, s);
		strcat(s_log, ";");
	}
}
static void reset(void) { s_log[0] = '\0'; }

static GLint s_link_status = GL_TRUE;
static GLuint s_next = 10;

static void APIENTRY s_bind_attrib(GLuint p, GLuint i, const GLchar *n)
{ char b[96]; sprintf(b, "attr %u %u %s", p, i, n); logf_(b); }
static GLuint APIENTRY s_create_shader(GLenum t)
{ char b[64]; sprintf(b, "shader %s=%u", t == GL_VERTEX_SHADER ? "v" : t == GL_FRAGMENT_SHADER ? "f" : "?", s_next); logf_(b); return s_next++; }
static void APIENTRY s_shader_source(GLuint s, GLsizei n, const GLchar *const *src, const GLint *len)
{
	/* the strings joined by '+', each one a part of the source */
	char b[160];
	GLsizei i;
	sprintf(b, "source %u %d ", s, (int)n);
	for (i = 0; i < n; ++i) {
		if (i > 0)
			strcat(b, "+");
		strcat(b, src[i]);
	}
	strcat(b, len == NULL ? " nul" : " len");
	logf_(b);
}
static void APIENTRY s_compile(GLuint s) { char b[32]; sprintf(b, "compile %u", s); logf_(b); }
static GLuint APIENTRY s_create_program(void) { char b[32]; sprintf(b, "program=%u", s_next); logf_(b); return s_next++; }
static void APIENTRY s_attach(GLuint p, GLuint s) { char b[32]; sprintf(b, "attach %u %u", p, s); logf_(b); }
static void APIENTRY s_link(GLuint p) { char b[32]; sprintf(b, "link %u", p); logf_(b); }
static void APIENTRY s_delete_shader(GLuint s) { char b[32]; sprintf(b, "delshader %u", s); logf_(b); }
static void APIENTRY s_get_programiv(GLuint p, GLenum e, GLint *v)
{ char b[48]; sprintf(b, "getprog %u %s", p, e == GL_LINK_STATUS ? "link" : "?"); logf_(b); *v = s_link_status; }
static void APIENTRY s_get_shaderiv(GLuint s, GLenum e, GLint *v)
{ char b[48]; sprintf(b, "getshader %u %u", s, (unsigned)e); logf_(b); *v = GL_FALSE; }

PFNGLBINDATTRIBLOCATIONPROC g_glBindAttribLocation = s_bind_attrib;
PFNGLCREATESHADERPROC g_glCreateShader = s_create_shader;
PFNGLSHADERSOURCEPROC g_glShaderSource = s_shader_source;
PFNGLCOMPILESHADERPROC g_glCompileShader = s_compile;
PFNGLCREATEPROGRAMPROC g_glCreateProgram = s_create_program;
PFNGLATTACHSHADERPROC g_glAttachShader = s_attach;
PFNGLLINKPROGRAMPROC g_glLinkProgram = s_link;
PFNGLDELETESHADERPROC g_glDeleteShader = s_delete_shader;
PFNGLGETPROGRAMIVPROC g_glGetProgramiv = s_get_programiv;
PFNGLGETSHADERIVPROC g_glGetShaderiv = s_get_shaderiv;

static void attributes(void)
{
	char want[512];

	reset();
	gli_glsl_locate_attributes(7, 0, 1);
	sprintf(want, "attr 7 %d aPosition;attr 7 %d aColor;attr 7 %d aNumLights;attr 7 %d aModify;attr 7 %d aTexCoord;",
		GLI_TRI_ATTRIB_POSITION, GLI_TRI_ATTRIB_COLOR, GLI_TRI_ATTRIB_NUMLIGHTS, GLI_TRI_ATTRIB_MODIFY,
		GLI_TRI_ATTRIB_TEXCOORD);
	CHECK(strcmp(s_log, want) == 0);

	reset();
	gli_glsl_locate_attributes(8, 0, 0);
	sprintf(want, "attr 8 %d aPosition;attr 8 %d aColor;attr 8 %d aNumLights;attr 8 %d aModify;",
		GLI_TRI_ATTRIB_POSITION, GLI_TRI_ATTRIB_COLOR, GLI_TRI_ATTRIB_NUMLIGHTS, GLI_TRI_ATTRIB_MODIFY);
	CHECK(strcmp(s_log, want) == 0);

	reset();
	gli_glsl_locate_attributes(9, 1, 1);
	sprintf(want, "attr 9 %d aRectPosition;attr 9 %d aTexCoord0;attr 9 %d aTexCoord1;",
		GLI_RECT_ATTRIB_POSITION, GLI_RECT_ATTRIB_TEXCOORD0, GLI_RECT_ATTRIB_TEXCOORD1);
	CHECK(strcmp(s_log, want) == 0);

	reset();
	gli_glsl_locate_attributes(5, 1, 0);
	sprintf(want, "attr 5 %d aRectPosition;", GLI_RECT_ATTRIB_POSITION);
	CHECK(strcmp(s_log, want) == 0);

	/* the locations the shaders' layouts and the drawers expect */
	CHECK(GLI_TRI_ATTRIB_POSITION != GLI_TRI_ATTRIB_COLOR);
	CHECK(GLI_RECT_ATTRIB_TEXCOORD0 != GLI_RECT_ATTRIB_TEXCOORD1);
}

static void checks(void)
{
	/* without GL_DEBUG a shader is taken as compiled, nothing asked */
	reset();
	CHECK(gli_glsl_compile_ok(3) == 1);
	CHECK(s_log[0] == '\0');

	/* a link asked only when forced, and answered as the driver says */
	reset();
	s_link_status = GL_FALSE;
	CHECK(gli_glsl_link_ok(4, 0) == 1);
	CHECK(s_log[0] == '\0');
	CHECK(gli_glsl_link_ok(4, 1) == 0);
	CHECK(strcmp(s_log, "getprog 4 link;") == 0);
	reset();
	s_link_status = GL_TRUE;
	CHECK(gli_glsl_link_ok(6, 1) == 1);
	CHECK(strcmp(s_log, "getprog 6 link;") == 0);
	reset();
	s_link_status = 2; /* any status not GL_FALSE linked */
	CHECK(gli_glsl_link_ok(6, 1) == 1);
	s_link_status = GL_TRUE;
}

static void rect_program(void)
{
	static const GLchar *const vs[] = { "VH", "VS", "unused" };
	static const GLchar *const fs[] = { "FH", "FS", "FE", "unused" };
	GLuint p;
	char want[1024];

	reset();
	s_next = 20;
	s_link_status = GL_FALSE; /* not asked: the program comes back as made */
	p = gli_glsl_rect_program(vs, 2, fs, 3);
	CHECK(p == 22);
	sprintf(want,
		"shader v=20;source 20 2 VH+VS nul;compile 20;"
		"shader f=21;source 21 3 FH+FS+FE nul;compile 21;"
		"program=22;attr 22 %d aRectPosition;attr 22 %d aTexCoord0;attr 22 %d aTexCoord1;"
		"attach 22 20;attach 22 21;link 22;delshader 20;delshader 21;",
		GLI_RECT_ATTRIB_POSITION, GLI_RECT_ATTRIB_TEXCOORD0, GLI_RECT_ATTRIB_TEXCOORD1);
	CHECK(strcmp(s_log, want) == 0);
	if (strcmp(s_log, want) != 0)
		printf("  got  %s\n  want %s\n", s_log, want);
	s_link_status = GL_TRUE;
}

/* the lines joined: their count, length, and FNV-1a of the text */
static void text(const char *const *lines, unsigned *nlines, unsigned long *len, unsigned long *hash, int *each_nl)
{
	unsigned long h = 2166136261UL;
	*nlines = 0;
	*len = 0;
	*each_nl = 1;
	for (; *lines != NULL; ++lines) {
		const unsigned char *c = (const unsigned char *)*lines;
		size_t n = strlen(*lines);
		if (n == 0 || (*lines)[n - 1] != '\n')
			*each_nl = 0;
		for (; *c; ++c)
			h = ((h ^ *c) * 16777619UL) & 0xffffffffUL;
		*len += (unsigned long)n;
		++*nlines;
	}
	*hash = h;
}

static void fxaa(void)
{
	unsigned n;
	unsigned long len, hash;
	int nl;
	const char *const *v = gli_glsl_fxaa_vertex();
	const char *const *f = gli_glsl_fxaa_fragment();

	/* the C++'s two strings, as they were */
	text(v, &n, &len, &hash, &nl);
	CHECK(n == 26);
	CHECK(len == 1508);
	CHECK(hash == 0x193f297eUL);
	CHECK(nl);
	text(f, &n, &len, &hash, &nl);
	CHECK(n == 69);
	CHECK(len == 5589);
	CHECK(hash == 0x3c184208UL);
	CHECK(nl);

	CHECK(strncmp(v[0], "precision mediump float;", 24) == 0);
	CHECK(strncmp(f[0], "#ifndef FXAA_REDUCE_MIN", 23) == 0);
	CHECK(strncmp(f[68], "  fragColor = fxaa(gl_FragCoord.xy);", 36) == 0);
	CHECK(f[69] == NULL);
	CHECK(v[26] == NULL);
	/* the same tables each time: nothing made */
	CHECK(gli_glsl_fxaa_vertex() == v);
	CHECK(gli_glsl_fxaa_fragment() == f);
}

int main(void)
{
	attributes();
	checks();
	rect_program();
	fxaa();
	if (fails) {
		printf("test_gliden64_glsl_utils: %d failures\n", fails);
		return 1;
	}
	printf("test_gliden64_glsl_utils: ok\n");
	return 0;
}
