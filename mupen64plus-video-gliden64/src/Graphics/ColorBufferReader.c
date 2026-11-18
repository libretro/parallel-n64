/* Reading a frame buffer texture's rows back (ColorBufferReader.h). C89. */
#include <stdlib.h>
#include <string.h>

#include "ColorBufferReader.h"
#include "gli_context.h"
#include "GBI.h"

gli_cbr *gli_cbr_new(size_t _bytes, const gli_cbr_ops *_ops, struct gli_tex *_texture)
{
	/* allocated once, as the copy's texture is made, never as it reads */
	gli_cbr *reader = (gli_cbr *)calloc(1, _bytes);
	if (reader == NULL)
		return NULL;
	reader->pixelData = (u8 *)calloc(_texture->textureBytes != 0 ? _texture->textureBytes : 1, 1);
	if (reader->pixelData == NULL) {
		free(reader);
		return NULL;
	}
	reader->ops = _ops;
	reader->texture = _texture;
	return reader;
}

void gli_cbr_delete(gli_cbr *_reader)
{
	if (_reader == NULL)
		return;
	_reader->ops->destroy(_reader);
	free(_reader->pixelData);
	free(_reader);
}

void gli_cbr_format(u32 _size, u32 *_format, u32 *_type, u32 *_bytes)
{
	const gli_fb_formats *formats = &gli_ctx.formats;
	if (_size > G_IM_SIZ_8b) {
		*_format = formats->colorFormat;
		*_type = formats->colorType;
		*_bytes = formats->colorFormatBytes;
	} else {
		*_format = formats->monochromeFormat;
		*_type = formats->monochromeType;
		*_bytes = formats->monochromeFormatBytes;
	}
}

/* Float rows as bytes, in the reader's memory: as many as it holds */
static const u8 *float_rows(gli_cbr *_reader, const u8 *_gpuData, u32 _width, u32 *_height,
	u32 _heightOffset, u32 _stride)
{
	u8 *dest = _reader->pixelData;
	const float *src = (const float *)_gpuData;
	const u32 widthPixels = _width * 4;
	const u32 stridePixels = _stride * 4;
	const u32 bytes = _reader->texture->textureBytes;
	u32 y, x;

	if (widthPixels != 0 && *_height * widthPixels > bytes)
		*_height = bytes / widthPixels;

	for (y = 0; y < *_height; ++y) {
		const float *row = src + (y + _heightOffset) * stridePixels;
		u8 *out = dest + y * widthPixels;
		for (x = 0; x < widthPixels; ++x)
			out[x] = (u8)(row[x] * 255.0);
	}

	return dest;
}

const u8 *gli_cbr_slot_pixels(gli_cbr *_reader, u32 _slot, u32 _width, u32 *_height, u32 _size, u32 *_stride)
{
	u32 format, type, bytes, stride = 0;
	const u8 *pixelData;
	gli_cbr_format(_size, &format, &type, &bytes);

	pixelData = _reader->ops->map_slot(_reader, _slot, *_height, bytes, &stride);
	if (pixelData == NULL)
		return NULL;
	if (type == GLI_DT_FLOAT && _size > G_IM_SIZ_8b) {
		*_stride = _width;
		return float_rows(_reader, pixelData, _width, _height, 0, stride);
	}
	*_stride = stride;
	return pixelData;
}

const u8 *gli_cbr_read_pixels(gli_cbr *_reader, s32 _x0, s32 _y0, u32 _width, u32 *_height, u32 _size,
	int _sync, u32 *_stride)
{
	gli_cbr_read params;
	u32 heightOffset = 0, stride = 0, strideBytes, rows;
	const u8 *pixelData;

	params.x0 = _x0;
	params.y0 = _y0;
	params.width = _width;
	params.height = *_height;
	params.sync = _sync;
	gli_cbr_format(_size, &params.colorFormat, &params.colorType, &params.colorFormatBytes);

	pixelData = _reader->ops->read(_reader, &params, &heightOffset, &stride);
	if (pixelData == NULL)
		return NULL;

	if (params.colorType == GLI_DT_FLOAT && _size > G_IM_SIZ_8b) {
		*_stride = _width;
		return float_rows(_reader, pixelData, params.width, _height, heightOffset, stride);
	}

	/* no compacting copy: the consumer steps by the stride */
	strideBytes = stride * params.colorFormatBytes;
	rows = strideBytes != 0 ? _reader->texture->textureBytes / strideBytes : 0;
	if (rows > heightOffset) {
		if (*_height > rows - heightOffset)
			*_height = rows - heightOffset;
	} else
		*_height = 0;
	*_stride = stride;
	return pixelData + heightOffset * strideBytes;
}
