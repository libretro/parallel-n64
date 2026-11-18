#ifndef GLI_PIXEL_BUFFER_H
#define GLI_PIXEL_BUFFER_H

/* A buffer a frame buffer's rows are read back into, for the depth copy to
 * RDRAM: a read left in flight, polled or waited for, then mapped where it
 * lies. A backend gives a buffer its ops. C89. */

#include <stddef.h>
#include <boolean.h>
#include "Types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct gli_prb gli_prb;

typedef struct gli_prb_ops
{
	/* rows of the bound read frame buffer, @_format and @_type the
	 * gli_params.h numbers */
	void (*read_pixels)(gli_prb *_buffer, s32 _x, s32 _y, u32 _width, u32 _height, u32 _format, u32 _type);
	/* whether the last read has landed, waiting for it when @_wait */
	bool (*ready)(gli_prb *_buffer, bool _wait);
	/* @_range bytes of it from @_offset, NULL where it cannot be mapped;
	 * unmap lets them go */
	void *(*map)(gli_prb *_buffer, u32 _offset, u32 _range);
	void (*unmap)(gli_prb *_buffer);
	/* around a read and a map */
	void (*bind)(gli_prb *_buffer);
	void (*unbind)(gli_prb *_buffer);
	/* the buffer let go and freed */
	void (*destroy)(gli_prb *_buffer);
} gli_prb_ops;

struct gli_prb
{
	const gli_prb_ops *ops;
};

#ifdef __cplusplus
}
#endif

#endif
