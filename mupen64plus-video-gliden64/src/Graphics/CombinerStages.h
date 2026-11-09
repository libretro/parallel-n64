#pragma once
#include <Combiner.h>
#include <gDP.h>
#include <GBI.h>

/* What every backend does to a combiner's stages before writing them. */

namespace graphics {

inline int correctFirstStageParam(int _param)
{
	switch (_param) {
	case G_GCI_TEXEL1:
		return G_GCI_TEXEL0;
	case G_GCI_TEXEL1_ALPHA:
		return G_GCI_TEXEL0_ALPHA;
	}
	return _param;
}

inline void correctFirstStageParams(CombinerStage & _stage)
{
	for (int i = 0; i < _stage.numOps; ++i) {
		_stage.op[i].param1 = correctFirstStageParam(_stage.op[i].param1);
		_stage.op[i].param2 = correctFirstStageParam(_stage.op[i].param2);
		_stage.op[i].param3 = correctFirstStageParam(_stage.op[i].param3);
	}
}

inline int correctSecondStageParam(int _param)
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

inline void correctSecondStageParams(CombinerStage & _stage)
{
	for (int i = 0; i < _stage.numOps; ++i) {
		_stage.op[i].param1 = correctSecondStageParam(_stage.op[i].param1);
		_stage.op[i].param2 = correctSecondStageParam(_stage.op[i].param2);
		_stage.op[i].param3 = correctSecondStageParam(_stage.op[i].param3);
	}
}

/* N64 sign extension of the first cycle's result, by how the second reads it */
inline bool combinedColorC(u32 _cycleType, const gDPCombine & _combine)
{
	return _cycleType == G_CYC_2CYCLE && _combine.mRGB1 == G_CCMUX_COMBINED;
}

inline bool combinedAlphaC(u32 _cycleType, const gDPCombine & _combine)
{
	return _cycleType == G_CYC_2CYCLE && _combine.mA1 == G_ACMUX_COMBINED;
}

inline bool combinedColorABD(u32 _cycleType, const gDPCombine & _combine)
{
	if (_cycleType != G_CYC_2CYCLE)
		return false;
	if (_combine.aRGB1 == G_CCMUX_COMBINED)
		return true;
	if (_combine.saRGB1 == G_CCMUX_COMBINED || _combine.sbRGB1 == G_CCMUX_COMBINED)
		return _combine.mRGB1 != G_CCMUX_0;
	return false;
}

inline bool combinedAlphaABD(u32 _cycleType, const gDPCombine & _combine)
{
	if (_cycleType != G_CYC_2CYCLE)
		return false;
	if (_combine.aA1 == G_ACMUX_COMBINED)
		return true;
	if (_combine.saA1 == G_ACMUX_COMBINED || _combine.sbA1 == G_ACMUX_COMBINED)
		return _combine.mA1 != G_ACMUX_0;
	return false;
}

/* Four words a stage: op * 256 + input, -1 to end, INTER keeping its other
 * two inputs in the next two. */
inline void encodeStage(const CombinerStage & _stage, int * _ops)
{
	for (int i = 0; i < 4; ++i)
		_ops[i] = -1;
	for (int i = 0; i < _stage.numOps && i < 4; ++i) {
		const CombinerOp & op = _stage.op[i];
		if (op.op == INTER) {
			_ops[0] = (INTER << 8) | op.param1;
			_ops[1] = op.param2;
			_ops[2] = op.param3;
			_ops[3] = -1;
			return;
		}
		_ops[i] = (op.op << 8) | op.param1;
	}
}

}
