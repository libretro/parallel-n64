/* test_gliden64_gl_info -- what GLideN64 finds a GL context has
 * (Graphics/OpenGLContext/opengl_GLInfo.c).
 *
 * The renderer is told from GL_RENDERER as regular expressions told it,
 * whole: Adreno 530; Adreno 540 and 6xx and freedreno 6xx without the
 * Adreno bugs; other Adrenos and freedrenos with them; VideoCore IV,
 * Intel, PowerVR and Tegra by name; and ANGLE, from GL_VERSION, over all.
 * A Tegra driver below 390, or of no version, has the broken interlock.
 * On a context the version and extensions decide what it has, and some
 * settings it cannot have are turned off: on GLES 2, fragment depth
 * writes, a copy to RDRAM past synchronous, and LOD without its
 * extensions; on PowerVR, fragment depth writes, with the depth buffer
 * cleared; the N64 depth compare with neither image textures nor
 * framebuffer fetch. GLES before 3.2 takes glEnablei and glDisablei by
 * their EXT names. The GL entry points are stubs.
 *
 *   ./test_gliden64_gl_info
 */
#include <stdio.h>
#include <string.h>

#include "opengl_GLInfo.h"
#include "Config.h"

static int failures;

static void check(int _ok, const char *_what)
{
	if (!_ok) {
		printf("FAIL: %s\n", _what);
		failures++;
	}
}

Config gliden64Config;

/* ---- the context ---- */

static const char *version, *rendererName;
static const char *extensions[16];
static GLint major, minor, binaryFormats, maxAttribs;
static char procAsked[64];

static void APIENTRY get_integerv(GLenum _p, GLint *_v)
{
	GLint n = 0;
	switch (_p) {
	case GL_MAJOR_VERSION: *_v = major; break;
	case GL_MINOR_VERSION: *_v = minor; break;
	case GL_NUM_PROGRAM_BINARY_FORMATS: *_v = binaryFormats; break;
	case GL_MAX_VERTEX_ATTRIBS: *_v = maxAttribs; break;
	case GL_NUM_EXTENSIONS:
		while (extensions[n] != NULL)
			n++;
		*_v = n;
		break;
	}
}

static const GLubyte *APIENTRY get_string(GLenum _n)
{
	static char joined[1024];
	int i;
	if (_n == GL_VERSION)
		return (const GLubyte *)version;
	if (_n == GL_RENDERER)
		return (const GLubyte *)rendererName;
	joined[0] = 0;
	for (i = 0; extensions[i] != NULL; ++i) {
		if (i)
			strcat(joined, " ");
		strcat(joined, extensions[i]);
	}
	return (const GLubyte *)joined;
}

static const GLubyte *APIENTRY get_stringi(GLenum _n, GLuint _i)
{
	(void)_n;
	return (const GLubyte *)extensions[_i];
}

static void APIENTRY enablei_stub(GLenum _c, GLuint _i) { (void)_c; (void)_i; }

PFNGLGETINTEGERVPROC g_glGetIntegerv = get_integerv;
PFNGLGETSTRINGPROC g_glGetString = get_string;
PFNGLGETSTRINGIPROC g_glGetStringi = get_stringi;
PFNGLGETERRORPROC g_glGetError;
PFNGLCHECKFRAMEBUFFERSTATUSPROC g_glCheckFramebufferStatus;
PFNGLENABLEIPROC g_glEnablei = enablei_stub;
PFNGLDISABLEIPROC g_glDisablei = enablei_stub;

static char procMark;
void *glsm_get_proc_address(const char *_name)
{
	strncpy(procAsked, _name, sizeof(procAsked) - 1);
	return &procMark;
}

static void set_extensions(const char *_a, const char *_b, const char *_c, const char *_d, const char *_e)
{
	extensions[0] = _a; extensions[1] = _b; extensions[2] = _c; extensions[3] = _d; extensions[4] = _e;
	extensions[5] = NULL;
}

static void reset_config(void)
{
	memset(&config, 0, sizeof(config));
	config.generalEmulation.enableFragmentDepthWrite = 1;
	config.frameBufferEmulation.copyToRDRAM = 2;
	config.generalEmulation.enableLOD = 1;
	config.frameBufferEmulation.N64DepthCompare = 1;
	config.generalEmulation.enableShadersStorage = 1;
}

static void test_renderers(void)
{
	check(gli_gl_renderer_of("Adreno (TM) 530", "OpenGL ES 3.2") == GLI_GL_ADRENO530, "Adreno 530");
	check(gli_gl_renderer_of("Adreno530", "") == GLI_GL_ADRENO530, "Adreno and 530 with nothing between");
	check(gli_gl_renderer_of("Adreno (TM) 5300", "") == GLI_GL_ADRENO, "but 530 must end the name");
	check(gli_gl_renderer_of("Adreno (TM) 540", "") == GLI_GL_ADRENO_NO_BUGS, "Adreno 540 without the bugs");
	check(gli_gl_renderer_of("Adreno (TM) 640", "") == GLI_GL_ADRENO_NO_BUGS, "an Adreno 6xx");
	check(gli_gl_renderer_of("Adreno (TM) 6x0", "") == GLI_GL_ADRENO, "6 and two digits, not a letter");
	check(gli_gl_renderer_of("Adreno\n640", "") == GLI_GL_ADRENO, "nor across a line break");
	check(gli_gl_renderer_of("Adreno 640\n", "") == GLI_GL_ADRENO, "nor a name that does not end with them");
	check(gli_gl_renderer_of("FD630", "") == GLI_GL_ADRENO_NO_BUGS, "freedreno 6xx");
	check(gli_gl_renderer_of("Mesa FD618 (x)", "") == GLI_GL_ADRENO_NO_BUGS, "anywhere in the name");
	check(gli_gl_renderer_of("FD618\n", "") == GLI_GL_ADRENO, "but not with a line break in it");
	check(gli_gl_renderer_of("FD540", "") == GLI_GL_ADRENO, "freedreno 5xx with the bugs");
	check(gli_gl_renderer_of("FD63x", "") == GLI_GL_ADRENO, "6 and two digits after FD, not one");
	check(gli_gl_renderer_of("FDx FD630", "") == GLI_GL_ADRENO_NO_BUGS, "found past an FD that is not it");
	check(gli_gl_renderer_of("Adreno (TM) 531", "") == GLI_GL_ADRENO, "531 is no 530");
	check(gli_gl_renderer_of("FD", "") == GLI_GL_ADRENO, "any freedreno");
	check(gli_gl_renderer_of("VideoCore IV HW", "") == GLI_GL_VIDEOCORE, "VideoCore IV");
	check(gli_gl_renderer_of("Mesa Intel(R) UHD", "") == GLI_GL_INTEL, "Intel");
	check(gli_gl_renderer_of("PowerVR Rogue", "") == GLI_GL_POWERVR, "PowerVR");
	check(gli_gl_renderer_of("NVIDIA Tegra X1", "") == GLI_GL_TEGRA, "Tegra");
	check(gli_gl_renderer_of("llvmpipe", "4.5 Mesa") == GLI_GL_OTHER, "any other");
	check(gli_gl_renderer_of("Adreno (TM) 530", "OpenGL ES 3.0 (ANGLE 2.1)") == GLI_GL_ANGLE, "ANGLE over all");

	check(gli_gl_tegra_interlock_broken("3.2 NVIDIA 389.9") && !gli_gl_tegra_interlock_broken("3.2 NVIDIA 390.12"),
		"Tegra's interlock broken below 390");
	check(!gli_gl_tegra_interlock_broken("4.6 NVIDIAx400"), "the version one character past the name, whatever it is");
	check(gli_gl_tegra_interlock_broken("3.2 Mesa") && gli_gl_tegra_interlock_broken("NVIDIA") &&
		gli_gl_tegra_interlock_broken("NVIDIA abc"), "and with no version found");
	{
		/* nothing past the name's end is read as its version */
		static const char ends[] = "NVIDIA\0 500";
		check(gli_gl_tegra_interlock_broken(ends), "not even what lies past the string");
	}
}

static void test_desktop(void)
{
	gli_gl_info info;
	reset_config();
	version = "4.6 (Core Profile) Mesa";
	rendererName = "llvmpipe";
	major = 4; minor = 6; binaryFormats = 1; maxAttribs = 16;
	set_extensions("GL_ARB_fragment_shader_interlock", "GL_EXT_shader_framebuffer_fetch",
		"GL_EXT_texture_filter_anisotropic", "GL_ARB_parallel_shader_compile", NULL);
	memset(&info, 0xff, sizeof(info));
	gli_gl_info_init(&info);
	check(!info.isGLES2 && !info.isGLESX && info.majorVersion == 4 && info.minorVersion == 6, "desktop GL 4.6");
	check(info.bufferStorage && info.texStorage && info.msaa && info.texture_barrier && info.drawElementsBaseVertex,
		"from its version: buffer and texture storage, MSAA, texture barrier, base vertex");
	check(info.imageTextures && info.fragment_interlock && !info.fragment_interlockNV,
		"image textures with an interlock");
	check(!info.ext_fetch && !info.ext_fetch_arm && !info.n64DepthWithFbFetch, "no framebuffer fetch with image textures");
	check(info.anisotropic_filtering && info.parallelShaderCompile && info.shaderStorage, "from its extensions");
	check(!info.coverage && !info.dual_source_blending && !info.noPerspective, "and nothing it lacks");
	check(info.depthTexture, "a depth texture, as not GLES 2");
	check(info.renderer == GLI_GL_OTHER, "an other renderer");
	check(config.frameBufferEmulation.N64DepthCompare == 1 && config.generalEmulation.enableFragmentDepthWrite == 1,
		"and the config as it was");

	reset_config();
	set_extensions("GL_EXT_shader_framebuffer_fetch", NULL, NULL, NULL, NULL);
	maxAttribs = 16;
	gli_gl_info_init(&info);
	check(!info.imageTextures && info.ext_fetch && info.n64DepthWithFbFetch && info.coverage,
		"without an interlock: framebuffer fetch, and coverage");
	maxAttribs = 9;
	gli_gl_info_init(&info);
	check(!info.coverage, "but no coverage under ten vertex attributes");
	rendererName = "FD630";
	gli_gl_info_init(&info);
	check(!info.ext_fetch && info.renderer == GLI_GL_ADRENO_NO_BUGS, "and no framebuffer fetch on desktop freedreno");
	check(config.frameBufferEmulation.N64DepthCompare == 0, "so no N64 depth compare");

	reset_config();
	rendererName = "PowerVR";
	set_extensions(NULL, NULL, NULL, NULL, NULL);
	gli_gl_info_init(&info);
	check(config.frameBufferEmulation.forceDepthBufferClear == 1 && config.generalEmulation.enableFragmentDepthWrite == 0,
		"PowerVR: depth cleared, no fragment depth writes");
	binaryFormats = 0;
	gli_gl_info_init(&info);
	check(!info.shaderStorage, "no shader storage with no binary formats");
	binaryFormats = 1;
	config.generalEmulation.enableShadersStorage = 0;
	gli_gl_info_init(&info);
	check(!info.shaderStorage, "nor when it is off");

	reset_config();
	rendererName = "NVIDIA Tegra X1";
	version = "4.6 NVIDIA 380.0";
	set_extensions("GL_ARB_fragment_shader_interlock", "GL_NV_fragment_shader_interlock",
		"GL_INTEL_fragment_shader_ordering", NULL, NULL);
	gli_gl_info_init(&info);
	check(!info.fragment_interlock && !info.fragment_interlockNV && info.fragment_ordering,
		"an old Tegra driver: no interlock");
	version = "4.6 NVIDIA 410.0";
	gli_gl_info_init(&info);
	check(info.fragment_interlock && !info.fragment_interlockNV && !info.fragment_ordering, "a new one: the ARB one");
}

static void test_gles(void)
{
	gli_gl_info info;
	reset_config();
	version = "OpenGL ES 2.0 Mesa";
	rendererName = "Mali";
	major = 9; minor = 9;
	set_extensions("GL_OES_depth_texture", "GL_EXT_shader_texture_lod", NULL, NULL, NULL);
	gli_gl_info_init(&info);
	check(info.isGLES2 && info.isGLESX && info.majorVersion == 2 && info.minorVersion == 0, "GLES 2: version 2.0, not asked");
	check(!info.imageTextures && !info.msaa && !info.bufferStorage && !info.texStorage && info.depthTexture,
		"no image textures, MSAA or storage; a depth texture by its extension");
	check(config.generalEmulation.enableFragmentDepthWrite == 0, "no fragment depth writes");
#ifndef OS_ANDROID
	check(config.frameBufferEmulation.copyToRDRAM == ctDisable, "no copy to RDRAM past synchronous");
#endif
	check(config.generalEmulation.enableLOD == 0, "no LOD without both its extensions");
	reset_config();
	set_extensions("GL_OES_standard_derivatives", NULL, NULL, NULL, NULL);
	gli_gl_info_init(&info);
	check(config.generalEmulation.enableLOD == 0, "either of them");
	reset_config();
	set_extensions("GL_OES_standard_derivatives", "GL_EXT_shader_texture_lod", NULL, NULL, NULL);
	gli_gl_info_init(&info);
	check(config.generalEmulation.enableLOD == 1, "kept with both");
	extensions[0] = NULL;
	reset_config();
	config.frameBufferEmulation.copyToRDRAM = ctSync;
	gli_gl_info_init(&info);
	check(!info.depthTexture && config.frameBufferEmulation.copyToRDRAM == ctSync, "a synchronous copy kept");

	reset_config();
	version = "OpenGL ES 3.1";
	major = 3; minor = 1;
	set_extensions("GL_EXT_draw_buffers_indexed", "GL_EXT_shader_framebuffer_fetch", NULL, NULL, NULL);
	procAsked[0] = 0;
	g_glEnablei = NULL;
	gli_gl_info_init(&info);
	check(info.isGLESX && !info.isGLES2 && info.msaa && info.texStorage && !info.bufferStorage, "GLES 3.1");
	check((void *)g_glEnablei != NULL && strcmp(procAsked, "glDisableiEXT") == 0, "glEnablei and glDisablei by their EXT names");
	check(info.ext_fetch && !info.drawElementsBaseVertex, "framebuffer fetch with indexed draw buffers; no base vertex");
	set_extensions("GL_EXT_shader_framebuffer_fetch", NULL, NULL, NULL, NULL);
	gli_gl_info_init(&info);
	check(g_glEnablei == NULL && g_glDisablei == NULL && !info.ext_fetch, "without them, neither, nor framebuffer fetch");
	major = 3; minor = 2;
	g_glEnablei = enablei_stub;
	gli_gl_info_init(&info);
	check(g_glEnablei == enablei_stub && info.ext_fetch && info.drawElementsBaseVertex, "GLES 3.2 keeps its own");
	major = 4; minor = 4;
	set_extensions(NULL, NULL, NULL, NULL, NULL);
	gli_gl_info_init(&info);
	check(!info.bufferStorage, "no buffer storage by version on GLES");
}

int main(void)
{
	test_renderers();
	test_desktop();
	test_gles();
	if (failures) {
		printf("%d failed\n", failures);
		return 1;
	}
	printf("gl info: ok\n");
	return 0;
}
