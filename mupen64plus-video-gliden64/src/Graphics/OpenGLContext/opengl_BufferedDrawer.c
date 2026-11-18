#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <Config.h>
#include <CRC.h>
#include <gSP.h>
#include "GLFunctions.h"
#include "opengl_Attributes.h"
#include "opengl_RingSegments.h"
#include "opengl_Drawer.h"

/* The draws streamed through rings of buffers: the rects' vertices, and the
 * triangles' vertices and elements, each where the ring places it
 * (opengl_RingSegments.h). With buffer storage the rings stay mapped and
 * the vertices are written straight into them; without it each write maps
 * the range it goes to, unsynchronized, the segment fences keeping the
 * GPU's data apart. */

#define GLI_RING_MAX_BYTES (8u * 1024u * 1024u)
/* a segment still holds the largest vertex batch (VERTBUFF_SIZE) */
#define GLI_RING_MIN_BYTES (48u * 1024u)

#ifndef GL_DEBUG
#define GLI_RING_ACCESS_BITS (GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT)
#define GLI_RING_MAP_BITS (GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT)
#else
#define GLI_RING_ACCESS_BITS (GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT)
#define GLI_RING_MAP_BITS (GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_FLUSH_EXPLICIT_BIT)
#endif

/* what the triangles' ring holds a vertex as */
typedef struct gli_gl_vertex
{
	f32 x, y, z, w;
	f32 r, g, b, a;
	f32 s, t;
	u32 modify;
} gli_gl_vertex;

typedef struct gli_gl_ring_buffer
{
	GLenum type;
	GLuint handle;
	u32 offset;
	GLint pos;
	GLubyte *data;
	gli_ring ring;
	GLsync fences[GLI_RING_SEGMENTS];
} gli_gl_ring_buffer;

/* Without buffer storage the same rect is drawn from where it was put
 * before, while the ring has yet to come round to it: its vertices' CRC to
 * the vertex after them. Open addressing; an entry counts only in the lap
 * it was made in (gen), so a lap's end drops them all at once. Sized for a
 * lap of the smallest rects at half full; past that, all are dropped. */
typedef struct gli_gl_rect_entry
{
	u64 crc;
	u32 pos;
	u32 gen;
} gli_gl_rect_entry;

enum { GLI_BUFFERS_NONE, GLI_BUFFERS_RECTS, GLI_BUFFERS_TRIANGLES };

typedef struct gli_gl_buffered
{
	gli_gl_drawer base;
	const gli_gl_info *info;
	gli_gl_attribs *attribs;
	void *bindCache;
	GLuint rectsVao;
	GLuint trisVao;
	gli_gl_ring_buffer rects;
	gli_gl_ring_buffer tris;
	gli_gl_ring_buffer elements;
	int bound;
	gli_gl_rect_entry *rectEntries;
	u32 rectMask;
	u32 rectCount;
	u32 rectGen;
} gli_gl_buffered;

/* GLIDEN64_RING_BYTES shrinks the rings so a short test goes round them
 * many times: CI draws the same frames with a small ring as with the
 * full one, which every draw placed at a segment start or back at 0
 * must keep. */
static u32 ring_size(void)
{
	const char *env = getenv("GLIDEN64_RING_BYTES");
	unsigned long bytes;
	if (env == NULL || *env == '\0')
		return GLI_RING_MAX_BYTES;
	bytes = strtoul(env, NULL, 0);
	if (bytes < GLI_RING_MIN_BYTES)
		return GLI_RING_MIN_BYTES;
	return bytes > GLI_RING_MAX_BYTES ? GLI_RING_MAX_BYTES : (u32)bytes;
}

static void init_buffer(gli_gl_buffered *_d, gli_gl_ring_buffer *_b, GLenum _type, u32 _size, u32 _stride)
{
	_b->type = _type;
	gli_ring_init(&_b->ring, _size, _stride);
	glGenBuffers(1, &_b->handle);
	gli_gl_bind_buffer(_d->bindCache, _type, _b->handle);
	if (_d->info->bufferStorage) {
		glBufferStorage(_type, _size, NULL, GLI_RING_ACCESS_BITS);
		_b->data = (GLubyte *)glMapBufferRange(_type, 0, _size, GLI_RING_MAP_BITS);
	} else {
		glBufferData(_type, _size, NULL, GL_DYNAMIC_DRAW);
	}
}

/* the draws so far, marked when the writes leave a segment */
static void fence_segment(void *_fences, u32 _seg)
{
	GLsync *fences = (GLsync *)_fences;
	if (fences[_seg] != 0)
		glDeleteSync(fences[_seg]);
	fences[_seg] = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
}

static void wait_segment(void *_fences, u32 _seg)
{
	GLsync *fences = (GLsync *)_fences;
	GLenum res;
	if (fences[_seg] == 0)
		return;
	res = glClientWaitSync(fences[_seg], GL_SYNC_FLUSH_COMMANDS_BIT, 0);
	while (res == GL_TIMEOUT_EXPIRED)
		res = glClientWaitSync(fences[_seg], 0, 1000000000);
	glDeleteSync(fences[_seg]);
	fences[_seg] = 0;
}

/* Where @_bytes go: mapped, written to the ring (or, without buffer
 * storage, to the range mapped for them), and then committed. */
static GLubyte *reserve(gli_gl_buffered *_d, gli_gl_ring_buffer *_b, u32 _bytes)
{
	gli_ring_ops ops;
	u32 offset;
	ops.fence = fence_segment;
	ops.wait = wait_segment;
	ops.ctx = _b->fences;
	offset = gli_ring_place(&_b->ring, _b->offset, _bytes, &ops);
	if (offset != _b->offset) {
		/* a segment start, or 0: the draws index from there */
		_b->offset = offset;
		_b->pos = (GLint)gli_ring_element(&_b->ring, offset);
	}
	if (_d->info->bufferStorage)
		return _b->data + _b->offset;
	/* unsynchronized: the segment fences keep the GPU's data apart */
	gli_gl_bind_buffer(_d->bindCache, _b->type, _b->handle);
	return (GLubyte *)glMapBufferRange(_b->type, _b->offset, _bytes, GL_MAP_WRITE_BIT | GL_MAP_UNSYNCHRONIZED_BIT);
}

static void commit(gli_gl_buffered *_d, gli_gl_ring_buffer *_b, u32 _count, u32 _bytes)
{
	if (_d->info->bufferStorage) {
#ifdef GL_DEBUG
		gli_gl_bind_buffer(_d->bindCache, _b->type, _b->handle);
		glFlushMappedBufferRange(_b->type, _b->offset, _bytes);
#endif
	} else {
		glUnmapBuffer(_b->type);
	}
	_b->offset += _bytes;
	_b->pos += (GLint)_count;
}

static void update_buffer(gli_gl_buffered *_d, gli_gl_ring_buffer *_b, u32 _count, u32 _bytes, const void *_data)
{
	memcpy(reserve(_d, _b, _bytes), _data, _bytes);
	commit(_d, _b, _count, _bytes);
}

static void bind_arrays(gli_gl_buffered *_d, int _which)
{
	if (_d->bound == _which)
		return;
	glBindVertexArray(_which == GLI_BUFFERS_RECTS ? _d->rectsVao : _d->trisVao);
	_d->bound = _which;
}

static gli_gl_rect_entry *rect_slot(gli_gl_buffered *_d, u64 _crc)
{
	u32 i = ((u32)_crc ^ (u32)(_crc >> 32)) & _d->rectMask;
	for (;;) {
		gli_gl_rect_entry *e = &_d->rectEntries[i];
		if (e->gen != _d->rectGen || e->crc == _crc)
			return e;
		i = (i + 1) & _d->rectMask;
	}
}

static void rects_forget(gli_gl_buffered *_d)
{
	_d->rectCount = 0;
	if (++_d->rectGen == 0) {
		memset(_d->rectEntries, 0, (_d->rectMask + 1) * sizeof(gli_gl_rect_entry));
		_d->rectGen = 1;
	}
}

static void update_rects(gli_gl_buffered *_d, const gli_draw_rects_params *_params)
{
	gli_gl_ring_buffer *b = &_d->rects;
	const u32 bytes = _params->verticesCount * (u32)sizeof(RectVertex);
	u64 crc;
	u32 prevOffset;
	gli_gl_rect_entry *e;

	bind_arrays(_d, GLI_BUFFERS_RECTS);

	if (_d->info->bufferStorage) {
		update_buffer(_d, b, _params->verticesCount, bytes, _params->vertices);
		return;
	}

	crc = CRC_Calculate(UINT64_MAX, _params->vertices, bytes);
	e = rect_slot(_d, crc);
	if (e->gen == _d->rectGen) {
		b->pos = (GLint)e->pos;
		return;
	}

	prevOffset = b->offset;
	update_buffer(_d, b, _params->verticesCount, bytes, _params->vertices);
	if (b->offset < prevOffset || _d->rectCount >= (_d->rectMask + 1) / 2) {
		rects_forget(_d);
		e = rect_slot(_d, crc);
	}
	b->pos = (GLint)(b->offset / sizeof(RectVertex));
	e->crc = crc;
	e->pos = (u32)b->pos;
	e->gen = _d->rectGen;
	++_d->rectCount;
}

static void draw_rects(gli_gl_drawer *_drawer, const gli_draw_rects_params *_params)
{
	gli_gl_buffered *d = (gli_gl_buffered *)_drawer;
	update_rects(d, _params);
	gli_gl_attribs_enable(d->attribs, GLI_RECT_ATTRIB_TEXCOORD0, _params->texrect);
	gli_gl_attribs_enable(d->attribs, GLI_RECT_ATTRIB_TEXCOORD1, _params->texrect);
	glDrawArrays((GLenum)_params->mode, d->rects.pos - (GLint)_params->verticesCount, _params->verticesCount);
}

/* the vertices converted straight into where they go */
static void update_vertices(gli_gl_buffered *_d, bool _flatColors, u32 _count, const SPVertex *_src)
{
	gli_gl_ring_buffer *b = &_d->tris;
	const u32 bytes = _count * (u32)sizeof(gli_gl_vertex);
	gli_gl_vertex *dst = (gli_gl_vertex *)reserve(_d, b, bytes);
	u32 i;
	for (i = 0; i < _count; ++i) {
		const SPVertex *s = &_src[i];
		gli_gl_vertex *v = &dst[i];
		v->x = s->x;
		v->y = s->y;
		v->z = s->z;
		v->w = s->w;
		if (_flatColors) {
			v->r = s->flat_r;
			v->g = s->flat_g;
			v->b = s->flat_b;
			v->a = s->flat_a;
		} else {
			v->r = s->r;
			v->g = s->g;
			v->b = s->b;
			v->a = s->a;
		}
		v->s = s->s;
		v->t = s->t;
		v->modify = s->modify;
	}
	commit(_d, b, _count, bytes);
}

static void draw_triangles(gli_gl_drawer *_drawer, const gli_draw_triangles_params *_params)
{
	gli_gl_buffered *d = (gli_gl_buffered *)_drawer;

	bind_arrays(d, GLI_BUFFERS_TRIANGLES);
	update_vertices(d, _params->flatColors, _params->verticesCount, _params->vertices);
	if (_params->elements != NULL)
		update_buffer(d, &d->elements, _params->elementsCount,
			_params->elementsCount * (u32)sizeof(GLushort), _params->elements);

	if (gli_isHWLightingAllowed())
		glVertexAttrib1f(GLI_TRI_ATTRIB_NUMLIGHTS, (GLfloat)_params->vertices[0].HWLight);

	if (_params->elements == NULL) {
		glDrawArrays((GLenum)_params->mode, d->tris.pos - (GLint)_params->verticesCount, _params->verticesCount);
		return;
	}
	glDrawRangeElementsBaseVertex((GLenum)_params->mode, 0, _params->verticesCount - 1, _params->elementsCount,
		GL_UNSIGNED_SHORT, (const GLushort *)NULL + (d->elements.pos - (GLint)_params->elementsCount),
		d->tris.pos - (GLint)_params->verticesCount);
}

static void draw_line(gli_gl_drawer *_drawer, f32 _width, SPVertex *_vertices)
{
	gli_gl_buffered *d = (gli_gl_buffered *)_drawer;
	bind_arrays(d, GLI_BUFFERS_TRIANGLES);
	update_vertices(d, 0, 2, _vertices);
	glLineWidth(_width);
	glDrawArrays(GL_LINES, d->tris.pos - 2, 2);
}

static void destroy(gli_gl_drawer *_drawer)
{
	gli_gl_buffered *d = (gli_gl_buffered *)_drawer;
	gli_gl_ring_buffer *all[3];
	GLuint buffers[3], arrays[2];
	u32 i, j;
	all[0] = &d->rects;
	all[1] = &d->tris;
	all[2] = &d->elements;
	gli_gl_bind_buffer(d->bindCache, GL_ARRAY_BUFFER, 0);
	gli_gl_bind_buffer(d->bindCache, GL_ELEMENT_ARRAY_BUFFER, 0);
	for (i = 0; i < 3; ++i) {
		for (j = 0; j < GLI_RING_SEGMENTS; ++j)
			if (all[i]->fences[j] != 0)
				glDeleteSync(all[i]->fences[j]);
		buffers[i] = all[i]->handle;
	}
	glDeleteBuffers(3, buffers);
	glBindVertexArray(0);
	arrays[0] = d->rectsVao;
	arrays[1] = d->trisVao;
	glDeleteVertexArrays(2, arrays);
	free(d->rectEntries);
	free(d);
}

static const gli_gl_drawer_ops buffered_ops = { draw_triangles, draw_rects, draw_line, destroy };

#define GLI_OFFSET(_type, _field) ((const GLvoid *)offsetof(_type, _field))

gli_gl_drawer *gli_gl_drawer_new_buffered(const gli_gl_info *_info, gli_gl_attribs *_attribs, void *_bindCache)
{
	gli_gl_buffered *d = (gli_gl_buffered *)calloc(1, sizeof(*d));
	const u32 size = ring_size();
	if (d == NULL)
		return NULL;
	d->base.ops = &buffered_ops;
	d->info = _info;
	d->attribs = _attribs;
	d->bindCache = _bindCache;
	d->bound = GLI_BUFFERS_NONE;

	if (!_info->bufferStorage) {
		/* twice the most rects of four vertices a lap holds */
		u32 slots = 1024;
		while (slots < size / (4 * (u32)sizeof(RectVertex)) * 2)
			slots <<= 1;
		d->rectEntries = (gli_gl_rect_entry *)calloc(slots, sizeof(gli_gl_rect_entry));
		if (d->rectEntries == NULL) {
			free(d);
			return NULL;
		}
		d->rectMask = slots - 1;
		d->rectGen = 1;
	}

	glGenVertexArrays(1, &d->rectsVao);
	glBindVertexArray(d->rectsVao);
	init_buffer(d, &d->rects, GL_ARRAY_BUFFER, size, sizeof(RectVertex));
	gli_gl_attribs_enable(_attribs, GLI_RECT_ATTRIB_POSITION, 1);
	gli_gl_attribs_enable(_attribs, GLI_RECT_ATTRIB_TEXCOORD0, 1);
	gli_gl_attribs_enable(_attribs, GLI_RECT_ATTRIB_TEXCOORD1, 1);
	glVertexAttribPointer(GLI_RECT_ATTRIB_POSITION, 4, GL_FLOAT, GL_FALSE, sizeof(RectVertex), GLI_OFFSET(RectVertex, x));
	glVertexAttribPointer(GLI_RECT_ATTRIB_TEXCOORD0, 2, GL_FLOAT, GL_FALSE, sizeof(RectVertex), GLI_OFFSET(RectVertex, s0));
	glVertexAttribPointer(GLI_RECT_ATTRIB_TEXCOORD1, 2, GL_FLOAT, GL_FALSE, sizeof(RectVertex), GLI_OFFSET(RectVertex, s1));

	glGenVertexArrays(1, &d->trisVao);
	glBindVertexArray(d->trisVao);
	init_buffer(d, &d->tris, GL_ARRAY_BUFFER, size, sizeof(gli_gl_vertex));
	init_buffer(d, &d->elements, GL_ELEMENT_ARRAY_BUFFER, size, sizeof(GLushort));
	gli_gl_attribs_enable(_attribs, GLI_TRI_ATTRIB_POSITION, 1);
	gli_gl_attribs_enable(_attribs, GLI_TRI_ATTRIB_COLOR, 1);
	gli_gl_attribs_enable(_attribs, GLI_TRI_ATTRIB_TEXCOORD, 1);
	gli_gl_attribs_enable(_attribs, GLI_TRI_ATTRIB_MODIFY, 1);
	gli_gl_attribs_enable(_attribs, GLI_TRI_ATTRIB_NUMLIGHTS, 0);
	glVertexAttribPointer(GLI_TRI_ATTRIB_POSITION, 4, GL_FLOAT, GL_FALSE, sizeof(gli_gl_vertex), GLI_OFFSET(gli_gl_vertex, x));
	glVertexAttribPointer(GLI_TRI_ATTRIB_COLOR, 4, GL_FLOAT, GL_FALSE, sizeof(gli_gl_vertex), GLI_OFFSET(gli_gl_vertex, r));
	glVertexAttribPointer(GLI_TRI_ATTRIB_TEXCOORD, 2, GL_FLOAT, GL_FALSE, sizeof(gli_gl_vertex), GLI_OFFSET(gli_gl_vertex, s));
	glVertexAttribPointer(GLI_TRI_ATTRIB_MODIFY, 4, GL_BYTE, GL_TRUE, sizeof(gli_gl_vertex), GLI_OFFSET(gli_gl_vertex, modify));
	return &d->base;
}
