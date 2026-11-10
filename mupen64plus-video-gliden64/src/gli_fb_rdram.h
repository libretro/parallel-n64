#ifndef GLI_FB_RDRAM_H
#define GLI_FB_RDRAM_H

#include "Types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* How many of @_height lines of @_stride bytes at @_address fit in RDRAM */
u32 cutHeight(u32 _address, u32 _height, u32 _stride);

/* Marks an auxiliary buffer's RDRAM so a game restoring the old content
 * cannot make it look untouched */
void gli_fb_rdram_mark(u32 _startAddress, u32 _dataSize);

/* Whether RDRAM still holds what the buffer left there: within 1% of
 * the dwords for a clear or a copy, all of the mark */
int gli_fb_rdram_cleared_valid(u32 _startAddress, u32 _endAddress, u32 _width,
		u32 _size, u32 _fillcolor, s32 _ulx, s32 _uly, s32 _lrx, s32 _lry);
int gli_fb_rdram_mark_valid(u32 _startAddress);
int gli_fb_rdram_copy_valid(u32 _startAddress, const u8 *_copy, u32 _size);

#ifdef __cplusplus
}
#endif

#endif
