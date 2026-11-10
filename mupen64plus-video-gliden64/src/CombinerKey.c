#include "GBI.h"
#include "gDP.h"
#include "CombinerKey.h"

/* The high byte of muxs0 is zero: it holds the combiner's mode flags.
 * [0 - 0] polygon type: 0 - triangle, 1 - rect
 * [1 - 2] cycle type
 * [3 - 3] bi_lerp1
 * [4 - 4] bi_lerp0
 * [5 - 5] is HWL supported */
u64 gli_combiner_key(u64 _mux)
{
	gDPCombine key;
	u32 flags = gli_combiner_is_rect_mode() ? 1U : 0U;
	const u32 cycleType = gDP.otherMode.cycleType;
	const u32 bilerp = (gDP.otherMode.h >> 10) & 3;
	const u32 isHWLSupported = (u32)gli_GBI_hwlSupported;
	flags |= (cycleType << 1);
	flags |= (bilerp << 3);
	flags |= (isHWLSupported << 5);

	key.mux = _mux;
	key.muxs0 |= (flags << 24);
	return key.mux;
}

static u32 _muxs0(u64 _key)
{
	gDPCombine key;
	key.mux = _key;
	return key.muxs0;
}

u32 gli_combiner_key_cycle_type(u64 _key)
{
	return (_muxs0(_key) >> 25) & 3;
}

u32 gli_combiner_key_bilerp(u64 _key)
{
	return (_muxs0(_key) >> 27) & 3;
}

bool gli_combiner_key_is_rect(u64 _key)
{
	return ((_muxs0(_key) >> 24) & 1) != 0;
}

bool gli_combiner_key_hwl(u64 _key)
{
	return ((_muxs0(_key) >> 29) & 1) != 0;
}
