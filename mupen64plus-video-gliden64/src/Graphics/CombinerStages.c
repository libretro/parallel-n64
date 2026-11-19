/* What every backend does to a combiner's stages before writing them, and
 * the inputs they read (CombinerStages.h). C89. */
#include "CombinerStages.h"
#include "GBI.h"

static int correct_first(int _param)
{
	switch (_param) {
	case G_GCI_TEXEL1:
		return G_GCI_TEXEL0;
	case G_GCI_TEXEL1_ALPHA:
		return G_GCI_TEXEL0_ALPHA;
	}
	return _param;
}

void gli_stage_correct_first(CombinerStage *_stage)
{
	int i;
	for (i = 0; i < _stage->numOps; ++i) {
		_stage->op[i].param1 = correct_first(_stage->op[i].param1);
		_stage->op[i].param2 = correct_first(_stage->op[i].param2);
		_stage->op[i].param3 = correct_first(_stage->op[i].param3);
	}
}

static int correct_second(int _param)
{
	switch (_param) {
	case G_GCI_TEXEL0:
		return G_GCI_TEXEL1;
	case G_GCI_TEXEL1:
		return G_GCI_TEXEL0;
	case G_GCI_TEXEL0_ALPHA:
		return G_GCI_TEXEL1_ALPHA;
	case G_GCI_TEXEL1_ALPHA:
		return G_GCI_TEXEL0_ALPHA;
	}
	return _param;
}

void gli_stage_correct_second(CombinerStage *_stage)
{
	int i;
	for (i = 0; i < _stage->numOps; ++i) {
		_stage->op[i].param1 = correct_second(_stage->op[i].param1);
		_stage->op[i].param2 = correct_second(_stage->op[i].param2);
		_stage->op[i].param3 = correct_second(_stage->op[i].param3);
	}
}

bool gli_combined_color_c(u32 _cycleType, const gDPCombine *_combine)
{
	return _cycleType == G_CYC_2CYCLE && _combine->mRGB1 == G_CCMUX_COMBINED;
}

bool gli_combined_alpha_c(u32 _cycleType, const gDPCombine *_combine)
{
	return _cycleType == G_CYC_2CYCLE && _combine->mA1 == G_ACMUX_COMBINED;
}

bool gli_combined_color_abd(u32 _cycleType, const gDPCombine *_combine)
{
	if (_cycleType != G_CYC_2CYCLE)
		return 0;
	if (_combine->aRGB1 == G_CCMUX_COMBINED)
		return 1;
	if (_combine->saRGB1 == G_CCMUX_COMBINED || _combine->sbRGB1 == G_CCMUX_COMBINED)
		return _combine->mRGB1 != G_CCMUX_0;
	return 0;
}

bool gli_combined_alpha_abd(u32 _cycleType, const gDPCombine *_combine)
{
	if (_cycleType != G_CYC_2CYCLE)
		return 0;
	if (_combine->aA1 == G_ACMUX_COMBINED)
		return 1;
	if (_combine->saA1 == G_ACMUX_COMBINED || _combine->sbA1 == G_ACMUX_COMBINED)
		return _combine->mA1 != G_ACMUX_0;
	return 0;
}

void gli_stage_encode(const CombinerStage *_stage, int *_ops)
{
	int i;
	for (i = 0; i < 4; ++i)
		_ops[i] = -1;
	for (i = 0; i < _stage->numOps && i < 4; ++i) {
		const CombinerOp *op = &_stage->op[i];
		if (op->op == INTER) {
			_ops[0] = (INTER << 8) | op->param1;
			_ops[1] = op->param2;
			_ops[2] = op->param3;
			_ops[3] = -1;
			return;
		}
		_ops[i] = (op->op << 8) | op->param1;
	}
}

u32 gli_inputs_add_stage(u32 _inputs, const CombinerStage *_stage)
{
	int i;
	for (i = 0; i < _stage->numOps; ++i) {
		_inputs |= 1u << _stage->op[i].param1;
		if (_stage->op[i].op == INTER) {
			_inputs |= 1u << _stage->op[i].param2;
			_inputs |= 1u << _stage->op[i].param3;
		}
	}
	return _inputs;
}

bool gli_inputs_tile(u32 _inputs, u32 _tile)
{
	if (_tile == 0)
		return (_inputs & ((1u << G_GCI_TEXEL0) | (1u << G_GCI_TEXEL0_ALPHA))) != 0;
	return (_inputs & ((1u << G_GCI_TEXEL1) | (1u << G_GCI_TEXEL1_ALPHA))) != 0;
}

bool gli_inputs_texture(u32 _inputs)
{
	/* the LOD fraction is worked out by the texture path: without it the
	 * fragment shader reads a lod_frac it never declares */
	return (_inputs & ((1u << G_GCI_TEXEL1) | (1u << G_GCI_TEXEL1_ALPHA) | (1u << G_GCI_TEXEL0) |
		(1u << G_GCI_TEXEL0_ALPHA) | (1u << G_GCI_LOD_FRACTION))) != 0;
}

bool gli_inputs_lod(u32 _inputs)
{
	return (_inputs & (1u << G_GCI_LOD_FRACTION)) != 0;
}

bool gli_inputs_noise(u32 _inputs)
{
	return (_inputs & (1u << G_GCI_NOISE)) != 0;
}

bool gli_inputs_shade(u32 _inputs)
{
	return (_inputs & ((1u << G_GCI_SHADE) | (1u << G_GCI_SHADE_ALPHA))) != 0;
}

bool gli_inputs_shade_color(u32 _inputs)
{
	return (_inputs & (1u << G_GCI_SHADE)) != 0;
}

bool gli_inputs_hw_lighting(u32 _inputs)
{
	return (_inputs & (1u << G_GCI_HW_LIGHT)) != 0;
}
