#ifndef DepthBufferToRDRAM_H
#define DepthBufferToRDRAM_H

/* The copy of a frame buffer's depth to the depth image in RDRAM. A full
 * copy at the default Async or TripleBuffer colour copy setting is left in
 * flight and written to RDRAM once the GPU has finished it, at a later
 * copy, like the colour copies; a chunk copy, which the CPU is waiting on,
 * writes out what is in flight and then its own at once. C89. */

#include <boolean.h>
#include "../Types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* its read-back buffers, textures and frame buffer, made and dropped */
void gli_d2r_init(void);
void gli_d2r_destroy(void);

/* the depth buffer of the frame buffer at @_address, all of it, or the
 * 4K chunk at @_startAddress; whether it was copied */
bool gli_d2r_copy_to_rdram(u32 _address);
bool gli_d2r_copy_chunk_to_rdram(u32 _startAddress);

#ifdef __cplusplus
}
#endif

#endif
