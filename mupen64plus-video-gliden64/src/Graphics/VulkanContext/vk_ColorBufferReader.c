/* Vulkan's color buffer reader (vk_ColorBufferReader.h). C89.
 *
 * Each slot is a readback of the renderer's, whose memory the rows are
 * read in, where they lie; a read that waits is slot 0's. */
#include "vk_ColorBufferReader.h"
#include "Config.h"

#define MAX_SLOTS 8

typedef struct vk_cbr
{
	gli_cbr base;
	vkr_renderer *renderer;
	vkr_readback *rb[MAX_SLOTS];
} vk_cbr;

static void read_to_slot(gli_cbr *_reader, u32 _slot, s32 _x0, s32 _y0, u32 _height, u32 _size)
{
	vk_cbr *r = (vk_cbr *)_reader;
	(void)_size;
	vkr_readback_tex(r->renderer, r->rb[_slot], _reader->texture->name, _x0, _y0, _reader->texture->width, _height);
}

static int slot_ready(gli_cbr *_reader, u32 _slot, int _wait)
{
	vk_cbr *r = (vk_cbr *)_reader;
	return vkr_readback_ready(r->renderer, r->rb[_slot], _wait ? 1 : 0) != 0;
}

static const u8 *map_slot(gli_cbr *_reader, u32 _slot, u32 _height, u32 _bytes, u32 *_stride)
{
	(void)_height;
	(void)_bytes;
	*_stride = _reader->texture->width;
	return (const u8 *)vkr_readback_data(((vk_cbr *)_reader)->rb[_slot]);
}

static const u8 *read_wait(gli_cbr *_reader, const gli_cbr_read *_read, u32 *_heightOffset, u32 *_stride)
{
	vk_cbr *r = (vk_cbr *)_reader;
	vkr_readback_tex(r->renderer, r->rb[0], _reader->texture->name, _read->x0, _read->y0,
		_reader->texture->width, _read->height);
	vkr_readback_ready(r->renderer, r->rb[0], 1);
	*_heightOffset = 0;
	*_stride = _reader->texture->width;
	return (const u8 *)vkr_readback_data(r->rb[0]);
}

static void clean_up(gli_cbr *_reader)
{
	(void)_reader;
}

static void destroy(gli_cbr *_reader)
{
	vk_cbr *r = (vk_cbr *)_reader;
	u32 i;
	for (i = 0; i < _reader->numSlots; ++i)
		vkr_readback_delete(r->renderer, r->rb[i]);
}

static const gli_cbr_ops vk_ops = {
	destroy, read_wait, clean_up, read_to_slot, slot_ready, map_slot
};

gli_cbr *gli_vk_cbr_new(vkr_renderer *_renderer, struct gli_tex *_texture)
{
	vk_cbr *r = (vk_cbr *)gli_cbr_new(sizeof(vk_cbr), &vk_ops, _texture);
	u32 i, n;
	if (r == NULL)
		return NULL;
	r->renderer = _renderer;
	n = config.frameBufferEmulation.copyToRDRAM > 1 ? config.frameBufferEmulation.copyToRDRAM : 1;
	if (n > MAX_SLOTS)
		n = MAX_SLOTS;
	r->base.numSlots = n;
	for (i = 0; i < n; ++i)
		r->rb[i] = vkr_readback_new(_renderer, _texture->textureBytes);
	return &r->base;
}
