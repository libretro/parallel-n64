#pragma once
#include <boolean.h>
#include "GLFunctions.h"

/* What the GL context has: its version, the extensions GLideN64 uses,
 * and its renderer, of which some have quirks. C89. */

#ifdef __cplusplus
extern "C" {
#endif

enum gli_gl_renderer
{
	GLI_GL_ADRENO530,
	GLI_GL_ADRENO_NO_BUGS,
	GLI_GL_ADRENO,
	GLI_GL_VIDEOCORE,
	GLI_GL_INTEL,
	GLI_GL_POWERVR,
	GLI_GL_TEGRA,
	GLI_GL_ANGLE,
	GLI_GL_OTHER
};

typedef struct gli_gl_info
{
	GLint majorVersion;
	GLint minorVersion;
	bool isGLES2;
	bool isGLESX;
	bool imageTextures;
	bool bufferStorage;
	bool parallelShaderCompile;
	bool drawElementsBaseVertex;
	bool texStorage;
	bool shaderStorage;
	bool msaa;
	bool depthTexture;
	bool noPerspective;
	bool fetch_depth;
	bool texture_barrier;
	bool texture_barrierNV;
	bool fragment_interlock;
	bool fragment_interlockNV;
	bool fragment_ordering;
	bool ext_fetch;
	bool ext_fetch_arm;
	bool n64DepthWithFbFetch;
	bool dual_source_blending;
	bool anisotropic_filtering;
	bool coverage;
	enum gli_gl_renderer renderer;
} gli_gl_info;

/* @_info for the current context, and the config's settings it cannot
 * have changed to what it can */
void gli_gl_info_init(gli_gl_info *_info);

/* The renderer GL_RENDERER @_renderer and GL_VERSION @_version name */
enum gli_gl_renderer gli_gl_renderer_of(const char *_renderer, const char *_version);

/* Whether a Tegra driver of GL_VERSION @_version has the broken
 * fragment_shader_interlock that locks up below 390.00: so with no
 * version found */
int gli_gl_tegra_interlock_broken(const char *_version);

#ifdef __cplusplus
}
#endif
