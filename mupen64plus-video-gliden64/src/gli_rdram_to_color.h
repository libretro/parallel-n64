#ifndef GLI_RDRAM_TO_COLOR_H
#define GLI_RDRAM_TO_COLOR_H

#include "Types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The upload buffer is this big; a frame buffer wider or taller is not
 * copied */
#define GLI_R2C_WIDTH 640
#define GLI_R2C_HEIGHT 580

/* One bit per 16-bit word of the largest RDRAM */
#define GLI_R2C_MARK_WORDS ((8u * 1024u * 1024u / 2u) / 32u)

/* The RDRAM pixels the CPU wrote since the last copy. A set of bits, not
 * a list: it never grows, and a pixel written twice is copied once */
typedef struct gli_r2c_marks
{
	u32 bits[GLI_R2C_MARK_WORDS];
	u32 lo, hi;     /* the words with bits set, lo..hi-1 */
	u32 minAddress; /* the lowest and highest addresses marked */
	u32 maxAddress;
	u32 count;      /* nonzero once something is marked */
} gli_r2c_marks;

void gli_r2c_marks_init(gli_r2c_marks *_m);
/* Forgets the marks, clearing only the words that hold any */
void gli_r2c_marks_clear(gli_r2c_marks *_m);
void gli_r2c_mark(gli_r2c_marks *_m, u32 _address);

/* Converts the @_width x @_height image at @_address of texel @_size
 * (2 or 3) to ABGR8888 in @_dst, rows @_width apart; a row stops at the
 * end of RDRAM. With @_fullAlpha alpha is opaque. Returns nonzero when
 * any source pixel is not zero */
int gli_r2c_copy_image(u32 _address, u32 _size, u32 *_dst, u32 _width, u32 _height, int _fullAlpha);

/* Only the marked pixels of that image, the rest left zero. Returns zero,
 * converting nothing worth drawing, when a mark lies before @_address or
 * past the row under the image */
int gli_r2c_copy_marked(const gli_r2c_marks *_m, u32 _address, u32 _size, u32 *_dst, u32 _width, u32 _height, int _fullAlpha);

/* Rewrites @_count ABGR8888 pixels in place as RGBA float, four floats
 * each (the buffer must hold them), or as RGB565 */
void gli_r2c_to_float(void *_buf, u32 _count);
void gli_r2c_to_565(void *_buf, u32 _count);

#ifdef __cplusplus
}
#endif

#endif
