#include <assert.h>

#include "Types.h"
#include "GBI.h"
#include "gDP.h"
#include "gSP.h"
#include "Config.h"
#include "gli_blend.h"

static void set(gli_blend_state *_o, u8 _src, u8 _dst)
{
	_o->enable = 1;
	_o->src = _src;
	_o->dst = _dst;
}

/* Without FORCE_BL the blender passes its first mux through */
static void first_mux(gli_blend_state *_o)
{
	int useMemColor = 0;
	if (gDP.otherMode.cycleType == G_CYC_1CYCLE)
		useMemColor = gDP.otherMode.c1_m1a == 1;
	else if (gDP.otherMode.cycleType == G_CYC_2CYCLE)
		useMemColor = gDP.otherMode.c2_m1a == 1;
	if (useMemColor)
		set(_o, GLI_BF_ZERO, GLI_BF_ONE);
}

static void legacy_blend_mode(int _blastCorpsTexture, gli_blend_state *_o)
{
	const u32 blendmode = gDP.otherMode.l >> 16;
	u8 sfactor, dfactor;
	/* 0x7000 = CVG_X_ALPHA|ALPHA_CVG_SEL|FORCE_BL */
	if (gDP.otherMode.alphaCvgSel != 0 && (gDP.otherMode.l & 0x7000) != 0x7000) {
		switch (blendmode) {
		case 0x4055: /* Mario Golf */
		case 0x5055: /* Paper Mario intro clr_mem * a_in + clr_mem * a_mem */
			set(_o, GLI_BF_ZERO, GLI_BF_ONE);
			break;
		}
		return;
	}

	if (gDP.otherMode.forceBlender != 0 && gDP.otherMode.cycleType < G_CYC_COPY) {
		switch (blendmode) {
		case 0x0382: /* Mace objects */
		case 0x0091: /* Mace special blend mode, see GLSLCombiner.cpp */
		case 0x0C08: /* 1080 Sky */
		case 0x0F0A: /* Used LOTS of places */
		case 0x0302: /* DK64 blue prints */
		case 0xA500: /* Bomberman 2 special blend mode, see GLSLCombiner.cpp */
		case 0xCB02: /* Sin and Punishment */
		case 0xC800: /* Battlezone: clr_in * a + clr_in * (1-a) */
		case 0x07C2: /* Conker BFD: clr_in * a_fog + clr_fog * (1-a), clr_in * 0 + clr_in * 1 */
		case 0x00C0:
		case 0xC302: /* ISS64 */
		case 0xC702: /* Donald Duck */
			sfactor = GLI_BF_ONE;
			dfactor = GLI_BF_ZERO;
			break;
		case 0x55f0: /* Bust-A-Move 3 DX: CLR_MEM * A_FOG + CLR_FOG * 1MA */
			sfactor = GLI_BF_ONE;
			dfactor = GLI_BF_SRC_ALPHA;
			break;
		case 0x0F1A:
			if (gDP.otherMode.cycleType == G_CYC_1CYCLE) {
				sfactor = GLI_BF_ONE;
				dfactor = GLI_BF_ZERO;
			} else {
				sfactor = GLI_BF_ZERO;
				dfactor = GLI_BF_ONE;
			}
			break;
		case 0x0448: /* Space Invaders: add */
		case 0x055A:
			sfactor = GLI_BF_ONE;
			dfactor = GLI_BF_ONE;
			break;
		case 0xc712: /* Pokemon Stadium? */
		case 0xAF50: /* LOT in Zelda: MM */
		case 0x0F5A: /* LOT in Zelda: MM */
		case 0x0FA5: /* Seems to be doing just blend color */
		case 0x5055: /* Paper Mario intro: clr_in * 0 + clr_mem * 1 */
			sfactor = GLI_BF_ZERO;
			dfactor = GLI_BF_ONE;
			break;
		case 0x5F50: /* clr_mem * 0 + clr_mem * (1-a) */
			sfactor = GLI_BF_ZERO;
			dfactor = GLI_BF_ONE_MINUS_SRC_ALPHA;
			break;
		case 0xF550: /* clr_fog * a_fog + clr_mem * (1-a) */
		case 0x0150: /* spiderman */
		case 0x0550: /* bomberman 64 */
		case 0x0D18: /* clr_in * a_fog + clr_mem * (1-a) */
			sfactor = GLI_BF_SRC_ALPHA;
			dfactor = GLI_BF_ONE_MINUS_SRC_ALPHA;
			break;
		case 0xC912: /* 40 winks, clr_in * a_fog + clr_mem * 1 */
			sfactor = GLI_BF_SRC_ALPHA;
			dfactor = GLI_BF_ONE;
			break;
		case 0x0040: /* Fzero */
		case 0xC810: /* Blends fog */
		case 0x0C18: /* Standard interpolated blend */
		case 0x0050:
		case 0x0051:
		case 0x0055: /* Used for antialiasing */
			sfactor = GLI_BF_SRC_ALPHA;
			dfactor = GLI_BF_ONE_MINUS_SRC_ALPHA;
			break;
		case 0x0C19: /* Used for antialiasing */
		case 0xC811: /* Blends fog */
			sfactor = GLI_BF_SRC_ALPHA;
			dfactor = GLI_BF_DST_ALPHA;
			break;
		case 0x5000: /* V8 explosions */
			sfactor = GLI_BF_ONE_MINUS_SRC_ALPHA;
			dfactor = GLI_BF_SRC_ALPHA;
			break;
		case 0xFA00: /* Bomberman second attack */
			sfactor = GLI_BF_ONE;
			dfactor = GLI_BF_ZERO;
			break;
		default:
			sfactor = GLI_BF_SRC_ALPHA;
			dfactor = GLI_BF_ONE_MINUS_SRC_ALPHA;
			break;
		}
		set(_o, sfactor, dfactor);
	} else if (gDP.otherMode.colorOnCvg != 0) {
		first_mux(_o);
	} else if (_blastCorpsTexture) {
		set(_o, GLI_BF_ZERO, GLI_BF_ONE);
	}
}

/* Modes the shader blender cannot emulate */
static int unsupported_blend_mode(gli_blend_state *_o)
{
	if (gDP.otherMode.cycleType != G_CYC_2CYCLE)
		return 0;
	switch (_SHIFTR(gDP.otherMode.l, 16, 16)) {
	case 0x0040: /* Mia Hamm Soccer */
	case 0x0050: /* A Bug's Life */
		set(_o, GLI_BF_SRC_ALPHA, GLI_BF_ONE_MINUS_SRC_ALPHA);
		return 1;
	case 0x0150: /* Tony Hawk */
		if ((config.generalEmulation.hacks & hack_TonyHawk) != 0) {
			set(_o, GLI_BF_SRC_ALPHA, GLI_BF_ONE_MINUS_SRC_ALPHA);
			return 1;
		}
		break;
	}
	return 0;
}

void gli_blend_mode(int _legacy, int _blastCorpsTexture, gli_blend_state *_o)
{
	_o->enable = 0;
	_o->src = GLI_BF_ONE;
	_o->dst = GLI_BF_ZERO;
	_o->fogColor = 0;

	if (_legacy || config.generalEmulation.enableLegacyBlending != 0) {
		legacy_blend_mode(_blastCorpsTexture, _o);
		return;
	}

	if (unsupported_blend_mode(_o))
		return;

	if (gDP.otherMode.forceBlender != 0 && gDP.otherMode.cycleType < G_CYC_COPY) {
		u8 srcFactor = GLI_BF_ONE;
		u8 dstFactor = GLI_BF_ZERO;
		u32 memFactorSource = 2, muxA, muxB;
		if (gDP.otherMode.cycleType == G_CYC_2CYCLE) {
			muxA = gDP.otherMode.c2_m1b;
			muxB = gDP.otherMode.c2_m2b;
			if (gDP.otherMode.c2_m1a == 1) {
				if (gDP.otherMode.c2_m2a == 1) {
					set(_o, GLI_BF_ZERO, GLI_BF_ONE);
					return;
				}
				memFactorSource = 0;
			} else if (gDP.otherMode.c2_m2a == 1) {
				memFactorSource = 1;
			}
			if (gDP.otherMode.c2_m2a == 0 && gDP.otherMode.c2_m2b == 1)
				srcFactor = GLI_BF_DST_ALPHA; /* c_in * a_mem */
		} else {
			muxA = gDP.otherMode.c1_m1b;
			muxB = gDP.otherMode.c1_m2b;
			if (gDP.otherMode.c1_m1a == 1) {
				if (gDP.otherMode.c1_m2a == 1) {
					set(_o, GLI_BF_ZERO, GLI_BF_ONE);
					return;
				}
				memFactorSource = 0;
			} else if (gDP.otherMode.c1_m2a == 1) {
				memFactorSource = 1;
			}
			if (gDP.otherMode.c1_m2a == 0 && gDP.otherMode.c1_m2b == 1)
				srcFactor = GLI_BF_DST_ALPHA; /* c_pixel * a_mem */
		}
		switch (memFactorSource) {
		case 0:
			switch (muxA) {
			case 0:
				dstFactor = GLI_BF_SRC_ALPHA;
				break;
			case 1:
				_o->fogColor = 1;
				dstFactor = GLI_BF_CONSTANT_ALPHA;
				break;
			case 2:
				assert(0); /* shade alpha */
				dstFactor = GLI_BF_SRC_ALPHA;
				break;
			case 3:
				dstFactor = GLI_BF_ZERO;
				break;
			}
			break;
		case 1:
			switch (muxB) {
			case 0: /* 1.0 - muxA */
				switch (muxA) {
				case 0:
					dstFactor = GLI_BF_ONE_MINUS_SRC_ALPHA;
					break;
				case 1:
					_o->fogColor = 1;
					dstFactor = GLI_BF_ONE_MINUS_CONSTANT_ALPHA;
					break;
				case 2:
					assert(0); /* shade alpha */
					dstFactor = GLI_BF_ONE_MINUS_SRC_ALPHA;
					break;
				case 3:
					dstFactor = GLI_BF_ONE;
					break;
				}
				break;
			case 1:
				dstFactor = GLI_BF_DST_ALPHA;
				break;
			case 2:
				dstFactor = GLI_BF_ONE;
				break;
			case 3:
				dstFactor = GLI_BF_ZERO;
				break;
			}
			break;
		default:
			dstFactor = GLI_BF_ZERO;
		}
		set(_o, srcFactor, dstFactor);
	} else if (_blastCorpsTexture) {
		set(_o, GLI_BF_ZERO, GLI_BF_ONE);
	} else if (gDP.otherMode.forceBlender == 0 && gDP.otherMode.cycleType < G_CYC_COPY) {
		first_mux(_o);
	}
}
