#ifndef ColorBufferToRDRAM_H
#define ColorBufferToRDRAM_H

/* The copy of a colour frame buffer to its image in RDRAM. With a reader
 * that has a ring of buffers, an asynchronous copy is left in flight and
 * written to RDRAM once the GPU has finished it, at a later copy; a
 * synchronous one, or a chunk the CPU is waiting on, writes out what is in
 * flight and then its own at once. C89. */

#include <boolean.h>
#include "../Types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* its frame buffer made and, with its texture and reader, dropped */
void gli_c2r_init(void);
void gli_c2r_destroy(void);

/* the frame buffer at @_address, all of it, or the 4K chunk at
 * @_startAddress */
void gli_c2r_copy_to_rdram(u32 _address, bool _sync);
void gli_c2r_copy_chunk_to_rdram(u32 _startAddress);

#ifdef __cplusplus
}
#endif

#endif
