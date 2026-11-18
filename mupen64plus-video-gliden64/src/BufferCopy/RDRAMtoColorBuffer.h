#ifndef RDRAMtoColorBuffer_H
#define RDRAMtoColorBuffer_H

/* The copy of what the CPU wrote to a frame buffer in RDRAM onto that
 * buffer's texture, drawn over it as a textured rect. C89. */

#include <boolean.h>
#include "Types.h"

#ifdef __cplusplus
extern "C" {
#endif

struct FrameBuffer;

/* the upload texture and buffer, made and dropped */
void gli_r2cb_init(void);
void gli_r2cb_destroy(void);

/* a pixel at @_address of @_size bytes the CPU wrote */
void gli_r2cb_add_address(u32 _address, u32 _size);

/* the buffer at @_address, a colour frame buffer @_bCFB, copied from
 * RDRAM; or @_pBuffer, up to the most the VI shows */
void gli_r2cb_copy_from_rdram(u32 _address, bool _bCFB);
void gli_r2cb_copy_buffer_from_rdram(struct FrameBuffer *_pBuffer);

#ifdef __cplusplus
}
#endif

#endif
