/* What the GL context has (opengl_GLInfo.h). C89.
 *
 * The renderer's name is matched as the patterns ECMAScript regular
 * expressions matched it with, whole: "Adreno.*530", "Adreno.*540",
 * "Adreno.*6\d\d", ".*FD6\d\d.*" and ".*FD5\d\d.*", where . is any
 * character but a line break. */
#include <stdlib.h>
#include <string.h>

#include "opengl_GLInfo.h"
#include "opengl_Utils.h"
#include "Config.h"

void *glsm_get_proc_address(const char *);

static int is_digit(char _c)
{
	return _c >= '0' && _c <= '9';
}

/* whether [@_from, @_to) of @_s holds no line break, which . does not match */
static int no_break(const char *_s, size_t _from, size_t _to)
{
	size_t i;
	for (i = _from; i < _to; ++i)
		if (_s[i] == '\n' || _s[i] == '\r')
			return 0;
	return 1;
}

/* "Adreno.*" then @_tail, whole; a '#' in @_tail is any digit */
static int adreno_then(const char *_s, const char *_tail)
{
	const size_t len = strlen(_s), tail = strlen(_tail);
	size_t i;
	if (len < 6 + tail || strncmp(_s, "Adreno", 6) != 0 || !no_break(_s, 6, len - tail))
		return 0;
	for (i = 0; i < tail; ++i) {
		const char c = _s[len - tail + i];
		if (_tail[i] == '#' ? !is_digit(c) : c != _tail[i])
			return 0;
	}
	return 1;
}

/* ".*FD" @_series "\d\d.*", whole */
static int freedreno_series(const char *_s, char _series)
{
	const char *p;
	if (!no_break(_s, 0, strlen(_s)))
		return 0;
	for (p = strstr(_s, "FD"); p != NULL; p = strstr(p + 1, "FD"))
		if (p[2] == _series && is_digit(p[3]) && is_digit(p[4]))
			return 1;
	return 0;
}

enum gli_gl_renderer gli_gl_renderer_of(const char *_renderer, const char *_version)
{
	const int isAnyAdreno = strstr(_renderer, "Adreno") != NULL;
	const int isFreedreno = strstr(_renderer, "FD") != NULL;   /* freedreno's "FDxxx" */
	enum gli_gl_renderer renderer = GLI_GL_OTHER;

	if (adreno_then(_renderer, "530"))
		renderer = GLI_GL_ADRENO530;
	else if (adreno_then(_renderer, "540") || adreno_then(_renderer, "6##") || freedreno_series(_renderer, '6'))
		renderer = GLI_GL_ADRENO_NO_BUGS;
	else if (freedreno_series(_renderer, '5') || isAnyAdreno || isFreedreno)
		renderer = GLI_GL_ADRENO;
	else if (strstr(_renderer, "VideoCore IV") != NULL)
		renderer = GLI_GL_VIDEOCORE;
	else if (strstr(_renderer, "Intel") != NULL)
		renderer = GLI_GL_INTEL;
	else if (strstr(_renderer, "PowerVR") != NULL)
		renderer = GLI_GL_POWERVR;
	else if (strstr(_renderer, "NVIDIA Tegra") != NULL)
		renderer = GLI_GL_TEGRA;

	/* ANGLE (Microsoft's GLES translator over D3D11 / D3D12 / Vulkan /
	 * Metal) names its underlying device in GL_RENDERER, so one of those
	 * above caught it, but says it is ANGLE in GL_VERSION. Its own quirks
	 * (fragment depth writes, below) win over the device's. */
	if (strstr(_version, "ANGLE") != NULL)
		renderer = GLI_GL_ANGLE;
	return renderer;
}

int gli_gl_tegra_interlock_broken(const char *_version)
{
	/* the driver's version follows "NVIDIA" and one character */
	const char *at = strstr(_version, "NVIDIA");
	/* none there is version 0 */
	if (at == NULL || strlen(at) < 7)
		return 1;
	return strtod(at + 7, NULL) < 390.0;
}

static int has(const gli_gl_info *_info, const char *_extension)
{
	return gli_gl_has_extension(_info->majorVersion, _extension);
}

/* a loader's address into a function pointer: copied, as C has no
 * conversion from an object pointer to a function pointer */
static void set_proc(void *_fn, void *_address)
{
	memcpy(_fn, &_address, sizeof(_address));
}

void gli_gl_info_init(gli_gl_info *_info)
{
	const char *strVersion = (const char *)glGetString(GL_VERSION);
	const char *strRenderer = (const char *)glGetString(GL_RENDERER);
	int isFreedreno, numericVersion, hasBuggyFragmentShaderInterlock = 0, ext_draw_buffers_indexed;

	memset(_info, 0, sizeof(*_info));
	_info->isGLESX = strstr(strVersion, "OpenGL ES") != NULL;
	_info->isGLES2 = strstr(strVersion, "OpenGL ES 2") != NULL;
	if (_info->isGLES2) {
		_info->majorVersion = 2;
		_info->minorVersion = 0;
	} else {
		glGetIntegerv(GL_MAJOR_VERSION, &_info->majorVersion);
		glGetIntegerv(GL_MINOR_VERSION, &_info->minorVersion);
	}

	isFreedreno = strstr(strRenderer, "FD") != NULL;
	_info->renderer = gli_gl_renderer_of(strRenderer, strVersion);

	/* Renderers' config overrides:
	 *
	 *   PowerVR (Apple A4-A10, some embedded ARM SoCs): the GLES driver
	 *   silently drops gl_FragDepth writes, and needs forceDepthBufferClear
	 *   to keep the depth attachment sane across frames.
	 *
	 *   ANGLE on Android: the D3D / Vulkan translation does not forward
	 *   fragment depth writes through the GLES emulation; the desktop
	 *   ANGLE flavours handle them fine. */
	if (_info->renderer == GLI_GL_POWERVR) {
		config.frameBufferEmulation.forceDepthBufferClear = 1;
		config.generalEmulation.enableFragmentDepthWrite = 0;
	}

#ifdef OS_ANDROID
	if (_info->renderer == GLI_GL_ANGLE)
		config.generalEmulation.enableFragmentDepthWrite = 0;
#endif

	numericVersion = _info->majorVersion * 10 + _info->minorVersion;
	if (_info->isGLES2) {
		_info->imageTextures = 0;
		_info->msaa = 0;
	} else if (_info->isGLESX) {
		_info->imageTextures = numericVersion >= 31;
		_info->msaa = numericVersion >= 31;
	} else {
		_info->imageTextures = numericVersion >= 42 || has(_info, "GL_ARB_shader_image_load_store");
		_info->msaa = numericVersion >= 32;
	}

	/* Tegra's fragment_shader_interlock locks graphics up on drivers
	 * below 390.00 */
	if (_info->renderer == GLI_GL_TEGRA)
		hasBuggyFragmentShaderInterlock = gli_gl_tegra_interlock_broken(strVersion);

	_info->parallelShaderCompile = has(_info, "GL_KHR_parallel_shader_compile") ||
		has(_info, "GL_ARB_parallel_shader_compile");

	_info->fragment_interlock = has(_info, "GL_ARB_fragment_shader_interlock") && !hasBuggyFragmentShaderInterlock;
	_info->fragment_interlockNV = has(_info, "GL_NV_fragment_shader_interlock") && !_info->fragment_interlock &&
		!hasBuggyFragmentShaderInterlock;
	_info->fragment_ordering = has(_info, "GL_INTEL_fragment_shader_ordering") && !_info->fragment_interlock &&
		!_info->fragment_interlockNV;

	_info->imageTextures = _info->imageTextures &&
		(_info->fragment_interlock || _info->fragment_interlockNV || _info->fragment_ordering);

	if (_info->isGLES2)
		config.generalEmulation.enableFragmentDepthWrite = 0;

	_info->bufferStorage = (!_info->isGLESX && numericVersion >= 44) || has(_info, "GL_ARB_buffer_storage") ||
		has(_info, "GL_EXT_buffer_storage");

	_info->texStorage = (_info->isGLESX && numericVersion >= 30) || (!_info->isGLESX && numericVersion >= 42) ||
		has(_info, "GL_ARB_texture_storage");

	_info->shaderStorage = 0;
	if (config.generalEmulation.enableShadersStorage != 0) {
		const char *strGetProgramBinary = _info->isGLESX ? "GL_OES_get_program_binary" : "GL_ARB_get_program_binary";
		if ((_info->isGLESX && numericVersion >= 30) || (!_info->isGLESX && numericVersion >= 41) ||
				has(_info, strGetProgramBinary)) {
			GLint numBinaryFormats = 0;
			glGetIntegerv(GL_NUM_PROGRAM_BINARY_FORMATS, &numBinaryFormats);
			_info->shaderStorage = numBinaryFormats > 0;
		}
	}

	ext_draw_buffers_indexed = _info->isGLESX && (has(_info, "GL_EXT_draw_buffers_indexed") || numericVersion >= 32);
	/* GLES before 3.2 has these only as the EXT names */
	if (_info->isGLESX && numericVersion < 32) {
		if (ext_draw_buffers_indexed) {
			set_proc(&g_glEnablei, glsm_get_proc_address("glEnableiEXT"));
			set_proc(&g_glDisablei, glsm_get_proc_address("glDisableiEXT"));
		} else {
			g_glEnablei = NULL;
			g_glDisablei = NULL;
		}
	}
#ifndef OS_ANDROID
	if (_info->isGLES2 && config.frameBufferEmulation.copyToRDRAM > ctSync)
		config.frameBufferEmulation.copyToRDRAM = ctDisable;
#endif
	if (_info->isGLES2 && config.generalEmulation.enableLOD) {
		if (!has(_info, "GL_EXT_shader_texture_lod") || !has(_info, "GL_OES_standard_derivatives"))
			config.generalEmulation.enableLOD = 0;
	}

	_info->depthTexture = !_info->isGLES2 || has(_info, "GL_OES_depth_texture");
	_info->noPerspective = has(_info, "GL_NV_shader_noperspective_interpolation");

	_info->fetch_depth = has(_info, "GL_ARM_shader_framebuffer_fetch_depth_stencil");
	_info->texture_barrier = !_info->isGLESX && (numericVersion >= 45 || has(_info, "GL_ARB_texture_barrier"));
	_info->texture_barrierNV = has(_info, "GL_NV_texture_barrier");

	_info->ext_fetch = has(_info, "GL_EXT_shader_framebuffer_fetch") && !_info->isGLES2 &&
		(!_info->isGLESX || ext_draw_buffers_indexed) && !_info->imageTextures;

	/* Mesa freedreno on the desktop GL profile (not GLES) has
	 * GL_EXT_shader_framebuffer_fetch but miscompiles fragment shaders
	 * that read gl_LastFragData, which breaks the N64 depth compare: off
	 * there, for imageTextures or for no N64 depth compare. Its GLES
	 * profile is fine. */
	if (isFreedreno && !_info->isGLESX)
		_info->ext_fetch = 0;

	/* dual_source_blending stays off, as upstream's has bugs across
	 * drivers; nothing here uses it yet. Unlike upstream, ext_fetch does
	 * not go with it: this ext_fetch path reads gl_LastFragData and needs
	 * no dual-source output, and turning it off with dual-source blending
	 * would lose N64DepthCompare wherever it works now. */
	_info->ext_fetch_arm = has(_info, "GL_ARM_shader_framebuffer_fetch") && !_info->ext_fetch;
	_info->n64DepthWithFbFetch = _info->ext_fetch;
	_info->dual_source_blending = 0;
	_info->anisotropic_filtering = has(_info, "GL_EXT_texture_filter_anisotropic");
	_info->coverage = _info->dual_source_blending || _info->ext_fetch || _info->ext_fetch_arm;
	if (_info->coverage) {
		GLint maxVertexAttribs = 0;
		glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &maxVertexAttribs);
		_info->coverage = maxVertexAttribs >= 10;
	}
	_info->drawElementsBaseVertex = !_info->isGLESX ||
		has(_info, "GL_EXT_draw_elements_base_vertex") || numericVersion >= 32;

	if (config.frameBufferEmulation.N64DepthCompare != 0) {
		if (!_info->imageTextures && !_info->ext_fetch)
			config.frameBufferEmulation.N64DepthCompare = 0;
	}
}
