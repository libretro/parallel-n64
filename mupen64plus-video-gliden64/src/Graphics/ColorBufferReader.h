#ifndef GLI_COLOR_BUFFER_READER_H
#define GLI_COLOR_BUFFER_READER_H

/* Reading a frame buffer texture's rows back, for the copy to RDRAM. C89.
 *
 * A backend gives a reader its ops: a read that waits for its rows, and,
 * for a reader with a ring of buffers (numSlots of them), reads into
 * numbered slots whose waits the caller decides. Rows come back in the
 * reader's own memory, read where they lie, valid until clean_up; float
 * rows, which RDRAM has no use for, are first turned into bytes. */

#include <stddef.h>
#include "Types.h"
#include "gli_tex.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct gli_cbr gli_cbr;

/* A read of @height rows from (@x0, @y0), @width pixels wide, in a format
 * of @colorFormatBytes a pixel */
typedef struct gli_cbr_read
{
	s32 x0, y0;
	u32 width, height;
	int sync;
	u32 colorFormat, colorType, colorFormatBytes;
} gli_cbr_read;

typedef struct gli_cbr_ops
{
	/* lets go of the backend's buffers; the reader itself is freed after */
	void (*destroy)(gli_cbr *_reader);
	/* the rows of @_read, waited for: @_heightOffset rows in, @_stride
	 * pixels apart */
	const u8 *(*read)(gli_cbr *_reader, const gli_cbr_read *_read, u32 *_heightOffset, u32 *_stride);
	void (*clean_up)(gli_cbr *_reader);
	/* with slots: a read of @_height rows starts into @_slot ... */
	void (*read_to_slot)(gli_cbr *_reader, u32 _slot, s32 _x0, s32 _y0, u32 _height, u32 _size);
	/* ... whether it is done, polled or waited for ... */
	int (*slot_ready)(gli_cbr *_reader, u32 _slot, int _wait);
	/* ... and its rows, @_stride pixels apart */
	const u8 *(*map_slot)(gli_cbr *_reader, u32 _slot, u32 _height, u32 _bytes, u32 *_stride);
} gli_cbr_ops;

struct gli_cbr
{
	const gli_cbr_ops *ops;
	struct gli_tex *texture;
	u8 *pixelData;      /* the texture's bytes, for rows read to the CPU */
	u32 numSlots;       /* 0: reads wait */
};

/* A backend's reader, of @_bytes, whose first member is the gli_cbr: the
 * pixel memory comes with it. NULL when there is no memory. */
gli_cbr *gli_cbr_new(size_t _bytes, const gli_cbr_ops *_ops, struct gli_tex *_texture);
void gli_cbr_delete(gli_cbr *_reader);

/* The format, type and bytes a pixel of a read of a @_size image takes */
void gli_cbr_format(u32 _size, u32 *_format, u32 *_type, u32 *_bytes);

/* The rows read back, @_stride pixels apart; @_height comes back as the
 * rows the buffer holds */
const u8 *gli_cbr_read_pixels(gli_cbr *_reader, s32 _x0, s32 _y0, u32 _width, u32 *_height, u32 _size,
	int _sync, u32 *_stride);

/* A slot's @_height rows of @_width pixels, @_stride apart */
const u8 *gli_cbr_slot_pixels(gli_cbr *_reader, u32 _slot, u32 _width, u32 *_height, u32 _size, u32 *_stride);

#ifdef __cplusplus
}
#endif

#endif
