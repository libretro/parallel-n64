/* GL's color buffer readers (opengl_ColorBufferReader.h), and the depth
 * copy's read-back buffer at the end. C89.
 *
 * With buffer storage, a ring of buffers mapped once, persistently; with
 * pixel buffers, a ring mapped as each read is taken; with neither,
 * glReadPixels into the reader's memory, waiting. A buffer reader's ring
 * has as many buffers as copyToRDRAM asks for, at least one, at most
 * three, and its waits go by fences. */
#include <stddef.h>
#include <stdlib.h>

#include "opengl_ColorBufferReader.h"
#include "GLFunctions.h"
#include "Config.h"

#define MAX_PBO 3

typedef struct gl_cbr
{
	gli_cbr base;
	void *bindCache;
	GLuint pbo[MAX_PBO];
	GLsync fence[MAX_PBO];
	void *data[MAX_PBO];    /* buffer storage: each buffer's mapping */
	int mapped;             /* pixel buffers: one is mapped */
} gl_cbr;

static void bind_pack(gl_cbr *_r, GLuint _pbo)
{
	gli_gl_bind_buffer(_r->bindCache, GL_PIXEL_PACK_BUFFER, _pbo);
}

static u32 ring_size(void)
{
	u32 n = config.frameBufferEmulation.copyToRDRAM;
	if (n < 1)
		n = 1;
	return n > MAX_PBO ? MAX_PBO : n;
}

static void fence_slot(gl_cbr *_r, u32 _slot)
{
	if (_r->fence[_slot] != 0)
		glDeleteSync(_r->fence[_slot]);
	_r->fence[_slot] = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
}

/* the rows of a slot's read, the texture's width, into its buffer */
static void read_to_slot(gli_cbr *_reader, u32 _slot, s32 _x0, s32 _y0, u32 _height, u32 _size)
{
	gl_cbr *r = (gl_cbr *)_reader;
	u32 format, type, bytes;
	gli_cbr_format(_size, &format, &type, &bytes);

	bind_pack(r, r->pbo[_slot]);
	glReadPixels(_x0, _y0, _reader->texture->width, _height, (GLenum)format, (GLenum)type, 0);
	bind_pack(r, 0);
	fence_slot(r, _slot);
}

static int slot_ready(gli_cbr *_reader, u32 _slot, int _wait)
{
	gl_cbr *r = (gl_cbr *)_reader;
	GLenum res;
	if (r->fence[_slot] == 0)
		return 1;
	/* the poll flushes, so the fence is bound to signal; a wait has no
	 * limit, as the rows are not valid before it does, and mapping before
	 * it signals would stall in the driver */
	res = glClientWaitSync(r->fence[_slot], GL_SYNC_FLUSH_COMMANDS_BIT, 0);
	while (_wait && res == GL_TIMEOUT_EXPIRED)
		res = glClientWaitSync(r->fence[_slot], 0, 1000000000);
	if (res == GL_TIMEOUT_EXPIRED)
		return 0;
	glDeleteSync(r->fence[_slot]);
	r->fence[_slot] = 0;
	return 1;
}

static void delete_ring(gl_cbr *_r)
{
	u32 i;
	glDeleteBuffers(_r->base.numSlots, _r->pbo);
	for (i = 0; i < _r->base.numSlots; ++i) {
		_r->pbo[i] = 0;
		if (_r->fence[i] != 0)
			glDeleteSync(_r->fence[i]);
		_r->fence[i] = 0;
	}
}

/* ---- buffer storage ---- */

static const u8 *storage_read(gli_cbr *_reader, const gli_cbr_read *_read, u32 *_heightOffset, u32 *_stride)
{
	gl_cbr *r = (gl_cbr *)_reader;
	bind_pack(r, r->pbo[0]);
	glReadPixels(_read->x0, _read->y0, _reader->texture->width, _read->height,
		(GLenum)_read->colorFormat, (GLenum)_read->colorType, 0);
	fence_slot(r, 0);
	slot_ready(_reader, 0, 1);
	*_heightOffset = 0;
	*_stride = _reader->texture->width;
	return (const u8 *)r->data[0];
}

static const u8 *storage_map(gli_cbr *_reader, u32 _slot, u32 _height, u32 _bytes, u32 *_stride)
{
	(void)_height;
	(void)_bytes;
	*_stride = _reader->texture->width;
	return (const u8 *)((gl_cbr *)_reader)->data[_slot];
}

static void storage_clean_up(gli_cbr *_reader)
{
	bind_pack((gl_cbr *)_reader, 0);
}

static void storage_destroy(gli_cbr *_reader)
{
	delete_ring((gl_cbr *)_reader);
}

static const gli_cbr_ops storage_ops = {
	storage_destroy, storage_read, storage_clean_up, read_to_slot, slot_ready, storage_map
};

gli_cbr *gli_gl_cbr_new_buffer_storage(struct gli_tex *_texture, void *_bindCache)
{
	gl_cbr *r = (gl_cbr *)gli_cbr_new(sizeof(gl_cbr), &storage_ops, _texture);
	u32 i;
	if (r == NULL)
		return NULL;
	r->bindCache = _bindCache;
	r->base.numSlots = ring_size();
	glGenBuffers(r->base.numSlots, r->pbo);
	for (i = 0; i < r->base.numSlots; ++i) {
		bind_pack(r, r->pbo[i]);
		r->fence[i] = 0;
		glBufferStorage(GL_PIXEL_PACK_BUFFER, _texture->textureBytes, NULL,
			GL_MAP_READ_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT | GL_CLIENT_STORAGE_BIT);
		r->data[i] = glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, _texture->textureBytes,
			GL_MAP_READ_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT);
	}
	bind_pack(r, 0);
	return &r->base;
}

/* ---- pixel buffers ---- */

static const u8 *pbo_read(gli_cbr *_reader, const gli_cbr_read *_read, u32 *_heightOffset, u32 *_stride)
{
	gl_cbr *r = (gl_cbr *)_reader;
	bind_pack(r, r->pbo[0]);
	glReadPixels(_read->x0, _read->y0, _reader->texture->width, _read->height,
		(GLenum)_read->colorFormat, (GLenum)_read->colorType, 0);
	*_heightOffset = 0;
	*_stride = _reader->texture->width;
	r->mapped = 1;
	return (const u8 *)glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0,
		_reader->texture->width * _read->height * _read->colorFormatBytes, GL_MAP_READ_BIT);
}

static const u8 *pbo_map(gli_cbr *_reader, u32 _slot, u32 _height, u32 _bytes, u32 *_stride)
{
	gl_cbr *r = (gl_cbr *)_reader;
	*_stride = _reader->texture->width;
	bind_pack(r, r->pbo[_slot]);
	r->mapped = 1;
	return (const u8 *)glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0,
		_reader->texture->width * _height * _bytes, GL_MAP_READ_BIT);
}

static void pbo_clean_up(gli_cbr *_reader)
{
	gl_cbr *r = (gl_cbr *)_reader;
	if (r->mapped)
		glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
	r->mapped = 0;
	bind_pack(r, 0);
}

static void pbo_destroy(gli_cbr *_reader)
{
	delete_ring((gl_cbr *)_reader);
}

static const gli_cbr_ops pbo_ops = {
	pbo_destroy, pbo_read, pbo_clean_up, read_to_slot, slot_ready, pbo_map
};

gli_cbr *gli_gl_cbr_new_pixel_buffer(struct gli_tex *_texture, void *_bindCache)
{
	gl_cbr *r = (gl_cbr *)gli_cbr_new(sizeof(gl_cbr), &pbo_ops, _texture);
	u32 i;
	if (r == NULL)
		return NULL;
	r->bindCache = _bindCache;
	r->base.numSlots = ring_size();
	glGenBuffers(r->base.numSlots, r->pbo);
	r->mapped = 0;
	for (i = 0; i < r->base.numSlots; ++i) {
		bind_pack(r, r->pbo[i]);
		glBufferData(GL_PIXEL_PACK_BUFFER, _texture->textureBytes, NULL, GL_DYNAMIC_READ);
		r->fence[i] = 0;
	}
	bind_pack(r, 0);
	return &r->base;
}

/* ---- glReadPixels, the last resort ---- */

static const u8 *pixels_read(gli_cbr *_reader, const gli_cbr_read *_read, u32 *_heightOffset, u32 *_stride)
{
	/* no asynchronous copies here: into the reader's own memory */
	glReadPixels(_read->x0, _read->y0, _reader->texture->width, _read->height,
		(GLenum)_read->colorFormat, (GLenum)_read->colorType, _reader->pixelData);
	*_heightOffset = 0;
	*_stride = _reader->texture->width;
	return _reader->pixelData;
}

static void pixels_nothing(gli_cbr *_reader)
{
	(void)_reader;
}

static const gli_cbr_ops pixels_ops = {
	pixels_nothing, pixels_read, pixels_nothing, NULL, NULL, NULL
};

gli_cbr *gli_gl_cbr_new_read_pixels(struct gli_tex *_texture)
{
	return gli_cbr_new(sizeof(gli_cbr), &pixels_ops, _texture);
}

/* ---- the depth copy's read-back buffer (Graphics/PixelBuffer.h) ---- */

typedef struct gl_prb
{
	gli_prb base;
	void *bindCache;
	u32 size;
	GLuint pbo;
	GLsync fence;
} gl_prb;

static void prb_read_pixels(gli_prb *_buffer, s32 _x, s32 _y, u32 _width, u32 _height, u32 _format, u32 _type)
{
	gl_prb *b = (gl_prb *)_buffer;
	glReadPixels(_x, _y, _width, _height, (GLenum)_format, (GLenum)_type, 0);
	/* polled by ready, so the read can be left in flight */
	if (b->fence != 0)
		glDeleteSync(b->fence);
	b->fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
}

static bool prb_ready(gli_prb *_buffer, bool _wait)
{
	gl_prb *b = (gl_prb *)_buffer;
	GLenum res;
	if (b->fence == 0)
		return 1;
	res = glClientWaitSync(b->fence, GL_SYNC_FLUSH_COMMANDS_BIT, 0);
	while (_wait && res == GL_TIMEOUT_EXPIRED)
		res = glClientWaitSync(b->fence, 0, 1000000000);
	if (res == GL_TIMEOUT_EXPIRED)
		return 0;
	glDeleteSync(b->fence);
	b->fence = 0;
	return 1;
}

static void *prb_map(gli_prb *_buffer, u32 _offset, u32 _range)
{
	gl_prb *b = (gl_prb *)_buffer;
	if (_range > b->size)
		_range = b->size;
	return glMapBufferRange(GL_PIXEL_PACK_BUFFER, _offset, _range, GL_MAP_READ_BIT);
}

static void prb_unmap(gli_prb *_buffer)
{
	(void)_buffer;
	glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
}

static void prb_bind(gli_prb *_buffer)
{
	gl_prb *b = (gl_prb *)_buffer;
	gli_gl_bind_buffer(b->bindCache, GL_PIXEL_PACK_BUFFER, b->pbo);
}

static void prb_unbind(gli_prb *_buffer)
{
	gl_prb *b = (gl_prb *)_buffer;
	gli_gl_bind_buffer(b->bindCache, GL_PIXEL_PACK_BUFFER, 0);
}

static void prb_destroy(gli_prb *_buffer)
{
	gl_prb *b = (gl_prb *)_buffer;
	if (b->fence != 0)
		glDeleteSync(b->fence);
	glDeleteBuffers(1, &b->pbo);
	free(b);
}

static const gli_prb_ops prb_ops = {
	prb_read_pixels, prb_ready, prb_map, prb_unmap, prb_bind, prb_unbind, prb_destroy
};

gli_prb *gli_gl_prb_new(void *_bindCache, size_t _bytes)
{
	gl_prb *b = (gl_prb *)calloc(1, sizeof(*b));
	if (b == NULL)
		return NULL;
	b->base.ops = &prb_ops;
	b->bindCache = _bindCache;
	b->size = (u32)_bytes;
	glGenBuffers(1, &b->pbo);
	gli_gl_bind_buffer(_bindCache, GL_PIXEL_PACK_BUFFER, b->pbo);
	glBufferData(GL_PIXEL_PACK_BUFFER, (GLsizeiptr)_bytes, NULL, GL_DYNAMIC_READ);
	gli_gl_bind_buffer(_bindCache, GL_PIXEL_PACK_BUFFER, 0);
	return &b->base;
}
