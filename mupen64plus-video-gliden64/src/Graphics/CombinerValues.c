/* What the combiner shaders read, worked out from the N64's state for
 * every draw (CombinerValues.h). C89.
 *
 * The current frame buffer and the textures in use are fetched once a
 * call, and the window's scale read from its C state: the C++ reached
 * each through its singleton again at every use. */
#include <string.h>
#include "Config.h"
#include "CombinerKey.h"
#include "NoiseTexture.h"
#include "DisplayWindow.h"
#include "TexrectDrawer.h"
#include "GBI.h"
#include "RSP.h"
#include "gSP.h"
#include "gDP.h"
#include "VI.h"
#include "gli_tex.h"
#include "gli_textures.h"
#include "gli_fb_rdram.h"
#include "gli_framebuffer.h"
#include "CombinerValues.h"

#define SET2(_d, _a, _b) do { (_d)[0] = (_a); (_d)[1] = (_b); } while (0)
#define SET4(_d, _a, _b, _c, _e) do { (_d)[0] = (_a); (_d)[1] = (_b); (_d)[2] = (_c); (_d)[3] = (_e); } while (0)
#define COPY4(_d, _s) memcpy((_d), (_s), sizeof(f32) * 4)

unsigned combinerValueGroups(const CombinerNeeds * _n)
{
	u32 g = 0;

	if (_n->usesNoise)
		g |= CVG_UsesNoise;
	if (_n->usesTile0)
		g |= CVG_UsesTile0;
	if (_n->usesTile1)
		g |= CVG_UsesTile1;
	if (_n->depthScale)
		g |= CVG_DepthScale;

	if (_n->usesTexture) {
		if (config.video.multisampling != 0)
			g |= CVG_MSAA;
		g |= CVG_FrameBuffer;
		if (_n->usesLOD) {
			g |= CVG_Mipmap1;
			if (config.generalEmulation.enableLOD != 0)
				g |= CVG_Mipmap2;
		} else if (_n->cycleType < G_CYC_COPY) {
			g |= CVG_TextureFetch;
		}
		g |= CVG_TexturePersp;
		if (_n->textureSize)
			g |= CVG_TextureSize;
		if (!_n->isRect)
			g |= CVG_TextureParams;
	}

	if (config.generalEmulation.enableLegacyBlending == 0) {
		if (_n->cycleType == G_CYC_1CYCLE)
			g |= CVG_Blend1Cycle;
		else if (_n->cycleType == G_CYC_2CYCLE)
			g |= CVG_Blend2Cycle;
	}

	g |= config.frameBufferEmulation.N64DepthCompare != 0 ? CVG_DepthInfo : CVG_DepthSource;

	if (config.generalEmulation.enableFragmentDepthWrite != 0 ||
		config.frameBufferEmulation.N64DepthCompare != 0)
		g |= CVG_RenderTarget;

	if (_n->clampOffset)
		g |= CVG_ClampOffset;
	if (_n->isRect)
		g |= CVG_RectColor;
	if (_n->usesHwLighting)
		g |= CVG_Lights;
	return g;
}

static void updateFrameBufferInfo(CombinerValues * _v, const gli_tex * const * _cur)
{
	int mono0 = 0, mono1 = 0;
	int fixedAlpha0 = 0, fixedAlpha1 = 0;
	int ms0 = 0, ms1 = 0;
	if (_cur[0] != NULL && _cur[0]->frameBufferTexture != GLI_TEX_FB_NONE) {
		if (_cur[0]->size == G_IM_SIZ_8b) {
			mono0 = 1;
			if (gDP.otherMode.imageRead == 0)
				fixedAlpha0 = 1;
		} else if (gSP.textureTile[0]->size == G_IM_SIZ_16b && gSP.textureTile[0]->format == G_IM_FMT_IA) {
			mono0 = 2;
		} else if ((config.generalEmulation.hacks & hack_ZeldaMonochrome) != 0 &&
				   _cur[0]->size == G_IM_SIZ_16b &&
				   gSP.textureTile[0]->size == G_IM_SIZ_8b &&
				   gSP.textureTile[0]->format == G_IM_FMT_CI) {
			/* Zelda monochrome effect */
			mono0 = 3;
			mono1 = 3;
		}
		ms0 = _cur[0]->frameBufferTexture == GLI_TEX_FB_MULTI_SAMPLE ? 1 : 0;
	}
	if (_cur[1] != NULL && _cur[1]->frameBufferTexture != GLI_TEX_FB_NONE) {
		if (_cur[1]->size == G_IM_SIZ_8b) {
			mono1 = 1;
			if (gDP.otherMode.imageRead == 0)
				fixedAlpha1 = 1;
		} else if (gSP.textureTile[1]->size == G_IM_SIZ_16b && gSP.textureTile[1]->format == G_IM_FMT_IA) {
			mono1 = 2;
		}
		ms1 = _cur[1]->frameBufferTexture == GLI_TEX_FB_MULTI_SAMPLE ? 1 : 0;
	}
	SET2(_v->fbMonochrome, mono0, mono1);
	SET2(_v->fbFixedAlpha, fixedAlpha0, fixedAlpha1);
	SET2(_v->msTexEnabled, ms0, ms1);
	gDP.changed &= ~CHANGED_FB_TEXTURE;
}

static void updateFog(CombinerValues * _v)
{
	int usage;
	if (RSP.LLE) {
		_v->fogUsage = 0;
		return;
	}

	usage = ((gSP.geometryMode & G_FOG) != 0) ? 1 : 0;
	if (gli_GBI->type == F3DAM) {
		const s16 fogMode = (s16)(((gSP.geometryMode >> 13) & 9) + 0xFFF8);
		if (fogMode == 0)
			usage = 1;
		else if (fogMode > 0)
			usage = 2;
	}
	_v->fogUsage = usage;
	SET2(_v->fogScale, gSP.fog.multiplierf, gSP.fog.offsetf);
}

static void updateBlend1Cycle(CombinerValues * _v)
{
	if (config.generalEmulation.enableLegacyBlending == 1) {
		_v->forceBlendCycle1 = 0;
		return;
	}
	SET4(_v->blendMux1, gDP.otherMode.c1_m1a, gDP.otherMode.c1_m1b, gDP.otherMode.c1_m2a, gDP.otherMode.c1_m2b);
	_v->forceBlendCycle1 = (int)gDP.otherMode.forceBlender;
}

static void updateBlend2Cycle(CombinerValues * _v)
{
	if (config.generalEmulation.enableLegacyBlending == 1) {
		_v->forceBlendCycle1 = 0;
		_v->forceBlendCycle2 = 0;
		return;
	}

	SET4(_v->blendMux1, gDP.otherMode.c1_m1a, gDP.otherMode.c1_m1b, gDP.otherMode.c1_m2a, gDP.otherMode.c1_m2b);
	SET4(_v->blendMux2, gDP.otherMode.c2_m1a, gDP.otherMode.c2_m1b, gDP.otherMode.c2_m2a, gDP.otherMode.c2_m2b);
	_v->forceBlendCycle1 = 1;
	_v->forceBlendCycle2 = gDP.otherMode.forceBlender;

	/* Modes, which shader blender can't emulate */
	switch (_SHIFTR(gDP.otherMode.l, 16, 16)) {
	case 0x0040:
		/* Mia Hamm Soccer
		 * clr_in * a_in + clr_mem * (1-a)
		 * clr_in * a_in + clr_in * (1-a) */
	case 0x0050:
		/* A Bug's Life
		 * clr_in * a_in + clr_mem * (1-a)
		 * clr_in * a_in + clr_mem * (1-a) */
		_v->forceBlendCycle1 = 0;
		_v->forceBlendCycle2 = 0;
		break;
	case 0x0150:
		/* Tony Hawk
		 * clr_in * a_in + clr_mem * (1-a)
		 * clr_in * a_fog + clr_mem * (1-a_fog) */
		if ((config.generalEmulation.hacks & hack_TonyHawk) != 0) {
			_v->forceBlendCycle1 = 0;
			_v->forceBlendCycle2 = 0;
		}
		break;
	}
}

static void updateDither(CombinerValues * _v, bool _usesNoise)
{
	bool updateNoiseTex = _usesNoise;
	if (gDP.otherMode.cycleType < G_CYC_COPY) {
		_v->alphaCompareMode = gDP.otherMode.alphaCompare;
		_v->alphaDitherMode = gDP.otherMode.alphaDither;
		_v->colorDitherMode = gDP.otherMode.colorDither;
		updateNoiseTex |= gDP.otherMode.colorDither == G_CD_NOISE || gDP.otherMode.alphaDither == G_AD_NOISE ||
			gDP.otherMode.alphaCompare == G_AC_DITHER;
	} else {
		_v->alphaCompareMode = 0;
		_v->alphaDitherMode = 0;
		_v->colorDitherMode = 0;
	}

	if (updateNoiseTex) {
		gli_noise_update();
		SET2(_v->noiseOffset, (f32)gli_noise_offset_x(), (f32)gli_noise_offset_y());
	}
}

static void updateTextureFetch(CombinerValues * _v)
{
	_v->textureFilterMode = gDP.otherMode.textureFilter;
	SET2(_v->textureFormat, gSP.textureTile[0]->format, gSP.textureTile[1]->format);
	_v->textureConvert = gDP.otherMode.convert_one;
	if (gDP.otherMode.bi_lerp0 == 0 || gDP.otherMode.bi_lerp1 == 0)
		SET4(_v->convertParams, gDP.convert.k0, gDP.convert.k1, gDP.convert.k2, gDP.convert.k3);
}

static void updateAlphaTest(CombinerValues * _v)
{
	if (gDP.otherMode.cycleType == G_CYC_FILL) {
		_v->enableAlphaTest = 0;
	} else if (gDP.otherMode.cycleType == G_CYC_COPY) {
		if (gDP.otherMode.alphaCompare & G_AC_THRESHOLD) {
			_v->enableAlphaTest = 1;
			_v->alphaCvgSel = 0;
			_v->alphaTestValue = 0.5f;
		} else {
			_v->enableAlphaTest = 0;
		}
	} else if ((gDP.otherMode.alphaCompare & G_AC_THRESHOLD) != 0) {
		_v->enableAlphaTest = 1;
		_v->alphaTestValue = gDP.blendColor.a;
		_v->alphaCvgSel = gDP.otherMode.alphaCvgSel;
	} else {
		_v->enableAlphaTest = 0;
	}
	_v->cvgXAlpha = gDP.otherMode.cvgXAlpha;
}

static void updateDepthInfo(CombinerValues * _v, const gli_fb * _fb)
{
	bool depthEnabled;
	if (_fb == NULL || !gli_fb_current_has_depth())
		return;

	depthEnabled = ((gSP.geometryMode & G_ZBUFFER) || gDP.otherMode.depthSource == G_ZS_PRIM) &&
		gDP.otherMode.cycleType <= G_CYC_2CYCLE;
	_v->enableDepth = depthEnabled ? 1 : 0;
	if (depthEnabled) {
		_v->enableDepthCompare = gDP.otherMode.depthCompare;
		_v->enableDepthUpdate = gDP.otherMode.depthUpdate;
	} else {
		_v->enableDepthCompare = 0;
		_v->enableDepthUpdate = 0;
	}
	_v->depthMode = gDP.otherMode.depthMode;
	_v->depthSource = gDP.otherMode.depthSource;
	if (gDP.otherMode.depthSource == G_ZS_PRIM) {
		_v->deltaZ = gDP.primDepth.deltaZ;
		_v->primDepth = gDP.primDepth.z;
	}
}

/* the shift scale of tile @t's texture @_tex */
static void textureShiftScale(u32 t, const gli_tex * _tex, f32 * _s, f32 * _t)
{
	if (gSP.textureTile[t]->textureMode != TEXTUREMODE_NORMAL) {
		*_s = _tex->shiftScaleS;
		*_t = _tex->shiftScaleT;
		return;
	}

	if (gDP.otherMode.textureLOD == G_TL_LOD && gSP.texture.level == 0 && !gli_combiner_uses_lod())
		t = 0;

	*_s = GLI_SHIFT_SCALE(gSP.textureTile[t]->shifts);
	*_t = GLI_SHIFT_SCALE(gSP.textureTile[t]->shiftt);
}

static void updateTextureParams(CombinerValues * _v, u32 _groups, const gli_tex * const * _cur)
{
	int nFB[2] = { 0, 0 };
	f32 * offset[2];
	f32 * shiftScale[2];
	f32 * scale[2];
	f32 * cacheOffset[2];
	u32 t;
	offset[0] = _v->texOffset0; offset[1] = _v->texOffset1;
	shiftScale[0] = _v->cacheShiftScale0; shiftScale[1] = _v->cacheShiftScale1;
	scale[0] = _v->cacheScale0; scale[1] = _v->cacheScale1;
	cacheOffset[0] = _v->cacheOffset0; cacheOffset[1] = _v->cacheOffset1;

	for (t = 0; t < 2; ++t) {
		gDPTile * const tile = gSP.textureTile[t];
		if ((_groups & (t == 0 ? CVG_UsesTile0 : CVG_UsesTile1)) == 0)
			continue;

		if (tile != NULL) {
			if (tile->textureMode == TEXTUREMODE_BGIMAGE || tile->textureMode == TEXTUREMODE_FRAMEBUFFER_BG)
				SET2(offset[t], 0.0f, 0.0f);
			else {
				float fuls = tile->fuls;
				float fult = tile->fult;
				if (tile->frameBufferAddress > 0) {
					if (gli_fb_is_at(tile->frameBufferAddress)) {
						if (tile->masks > 0 && tile->clamps == 0)
							fuls = (float)(tile->uls % (1 << tile->masks));
						if (tile->maskt > 0 && tile->clampt == 0)
							fult = (float)(tile->ult % (1 << tile->maskt));
					} else {
						tile->frameBufferAddress = 0;
					}
				}
				SET2(offset[t], fuls, fult);
			}
		}

		if (_cur[t] != NULL) {
			textureShiftScale(t, _cur[t], &shiftScale[t][0], &shiftScale[t][1]);
			SET2(scale[t], _cur[t]->scaleS, _cur[t]->scaleT);
			SET2(cacheOffset[t], _cur[t]->offsetS, _cur[t]->offsetT);
			nFB[t] = _cur[t]->frameBufferTexture;
		}
	}

	SET2(_v->cacheFrameBuffer, nFB[0], nFB[1]);
	SET2(_v->texScale, gSP.texture.scales, gSP.texture.scalet);
}

static void updateColors(CombinerValues * _v)
{
	COPY4(_v->fogColor, &gDP.fogColor.r);
	COPY4(_v->centerColor, &gDP.key.center.r);
	COPY4(_v->scaleColor, &gDP.key.scale.r);
	COPY4(_v->blendColor, &gDP.blendColor.r);
	COPY4(_v->envColor, &gDP.envColor.r);
	COPY4(_v->primColor, &gDP.primColor.r);
	_v->primLod = gDP.primColor.l;
	_v->k4 = _FIXED2FLOATCOLOR(gDP.convert.k4, 8 );
	_v->k5 = _FIXED2FLOATCOLOR(gDP.convert.k5, 8 );
}

void updateCombinerValues(CombinerValues * _v, unsigned _groups)
{
	/* what is drawn into, and, when any group reads them, the textures */
	const gli_fb * const fb = gli_fb_current_info();
	const gli_tex * cur[2];
	if (_groups & (CVG_FrameBuffer | CVG_TextureSize | CVG_TextureParams))
		gli_textures_current(cur);

	if (_groups & CVG_DepthScale) {
		if (RSP.LLE)
			SET2(_v->depthScale, 0.5f, 0.5f);
		else
			SET2(_v->depthScale, gSP.viewport.vscale[2], gSP.viewport.vtrans[2]);
	}

	if (_groups & CVG_MSAA)
		_v->msaaSamples = config.video.multisampling;

	if (_groups & CVG_FrameBuffer)
		updateFrameBufferInfo(_v, cur);

	if (_groups & CVG_Mipmap1) {
		_v->minLod = gDP.primColor.m;
		_v->maxTile = gSP.texture.level;
	}

	if (_groups & CVG_Mipmap2) {
		_v->enableLod = (gDP.otherMode.textureLOD == G_TL_LOD) ? 1 : 0;
		_v->textureDetail = gDP.otherMode.textureDetail;
	}

	if (_groups & CVG_TextureFetch)
		updateTextureFetch(_v);

	if (_groups & CVG_TexturePersp)
		_v->texturePersp = (RSP.LLE || gli_GBI->texturePersp) ? gDP.otherMode.texturePersp : 1U;

	if (_groups & CVG_TextureSize) {
		if ((_groups & CVG_UsesTile0) && cur[0] != NULL)
			SET2(_v->textureSize0, (f32)cur[0]->realWidth, (f32)cur[0]->realHeight);
		if ((_groups & CVG_UsesTile1) && cur[1] != NULL)
			SET2(_v->textureSize1, (f32)cur[1]->realWidth, (f32)cur[1]->realHeight);
	}

	if (_groups & CVG_TextureParams)
		updateTextureParams(_v, _groups, cur);

	updateFog(_v);

	if (_groups & CVG_Blend1Cycle)
		updateBlend1Cycle(_v);
	else if (_groups & CVG_Blend2Cycle)
		updateBlend2Cycle(_v);

	updateDither(_v, (_groups & CVG_UsesNoise) != 0);

	/* the screen's scale */
	if (!gli_texrect_is_empty())
		SET2(_v->screenScale, 1.0f, 1.0f);
	else if (fb == NULL)
		SET2(_v->screenScale, g_window.scaleX, g_window.scaleY);
	else
		SET2(_v->screenScale, fb->m_scale, fb->m_scale);

	updateAlphaTest(_v);

	if (_groups & CVG_DepthInfo) {
		updateDepthInfo(_v, fb);
	} else if (_groups & CVG_DepthSource) {
		_v->depthSource = gDP.otherMode.depthSource;
		if (gDP.otherMode.depthSource == G_ZS_PRIM)
			_v->primDepth = gDP.primDepth.z;
	}

	if (_groups & CVG_RenderTarget) {
		int renderTarget = 0;
		if (gli_isCurrentColorImageDepthImage())
			renderTarget = gli_isDepthCompareEnabled() ? 2 : 1;
		_v->renderTarget = renderTarget;
	}

	if (_groups & CVG_ClampOffset) {
		_v->clampMode = (int)gli_gfx_clamp_mode();
		_v->polygonOffset = gli_gfx_polygon_offset_fill() ? 0.003f : 0.0f;
	}

	{
		/* twice 1 over the buffer's size (calcCoordsScales) */
		const u32 bufferWidth = fb != NULL ? fb->m_width : VI.width;
		const u32 bufferHeight = gli_VI_GetMaxBufferHeight((u16)bufferWidth);
		SET2(_v->screenCoordsScale, GLI_COORDS_SCALE(bufferWidth), -GLI_COORDS_SCALE(bufferHeight));
	}

	updateColors(_v);

	if (_groups & CVG_RectColor)
		COPY4(_v->rectColor, &gDP.rectColor.r);

	if (_groups & CVG_Lights) {
		u32 i;
		for (i = 0; i <= gSP.numLights; ++i) {
			memcpy(_v->lightDirection[i], gSP.lights.xyz[i], sizeof(f32) * 3);
			memcpy(_v->lightColor[i], gSP.lights.rgb[i], sizeof(f32) * 3);
		}
	}
}
