#ifndef GLI_RDRAM_WRITE_H
#define GLI_RDRAM_WRITE_H

#include "Types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A copy of @height rows of @width pixels, @srcStride pixels apart, into
 * RDRAM at @startAddress, @numPixels of them, inside the buffer at
 * @bufferAddress of texel @size; a copy starting mid-row starts there */
typedef struct gli_rdram_write
{
	u32 srcStride;
	u32 width, height;
	u32 numPixels;
	u32 startAddress;
	u32 bufferAddress;
	u32 size;
} gli_rdram_write;

/* RGBA8 pixels to RGBA8888, RGBA5551 and 8-bit images; black pixels with
 * no alpha are left as they are in RDRAM */
void gli_rdram_write_rgba32(const u32 *_src, const gli_rdram_write *_w);
void gli_rdram_write_rgba16(const u32 *_src, const gli_rdram_write *_w);
void gli_rdram_write_r8(const u8 *_src, const gli_rdram_write *_w);
/* Depth floats to N64 depth words; 2.0 marks a pixel left as it is */
void gli_rdram_write_depth(const f32 *_src, const gli_rdram_write *_w);
/* An N64 depth word for depth @_z */
u16 gli_depth_to_n64(f32 _z);

/* Fills a VI-sized image at @_address of texel @_size with white */
void gli_rdram_write_white(u32 _address, u32 _size);

#ifdef __cplusplus
}
#endif

#endif
