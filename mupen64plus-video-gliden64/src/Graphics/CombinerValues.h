#ifndef COMBINER_VALUES_H
#define COMBINER_VALUES_H

/* What the combiner shaders read, worked out from the N64's state here so
 * that a backend only has to get it to its shaders. Laid out for std140:
 * no member straddles what its type aligns to, and a Vulkan uniform block
 * maps onto it as it stands. C89. */

#ifdef __cplusplus
extern "C" {
#endif

	typedef struct CombinerValues
	{
		float fogColor[4];
		float centerColor[4];
		float scaleColor[4];
		float blendColor[4];
		float envColor[4];
		float primColor[4];
		float rectColor[4];
		int blendMux1[4];
		int blendMux2[4];
		int convertParams[4];

		float fogScale[2];
		float noiseOffset[2];
		float screenScale[2];
		float depthScale[2];
		float screenCoordsScale[2];
		float texScale[2];
		float texOffset0[2];
		float texOffset1[2];
		float cacheShiftScale0[2];
		float cacheShiftScale1[2];
		float cacheScale0[2];
		float cacheScale1[2];
		float cacheOffset0[2];
		float cacheOffset1[2];
		float textureSize0[2];
		float textureSize1[2];
		int fbMonochrome[2];
		int fbFixedAlpha[2];
		int msTexEnabled[2];
		int textureFormat[2];
		int cacheFrameBuffer[2];

		int fogUsage;
		int forceBlendCycle1;
		int forceBlendCycle2;
		int alphaCompareMode;
		int alphaDitherMode;
		int colorDitherMode;
		int maxTile;
		int enableLod;
		int textureDetail;
		int texturePersp;
		int textureFilterMode;
		int textureConvert;
		int enableAlphaTest;
		int alphaCvgSel;
		int cvgXAlpha;
		int enableDepth;
		int enableDepthCompare;
		int enableDepthUpdate;
		int depthMode;
		int depthSource;
		int renderTarget;
		int clampMode;
		float alphaTestValue;
		float minLod;
		float primDepth;
		float deltaZ;
		float polygonOffset;
		float primLod;
		float k4;
		float k5;
		int msaaSamples;
		int pad0;
		int pad1;
		int pad2;

		/* vec3 in the shaders, a vec4's room each */
		float lightDirection[8][4];
		float lightColor[8][4];
	} CombinerValues;

	/* Which parts of it a combiner needs. */
	enum CombinerValueGroup {
		CVG_DepthScale    = 1 << 1,
		CVG_MSAA          = 1 << 2,
		CVG_FrameBuffer   = 1 << 3,
		CVG_Mipmap1       = 1 << 4,
		CVG_Mipmap2       = 1 << 5,
		CVG_TextureFetch  = 1 << 6,
		CVG_TexturePersp  = 1 << 7,
		CVG_TextureSize   = 1 << 8,
		CVG_TextureParams = 1 << 9,
		CVG_Blend1Cycle   = 1 << 10,
		CVG_Blend2Cycle   = 1 << 11,
		CVG_DepthInfo     = 1 << 12,
		CVG_DepthSource   = 1 << 13,
		CVG_RenderTarget  = 1 << 14,
		CVG_ClampOffset   = 1 << 15,
		CVG_RectColor     = 1 << 16,
		CVG_Lights        = 1 << 17,
		CVG_UsesNoise     = 1 << 18,
		CVG_UsesTile0     = 1 << 19,
		CVG_UsesTile1     = 1 << 20
	};

	typedef struct CombinerNeeds
	{
		int usesTexture;
		int usesLOD;
		int usesNoise;
		int usesTile0;
		int usesTile1;
		int usesHwLighting;
		int textureSize;   /* shaders that cannot ask a texture its size */
		int depthScale;
		int clampOffset;   /* shaders that work out clipping themselves */
		int cycleType;
		int isRect;
	} CombinerNeeds;

	unsigned combinerValueGroups(const CombinerNeeds * _needs);

	/* Writes the groups' members; what a group does not write stays. */
	void updateCombinerValues(CombinerValues * _values, unsigned _groups);

#ifdef __cplusplus
}
#endif

#endif
