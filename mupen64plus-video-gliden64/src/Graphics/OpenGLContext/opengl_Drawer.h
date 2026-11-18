#ifndef GLI_GL_DRAWER_H
#define GLI_GL_DRAWER_H

/* GL's drawers: what sends the triangles, rects and lines (Graphics/
 * gli_context.h's draw blocks) to GL, with buffers or from the CPU's
 * memory. C89. */

#include <Graphics/gli_context.h>
#include "opengl_GLInfo.h"
#include "opengl_AttribArrays.h"

#ifdef __cplusplus
extern "C" {
#endif

struct SPVertex;
typedef struct gli_gl_drawer gli_gl_drawer;

typedef struct gli_gl_drawer_ops
{
	void (*draw_triangles)(gli_gl_drawer *_drawer, const gli_draw_triangles_params *_params);
	void (*draw_rects)(gli_gl_drawer *_drawer, const gli_draw_rects_params *_params);
	void (*draw_line)(gli_gl_drawer *_drawer, f32 _width, struct SPVertex *_vertices);
	void (*destroy)(gli_gl_drawer *_drawer);
} gli_gl_drawer_ops;

struct gli_gl_drawer
{
	const gli_gl_drawer_ops *ops;
};

/* A drawer streaming through rings of buffers, persistently mapped with
 * buffer storage; it binds them through the binding cache @_bindCache */
gli_gl_drawer *gli_gl_drawer_new_buffered(const gli_gl_info *_info, gli_gl_attribs *_attribs, void *_bindCache);
/* A drawer pointing GL at the vertices where they lie. Either is dropped
 * by its ops->destroy. */
gli_gl_drawer *gli_gl_drawer_new_unbuffered(const gli_gl_info *_info, gli_gl_attribs *_attribs);
/* the binding cache's bind (opengl_CachedFunctions.cpp) */
void gli_gl_bind_buffer(void *_cache, u32 _target, u32 _name);

#ifdef __cplusplus
}
#endif

#endif
