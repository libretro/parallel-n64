/* test_gliden64_glsl_program: GLideN64's GL combiner program
 * (glsl_CombinerProgramImpl.c) and its uniforms
 * (glsl_CombinerProgramUniformFactory.c) on stub GL calls.
 *
 * The uniforms a program has found by name, each where its value sits and
 * of its kind, the lights' only with hardware lighting, the samplers with
 * their units; the value groups from what it reads, the key and what GL
 * is; each value sent by its kind when it changed or when forced, the
 * samplers only when forced. A program made from its key, GL program and
 * inputs, used, updated (forced the first time), asked what it reads, and
 * let go; its stored form sized and written, key, inputs, format, length
 * and the binary GL writes in place. C89. */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Graphics/OpenGLContext/GLFunctions.h"
#include "Graphics/OpenGLContext/opengl_GLInfo.h"
#include "Graphics/OpenGLContext/opengl_CachedFunctions.h"
#include "Graphics/OpenGLContext/GLSL/glsl_CombinerProgramImpl.h"
#include "Graphics/CombinerStages.h"
#include "Graphics/gli_params.h"
#include "CombinerKey.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

/* every call, as text */
static char s_log[16384];
static void logs(const char *s)
{
	if (strlen(s_log) + strlen(s) + 2 < sizeof(s_log)) {
		strcat(s_log, s);
		strcat(s_log, ";");
	}
}
static void reset(void) { s_log[0] = '\0'; }

/* the uniforms the program has: a name's location is its place here plus
 * 100; NULL ends them */
static const char *const *s_has;
static GLuint s_asked;
static int s_otherProgram; /* a lookup in a program other than the last */
static GLint APIENTRY s_get_uniform(GLuint p, const GLchar *n)
{
	int i;
	if (s_asked != 0 && p != s_asked)
		s_otherProgram = 1;
	s_asked = p;
	for (i = 0; s_has != NULL && s_has[i] != NULL; ++i)
		if (strcmp(s_has[i], n) == 0)
			return 100 + i;
	return -1;
}
static void APIENTRY s_u1i(GLint l, GLint a) { char b[64]; sprintf(b, "1i %d %d", l, a); logs(b); }
static void APIENTRY s_u2i(GLint l, GLint a, GLint c) { char b[64]; sprintf(b, "2i %d %d %d", l, a, c); logs(b); }
static void APIENTRY s_u4i(GLint l, GLint a, GLint c, GLint d, GLint e)
{ char b[80]; sprintf(b, "4i %d %d %d %d %d", l, a, c, d, e); logs(b); }
static void APIENTRY s_u1f(GLint l, GLfloat a) { char b[64]; sprintf(b, "1f %d %g", l, a); logs(b); }
static void APIENTRY s_u2f(GLint l, GLfloat a, GLfloat c) { char b[64]; sprintf(b, "2f %d %g %g", l, a, c); logs(b); }
static void APIENTRY s_u3fv(GLint l, GLsizei n, const GLfloat *v)
{ char b[96]; sprintf(b, "3fv %d %d %g %g %g", l, (int)n, v[0], v[1], v[2]); logs(b); }
static void APIENTRY s_u4fv(GLint l, GLsizei n, const GLfloat *v)
{ char b[96]; sprintf(b, "4fv %d %d %g %g %g %g", l, (int)n, v[0], v[1], v[2], v[3]); logs(b); }
static void APIENTRY s_use(GLuint p) { char b[32]; sprintf(b, "use %u", p); logs(b); }
static void APIENTRY s_delete(GLuint p) { char b[32]; sprintf(b, "del %u", p); logs(b); }

static GLint s_binLen;
static GLuint s_binProg;
static void APIENTRY s_get_programiv(GLuint p, GLenum e, GLint *v)
{
	char b[48];
	sprintf(b, "getprog %u %s", p, e == GL_PROGRAM_BINARY_LENGTH ? "len" : "?");
	logs(b);
	*v = s_binLen;
}
static void APIENTRY s_get_binary(GLuint p, GLsizei size, GLsizei *len, GLenum *fmt, void *out)
{
	char b[48];
	GLint i;
	sprintf(b, "bin %u %d", p, (int)size);
	logs(b);
	s_binProg = p;
	for (i = 0; i < s_binLen && i < size; ++i)
		((unsigned char *)out)[i] = (unsigned char)(0xA0 + i);
	*len = s_binLen < size ? s_binLen : size;
	*fmt = 0x1234;
}

PFNGLGETUNIFORMLOCATIONPROC g_glGetUniformLocation = s_get_uniform;
PFNGLUNIFORM1IPROC g_glUniform1i = s_u1i;
PFNGLUNIFORM2IPROC g_glUniform2i = s_u2i;
PFNGLUNIFORM4IPROC g_glUniform4i = s_u4i;
PFNGLUNIFORM1FPROC g_glUniform1f = s_u1f;
PFNGLUNIFORM2FPROC g_glUniform2f = s_u2f;
PFNGLUNIFORM3FVPROC g_glUniform3fv = s_u3fv;
PFNGLUNIFORM4FVPROC g_glUniform4fv = s_u4fv;
PFNGLUSEPROGRAMPROC g_glUseProgram = s_use;
PFNGLDELETEPROGRAMPROC g_glDeleteProgram = s_delete;
PFNGLGETPROGRAMIVPROC g_glGetProgramiv = s_get_programiv;
PFNGLGETPROGRAMBINARYPROC g_glGetProgramBinary = s_get_binary;

static int s_glError;
int gli_gl_has_error(void) { return s_glError; }

/* the values, worked out: what s_src holds, and what was asked */
static CombinerValues s_src;
static unsigned s_groupsAsked;
static int s_updates;
static CombinerNeeds s_needs;
unsigned combinerValueGroups(const CombinerNeeds *_needs) { s_needs = *_needs; return 0xABCu; }
void updateCombinerValues(CombinerValues *_values, unsigned _groups)
{
	s_groupsAsked = _groups;
	++s_updates;
	*_values = s_src;
}

/* the key's cycle type and rect bit, as this test says */
static u64 s_keyAsked;
static u32 s_cycle;
static bool s_rect;
u32 gli_combiner_key_cycle_type(u64 _key) { s_keyAsked = _key; return s_cycle; }
bool gli_combiner_key_is_rect(u64 _key) { s_keyAsked = _key; return s_rect; }

#define BIT(x) (1u << (x))

static int has(const char *_entry)
{
	char b[160];
	sprintf(b, "%s;", _entry);
	return strstr(s_log, b) != NULL;
}

static int count_calls(void)
{
	int n = 0;
	const char *p;
	for (p = s_log; *p; ++p)
		if (*p == ';')
			++n;
	return n;
}

/* every name the factory asks for */
static const char *const s_all[] = {
	"uFogColor", "uCenterColor", "uScaleColor", "uBlendColor", "uEnvColor", "uPrimColor", "uRectColor",
	"uBlendMux1", "uBlendMux2", "uConvertParams",
	"uFogScale", "uNoiseOffset", "uScreenScale", "uDepthScale", "uScreenCoordsScale", "uTexScale",
	"uTexOffset[0]", "uTexOffset[1]", "uCacheShiftScale[0]", "uCacheShiftScale[1]",
	"uCacheScale[0]", "uCacheScale[1]", "uCacheOffset[0]", "uCacheOffset[1]",
	"uTextureSize[0]", "uTextureSize[1]",
	"uFbMonochrome", "uFbFixedAlpha", "uMSTexEnabled", "uTextureFormat", "uCacheFrameBuffer",
	"uFogUsage", "uForceBlendCycle1", "uForceBlendCycle2", "uAlphaCompareMode", "uAlphaDitherMode",
	"uColorDitherMode", "uMaxTile", "uEnableLod", "uTextureDetail", "uTexturePersp", "uTextureFilterMode",
	"uTextureConvert", "uEnableAlphaTest", "uAlphaCvgSel", "uCvgXAlpha", "uEnableDepth",
	"uEnableDepthCompare", "uEnableDepthUpdate", "uDepthMode", "uDepthSource", "uRenderTarget",
	"uClampMode", "uAlphaTestValue", "uMinLod", "uPrimDepth", "uDeltaZ", "uPolygonOffset", "uPrimLod",
	"uK4", "uK5", "uMSAASamples",
	"uLightDirection[0]", "uLightColor[0]", "uLightDirection[1]", "uLightColor[1]",
	"uLightDirection[2]", "uLightColor[2]", "uLightDirection[3]", "uLightColor[3]",
	"uLightDirection[4]", "uLightColor[4]", "uLightDirection[5]", "uLightColor[5]",
	"uLightDirection[6]", "uLightColor[6]", "uLightDirection[7]", "uLightColor[7]",
	"uTexNoise", "uDepthTex", "uTex0", "uTex1", "uMSTex0", "uMSTex1",
	NULL
};
#define LOC(_name) (100 + index_of(_name))
static int index_of(const char *_name)
{
	int i;
	for (i = 0; s_all[i] != NULL; ++i)
		if (strcmp(s_all[i], _name) == 0)
			return i;
	return -1000;
}

/* the entry for location @_loc, or NULL */
static const gli_glsl_uniform *entry(const gli_glsl_uniforms *_u, GLint _loc)
{
	u32 i;
	for (i = 0; i < _u->count; ++i)
		if (_u->entries[i].loc == _loc)
			return &_u->entries[i];
	return NULL;
}

static int is_entry(const gli_glsl_uniforms *_u, const char *_name, size_t _offset, int _bytes)
{
	const gli_glsl_uniform *e = entry(_u, LOC(_name));
	return e != NULL && e->offset == _offset && e->bytes == _bytes;
}

/* every named uniform: the call that sends it, where its value sits, its
 * size, in the factory's order */
typedef struct expect { const char *name; const char *call; size_t offset; int bytes; } expect;
static const expect s_expect[] = {
	{ "uFogColor", "4fv", offsetof(CombinerValues, fogColor), 16 },
	{ "uCenterColor", "4fv", offsetof(CombinerValues, centerColor), 16 },
	{ "uScaleColor", "4fv", offsetof(CombinerValues, scaleColor), 16 },
	{ "uBlendColor", "4fv", offsetof(CombinerValues, blendColor), 16 },
	{ "uEnvColor", "4fv", offsetof(CombinerValues, envColor), 16 },
	{ "uPrimColor", "4fv", offsetof(CombinerValues, primColor), 16 },
	{ "uRectColor", "4fv", offsetof(CombinerValues, rectColor), 16 },
	{ "uBlendMux1", "4i", offsetof(CombinerValues, blendMux1), 16 },
	{ "uBlendMux2", "4i", offsetof(CombinerValues, blendMux2), 16 },
	{ "uConvertParams", "4i", offsetof(CombinerValues, convertParams), 16 },
	{ "uFogScale", "2f", offsetof(CombinerValues, fogScale), 8 },
	{ "uNoiseOffset", "2f", offsetof(CombinerValues, noiseOffset), 8 },
	{ "uScreenScale", "2f", offsetof(CombinerValues, screenScale), 8 },
	{ "uDepthScale", "2f", offsetof(CombinerValues, depthScale), 8 },
	{ "uScreenCoordsScale", "2f", offsetof(CombinerValues, screenCoordsScale), 8 },
	{ "uTexScale", "2f", offsetof(CombinerValues, texScale), 8 },
	{ "uTexOffset[0]", "2f", offsetof(CombinerValues, texOffset0), 8 },
	{ "uTexOffset[1]", "2f", offsetof(CombinerValues, texOffset1), 8 },
	{ "uCacheShiftScale[0]", "2f", offsetof(CombinerValues, cacheShiftScale0), 8 },
	{ "uCacheShiftScale[1]", "2f", offsetof(CombinerValues, cacheShiftScale1), 8 },
	{ "uCacheScale[0]", "2f", offsetof(CombinerValues, cacheScale0), 8 },
	{ "uCacheScale[1]", "2f", offsetof(CombinerValues, cacheScale1), 8 },
	{ "uCacheOffset[0]", "2f", offsetof(CombinerValues, cacheOffset0), 8 },
	{ "uCacheOffset[1]", "2f", offsetof(CombinerValues, cacheOffset1), 8 },
	{ "uTextureSize[0]", "2f", offsetof(CombinerValues, textureSize0), 8 },
	{ "uTextureSize[1]", "2f", offsetof(CombinerValues, textureSize1), 8 },
	{ "uFbMonochrome", "2i", offsetof(CombinerValues, fbMonochrome), 8 },
	{ "uFbFixedAlpha", "2i", offsetof(CombinerValues, fbFixedAlpha), 8 },
	{ "uMSTexEnabled", "2i", offsetof(CombinerValues, msTexEnabled), 8 },
	{ "uTextureFormat", "2i", offsetof(CombinerValues, textureFormat), 8 },
	{ "uCacheFrameBuffer", "2i", offsetof(CombinerValues, cacheFrameBuffer), 8 },
	{ "uFogUsage", "1i", offsetof(CombinerValues, fogUsage), 4 },
	{ "uForceBlendCycle1", "1i", offsetof(CombinerValues, forceBlendCycle1), 4 },
	{ "uForceBlendCycle2", "1i", offsetof(CombinerValues, forceBlendCycle2), 4 },
	{ "uAlphaCompareMode", "1i", offsetof(CombinerValues, alphaCompareMode), 4 },
	{ "uAlphaDitherMode", "1i", offsetof(CombinerValues, alphaDitherMode), 4 },
	{ "uColorDitherMode", "1i", offsetof(CombinerValues, colorDitherMode), 4 },
	{ "uMaxTile", "1i", offsetof(CombinerValues, maxTile), 4 },
	{ "uEnableLod", "1i", offsetof(CombinerValues, enableLod), 4 },
	{ "uTextureDetail", "1i", offsetof(CombinerValues, textureDetail), 4 },
	{ "uTexturePersp", "1i", offsetof(CombinerValues, texturePersp), 4 },
	{ "uTextureFilterMode", "1i", offsetof(CombinerValues, textureFilterMode), 4 },
	{ "uTextureConvert", "1i", offsetof(CombinerValues, textureConvert), 4 },
	{ "uEnableAlphaTest", "1i", offsetof(CombinerValues, enableAlphaTest), 4 },
	{ "uAlphaCvgSel", "1i", offsetof(CombinerValues, alphaCvgSel), 4 },
	{ "uCvgXAlpha", "1i", offsetof(CombinerValues, cvgXAlpha), 4 },
	{ "uEnableDepth", "1i", offsetof(CombinerValues, enableDepth), 4 },
	{ "uEnableDepthCompare", "1i", offsetof(CombinerValues, enableDepthCompare), 4 },
	{ "uEnableDepthUpdate", "1i", offsetof(CombinerValues, enableDepthUpdate), 4 },
	{ "uDepthMode", "1i", offsetof(CombinerValues, depthMode), 4 },
	{ "uDepthSource", "1i", offsetof(CombinerValues, depthSource), 4 },
	{ "uRenderTarget", "1i", offsetof(CombinerValues, renderTarget), 4 },
	{ "uClampMode", "1i", offsetof(CombinerValues, clampMode), 4 },
	{ "uAlphaTestValue", "1f", offsetof(CombinerValues, alphaTestValue), 4 },
	{ "uMinLod", "1f", offsetof(CombinerValues, minLod), 4 },
	{ "uPrimDepth", "1f", offsetof(CombinerValues, primDepth), 4 },
	{ "uDeltaZ", "1f", offsetof(CombinerValues, deltaZ), 4 },
	{ "uPolygonOffset", "1f", offsetof(CombinerValues, polygonOffset), 4 },
	{ "uPrimLod", "1f", offsetof(CombinerValues, primLod), 4 },
	{ "uK4", "1f", offsetof(CombinerValues, k4), 4 },
	{ "uK5", "1f", offsetof(CombinerValues, k5), 4 },
	{ "uMSAASamples", "1i", offsetof(CombinerValues, msaaSamples), 4 }
};
#define EXPECTS (sizeof(s_expect) / sizeof(s_expect[0]))

static gli_gl_info s_info;

static void build_all(void)
{
	static gli_glsl_uniforms u;
	u32 i;
	memset(&u, 0, sizeof(u));
	s_has = s_all;
	s_cycle = 1;
	s_rect = 0;
	memset(&s_info, 0, sizeof(s_info));
	s_asked = 0;
	s_otherProgram = 0;
	gli_glsl_uniforms_build(&u, 42, BIT(G_GCI_TEXEL0) | BIT(G_GCI_HW_LIGHT) | BIT(G_GCI_SHADE), 0x1122334455667788ULL, &s_info);
	CHECK(s_asked == 42 && !s_otherProgram);
	CHECK(s_keyAsked == 0x1122334455667788ULL);
	CHECK(u.groups == 0xABCu);
	CHECK(u.count == 62 + 16);
	CHECK(GLI_GLSL_UNIFORMS_MAX == 62 + 16);
	/* in the order asked, names first */
	for (i = 0; i < u.count; ++i)
		CHECK(u.entries[i].loc == (GLint)(100 + i));
	/* each where its value sits, of its size */
	CHECK(is_entry(&u, "uFogColor", offsetof(CombinerValues, fogColor), 16));
	CHECK(is_entry(&u, "uRectColor", offsetof(CombinerValues, rectColor), 16));
	CHECK(is_entry(&u, "uBlendMux1", offsetof(CombinerValues, blendMux1), 16));
	CHECK(is_entry(&u, "uConvertParams", offsetof(CombinerValues, convertParams), 16));
	CHECK(is_entry(&u, "uFogScale", offsetof(CombinerValues, fogScale), 8));
	CHECK(is_entry(&u, "uTexOffset[1]", offsetof(CombinerValues, texOffset1), 8));
	CHECK(is_entry(&u, "uTextureSize[1]", offsetof(CombinerValues, textureSize1), 8));
	CHECK(is_entry(&u, "uFbMonochrome", offsetof(CombinerValues, fbMonochrome), 8));
	CHECK(is_entry(&u, "uCacheFrameBuffer", offsetof(CombinerValues, cacheFrameBuffer), 8));
	CHECK(is_entry(&u, "uFogUsage", offsetof(CombinerValues, fogUsage), 4));
	CHECK(is_entry(&u, "uClampMode", offsetof(CombinerValues, clampMode), 4));
	CHECK(is_entry(&u, "uAlphaTestValue", offsetof(CombinerValues, alphaTestValue), 4));
	CHECK(is_entry(&u, "uK5", offsetof(CombinerValues, k5), 4));
	CHECK(is_entry(&u, "uMSAASamples", offsetof(CombinerValues, msaaSamples), 4));
	CHECK(is_entry(&u, "uLightDirection[0]", offsetof(CombinerValues, lightDirection), 12));
	CHECK(is_entry(&u, "uLightColor[0]", offsetof(CombinerValues, lightColor), 12));
	CHECK(is_entry(&u, "uLightDirection[7]", offsetof(CombinerValues, lightDirection) + 7 * 16, 12));
	CHECK(is_entry(&u, "uLightColor[5]", offsetof(CombinerValues, lightColor) + 5 * 16, 12));
	/* every named one where its value sits, of its size, sent by its call */
	CHECK(EXPECTS == 62);
	for (i = 0; i < EXPECTS; ++i)
		CHECK(is_entry(&u, s_expect[i].name, s_expect[i].offset, s_expect[i].bytes));
	{
		unsigned char *b = (unsigned char *)&s_src;
		size_t k;
		for (k = 0; k < sizeof(s_src); ++k)
			b[k] = (unsigned char)(k * 7 + 1);
		reset();
		gli_glsl_uniforms_update(&u, 1);
		for (i = 0; i < EXPECTS; ++i) {
			char want[64];
			sprintf(want, ";%s %d ", s_expect[i].call, LOC(s_expect[i].name));
			CHECK(i == 0 ? strncmp(s_log, want + 1, strlen(want + 1)) == 0 : strstr(s_log, want) != NULL);
		}
		CHECK(count_calls() == 62 + 16 + 6);
	}
	/* every table entry checked against the values' layout */
	for (i = 0; i < u.count; ++i)
		CHECK(u.entries[i].offset + u.entries[i].bytes <= sizeof(CombinerValues));
	/* the samplers and their units */
	CHECK(u.samplerCount == 6);
	CHECK(u.samplers[0].loc == LOC("uTexNoise") && u.samplers[0].unit == GLI_TU_NoiseTex);
	CHECK(u.samplers[1].loc == LOC("uDepthTex") && u.samplers[1].unit == GLI_TU_DepthTex);
	CHECK(u.samplers[2].loc == LOC("uTex0") && u.samplers[2].unit == 0);
	CHECK(u.samplers[3].loc == LOC("uTex1") && u.samplers[3].unit == 1);
	CHECK(u.samplers[4].loc == LOC("uMSTex0") && u.samplers[4].unit == GLI_TU_MSTEX0);
	CHECK(u.samplers[5].loc == LOC("uMSTex1") && u.samplers[5].unit == GLI_TU_MSTEX1);
	/* what the values need, from the inputs, key and GL */
	CHECK(s_needs.usesTexture && !s_needs.usesLOD && !s_needs.usesNoise);
	CHECK(s_needs.usesTile0 && !s_needs.usesTile1 && s_needs.usesHwLighting);
	CHECK(!s_needs.textureSize && s_needs.depthScale && !s_needs.clampOffset);
	CHECK(s_needs.cycleType == 1 && !s_needs.isRect);
}

static void build_some(void)
{
	static const char *const some[] = { "uFogColor", "uTex1", "uK4", "uLightColor[0]", "uMSTex0", NULL };
	static gli_glsl_uniforms u;
	memset(&u, 0, sizeof(u));
	s_has = some;
	s_cycle = 2;
	s_rect = 1;
	memset(&s_info, 0, sizeof(s_info));
	s_info.isGLES2 = 1;
	s_info.isGLESX = 1;
	s_info.noPerspective = 1;
	gli_glsl_uniforms_build(&u, 7, BIT(G_GCI_TEXEL1_ALPHA) | BIT(G_GCI_LOD_FRACTION) | BIT(G_GCI_NOISE), 5, &s_info);
	/* the ones it has, and no lights without hardware lighting */
	CHECK(u.count == 2);
	CHECK(u.entries[0].loc == 100 && u.entries[0].offset == offsetof(CombinerValues, fogColor));
	CHECK(u.entries[1].loc == 102 && u.entries[1].offset == offsetof(CombinerValues, k4) && u.entries[1].bytes == 4);
	CHECK(u.samplerCount == 2);
	CHECK(u.samplers[0].loc == 101 && u.samplers[0].unit == 1);
	CHECK(u.samplers[1].loc == 104 && u.samplers[1].unit == GLI_TU_MSTEX0);
	CHECK(s_needs.usesTexture && s_needs.usesLOD && s_needs.usesNoise);
	CHECK(!s_needs.usesTile0 && s_needs.usesTile1 && !s_needs.usesHwLighting);
	CHECK(s_needs.textureSize && !s_needs.depthScale && s_needs.clampOffset);
	CHECK(s_needs.cycleType == 2 && s_needs.isRect);

	/* GLES 3 with perspective: no clamp offset; GL ES without GLESX none */
	memset(&u, 0, sizeof(u));
	s_info.isGLES2 = 0;
	s_info.noPerspective = 0;
	gli_glsl_uniforms_build(&u, 7, 0, 5, &s_info);
	CHECK(!s_needs.clampOffset && !s_needs.textureSize && s_needs.depthScale);
	CHECK(!s_needs.usesTexture);
	memset(&u, 0, sizeof(u));
	s_info.isGLESX = 0;
	s_info.noPerspective = 1;
	gli_glsl_uniforms_build(&u, 7, 0, 5, &s_info);
	CHECK(!s_needs.clampOffset);
}

static void update(void)
{
	static const char *const names[] = {
		"uK4", "uFogScale", "uBlendMux1", "uFbMonochrome", "uClampMode", "uFogColor", "uLightColor[2]",
		"uTex0", "uDepthTex", NULL
	};
	static gli_glsl_uniforms u;
	char want[1024];
	memset(&u, 0, sizeof(u));
	memset(&s_info, 0, sizeof(s_info));
	s_has = names;
	gli_glsl_uniforms_build(&u, 3, BIT(G_GCI_HW_LIGHT), 0, &s_info);
	CHECK(u.count == 7);

	memset(&s_src, 0, sizeof(s_src));
	s_src.k4 = 1.5f;
	s_src.fogScale[0] = 2.0f; s_src.fogScale[1] = 3.0f;
	s_src.blendMux1[0] = 1; s_src.blendMux1[1] = 2; s_src.blendMux1[2] = 3; s_src.blendMux1[3] = 4;
	s_src.fbMonochrome[0] = 5; s_src.fbMonochrome[1] = 6;
	s_src.clampMode = 7;
	s_src.fogColor[0] = 0.25f; s_src.fogColor[1] = 0.5f; s_src.fogColor[2] = 0.75f; s_src.fogColor[3] = 1.0f;
	s_src.lightColor[2][0] = 9.0f; s_src.lightColor[2][1] = 10.0f; s_src.lightColor[2][2] = 11.0f;
	s_src.lightColor[2][3] = 99.0f;

	/* forced: every value by its kind, then the samplers */
	reset();
	s_updates = 0;
	gli_glsl_uniforms_update(&u, 1);
	CHECK(s_updates == 1 && s_groupsAsked == 0xABCu);
	/* in the factory's order, not the program's */
	sprintf(want, "4fv 105 1 0.25 0.5 0.75 1;4i 102 1 2 3 4;2f 101 2 3;2i 103 5 6;1i 104 7;1f 100 1.5;"
		"3fv 106 1 9 10 11;1i 108 %d;1i 107 0;", GLI_TU_DepthTex);
	CHECK(strcmp(s_log, want) == 0);
	if (strcmp(s_log, want) != 0)
		printf("  got  %s\n  want %s\n", s_log, want);

	/* unchanged: nothing sent, no samplers */
	reset();
	gli_glsl_uniforms_update(&u, 0);
	CHECK(s_log[0] == '\0');
	CHECK(s_updates == 2);

	/* each changed alone, one call; a value no uniform reads, none */
	s_src.fogScale[1] = 4.0f;
	reset();
	gli_glsl_uniforms_update(&u, 0);
	CHECK(strcmp(s_log, "2f 101 2 4;") == 0);
	s_src.blendMux1[3] = 8;
	s_src.lightColor[2][1] = 12.0f;
	reset();
	gli_glsl_uniforms_update(&u, 0);
	CHECK(strcmp(s_log, "4i 102 1 2 3 8;3fv 106 1 9 12 11;") == 0);
	s_src.lightColor[2][3] = 98.0f; /* past what the 3 floats read */
	s_src.k5 = 3.0f;
	reset();
	gli_glsl_uniforms_update(&u, 0);
	CHECK(s_log[0] == '\0');
	s_src.fogColor[3] = 0.5f;
	s_src.clampMode = 8;
	s_src.fbMonochrome[1] = 1;
	s_src.k4 = 2.0f;
	reset();
	gli_glsl_uniforms_update(&u, 0);
	CHECK(strcmp(s_log, "4fv 105 1 0.25 0.5 0.75 0.5;2i 103 5 1;1i 104 8;1f 100 2;") == 0);
	CHECK(count_calls() == 4);
}

static void program(void)
{
	static const char *const names[] = { "uK4", "uTex1", NULL };
	gli_gl_cache1 cache;
	gli_cprog *p;
	gli_glsl_cprog *c;
	const u32 inputs = BIT(G_GCI_TEXEL1) | BIT(G_GCI_SHADE_ALPHA) | BIT(G_GCI_NOISE);

	cache.v = 0;
	s_has = names;
	memset(&s_info, 0, sizeof(s_info));
	memset(&s_src, 0, sizeof(s_src));
	s_src.k4 = 1.0f;
	p = gli_glsl_cprog_new(0xFEEDULL, 77, &cache, inputs, &s_info);
	CHECK(p != NULL);
	if (p == NULL)
		return;
	c = (gli_glsl_cprog *)p;
	CHECK(p->ops == &gli_glsl_cprog_ops);
	CHECK(p->key == 0xFEEDULL);
	CHECK(c->program == 77 && c->useProgram == &cache && c->inputs == inputs);
	CHECK(c->uniforms.count == 1 && c->uniforms.samplerCount == 1 && s_asked == 77);
	CHECK(s_keyAsked == 0xFEEDULL);
	CHECK(c->uniforms.uploaded.k4 == 0.0f);
	CHECK(s_needs.usesTexture && s_needs.usesNoise && s_needs.usesTile1 && !s_needs.usesLOD);

	/* what it reads */
	CHECK(p->ops->usesTexture(p));
	CHECK(!p->ops->usesTile(p, 0) && p->ops->usesTile(p, 1));
	CHECK(p->ops->usesShade(p));
	CHECK(!p->ops->usesLOD(p));
	CHECK(!p->ops->usesHwLighting(p));
	CHECK(p->ops->setTextureSize == NULL && p->ops->setEnableAlphaTest == NULL);

	/* used through the cache */
	reset();
	p->ops->activate(p);
	p->ops->activate(p);
	CHECK(strcmp(s_log, "use 77;") == 0);
	CHECK(cache.v == 77);

	/* the first update forced, whatever it is asked */
	cache.v = 0;
	reset();
	p->ops->update(p, 0);
	CHECK(strcmp(s_log, "use 77;1f 100 1;1i 101 1;") == 0);
	reset();
	p->ops->update(p, 0);
	CHECK(s_log[0] == '\0');
	reset();
	p->ops->update(p, 1);
	CHECK(strcmp(s_log, "1f 100 1;1i 101 1;") == 0);
	cache.v = 5;
	reset();
	p->ops->update(p, 0);
	CHECK(strcmp(s_log, "use 77;") == 0);

	/* the stored form */
	reset();
	s_binLen = 0;
	CHECK(gli_glsl_cprog_binary_size(p) == 0);
	CHECK(strcmp(s_log, "getprog 77 len;") == 0);
	s_binLen = -1;
	CHECK(gli_glsl_cprog_binary_size(p) == 0);
	s_binLen = 1;
	CHECK(gli_glsl_cprog_binary_size(p) == 8 + sizeof(int) + sizeof(GLenum) + sizeof(GLint) + 1);
	s_binLen = 10;
	{
		const size_t head = 8 + sizeof(int) + sizeof(GLenum) + sizeof(GLint);
		char out[64];
		u64 key;
		int in;
		GLenum fmt;
		GLint len;
		int i, ok = 1;
		CHECK(gli_glsl_cprog_binary_size(p) == head + 10);
		/* too little room: nothing asked of GL */
		reset();
		CHECK(gli_glsl_cprog_binary_write(p, out, head) == 0);
		CHECK(s_log[0] == '\0');
		/* a GL error: nothing */
		s_glError = 1;
		CHECK(gli_glsl_cprog_binary_write(p, out, head + 10) == 0);
		s_glError = 0;
		memset(out, 0, sizeof(out));
		reset();
		CHECK(gli_glsl_cprog_binary_write(p, out, head + 10) == head + 10);
		CHECK(strcmp(s_log, "bin 77 10;") == 0);
		memcpy(&key, out, 8);
		memcpy(&in, out + 8, sizeof(in));
		memcpy(&fmt, out + 8 + sizeof(int), sizeof(fmt));
		memcpy(&len, out + 8 + sizeof(int) + sizeof(GLenum), sizeof(len));
		CHECK(key == 0xFEEDULL);
		CHECK(in == (int)inputs);
		CHECK(fmt == 0x1234);
		CHECK(len == 10);
		for (i = 0; i < 10; ++i)
			ok &= (unsigned char)out[head + i] == (unsigned char)(0xA0 + i);
		CHECK(ok);
		CHECK(out[head + 10] == 0);
		/* GL writing less than it said: the length it wrote */
		s_binLen = 0;
		CHECK(gli_glsl_cprog_binary_write(p, out, head + 10) == 0);
		s_binLen = 10;
	}

	/* one that reads the LOD fraction says so */
	{
		gli_cprog *q = gli_glsl_cprog_new(1, 78, &cache, BIT(G_GCI_LOD_FRACTION), &s_info);
		CHECK(q != NULL && q->ops->usesLOD(q) && q->ops->usesTexture(q) && !q->ops->usesShade(q));
		CHECK(q != NULL && !q->ops->usesTile(q, 0) && !q->ops->usesTile(q, 1));
		if (q != NULL)
			q->ops->free_program(q);
	}

	/* let go: not used, deleted */
	cache.v = 77;
	reset();
	p->ops->free_program(p);
	CHECK(strcmp(s_log, "use 0;del 77;") == 0);
	CHECK(cache.v == 0);
}

int main(void)
{
	build_all();
	build_some();
	update();
	program();
	if (fails) {
		printf("test_gliden64_glsl_program: %d failures\n", fails);
		return 1;
	}
	printf("test_gliden64_glsl_program: ok\n");
	return 0;
}
