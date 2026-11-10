#include <string.h>
#include "Types.h"
#include "gDP.h"
#include "GBI.h"
#include "CombinerMux.h"

extern u32 LegacySm64ToolsHacks;

static const u32 saRGBExpanded[] =
{
	G_GCI_COMBINED,			G_GCI_TEXEL0,			G_GCI_TEXEL1,			G_GCI_PRIMITIVE,
	G_GCI_SHADE,			G_GCI_ENVIRONMENT,		G_GCI_ONE,				G_GCI_NOISE,
	G_GCI_ZERO,				G_GCI_ZERO,				G_GCI_ZERO,				G_GCI_ZERO,
	G_GCI_ZERO,				G_GCI_ZERO,				G_GCI_ZERO,				G_GCI_ZERO
};

static const u32 sbRGBExpanded[] =
{
	G_GCI_COMBINED,			G_GCI_TEXEL0,			G_GCI_TEXEL1,			G_GCI_PRIMITIVE,
	G_GCI_SHADE,			G_GCI_ENVIRONMENT,		G_GCI_CENTER,			G_GCI_K4,
	G_GCI_ZERO,				G_GCI_ZERO,				G_GCI_ZERO,				G_GCI_ZERO,
	G_GCI_ZERO,				G_GCI_ZERO,				G_GCI_ZERO,				G_GCI_ZERO
};

static const u32 mRGBExpanded[] =
{
	G_GCI_COMBINED,			G_GCI_TEXEL0,			G_GCI_TEXEL1,			G_GCI_PRIMITIVE,
	G_GCI_SHADE,			G_GCI_ENVIRONMENT,		G_GCI_SCALE,			G_GCI_COMBINED_ALPHA,
	G_GCI_TEXEL0_ALPHA,		G_GCI_TEXEL1_ALPHA,		G_GCI_PRIMITIVE_ALPHA,	G_GCI_SHADE_ALPHA,
	G_GCI_ENV_ALPHA,		G_GCI_LOD_FRACTION,		G_GCI_PRIM_LOD_FRAC,	G_GCI_K5,
	G_GCI_ZERO,				G_GCI_ZERO,				G_GCI_ZERO,				G_GCI_ZERO,
	G_GCI_ZERO,				G_GCI_ZERO,				G_GCI_ZERO,				G_GCI_ZERO,
	G_GCI_ZERO,				G_GCI_ZERO,				G_GCI_ZERO,				G_GCI_ZERO,
	G_GCI_ZERO,				G_GCI_ZERO,				G_GCI_ZERO,				G_GCI_ZERO
};

static const u32 aRGBExpanded[] =
{
	G_GCI_COMBINED,			G_GCI_TEXEL0,			G_GCI_TEXEL1,			G_GCI_PRIMITIVE,
	G_GCI_SHADE,			G_GCI_ENVIRONMENT,		G_GCI_ONE,				G_GCI_ZERO
};

static const u32 saAExpanded[] =
{
	G_GCI_COMBINED,			G_GCI_TEXEL0_ALPHA,		G_GCI_TEXEL1_ALPHA,		G_GCI_PRIMITIVE_ALPHA,
	G_GCI_SHADE_ALPHA,		G_GCI_ENV_ALPHA,		G_GCI_ONE,				G_GCI_ZERO
};

static const u32 sbAExpanded[] =
{
	G_GCI_COMBINED,			G_GCI_TEXEL0_ALPHA,		G_GCI_TEXEL1_ALPHA,		G_GCI_PRIMITIVE_ALPHA,
	G_GCI_SHADE_ALPHA,		G_GCI_ENV_ALPHA,		G_GCI_ONE,				G_GCI_ZERO
};

static const u32 mAExpanded[] =
{
	G_GCI_LOD_FRACTION,		G_GCI_TEXEL0_ALPHA,		G_GCI_TEXEL1_ALPHA,		G_GCI_PRIMITIVE_ALPHA,
	G_GCI_SHADE_ALPHA,		G_GCI_ENV_ALPHA,		G_GCI_PRIM_LOD_FRAC,	G_GCI_ZERO,
};

static const u32 aAExpanded[] =
{
	G_GCI_COMBINED,			G_GCI_TEXEL0_ALPHA,		G_GCI_TEXEL1_ALPHA,		G_GCI_PRIMITIVE_ALPHA,
	G_GCI_SHADE_ALPHA,		G_GCI_ENV_ALPHA,		G_GCI_ONE,				G_GCI_ZERO
};

static void SimplifyCycle(const CombineCycle *cc, CombinerStage *stage)
{
	/* Load the first operand */
	stage->op[0].op = LOAD;
	stage->op[0].param1 = cc->sa;
	stage->numOps = 1;

	/* If we're just subtracting zero, skip it */
	if (cc->sb != G_GCI_ZERO) {
		/* Subtracting a number from itself is zero */
		if (cc->sb == stage->op[0].param1)
			stage->op[0].param1 = G_GCI_ZERO;
		else {
			stage->op[1].op = SUB;
			stage->op[1].param1 = cc->sb;
			stage->numOps++;
		}
	}

	/* If we either subtracted, or didn't load a zero */
	if ((stage->numOps > 1) || (stage->op[0].param1 != G_GCI_ZERO)) {
		/* Multiplying by zero is zero */
		if (cc->m == G_GCI_ZERO) {
			stage->numOps = 1;
			stage->op[0].op = LOAD;
			stage->op[0].param1 = G_GCI_ZERO;
		} else {
			/* Multiplying by one, so just do a load */
			if ((stage->numOps == 1) && (stage->op[0].param1 == G_GCI_ONE))
				stage->op[0].param1 = cc->m;
			else {
				stage->op[stage->numOps].op = MUL;
				stage->op[stage->numOps].param1 = cc->m;
				stage->numOps++;
			}
		}
	}

	/* Don't bother adding zero */
	if (cc->a != G_GCI_ZERO) {
		/* If all we have so far is zero, then load this instead */
		if ((stage->numOps == 1) && (stage->op[0].param1 == G_GCI_ZERO))
			stage->op[0].param1 = cc->a;
		else {
			stage->op[stage->numOps].op = ADD;
			stage->op[stage->numOps].param1 = cc->a;
			stage->numOps++;
		}
	}

	/* Handle interpolation */
	if ((stage->numOps == 4) && (stage->op[1].param1 == stage->op[3].param1)) {
		stage->numOps = 1;
		stage->op[0].op = INTER;
		stage->op[0].param2 = stage->op[1].param1;
		stage->op[0].param3 = stage->op[2].param1;
	}
}

void gli_combiner_decode(u64 _mux, u32 _cycleType, Combiner * _color, Combiner * _alpha)
{
	gDPCombine combine;
	const u32 numCycles = _cycleType + 1;
	CombineCycle cc[2];
	CombineCycle ac[2];
	u32 i, s, k;

	combine.mux = _mux;

	/* what the builders read of an op the decode leaves alone */
	for (s = 0; s < 2; ++s) {
		_color->stage[s].numOps = 0;
		_alpha->stage[s].numOps = 0;
		for (k = 0; k < 6; ++k) {
			_color->stage[s].op[k].op = _alpha->stage[s].op[k].op = LOAD;
			_color->stage[s].op[k].param1 = _alpha->stage[s].op[k].param1 = -1;
			_color->stage[s].op[k].param2 = _alpha->stage[s].op[k].param2 = -1;
			_color->stage[s].op[k].param3 = _alpha->stage[s].op[k].param3 = -1;
		}
	}
	_color->numStages = numCycles;
	_alpha->numStages = numCycles;

	/* Simplify each RDP combiner cycle into a combiner stage */
	if (_cycleType == G_CYC_1CYCLE) {
		/* 1 cycle mode uses combiner equations from 2nd cycle */
		u32 colorMux[4];
		u32 alphaMux[4];
		colorMux[0] = saRGBExpanded[combine.saRGB1];
		colorMux[1] = sbRGBExpanded[combine.sbRGB1];
		colorMux[2] = mRGBExpanded[combine.mRGB1];
		colorMux[3] = aRGBExpanded[combine.aRGB1];
		/* Check for 'combined' mux */
		for (i = 0; i < 4; ++i) {
			if (colorMux[i] == G_GCI_COMBINED || colorMux[i] == G_GCI_COMBINED_ALPHA)
				colorMux[i] = G_GCI_ZERO;
		}
		cc[1].sa = colorMux[0];
		cc[1].sb = colorMux[1];
		cc[1].m = colorMux[2];
		cc[1].a = colorMux[3];
		SimplifyCycle(&cc[1], &_color->stage[0]);

		alphaMux[0] = saAExpanded[combine.saA1];
		alphaMux[1] = sbAExpanded[combine.sbA1];
		alphaMux[2] = mAExpanded[combine.mA1];
		alphaMux[3] = aAExpanded[combine.aA1];
		/* Check for 'combined' mux */
		for (i = 0; i < 4; ++i) {
			if (alphaMux[i] == G_GCI_COMBINED)
				alphaMux[i] = G_GCI_ZERO;
		}
		ac[1].sa = alphaMux[0];
		ac[1].sb = alphaMux[1];
		ac[1].m = alphaMux[2];
		ac[1].a = alphaMux[3];
		SimplifyCycle(&ac[1], &_alpha->stage[0]);
	} else {
		/* Decode and expand the combine mode into a more general form */
		cc[1].sa = saRGBExpanded[combine.saRGB1];
		cc[1].sb = sbRGBExpanded[combine.sbRGB1];
		cc[1].m = mRGBExpanded[combine.mRGB1];
		cc[1].a = aRGBExpanded[combine.aRGB1];
		ac[1].sa = saAExpanded[combine.saA1];
		ac[1].sb = sbAExpanded[combine.sbA1];
		ac[1].m = mAExpanded[combine.mA1];
		ac[1].a = aAExpanded[combine.aA1];

		cc[0].sa = saRGBExpanded[combine.saRGB0];
		cc[0].sb = sbRGBExpanded[combine.sbRGB0];
		cc[0].m = mRGBExpanded[combine.mRGB0];
		cc[0].a = aRGBExpanded[combine.aRGB0];
		ac[0].sa = saAExpanded[combine.saA0];
		ac[0].sb = sbAExpanded[combine.sbA0];
		ac[0].m = mAExpanded[combine.mA0];
		ac[0].a = aAExpanded[combine.aA0];

		SimplifyCycle(&cc[0], &_color->stage[0]);
		SimplifyCycle(&ac[0], &_alpha->stage[0]);

		if ((memcmp(cc, cc + 1, sizeof(CombineCycle)) | memcmp(ac, ac + 1, sizeof(CombineCycle))) != 0) {
			SimplifyCycle(&cc[1], &_color->stage[1]);
			SimplifyCycle(&ac[1], &_alpha->stage[1]);
		} else {
			_color->numStages = 1;
			_alpha->numStages = 1;
		}
	}
}

/* The combine the current cycle type draws with. The legacy SM64 tools
 * hack fixes up gDP.combine first. */
u64 gli_combiner_mux(void)
{
	if (gDP.otherMode.cycleType == G_CYC_COPY)
	{
		return EncodeCombineMode(0, 0, 0, TEXEL0, 0, 0, 0, TEXEL0, 0, 0, 0, TEXEL0, 0, 0, 0, TEXEL0);
	}
	else if (gDP.otherMode.cycleType == G_CYC_FILL)
	{
		return EncodeCombineMode(0, 0, 0, SHADE, 0, 0, 0, SHADE, 0, 0, 0, SHADE, 0, 0, 0, SHADE);
	}
	else
	{
		if (LegacySm64ToolsHacks)
		{
			/* This is sanity check of provided combiner - 1st and 2nd cycle must be equal */
			if (gDP.otherMode.cycleType == G_CYC_1CYCLE)
			{
#define FIX_COMBINE(val) gDP.combine.val##1 = gDP.combine.val##0
				FIX_COMBINE(aA);
				FIX_COMBINE(aRGB);
				FIX_COMBINE(mA);
				FIX_COMBINE(mRGB);
				FIX_COMBINE(saA);
				FIX_COMBINE(saRGB);
				FIX_COMBINE(sbA);
				FIX_COMBINE(sbRGB);
#undef FIX_COMBINE
			}

			/* This forces COMBINED usage for 2nd mode and assumes no magic is happening in 2nd cycle */
			/* Some magic may happen in the 2CYCLE mode, do not force anything */
			if (gDP.otherMode.cycleType == G_CYC_2CYCLE)
			{
				if (gDP.combine.aRGB0  == gDP.combine.aRGB1
				 && gDP.combine.mRGB0  == gDP.combine.mRGB1
				 && gDP.combine.saRGB0 == gDP.combine.saRGB1
				 && gDP.combine.sbRGB0 == gDP.combine.sbRGB1
					)
				{
					gDP.combine.aA1 = G_CCMUX_COMBINED;
					gDP.combine.aRGB1 = G_CCMUX_COMBINED;
					/* G_CCMUX_0 = 31 is the all-bits-set encoding of
					 * "zero source" in the N64 RDP combiner. Each sub-
					 * mux has its own bit width and reads "zero" as
					 * its own all-bits-set value: 3-bit fields use 7,
					 * 4-bit fields use 15, 5-bit fields use 31. Mask
					 * to the field width to document the truncation
					 * and silence -Woverflow. */
					gDP.combine.mA1 = G_CCMUX_0 & 0x7;
					gDP.combine.mRGB1 = G_CCMUX_0;
					gDP.combine.saA1 = G_CCMUX_0 & 0x7;
					gDP.combine.saRGB1 = G_CCMUX_0 & 0xF;
					gDP.combine.sbA1 = G_CCMUX_0 & 0x7;
					gDP.combine.sbRGB1 = G_CCMUX_0 & 0xF;
				}
			}
		}
		return gDP.combine.mux;
	}
}
