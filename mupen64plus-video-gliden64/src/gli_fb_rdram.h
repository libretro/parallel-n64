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

/* Fills the color image with the fill color over a rectangle clamped to
 * the scissor; @_rect gets the dword rectangle filled (ulx, uly, lrx, lry) */
void gli_fb_rdram_fill(s32 _ulx, s32 _uly, s32 _lrx, s32 _lry, s32 *_rect);

/* Whether buffer 1, ending inside buffer 2, only runs a few lines into it */
int gli_fb_overlapping(u32 _start1, u32 _end1, u32 _width1, u32 _size1,
		u32 _start2, u32 _end2, u32 _width2, u32 _size2);

/* Whether color image @_address of @_width and @_size, drawn this swap,
 * lies inside a buffer from @_bufStart to @_bufEnd off its line starts
 * or more than five lines before its end */
int gli_fb_is_sub_buffer(u32 _bufStart, u32 _bufEnd, u32 _bufWidth, u32 _bufSize,
		int _bufCfb, u32 _bufSwapCount, u32 _swapCount, u32 _address, u32 _width, u32 _size);

/* Whether a buffer ending at @_bufEnd runs under five lines into the
 * color image at @_address */
int gli_fb_runs_into(u32 _bufEnd, u32 _bufWidth, u32 _bufSize,
		u32 _address, u32 _width, u32 _size);

/* Where color image @_address starts inside the buffer at @_bufStart */
void gli_fb_sub_origin(u32 _bufStart, u32 _address, u32 _width, u32 _size, u32 *_originX, u32 *_originY);

#ifdef __cplusplus
}
#endif

#endif
