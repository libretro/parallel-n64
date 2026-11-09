#include <stddef.h>
#include <string.h>
#include <Config.h>
#include <Graphics/Parameters.h>
#include "glsl_CombinerProgramUniformFactory.h"

namespace glsl {

using graphics::CombinerValues;

enum Kind { I1, I2, I4, F1, F2, F3, F4 };

static const u8 kindBytes[] = { 4, 8, 16, 4, 8, 12, 16 };

struct UniformName {
	const char * name;
	Kind kind;
	u16 offset;
};

#define U(A, K, M) { A, K, u16(offsetof(CombinerValues, M)) }

static const UniformName uniformNames[] = {
	U("uFogColor", F4, fogColor), U("uCenterColor", F4, centerColor), U("uScaleColor", F4, scaleColor),
	U("uBlendColor", F4, blendColor), U("uEnvColor", F4, envColor), U("uPrimColor", F4, primColor),
	U("uRectColor", F4, rectColor),
	U("uBlendMux1", I4, blendMux1), U("uBlendMux2", I4, blendMux2), U("uConvertParams", I4, convertParams),
	U("uFogScale", F2, fogScale), U("uNoiseOffset", F2, noiseOffset), U("uScreenScale", F2, screenScale),
	U("uDepthScale", F2, depthScale), U("uScreenCoordsScale", F2, screenCoordsScale), U("uTexScale", F2, texScale),
	U("uTexOffset[0]", F2, texOffset0), U("uTexOffset[1]", F2, texOffset1),
	U("uCacheShiftScale[0]", F2, cacheShiftScale0), U("uCacheShiftScale[1]", F2, cacheShiftScale1),
	U("uCacheScale[0]", F2, cacheScale0), U("uCacheScale[1]", F2, cacheScale1),
	U("uCacheOffset[0]", F2, cacheOffset0), U("uCacheOffset[1]", F2, cacheOffset1),
	U("uTextureSize[0]", F2, textureSize0), U("uTextureSize[1]", F2, textureSize1),
	U("uFbMonochrome", I2, fbMonochrome), U("uFbFixedAlpha", I2, fbFixedAlpha), U("uMSTexEnabled", I2, msTexEnabled),
	U("uTextureFormat", I2, textureFormat), U("uCacheFrameBuffer", I2, cacheFrameBuffer),
	U("uFogUsage", I1, fogUsage), U("uForceBlendCycle1", I1, forceBlendCycle1), U("uForceBlendCycle2", I1, forceBlendCycle2),
	U("uAlphaCompareMode", I1, alphaCompareMode), U("uAlphaDitherMode", I1, alphaDitherMode), U("uColorDitherMode", I1, colorDitherMode),
	U("uMaxTile", I1, maxTile), U("uEnableLod", I1, enableLod), U("uTextureDetail", I1, textureDetail),
	U("uTexturePersp", I1, texturePersp), U("uTextureFilterMode", I1, textureFilterMode), U("uTextureConvert", I1, textureConvert),
	U("uEnableAlphaTest", I1, enableAlphaTest), U("uAlphaCvgSel", I1, alphaCvgSel), U("uCvgXAlpha", I1, cvgXAlpha),
	U("uEnableDepth", I1, enableDepth), U("uEnableDepthCompare", I1, enableDepthCompare), U("uEnableDepthUpdate", I1, enableDepthUpdate),
	U("uDepthMode", I1, depthMode), U("uDepthSource", I1, depthSource), U("uRenderTarget", I1, renderTarget), U("uClampMode", I1, clampMode),
	U("uAlphaTestValue", F1, alphaTestValue), U("uMinLod", F1, minLod), U("uPrimDepth", F1, primDepth), U("uDeltaZ", F1, deltaZ),
	U("uPolygonOffset", F1, polygonOffset), U("uPrimLod", F1, primLod), U("uK4", F1, k4), U("uK5", F1, k5),
	U("uMSAASamples", I1, msaaSamples),
};

#undef U

void CombinerUniforms::_add(GLint _loc, int _kind, u32 _offset)
{
	if (_loc < 0)
		return;
	Entry e = { _loc, u16(_offset), u8(_kind), kindBytes[_kind] };
	m_entries.push_back(e);
}

void CombinerUniforms::update(bool _force)
{
	graphics::updateCombinerValues(m_values, m_groups);

	const u8 * const values = reinterpret_cast<const u8*>(&m_values);
	u8 * const uploaded = reinterpret_cast<u8*>(&m_uploaded);
	for (const Entry & e : m_entries) {
		const void * const src = values + e.offset;
		if (!_force && memcmp(uploaded + e.offset, src, e.bytes) == 0)
			continue;
		memcpy(uploaded + e.offset, src, e.bytes);
		const GLint * const iv = static_cast<const GLint*>(src);
		const GLfloat * const fv = static_cast<const GLfloat*>(src);
		switch (e.kind) {
		case I1: glUniform1i(e.loc, iv[0]); break;
		case I2: glUniform2i(e.loc, iv[0], iv[1]); break;
		case I4: glUniform4i(e.loc, iv[0], iv[1], iv[2], iv[3]); break;
		case F1: glUniform1f(e.loc, fv[0]); break;
		case F2: glUniform2f(e.loc, fv[0], fv[1]); break;
		case F3: glUniform3fv(e.loc, 1, fv); break;
		case F4: glUniform4fv(e.loc, 1, fv); break;
		}
	}

	if (!_force)
		return;
	/* which texture unit each sampler reads */
	for (const Sampler & s : m_samplers)
		glUniform1i(s.loc, s.unit);
}

/*---------------CombinerProgramUniformFactory-------------*/

void CombinerProgramUniformFactory::buildUniforms(GLuint _program,
												  const CombinerInputs & _inputs,
												  const CombinerKey & _key,
												  CombinerUniforms & _uniforms)
{
	graphics::CombinerNeeds needs;
	needs.usesTexture = _inputs.usesTexture();
	needs.usesLOD = _inputs.usesLOD();
	needs.usesNoise = _inputs.usesNoise();
	needs.usesTile0 = _inputs.usesTile(0);
	needs.usesTile1 = _inputs.usesTile(1);
	needs.usesHwLighting = _inputs.usesHwLighting();
	needs.textureSize = m_glInfo.isGLES2;
	needs.depthScale = !m_glInfo.isGLES2;
	needs.clampOffset = m_glInfo.isGLESX && m_glInfo.noPerspective;
	_uniforms.m_groups = graphics::combinerValueGroups(needs, _key);

	_uniforms.m_entries.reserve(sizeof(uniformNames) / sizeof(uniformNames[0]) + 16);
	for (const UniformName & n : uniformNames)
		_uniforms._add(glGetUniformLocation(_program, n.name), n.kind, n.offset);

	if (needs.usesHwLighting) {
		char buf[32];
		for (u32 i = 0; i < 8; ++i) {
			sprintf(buf, "uLightDirection[%u]", i);
			_uniforms._add(glGetUniformLocation(_program, buf), F3, u32(offsetof(CombinerValues, lightDirection) + i * sizeof(f32) * 4));
			sprintf(buf, "uLightColor[%u]", i);
			_uniforms._add(glGetUniformLocation(_program, buf), F3, u32(offsetof(CombinerValues, lightColor) + i * sizeof(f32) * 4));
		}
	}

	const CombinerUniforms::Sampler samplers[] = {
		{ glGetUniformLocation(_program, "uTexNoise"), GLint(int(graphics::textureIndices::NoiseTex)) },
		{ glGetUniformLocation(_program, "uDepthTex"), GLint(int(graphics::textureIndices::DepthTex)) },
		{ glGetUniformLocation(_program, "uTex0"), 0 },
		{ glGetUniformLocation(_program, "uTex1"), 1 },
		{ glGetUniformLocation(_program, "uMSTex0"), GLint(int(graphics::textureIndices::MSTex[0])) },
		{ glGetUniformLocation(_program, "uMSTex1"), GLint(int(graphics::textureIndices::MSTex[1])) },
	};
	for (const CombinerUniforms::Sampler & s : samplers)
		if (s.loc >= 0)
			_uniforms.m_samplers.push_back(s);
}

CombinerProgramUniformFactory::CombinerProgramUniformFactory(const opengl::GLInfo & _glInfo)
: m_glInfo(_glInfo)
{
}

}
