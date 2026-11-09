#pragma once
#include <Types.h>

/* What the combiner shaders read, worked out from the N64's state here so
 * that a backend only has to get it to its shaders. Laid out for std140:
 * no member straddles what its type aligns to, and a Vulkan uniform block
 * maps onto it as it stands. */

class CombinerKey;

namespace graphics {

	struct CombinerValues
	{
		f32 fogColor[4];
		f32 centerColor[4];
		f32 scaleColor[4];
		f32 blendColor[4];
		f32 envColor[4];
		f32 primColor[4];
		f32 rectColor[4];
		s32 blendMux1[4];
		s32 blendMux2[4];
		s32 convertParams[4];

		f32 fogScale[2];
		f32 noiseOffset[2];
		f32 screenScale[2];
		f32 depthScale[2];
		f32 screenCoordsScale[2];
		f32 texScale[2];
		f32 texOffset0[2];
		f32 texOffset1[2];
		f32 cacheShiftScale0[2];
		f32 cacheShiftScale1[2];
		f32 cacheScale0[2];
		f32 cacheScale1[2];
		f32 cacheOffset0[2];
		f32 cacheOffset1[2];
		f32 textureSize0[2];
		f32 textureSize1[2];
		s32 fbMonochrome[2];
		s32 fbFixedAlpha[2];
		s32 msTexEnabled[2];
		s32 textureFormat[2];
		s32 cacheFrameBuffer[2];

		s32 fogUsage;
		s32 forceBlendCycle1;
		s32 forceBlendCycle2;
		s32 alphaCompareMode;
		s32 alphaDitherMode;
		s32 colorDitherMode;
		s32 maxTile;
		s32 enableLod;
		s32 textureDetail;
		s32 texturePersp;
		s32 textureFilterMode;
		s32 textureConvert;
		s32 enableAlphaTest;
		s32 alphaCvgSel;
		s32 cvgXAlpha;
		s32 enableDepth;
		s32 enableDepthCompare;
		s32 enableDepthUpdate;
		s32 depthMode;
		s32 depthSource;
		s32 renderTarget;
		s32 clampMode;
		f32 alphaTestValue;
		f32 minLod;
		f32 primDepth;
		f32 deltaZ;
		f32 polygonOffset;
		f32 primLod;
		f32 k4;
		f32 k5;
		s32 msaaSamples;
		s32 pad0;

		/* vec3 in the shaders, a vec4's room each */
		f32 lightDirection[8][4];
		f32 lightColor[8][4];
	};

	/* Which parts of it a combiner needs. */
	enum CombinerValueGroup : u32 {
		CVG_DepthScale    = 1u << 1,
		CVG_MSAA          = 1u << 2,
		CVG_FrameBuffer   = 1u << 3,
		CVG_Mipmap1       = 1u << 4,
		CVG_Mipmap2       = 1u << 5,
		CVG_TextureFetch  = 1u << 6,
		CVG_TexturePersp  = 1u << 7,
		CVG_TextureSize   = 1u << 8,
		CVG_TextureParams = 1u << 9,
		CVG_Blend1Cycle   = 1u << 10,
		CVG_Blend2Cycle   = 1u << 11,
		CVG_DepthInfo     = 1u << 12,
		CVG_DepthSource   = 1u << 13,
		CVG_RenderTarget  = 1u << 14,
		CVG_ClampOffset   = 1u << 15,
		CVG_RectColor     = 1u << 16,
		CVG_Lights        = 1u << 17,
		CVG_UsesNoise     = 1u << 18,
		CVG_UsesTile0     = 1u << 19,
		CVG_UsesTile1     = 1u << 20
	};

	struct CombinerNeeds
	{
		bool usesTexture;
		bool usesLOD;
		bool usesNoise;
		bool usesTile0;
		bool usesTile1;
		bool usesHwLighting;
		bool textureSize;   /* shaders that cannot ask a texture its size */
		bool depthScale;
		bool clampOffset;   /* shaders that work out clipping themselves */
	};

	u32 combinerValueGroups(const CombinerNeeds & _needs, const CombinerKey & _key);

	/* Writes the groups' members; what a group does not write stays. */
	void updateCombinerValues(CombinerValues & _values, u32 _groups);
}
