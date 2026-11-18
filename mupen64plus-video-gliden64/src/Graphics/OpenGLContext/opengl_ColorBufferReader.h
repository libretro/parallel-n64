#ifndef GLI_GL_COLOR_BUFFER_READER_H
#define GLI_GL_COLOR_BUFFER_READER_H

/* GL's color buffer readers (Graphics/ColorBufferReader.h). C89. */

#include <Graphics/ColorBufferReader.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A reader with a ring of persistently mapped buffers, of pixel buffers
 * mapped as they are read, or of neither, reading straight into its own
 * memory. The buffer readers bind GL_PIXEL_PACK_BUFFER through the
 * context's binding cache @_bindCache. */
gli_cbr *gli_gl_cbr_new_buffer_storage(struct gli_tex *_texture, void *_bindCache);
gli_cbr *gli_gl_cbr_new_pixel_buffer(struct gli_tex *_texture, void *_bindCache);
gli_cbr *gli_gl_cbr_new_read_pixels(struct gli_tex *_texture);

/* glBindBuffer(@_target, @_name) through the binding cache @_cache, which
 * skips the call when it is bound already (opengl_CachedFunctions.cpp) */
void gli_gl_bind_buffer(void *_cache, u32 _target, u32 _name);

#ifdef __cplusplus
}
#endif

#endif
