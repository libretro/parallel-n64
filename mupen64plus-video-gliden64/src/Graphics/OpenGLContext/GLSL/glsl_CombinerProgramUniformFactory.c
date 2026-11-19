/* A GL combiner program's uniforms: which of the combiner's values it
 * has, and getting them to it. C89. */
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <CombinerKey.h>
#include <Graphics/gli_params.h>
#include <Graphics/CombinerStages.h>
#include <Graphics/OpenGLContext/opengl_GLInfo.h>
#include "glsl_CombinerProgramImpl.h"

#define I1 GLI_GLSL_I1
#define I2 GLI_GLSL_I2
#define I4 GLI_GLSL_I4
#define F1 GLI_GLSL_F1
#define F2 GLI_GLSL_F2
#define F3 GLI_GLSL_F3
#define F4 GLI_GLSL_F4

static const u8 s_kindBytes[] = { 4, 8, 16, 4, 8, 12, 16 };

typedef struct uniform_name
{
	const char *name;
	u8 kind;
	u16 offset;
} uniform_name;

#define N(A, K, M) { A, K, (u16)offsetof(CombinerValues, M) }

static const uniform_name s_names[] = {
	N("uFogColor", F4, fogColor), N("uCenterColor", F4, centerColor), N("uScaleColor", F4, scaleColor),
	N("uBlendColor", F4, blendColor), N("uEnvColor", F4, envColor), N("uPrimColor", F4, primColor),
	N("uRectColor", F4, rectColor),
	N("uBlendMux1", I4, blendMux1), N("uBlendMux2", I4, blendMux2), N("uConvertParams", I4, convertParams),
	N("uFogScale", F2, fogScale), N("uNoiseOffset", F2, noiseOffset), N("uScreenScale", F2, screenScale),
	N("uDepthScale", F2, depthScale), N("uScreenCoordsScale", F2, screenCoordsScale), N("uTexScale", F2, texScale),
	N("uTexOffset[0]", F2, texOffset0), N("uTexOffset[1]", F2, texOffset1),
	N("uCacheShiftScale[0]", F2, cacheShiftScale0), N("uCacheShiftScale[1]", F2, cacheShiftScale1),
	N("uCacheScale[0]", F2, cacheScale0), N("uCacheScale[1]", F2, cacheScale1),
	N("uCacheOffset[0]", F2, cacheOffset0), N("uCacheOffset[1]", F2, cacheOffset1),
	N("uTextureSize[0]", F2, textureSize0), N("uTextureSize[1]", F2, textureSize1),
	N("uFbMonochrome", I2, fbMonochrome), N("uFbFixedAlpha", I2, fbFixedAlpha), N("uMSTexEnabled", I2, msTexEnabled),
	N("uTextureFormat", I2, textureFormat), N("uCacheFrameBuffer", I2, cacheFrameBuffer),
	N("uFogUsage", I1, fogUsage), N("uForceBlendCycle1", I1, forceBlendCycle1), N("uForceBlendCycle2", I1, forceBlendCycle2),
	N("uAlphaCompareMode", I1, alphaCompareMode), N("uAlphaDitherMode", I1, alphaDitherMode), N("uColorDitherMode", I1, colorDitherMode),
	N("uMaxTile", I1, maxTile), N("uEnableLod", I1, enableLod), N("uTextureDetail", I1, textureDetail),
	N("uTexturePersp", I1, texturePersp), N("uTextureFilterMode", I1, textureFilterMode), N("uTextureConvert", I1, textureConvert),
	N("uEnableAlphaTest", I1, enableAlphaTest), N("uAlphaCvgSel", I1, alphaCvgSel), N("uCvgXAlpha", I1, cvgXAlpha),
	N("uEnableDepth", I1, enableDepth), N("uEnableDepthCompare", I1, enableDepthCompare), N("uEnableDepthUpdate", I1, enableDepthUpdate),
	N("uDepthMode", I1, depthMode), N("uDepthSource", I1, depthSource), N("uRenderTarget", I1, renderTarget), N("uClampMode", I1, clampMode),
	N("uAlphaTestValue", F1, alphaTestValue), N("uMinLod", F1, minLod), N("uPrimDepth", F1, primDepth), N("uDeltaZ", F1, deltaZ),
	N("uPolygonOffset", F1, polygonOffset), N("uPrimLod", F1, primLod), N("uK4", F1, k4), N("uK5", F1, k5),
	N("uMSAASamples", I1, msaaSamples),
};
#undef N

#define NAMES (sizeof(s_names) / sizeof(s_names[0]))
/* the named uniforms and the lights fit */
typedef char gli_glsl_uniforms_fit[NAMES + 16 <= GLI_GLSL_UNIFORMS_MAX ? 1 : -1];

static void add(gli_glsl_uniforms *_u, GLint _loc, int _kind, u32 _offset)
{
	gli_glsl_uniform *e;
	if (_loc < 0)
		return;
	e = &_u->entries[_u->count++];
	e->loc = _loc;
	e->offset = (u16)_offset;
	e->kind = (u8)_kind;
	e->bytes = s_kindBytes[_kind];
}

void gli_glsl_uniforms_build(gli_glsl_uniforms *_u, GLuint _program, u32 _inputs, u64 _key,
	const struct gli_gl_info *_glinfo)
{
	static const char *const samplerNames[GLI_GLSL_SAMPLERS_MAX] = {
		"uTexNoise", "uDepthTex", "uTex0", "uTex1", "uMSTex0", "uMSTex1"
	};
	static const GLint samplerUnits[GLI_GLSL_SAMPLERS_MAX] = {
		GLI_TU_NoiseTex, GLI_TU_DepthTex, 0, 1, GLI_TU_MSTEX0, GLI_TU_MSTEX1
	};
	CombinerNeeds needs;
	u32 i;

	needs.usesTexture = gli_inputs_texture(_inputs);
	needs.usesLOD = gli_inputs_lod(_inputs);
	needs.usesNoise = gli_inputs_noise(_inputs);
	needs.usesTile0 = gli_inputs_tile(_inputs, 0);
	needs.usesTile1 = gli_inputs_tile(_inputs, 1);
	needs.usesHwLighting = gli_inputs_hw_lighting(_inputs);
	needs.textureSize = _glinfo->isGLES2;
	needs.depthScale = !_glinfo->isGLES2;
	needs.clampOffset = _glinfo->isGLESX && _glinfo->noPerspective;
	needs.cycleType = (int)gli_combiner_key_cycle_type(_key);
	needs.isRect = gli_combiner_key_is_rect(_key);
	_u->groups = combinerValueGroups(&needs);

	for (i = 0; i < NAMES; ++i)
		add(_u, glGetUniformLocation(_program, s_names[i].name), s_names[i].kind, s_names[i].offset);

	if (needs.usesHwLighting) {
		char buf[32];
		for (i = 0; i < 8; ++i) {
			sprintf(buf, "uLightDirection[%u]", (unsigned)i);
			add(_u, glGetUniformLocation(_program, buf), F3,
				(u32)(offsetof(CombinerValues, lightDirection) + i * sizeof(f32) * 4));
			sprintf(buf, "uLightColor[%u]", (unsigned)i);
			add(_u, glGetUniformLocation(_program, buf), F3,
				(u32)(offsetof(CombinerValues, lightColor) + i * sizeof(f32) * 4));
		}
	}

	for (i = 0; i < GLI_GLSL_SAMPLERS_MAX; ++i) {
		const GLint loc = glGetUniformLocation(_program, samplerNames[i]);
		if (loc >= 0) {
			_u->samplers[_u->samplerCount].loc = loc;
			_u->samplers[_u->samplerCount].unit = samplerUnits[i];
			++_u->samplerCount;
		}
	}
}
