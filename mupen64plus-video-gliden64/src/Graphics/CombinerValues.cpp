#include <string.h>
#include <Config.h>
#include <Combiner.h>
#include <Textures.h>
#include <NoiseTexture.h>
#include <FrameBuffer.h>
#include <DisplayWindow.h>
#include <GBI.h>
#include <RSP.h>
#include <gSP.h>
#include <gDP.h>
#include <VI.h>
#include <Graphics/Context.h>
#include <Graphics/Parameters.h>
#include "CombinerValues.h"

using namespace graphics;


static inline void set2(f32 * _d, f32 _a, f32 _b) { _d[0] = _a; _d[1] = _b; }
static inline void set2(s32 * _d, s32 _a, s32 _b) { _d[0] = _a; _d[1] = _b; }
static inline void set4(s32 * _d, s32 _a, s32 _b, s32 _c, s32 _e) { _d[0] = _a; _d[1] = _b; _d[2] = _c; _d[3] = _e; }
static inline void copy4(f32 * _d, const f32 * _s) { memcpy(_d, _s, sizeof(f32) * 4); }

extern "C" unsigned combinerValueGroups(const CombinerNeeds * _needs)
{
	const CombinerNeeds & _n = *_needs;
	u32 g = 0;

	if (_n.usesNoise)
		g |= CVG_UsesNoise;
	if (_n.usesTile0)
		g |= CVG_UsesTile0;
	if (_n.usesTile1)
		g |= CVG_UsesTile1;
	if (_n.depthScale)
		g |= CVG_DepthScale;

	if (_n.usesTexture) {
		if (config.video.multisampling != 0)
			g |= CVG_MSAA;
		g |= CVG_FrameBuffer;
		if (_n.usesLOD) {
			g |= CVG_Mipmap1;
			if (config.generalEmulation.enableLOD != 0)
				g |= CVG_Mipmap2;
		} else if (_n.cycleType < G_CYC_COPY) {
			g |= CVG_TextureFetch;
		}
		g |= CVG_TexturePersp;
		if (_n.textureSize)
			g |= CVG_TextureSize;
		if (!_n.isRect)
			g |= CVG_TextureParams;
	}

	if (config.generalEmulation.enableLegacyBlending == 0) {
		if (_n.cycleType == G_CYC_1CYCLE)
			g |= CVG_Blend1Cycle;
		else if (_n.cycleType == G_CYC_2CYCLE)
			g |= CVG_Blend2Cycle;
	}

	g |= config.frameBufferEmulation.N64DepthCompare != 0 ? CVG_DepthInfo : CVG_DepthSource;

	if (config.generalEmulation.enableFragmentDepthWrite != 0 ||
		config.frameBufferEmulation.N64DepthCompare != 0)
		g |= CVG_RenderTarget;

	if (_n.clampOffset)
		g |= CVG_ClampOffset;
	if (_n.isRect)
		g |= CVG_RectColor;
	if (_n.usesHwLighting)
		g |= CVG_Lights;
	return g;
}

static void updateFrameBufferInfo(CombinerValues & _v)
{
	int mono0 = 0, mono1 = 0;
	int fixedAlpha0 = 0, fixedAlpha1 = 0;
	int ms0 = 0, ms1 = 0;
	TextureCache & cache = textureCache();
	if (cache.current[0] != nullptr && cache.current[0]->frameBufferTexture != CachedTexture::fbNone) {
		if (cache.current[0]->size == G_IM_SIZ_8b) {
			mono0 = 1;
			if (gDP.otherMode.imageRead == 0)
				fixedAlpha0 = 1;
		} else if (gSP.textureTile[0]->size == G_IM_SIZ_16b && gSP.textureTile[0]->format == G_IM_FMT_IA) {
			mono0 = 2;
		} else if ((config.generalEmulation.hacks & hack_ZeldaMonochrome) != 0 &&
				   cache.current[0]->size == G_IM_SIZ_16b &&
				   gSP.textureTile[0]->size == G_IM_SIZ_8b &&
				   gSP.textureTile[0]->format == G_IM_FMT_CI) {
			// Zelda monochrome effect
			mono0 = 3;
			mono1 = 3;
		}
		ms0 = cache.current[0]->frameBufferTexture == CachedTexture::fbMultiSample ? 1 : 0;
	}
	if (cache.current[1] != nullptr && cache.current[1]->frameBufferTexture != CachedTexture::fbNone) {
		if (cache.current[1]->size == G_IM_SIZ_8b) {
			mono1 = 1;
			if (gDP.otherMode.imageRead == 0)
				fixedAlpha1 = 1;
		} else if (gSP.textureTile[1]->size == G_IM_SIZ_16b && gSP.textureTile[1]->format == G_IM_FMT_IA) {
			mono1 = 2;
		}
		ms1 = cache.current[1]->frameBufferTexture == CachedTexture::fbMultiSample ? 1 : 0;
	}
	set2(_v.fbMonochrome, mono0, mono1);
	set2(_v.fbFixedAlpha, fixedAlpha0, fixedAlpha1);
	set2(_v.msTexEnabled, ms0, ms1);
	gDP.changed &= ~CHANGED_FB_TEXTURE;
}

static void updateFog(CombinerValues & _v)
{
	if (RSP.LLE) {
		_v.fogUsage = 0;
		return;
	}

	int usage = ((gSP.geometryMode & G_FOG) != 0) ? 1 : 0;
	if (gli_GBI->type == F3DAM) {
		const s16 fogMode = ((gSP.geometryMode >> 13) & 9) + 0xFFF8;
		if (fogMode == 0)
			usage = 1;
		else if (fogMode > 0)
			usage = 2;
	}
	_v.fogUsage = usage;
	set2(_v.fogScale, gSP.fog.multiplierf, gSP.fog.offsetf);
}

static void updateBlend1Cycle(CombinerValues & _v)
{
	if (config.generalEmulation.enableLegacyBlending == 1) {
		_v.forceBlendCycle1 = 0;
		return;
	}
	set4(_v.blendMux1, gDP.otherMode.c1_m1a, gDP.otherMode.c1_m1b, gDP.otherMode.c1_m2a, gDP.otherMode.c1_m2b);
	_v.forceBlendCycle1 = (int)gDP.otherMode.forceBlender;
}

static void updateBlend2Cycle(CombinerValues & _v)
{
	if (config.generalEmulation.enableLegacyBlending == 1) {
		_v.forceBlendCycle1 = 0;
		_v.forceBlendCycle2 = 0;
		return;
	}

	set4(_v.blendMux1, gDP.otherMode.c1_m1a, gDP.otherMode.c1_m1b, gDP.otherMode.c1_m2a, gDP.otherMode.c1_m2b);
	set4(_v.blendMux2, gDP.otherMode.c2_m1a, gDP.otherMode.c2_m1b, gDP.otherMode.c2_m2a, gDP.otherMode.c2_m2b);
	_v.forceBlendCycle1 = 1;
	_v.forceBlendCycle2 = gDP.otherMode.forceBlender;

	// Modes, which shader blender can't emulate
	const u32 mode = _SHIFTR(gDP.otherMode.l, 16, 16);
	switch (mode) {
	case 0x0040:
		// Mia Hamm Soccer
		// clr_in * a_in + clr_mem * (1-a)
		// clr_in * a_in + clr_in * (1-a)
	case 0x0050:
		// A Bug's Life
		// clr_in * a_in + clr_mem * (1-a)
		// clr_in * a_in + clr_mem * (1-a)
		_v.forceBlendCycle1 = 0;
		_v.forceBlendCycle2 = 0;
		break;
	case 0x0150:
		// Tony Hawk
		// clr_in * a_in + clr_mem * (1-a)
		// clr_in * a_fog + clr_mem * (1-a_fog)
		if ((config.generalEmulation.hacks & hack_TonyHawk) != 0) {
			_v.forceBlendCycle1 = 0;
			_v.forceBlendCycle2 = 0;
		}
		break;
	}
}

static void updateDither(CombinerValues & _v, bool _usesNoise)
{
	if (gDP.otherMode.cycleType < G_CYC_COPY) {
		_v.alphaCompareMode = gDP.otherMode.alphaCompare;
		_v.alphaDitherMode = gDP.otherMode.alphaDither;
		_v.colorDitherMode = gDP.otherMode.colorDither;
	} else {
		_v.alphaCompareMode = 0;
		_v.alphaDitherMode = 0;
		_v.colorDitherMode = 0;
	}

	bool updateNoiseTex = _usesNoise;
	updateNoiseTex |= (gDP.otherMode.cycleType < G_CYC_COPY) && (gDP.otherMode.colorDither == G_CD_NOISE || gDP.otherMode.alphaDither == G_AD_NOISE || gDP.otherMode.alphaCompare == G_AC_DITHER);
	if (updateNoiseTex) {
		g_noiseTexture.update();
		set2(_v.noiseOffset, f32(g_noiseTexture.getOffsetX()), f32(g_noiseTexture.getOffsetY()));
	}
}

static void updateScreenScale(CombinerValues & _v)
{
	if (dwnd().getDrawer().isTexrectDrawerMode()) {
		set2(_v.screenScale, 1.0f, 1.0f);
		return;
	}

	FrameBuffer * pBuffer = frameBufferList().getCurrent();
	if (pBuffer == nullptr)
		set2(_v.screenScale, dwnd().getScaleX(), dwnd().getScaleY());
	else
		set2(_v.screenScale, pBuffer->m_scale, pBuffer->m_scale);
}

static void updateTextureFetch(CombinerValues & _v)
{
	_v.textureFilterMode = gDP.otherMode.textureFilter;
	set2(_v.textureFormat, gSP.textureTile[0]->format, gSP.textureTile[1]->format);
	_v.textureConvert = gDP.otherMode.convert_one;
	if (gDP.otherMode.bi_lerp0 == 0 || gDP.otherMode.bi_lerp1 == 0)
		set4(_v.convertParams, gDP.convert.k0, gDP.convert.k1, gDP.convert.k2, gDP.convert.k3);
}

static void updateAlphaTest(CombinerValues & _v)
{
	if (gDP.otherMode.cycleType == G_CYC_FILL) {
		_v.enableAlphaTest = 0;
	} else if (gDP.otherMode.cycleType == G_CYC_COPY) {
		if (gDP.otherMode.alphaCompare & G_AC_THRESHOLD) {
			_v.enableAlphaTest = 1;
			_v.alphaCvgSel = 0;
			_v.alphaTestValue = 0.5f;
		} else {
			_v.enableAlphaTest = 0;
		}
	} else if ((gDP.otherMode.alphaCompare & G_AC_THRESHOLD) != 0) {
		_v.enableAlphaTest = 1;
		_v.alphaTestValue = gDP.blendColor.a;
		_v.alphaCvgSel = gDP.otherMode.alphaCvgSel;
	} else {
		_v.enableAlphaTest = 0;
	}
	_v.cvgXAlpha = gDP.otherMode.cvgXAlpha;
}

static void updateDepthInfo(CombinerValues & _v)
{
	FrameBuffer * pBuffer = frameBufferList().getCurrent();
	if (pBuffer == nullptr || pBuffer->m_pDepthBuffer == nullptr)
		return;

	const bool depthEnabled = ((gSP.geometryMode & G_ZBUFFER) || gDP.otherMode.depthSource == G_ZS_PRIM) &&
							  gDP.otherMode.cycleType <= G_CYC_2CYCLE;
	_v.enableDepth = depthEnabled ? 1 : 0;
	if (depthEnabled) {
		_v.enableDepthCompare = gDP.otherMode.depthCompare;
		_v.enableDepthUpdate = gDP.otherMode.depthUpdate;
	} else {
		_v.enableDepthCompare = 0;
		_v.enableDepthUpdate = 0;
	}
	_v.depthMode = gDP.otherMode.depthMode;
	_v.depthSource = gDP.otherMode.depthSource;
	if (gDP.otherMode.depthSource == G_ZS_PRIM) {
		_v.deltaZ = gDP.primDepth.deltaZ;
		_v.primDepth = gDP.primDepth.z;
	}
}

static void updateTextureParams(CombinerValues & _v, u32 _groups)
{
	int nFB[2] = { 0, 0 };
	TextureCache & cache = textureCache();
	f32 * const offset[2] = { _v.texOffset0, _v.texOffset1 };
	f32 * const shiftScale[2] = { _v.cacheShiftScale0, _v.cacheShiftScale1 };
	f32 * const scale[2] = { _v.cacheScale0, _v.cacheScale1 };
	f32 * const cacheOffset[2] = { _v.cacheOffset0, _v.cacheOffset1 };

	for (u32 t = 0; t < 2; ++t) {
		if ((_groups & (t == 0 ? CVG_UsesTile0 : CVG_UsesTile1)) == 0)
			continue;

		if (gSP.textureTile[t] != nullptr) {
			if (gSP.textureTile[t]->textureMode == TEXTUREMODE_BGIMAGE || gSP.textureTile[t]->textureMode == TEXTUREMODE_FRAMEBUFFER_BG)
				set2(offset[t], 0.0f, 0.0f);
			else {
				float fuls = gSP.textureTile[t]->fuls;
				float fult = gSP.textureTile[t]->fult;
				if (gSP.textureTile[t]->frameBufferAddress > 0) {
					FrameBuffer * pBuffer = frameBufferList().getBuffer(gSP.textureTile[t]->frameBufferAddress);
					if (pBuffer != nullptr) {
						if (gSP.textureTile[t]->masks > 0 && gSP.textureTile[t]->clamps == 0)
							fuls = float(gSP.textureTile[t]->uls % (1 << gSP.textureTile[t]->masks));
						if (gSP.textureTile[t]->maskt > 0 && gSP.textureTile[t]->clampt == 0)
							fult = float(gSP.textureTile[t]->ult % (1 << gSP.textureTile[t]->maskt));
					} else {
						gSP.textureTile[t]->frameBufferAddress = 0;
					}
				}
				set2(offset[t], fuls, fult);
			}
		}

		if (cache.current[t] != nullptr) {
			f32 shiftScaleS = 1.0f;
			f32 shiftScaleT = 1.0f;
			getTextureShiftScale(t, cache, shiftScaleS, shiftScaleT);
			set2(shiftScale[t], shiftScaleS, shiftScaleT);
			set2(scale[t], cache.current[t]->scaleS, cache.current[t]->scaleT);
			set2(cacheOffset[t], cache.current[t]->offsetS, cache.current[t]->offsetT);
			nFB[t] = cache.current[t]->frameBufferTexture;
		}
	}

	set2(_v.cacheFrameBuffer, nFB[0], nFB[1]);
	set2(_v.texScale, gSP.texture.scales, gSP.texture.scalet);
}

static void updateColors(CombinerValues & _v)
{
	copy4(_v.fogColor, &gDP.fogColor.r);
	copy4(_v.centerColor, &gDP.key.center.r);
	copy4(_v.scaleColor, &gDP.key.scale.r);
	copy4(_v.blendColor, &gDP.blendColor.r);
	copy4(_v.envColor, &gDP.envColor.r);
	copy4(_v.primColor, &gDP.primColor.r);
	_v.primLod = gDP.primColor.l;
	_v.k4 = _FIXED2FLOATCOLOR(gDP.convert.k4, 8 );
	_v.k5 = _FIXED2FLOATCOLOR(gDP.convert.k5, 8 );
}

extern "C" void updateCombinerValues(CombinerValues * _values, unsigned _groups)
{
	CombinerValues & _v = *_values;
	if (_groups & CVG_DepthScale) {
		if (RSP.LLE)
			set2(_v.depthScale, 0.5f, 0.5f);
		else
			set2(_v.depthScale, gSP.viewport.vscale[2], gSP.viewport.vtrans[2]);
	}

	if (_groups & CVG_MSAA)
		_v.msaaSamples = config.video.multisampling;

	if (_groups & CVG_FrameBuffer)
		updateFrameBufferInfo(_v);

	if (_groups & CVG_Mipmap1) {
		_v.minLod = gDP.primColor.m;
		_v.maxTile = gSP.texture.level;
	}

	if (_groups & CVG_Mipmap2) {
		_v.enableLod = (gDP.otherMode.textureLOD == G_TL_LOD) ? 1 : 0;
		_v.textureDetail = gDP.otherMode.textureDetail;
	}

	if (_groups & CVG_TextureFetch)
		updateTextureFetch(_v);

	if (_groups & CVG_TexturePersp)
		_v.texturePersp = (RSP.LLE || gli_GBI->texturePersp) ? gDP.otherMode.texturePersp : 1U;

	if (_groups & CVG_TextureSize) {
		TextureCache & cache = textureCache();
		if ((_groups & CVG_UsesTile0) && cache.current[0] != nullptr)
			set2(_v.textureSize0, f32(cache.current[0]->realWidth), f32(cache.current[0]->realHeight));
		if ((_groups & CVG_UsesTile1) && cache.current[1] != nullptr)
			set2(_v.textureSize1, f32(cache.current[1]->realWidth), f32(cache.current[1]->realHeight));
	}

	if (_groups & CVG_TextureParams)
		updateTextureParams(_v, _groups);

	updateFog(_v);

	if (_groups & CVG_Blend1Cycle)
		updateBlend1Cycle(_v);
	else if (_groups & CVG_Blend2Cycle)
		updateBlend2Cycle(_v);

	updateDither(_v, (_groups & CVG_UsesNoise) != 0);
	updateScreenScale(_v);
	updateAlphaTest(_v);

	if (_groups & CVG_DepthInfo) {
		updateDepthInfo(_v);
	} else if (_groups & CVG_DepthSource) {
		_v.depthSource = gDP.otherMode.depthSource;
		if (gDP.otherMode.depthSource == G_ZS_PRIM)
			_v.primDepth = gDP.primDepth.z;
	}

	if (_groups & CVG_RenderTarget) {
		int renderTarget = 0;
		if (gli_isCurrentColorImageDepthImage())
			renderTarget = gli_isDepthCompareEnabled() ? 2 : 1;
		_v.renderTarget = renderTarget;
	}

	if (_groups & CVG_ClampOffset) {
		switch (gfxContext.getClampMode()) {
		case ClampMode::ClippingEnabled:
			_v.clampMode = 0;
			break;
		case ClampMode::NoNearPlaneClipping:
			_v.clampMode = 1;
			break;
		case ClampMode::NoClipping:
			_v.clampMode = 2;
			break;
		}
		_v.polygonOffset = gfxContext.isEnabled(enable::POLYGON_OFFSET_FILL) ? 0.003f : 0.0f;
	}

	{
		f32 scaleX, scaleY;
		calcCoordsScales(frameBufferList().getCurrent(), scaleX, scaleY);
		set2(_v.screenCoordsScale, 2.0f * scaleX, -2.0f * scaleY);
	}

	updateColors(_v);

	if (_groups & CVG_RectColor)
		copy4(_v.rectColor, &gDP.rectColor.r);

	if (_groups & CVG_Lights) {
		for (u32 i = 0; i <= gSP.numLights; ++i) {
			memcpy(_v.lightDirection[i], gSP.lights.xyz[i], sizeof(f32) * 3);
			memcpy(_v.lightColor[i], gSP.lights.rgb[i], sizeof(f32) * 3);
		}
	}
}
