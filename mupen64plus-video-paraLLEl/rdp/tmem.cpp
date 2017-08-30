#include "tmem.hpp"
#include "common.hpp"
#include <algorithm>
#include <assert.h>
#include <stdio.h>
#include <string.h>

using namespace std;

namespace RDP
{

TMEM::TMEM()
{
	memset(tmem, 0, sizeof(tmem));
	memset(tiles, 0, sizeof(tiles));

	for (auto &tile : tiles)
	{
		tile.size[0] = 1;
		tile.size[1] = 1;
	}
}

void TMEM::set_enable_tlut(bool state)
{
	enable_tlut = state;
	dirty_tiles |= (1 << TMEM_TILES) - 1;
}

void TMEM::set_tlut_type(bool type)
{
	tlut_type = type;
	dirty_tiles |= (1 << TMEM_TILES) - 1;
}

void TMEM::set_texture_image(uint32_t offset, unsigned format, unsigned pixel_size, unsigned width)
{
	texture.offset = offset;
	texture.format = format;
	texture.pixel_size = pixel_size;
	texture.width = width;
}

void TMEM::read_rgba32(uint8_t *buffer, unsigned word, unsigned x, unsigned y, unsigned line, unsigned)
{
	unsigned pix = (word + y * line) << 2;
	unsigned flip = y & 1 ? 2 : 0;
	pix += x;

	unsigned wrapped = (pix >> 2) & 0xff;
	unsigned bank = (pix & 3) ^ flip;

	uint32_t color = (tmem[wrapped + 0][bank] << 16) | (tmem[wrapped + 0x100][bank] << 0);

	*buffer++ = uint8_t(color >> 24);
	*buffer++ = uint8_t(color >> 16);
	*buffer++ = uint8_t(color >> 8);
	*buffer++ = uint8_t(color >> 0);
}

void TMEM::read_rgba16(uint8_t *buffer, unsigned word, unsigned x, unsigned y, unsigned line, unsigned)
{
	unsigned pix = (word + y * line) << 2;
	unsigned flip = y & 1 ? 2 : 0;
	pix += x;

	unsigned wrapped = (pix >> 2) & 0x1ff;
	unsigned bank = (pix & 3) ^ flip;

	uint32_t color = tmem[wrapped][bank];
	uint8_t r = (color >> 11) & 0x1f;
	uint8_t g = (color >> 6) & 0x1f;
	uint8_t b = (color >> 1) & 0x1f;
	uint8_t a = (color & 1) * 0xff;

	r = (r << 3) | (r >> 2);
	g = (g << 3) | (g >> 2);
	b = (b << 3) | (b >> 2);

	*buffer++ = r;
	*buffer++ = g;
	*buffer++ = b;
	*buffer++ = a;
}

void TMEM::read_ia16(uint8_t *buffer, unsigned word, unsigned x, unsigned y, unsigned line, unsigned)
{
	unsigned pix = (word + y * line) << 2;
	unsigned flip = y & 1 ? 2 : 0;
	pix += x;

	unsigned wrapped = (pix >> 2) & 0x1ff;
	unsigned bank = (pix & 3) ^ flip;

	uint32_t color = tmem[wrapped][bank];
	uint8_t i = (color >> 8) & 0xff;
	uint8_t a = (color >> 0) & 0xff;

	*buffer++ = i;
	*buffer++ = i;
	*buffer++ = i;
	*buffer++ = a;
}

void TMEM::read_ia8(uint8_t *buffer, unsigned word, unsigned x, unsigned y, unsigned line, unsigned)
{
	unsigned pix = (word + y * line) << 2;
	unsigned flip = y & 1 ? 2 : 0;
	pix += x >> 1;

	unsigned wrapped = (pix >> 2) & 0x1ff;
	unsigned bank = (pix & 3) ^ flip;
	unsigned shift = x & 1 ? 0 : 8;

	uint8_t color = (tmem[wrapped][bank] >> shift) & 0xff;
	uint8_t i = (color >> 4) & 0xf;
	uint8_t a = (color >> 0) & 0xf;
	i |= i << 4;
	a |= a << 4;

	*buffer++ = i;
	*buffer++ = i;
	*buffer++ = i;
	*buffer++ = a;
}

void TMEM::read_i16(uint8_t *buffer, unsigned word, unsigned x, unsigned y, unsigned line, unsigned)
{
	unsigned pix = (word + y * line) << 2;
	unsigned flip = y & 1 ? 2 : 0;
	pix += x;

	unsigned wrapped = (pix >> 2) & 0x1ff;
	unsigned bank = (pix & 3) ^ flip;

	uint16_t color = tmem[wrapped][bank];
	*buffer++ = color >> 8;
	*buffer++ = color & 0xff;
	*buffer++ = color >> 8;
	*buffer++ = (color & 1) ? 0xff : 0;
}

void TMEM::read_i8(uint8_t *buffer, unsigned word, unsigned x, unsigned y, unsigned line, unsigned)
{
	unsigned pix = (word + y * line) << 2;
	unsigned flip = y & 1 ? 2 : 0;
	pix += x >> 1;

	unsigned wrapped = (pix >> 2) & 0x1ff;
	unsigned bank = (pix & 3) ^ flip;
	unsigned shift = x & 1 ? 0 : 8;

	uint8_t color = (tmem[wrapped][bank] >> shift) & 0xff;
	*buffer++ = color;
	*buffer++ = color;
	*buffer++ = color;
	*buffer++ = color;
}

// No idea if this is correct.
void TMEM::read_ci16(uint8_t *buffer, unsigned word, unsigned x, unsigned y, unsigned line, unsigned)
{
	unsigned pix = (word + y * line) << 2;
	unsigned flip = y & 1 ? 2 : 0;
	pix += x;

	unsigned wrapped = (pix >> 2) & 0xff;
	unsigned bank = (pix & 3) ^ flip;
	uint16_t index = (tmem[wrapped][bank]) & 0xff;

	if (enable_tlut)
	{
		// We're just looking at bank 0 here, this should depend on where we sample in a quad I think ...
		// Seems ugly and unlikely to actually happen in practice, but hey.
		uint32_t color = tmem[index + 0x100][0];

		// Just assert quickly that this won't be a problem ...
		assert(tmem[index + 0x100][0] == tmem[index + 0x100][1]);
		assert(tmem[index + 0x100][2] == tmem[index + 0x100][3]);
		assert(tmem[index + 0x100][0] == tmem[index + 0x100][3]);

		uint8_t r, g, b, a;

		if (!tlut_type)
		{
			r = (color >> 11) & 0x1f;
			g = (color >> 6) & 0x1f;
			b = (color >> 1) & 0x1f;
			a = (color & 1) * 0xff;

			r = (r << 3) | (r >> 2);
			g = (g << 3) | (g >> 2);
			b = (b << 3) | (b >> 2);
		}
		else
		{
			// IA16
			uint8_t i = (color >> 8) & 0xff;
			r = g = b = i;
			a = (color >> 0) & 0xff;
		}

		*buffer++ = r;
		*buffer++ = g;
		*buffer++ = b;
		*buffer++ = a;
	}
	else
	{
		*buffer++ = index;
		*buffer++ = index;
		*buffer++ = index;
		*buffer++ = index;
	}
}

void TMEM::read_ci8(uint8_t *buffer, unsigned word, unsigned x, unsigned y, unsigned line, unsigned)
{
	unsigned pix = (word + y * line) << 2;
	unsigned flip = y & 1 ? 2 : 0;
	pix += x >> 1;

	unsigned wrapped = (pix >> 2) & 0xff;
	unsigned bank = (pix & 3) ^ flip;
	unsigned shift = x & 1 ? 0 : 8;

	uint8_t index = (tmem[wrapped][bank] >> shift) & 0xff;

	if (enable_tlut)
	{
		// We're just looking at bank 0 here, this should depend on where we sample in a quad I think ...
		// Seems ugly and unlikely to actually happen in practice, but hey.
		uint32_t color = tmem[index + 0x100][0];

		// Just assert quickly that this won't be a problem ...
		assert(tmem[index + 0x100][0] == tmem[index + 0x100][1]);
		assert(tmem[index + 0x100][2] == tmem[index + 0x100][3]);
		assert(tmem[index + 0x100][0] == tmem[index + 0x100][3]);

		uint8_t r, g, b, a;

		if (!tlut_type)
		{
			r = (color >> 11) & 0x1f;
			g = (color >> 6) & 0x1f;
			b = (color >> 1) & 0x1f;
			a = (color & 1) * 0xff;

			r = (r << 3) | (r >> 2);
			g = (g << 3) | (g >> 2);
			b = (b << 3) | (b >> 2);
		}
		else
		{
			// IA16
			uint8_t i = (color >> 8) & 0xff;
			r = g = b = i;
			a = (color >> 0) & 0xff;
		}

		*buffer++ = r;
		*buffer++ = g;
		*buffer++ = b;
		*buffer++ = a;
	}
	else
	{
		*buffer++ = index;
		*buffer++ = index;
		*buffer++ = index;
		*buffer++ = index;
	}
}

void TMEM::read_ia4(uint8_t *buffer, unsigned word, unsigned x, unsigned y, unsigned line, unsigned)
{
	unsigned pix = (word + y * line) << 2;
	unsigned flip = y & 1 ? 2 : 0;
	pix += x >> 2;

	unsigned wrapped = (pix >> 2) & 0x1ff;
	unsigned bank = (pix & 3) ^ flip;
	unsigned shift = 12 - (x & 3) * 4;

	uint8_t color = (tmem[wrapped][bank] >> shift) & 0xf;
	uint8_t i = (color >> 1) & 0x7;
	i = (i << 5) | (i << 2) | (i >> 1);
	uint8_t a = (color & 1) * 0xff;

	*buffer++ = i;
	*buffer++ = i;
	*buffer++ = i;
	*buffer++ = a;
}

void TMEM::read_i4(uint8_t *buffer, unsigned word, unsigned x, unsigned y, unsigned line, unsigned)
{
	unsigned pix = (word + y * line) << 2;
	unsigned flip = y & 1 ? 2 : 0;
	pix += x >> 2;

	unsigned wrapped = (pix >> 2) & 0x1ff;
	unsigned bank = (pix & 3) ^ flip;
	unsigned shift = 12 - (x & 3) * 4;

	uint8_t color = (tmem[wrapped][bank] >> shift) & 0xf;
	color |= color << 4;
	*buffer++ = color;
	*buffer++ = color;
	*buffer++ = color;
	*buffer++ = color;
}

void TMEM::read_ci4(uint8_t *buffer, unsigned word, unsigned x, unsigned y, unsigned line, unsigned palette)
{
	unsigned pix = (word + y * line) << 2;
	unsigned flip = y & 1 ? 2 : 0;
	pix += x >> 2;

	unsigned wrapped = (pix >> 2) & 0xff;
	unsigned bank = (pix & 3) ^ flip;
	unsigned shift = 12 - (x & 3) * 4;

	uint8_t index = (tmem[wrapped][bank] >> shift) & 0xf;
	index |= palette << 4;

	if (enable_tlut)
	{
		// We're just looking at bank 0 here, this should depend on where we sample in a quad I think ...
		// Seems ugly and unlikely to actually happen in practice, but hey.
		uint16_t color = tmem[index + 0x100][0];

		// Just assert quickly that this won't be a problem ...
		assert(tmem[index + 0x100][0] == tmem[index + 0x100][1]);
		assert(tmem[index + 0x100][2] == tmem[index + 0x100][3]);
		assert(tmem[index + 0x100][0] == tmem[index + 0x100][3]);

		uint8_t r, g, b, a;

		if (!tlut_type)
		{
			r = (color >> 11) & 0x1f;
			g = (color >> 6) & 0x1f;
			b = (color >> 1) & 0x1f;
			a = (color & 1) * 0xff;

			r = (r << 3) | (r >> 2);
			g = (g << 3) | (g >> 2);
			b = (b << 3) | (b >> 2);
		}
		else
		{
			// IA16
			uint8_t i = (color >> 8) & 0xff;
			r = g = b = i;
			a = (color >> 0) & 0xff;
		}

		*buffer++ = r;
		*buffer++ = g;
		*buffer++ = b;
		*buffer++ = a;
	}
	else
	{
		*buffer++ = index;
		*buffer++ = index;
		*buffer++ = index;
		*buffer++ = index;
	}
}

bool TMEM::get_framebuffer_transfer(unsigned index, TransferInfo *info) const
{
	auto &tile = tiles[index];

	if (enable_tlut || !fbe.tmem_is_framebuffer)
		return false;

	// Reinterpretation and repacking to memory format before decoding is a PITA.
	// Only bother with this if really needed.
	if (tile.pixel_size != fbe.hardware_framebuffer.pixel_size)
		return false;

	// This is solvable, but also a PITA in edge conditions.
	if (tile.tmem != fbe.tmem_base)
		return false;

	auto &fb = fbe.hardware_framebuffer;

	// Only support a subset of possible formats.
	// Add more formats as needed.
	if ((tile.format == FORMAT_I || tile.format == FORMAT_RGBA) && tile.pixel_size == PIXEL_SIZE_8BPP)
	{
		info->buffer = fbe.color_buffer;
		info->offset_pixels = fbe.offset_pixels;
		info->range_pixels = (fb.allocated_width * fb.allocated_height - info->offset_pixels);
		// Having stride_pixels == allocated_width is the only sensible way for games to use
		// HW FBE.
		info->stride_pixels = fb.allocated_width;
		info->transfer = TRANSFER_I8;
		return true;
	}
	else if (tile.format == FORMAT_IA && tile.pixel_size == PIXEL_SIZE_8BPP)
	{
		info->buffer = fbe.color_buffer;
		info->offset_pixels = fbe.offset_pixels;
		info->range_pixels = (fb.allocated_width * fb.allocated_height - info->offset_pixels);
		info->stride_pixels = fb.allocated_width;

		// Gross hack. If we rendered to I8, the only reason games want IA8 would be that they need
		// to blur the textures on the CPU and do format conversion for bizarro reasons (JFG).
		// It works if we actually read back to CPU, so this should be considered a speed hack if anything.
		info->transfer = TRANSFER_I8;
		//info->transfer = TRANSFER_IA8;
		return true;
	}
	else if (tile.format == FORMAT_RGBA && tile.pixel_size == PIXEL_SIZE_16BPP)
	{
		info->buffer = fbe.color_buffer;
		info->offset_pixels = fbe.offset_pixels;
		info->range_pixels = (fb.allocated_width * fb.allocated_height - info->offset_pixels);
		info->stride_pixels = fb.allocated_width;
		info->transfer = TRANSFER_RGBA16;
		return true;
	}
	else if (tile.format == FORMAT_IA && tile.pixel_size == PIXEL_SIZE_16BPP)
	{
		info->buffer = fbe.color_buffer;
		info->offset_pixels = fbe.offset_pixels;
		info->range_pixels = (fb.allocated_width * fb.allocated_height - info->offset_pixels);
		info->stride_pixels = fb.allocated_width;
		info->transfer = TRANSFER_IA16;
		return true;
	}
	else if (tile.format == FORMAT_RGBA && tile.pixel_size == PIXEL_SIZE_32BPP)
	{
		info->buffer = fbe.color_buffer;
		info->offset_pixels = fbe.offset_pixels;
		info->range_pixels = (fb.allocated_width * fb.allocated_height - info->offset_pixels);
		info->stride_pixels = fb.allocated_width;
		info->transfer = TRANSFER_RGBA32;
		return true;
	}
	else
		return false;
}

void TMEM::decode_tile(unsigned index, uint8_t *buffer, size_t stride)
{
	auto &tile = tiles[index];
	unsigned width = tile.size[0];
	unsigned height = tile.size[1];

//fprintf(stderr, "Decoding tile #%u, width: %u, height: %u, format: %u, pixel_size: %u, TMEM: %u\n", index, width,
//        height, tile.format, tile.pixel_size, tile.tmem);

//if (tile.format != FORMAT_CI && enable_tlut)
//	assert(0 && "Using TLUT without CI format.");

	if (tile.format == FORMAT_RGBA && tile.pixel_size == PIXEL_SIZE_32BPP)
	{
      unsigned y, x;
      for (y = 0; y < height; y++, buffer += stride)
         for (x = 0; x < width; x++)
            read_rgba32(buffer + 4 * x, tile.tmem, x, y, tile.line, tile.palette);
	}
	else if (tile.format == FORMAT_RGBA && tile.pixel_size == PIXEL_SIZE_16BPP)
	{
      unsigned y, x;
      for (y = 0; y < height; y++, buffer += stride)
         for (x = 0; x < width; x++)
            read_rgba16(buffer + 4 * x, tile.tmem, x, y, tile.line, tile.palette);
	}
	else if (tile.format == FORMAT_IA && tile.pixel_size == PIXEL_SIZE_16BPP)
	{
      unsigned y, x;
      for (y = 0; y < height; y++, buffer += stride)
         for (x = 0; x < width; x++)
            read_ia16(buffer + 4 * x, tile.tmem, x, y, tile.line, tile.palette);
	}
	else if (tile.format == FORMAT_IA && tile.pixel_size == PIXEL_SIZE_8BPP)
	{
      unsigned y, x;
      for (y = 0; y < height; y++, buffer += stride)
         for (x = 0; x < width; x++)
            read_ia8(buffer + 4 * x, tile.tmem, x, y, tile.line, tile.palette);
	}
	else if (tile.format == FORMAT_IA && tile.pixel_size == PIXEL_SIZE_4BPP)
	{
      unsigned y, x;
      for (y = 0; y < height; y++, buffer += stride)
         for (x = 0; x < width; x++)
            read_ia4(buffer + 4 * x, tile.tmem, x, y, tile.line, tile.palette);
	}
	else if (tile.format == FORMAT_I && (tile.pixel_size == PIXEL_SIZE_16BPP || tile.pixel_size == PIXEL_SIZE_32BPP))
	{
      unsigned y, x;
      for (y = 0; y < height; y++, buffer += stride)
         for (x = 0; x < width; x++)
            read_i16(buffer + 4 * x, tile.tmem, x, y, tile.line, tile.palette);
	}
	else if ((tile.format == FORMAT_I || tile.format == FORMAT_RGBA) && tile.pixel_size == PIXEL_SIZE_8BPP)
	{
      unsigned y, x;
      for (y = 0; y < height; y++, buffer += stride)
         for (x = 0; x < width; x++)
            read_i8(buffer + 4 * x, tile.tmem, x, y, tile.line, tile.palette);
	}
	else if ((tile.format == FORMAT_I || tile.format == FORMAT_RGBA) && tile.pixel_size == PIXEL_SIZE_4BPP)
	{
      unsigned y, x;
      for (y = 0; y < height; y++, buffer += stride)
         for (x = 0; x < width; x++)
            read_i4(buffer + 4 * x, tile.tmem, x, y, tile.line, tile.palette);
	}
	else if (tile.format == FORMAT_CI && tile.pixel_size == PIXEL_SIZE_16BPP)
	{
      unsigned y, x;
      for (y = 0; y < height; y++, buffer += stride)
         for (x = 0; x < width; x++)
            read_ci16(buffer + 4 * x, tile.tmem, x, y, tile.line, tile.palette);
	}
	else if (tile.format == FORMAT_CI && tile.pixel_size == PIXEL_SIZE_8BPP)
	{
      unsigned y, x;
      for (y = 0; y < height; y++, buffer += stride)
         for (x = 0; x < width; x++)
            read_ci8(buffer + 4 * x, tile.tmem, x, y, tile.line, tile.palette);
	}
	else if (tile.format == FORMAT_CI && tile.pixel_size == PIXEL_SIZE_4BPP)
	{
      unsigned y, x;
      for (y = 0; y < height; y++, buffer += stride)
         for (x = 0; x < width; x++)
            read_ci4(buffer + 4 * x, tile.tmem, x, y, tile.line, tile.palette);
	}
	else
		assert(0 && "Unsupported pixel format!");
}

void TMEM::set_tile_size(uint32_t w1, uint32_t w2)
{
	uint32_t sl = (w1 & 0x00fff000) >> (44 - 32);
	uint32_t tl = (w1 & 0x00000fff) >> (32 - 32);
	uint32_t tile = (w2 & 0x07000000) >> (24 - 0);
	uint32_t sh = (w2 & 0x00fff000) >> (12 - 0);
	uint32_t th = (w2 & 0x00000fff) >> (0 - 0);

	//fprintf(stderr, "Set Tile Size #%u, SL %d, TL %d, SH %d, TH %d\n", tile, sl, tl, sh, th);

	tiles[tile].sl = sl;
	tiles[tile].tl = tl;
	tiles[tile].sh = sh;
	tiles[tile].th = th;

	update_tile_info(tiles[tile]);
	dirty_tiles |= 1 << tile;
}

void TMEM::load_tlut(uint32_t w1, uint32_t w2)
{
	unsigned index = (w2 >> 24) & 7;
	auto &tile = tiles[index];

	uint32_t sl = (w1 >> 12) & 0xfff;
	uint32_t tl = (w1 >> 0) & 0xfff;
	uint32_t sh = (w2 >> 12) & 0xfff;
	uint32_t th = (w2 >> 0) & 0xfff;

	// No idea how this works.
	assert(tl == th);

	// Do we offset based on tmem or use sl/sh directly?
	assert(tile.tmem & 0x100);

	// Who knows :D
	sl >>= 2;
	sh >>= 2;
	assert(sh >= sl);

	unsigned length = ((sh - sl) & 0xff) + 1;
	auto tlut = &tmem[0x100];

	uint32_t addr = texture.offset + (sl + (tl >> 2)) * texture.pixel_size;
	//fprintf(stderr, "TLUT, TMEM: %u, ADDR: %u, LENGTH: %u, SL: %u\n", tile.tmem, addr, length, sl);

	for (unsigned i = 0; i < length; i++)
	{
		uint16_t col = READ_DRAM_U16(rdram.base, addr + i * 2);
		unsigned t = (tile.tmem + i) & 0xff;

		// Palette data is splat across all banks since we need 4-way
		// parallel lookups when texture filtering.
		tlut[t][0] = col;
		tlut[t][1] = col;
		tlut[t][2] = col;
		tlut[t][3] = col;
	}

	dirty_tiles |= (1 << TMEM_TILES) - 1;
}

bool TMEM::load_tile_framebuffer(const Tile &tile, uint32_t sl, uint32_t tl)
{
	uint32_t load_addr = texture.offset;
	switch (tile.pixel_size)
	{
	case PIXEL_SIZE_32BPP:
		load_addr += sl * 4;
		load_addr += tl * 4 * texture.width;
		break;

	case PIXEL_SIZE_16BPP:
		load_addr += sl * 2;
		load_addr += tl * 2 * texture.width;
		break;

	case PIXEL_SIZE_8BPP:
		load_addr += sl * 1;
		load_addr += tl * 1 * texture.width;
		break;

	default:
		assert(0 && "Unsupported pixel format.");
		break;
	}

	bool ret = false;

	for (int i = async_framebuffers->size() - 1; i >= 0; i--)
	{
		auto &fb = (*async_framebuffers)[i];
		uint32_t base = fb.framebuffer.addr;
		uint32_t size = fb.framebuffer.color_size();

		// If the base address is found to be within one async framebuffer transfer,
		// assume that our TMEM is now occupied by HWFBE data.
		// There are a million ways this can break, but we just emulate what is needed
		// to get the forwarding working.
		//
		// Failure cases would be if games partially load TMEM and decode part CPU-side, part GPU-side
		// data. Other cases would be loading data in a weird block format way.
		//
		// Other cases would be if load_block is doing weird swizzling that doesn't match decoding.
		// All these cases are assumed to be non-sensical from a game perspective.
		uint32_t wrap = WRAP_ADDR(load_addr - base);
		bool within = wrap < size;

		if (within)
		{
			ret = true;
			fbe.hardware_framebuffer = fb.framebuffer;
			fbe.color_buffer = fb.color_buffer;
			fbe.offset_pixels = fb.framebuffer.bytes_to_pixels(wrap);
			fbe.tmem_base = tile.tmem;

#ifdef ENABLE_LOGS
			fprintf(stderr, "HW FBE detected!\n");
#endif
			break;
		}
	}

	fbe.tmem_is_framebuffer = ret;
	if (!ret)
	{
		fbe.hardware_framebuffer = {};
		fbe.color_buffer = {};
	}

	return ret;
}

void TMEM::load_block(uint32_t w1, uint32_t w2)
{
	unsigned index = (w2 >> 24) & 7;
	auto &tile = tiles[index];

	uint32_t sl = (w1 >> 12) & 0xfff;
	uint32_t tl = (w1 >> 0) & 0xfff;
	uint32_t sh = (w2 >> 12) & 0xfff;
	uint32_t dxt = (w2 >> 0) & 0xfff;

	if (!load_tile_framebuffer(tile, sl, tl))
	{
		uint32_t t = 0;

		unsigned pixels = sh - sl + 1;

		// Need to figure out how to do this.
		assert(sl == 0 && tl == 0);
		assert(tile.line == 0);

		//fprintf(stderr, "Load Block #%u, TMEM: %u, ADDR: %u, pixels: %u, format: %u, pixel size: %u\n", index, tile.tmem, texture.offset, pixels,
		//        tile.format, tile.pixel_size);

		if (tile.pixel_size == PIXEL_SIZE_32BPP)
		{
			unsigned tmem_addr = tile.tmem << 2;
			unsigned addr = texture.offset;

			addr += sl * 4;
			addr += tl * 4 * texture.width;

			for (unsigned x = 0; x < pixels; x++, tmem_addr++)
			{
				// Quantize accumulated 1.11 DxT fixed point down to an integer.
				// t is incremented every 64-bit word (4 pixels for 16-bit).
				uint32_t y = t >> 11;

				unsigned flip = y & 1 ? 2 : 0;

				// When t rolls over, we skip a certain number of words in TMEM.
				uint32_t skipwords = y * tile.line;
				uint32_t stride_tmem = tmem_addr + (skipwords << 2);

				uint32_t c0 = READ_DRAM_U32(rdram.base, addr);
				unsigned wrapped = (stride_tmem >> 2) & 0xff;
				unsigned bank = (stride_tmem & 3) ^ flip;
				tmem[wrapped + 0x0][bank] = c0 >> 16;
				tmem[wrapped + 0x100][bank] = c0 & 0xffff;

				// No idea why it's 2 * dxt here, but it fixes issues in games.
				// Maybe the internal counter works on number of words *read*
				// and not 64-bit banks written?
				if ((x & 3) == 3)
					t += 2 * dxt;
				addr += 4;
			}
		}
		else if (tile.pixel_size == PIXEL_SIZE_16BPP)
		{
			unsigned tmem_addr = tile.tmem << 2;
			unsigned addr = texture.offset;

			addr += sl * 2;
			addr += tl * 2 * texture.width;

			for (unsigned x = 0; x < pixels; x++, tmem_addr++)
			{
				// Quantize accumulated 1.11 DxT fixed point down to an integer.
				// t is incremented every 64-bit word (4 pixels for 16-bit).
				uint32_t y = t >> 11;

				unsigned flip = y & 1 ? 2 : 0;

				// When t rolls over, we skip a certain number of words in TMEM.
				uint32_t skipwords = y * tile.line;
				uint32_t stride_tmem = tmem_addr + (skipwords << 2);

				uint16_t c0 = READ_DRAM_U16(rdram.base, addr);
				unsigned wrapped = (stride_tmem >> 2) & 0x1ff;
				unsigned bank = (stride_tmem & 3) ^ flip;
				tmem[wrapped][bank] = c0;

				if ((x & 3) == 3)
					t += dxt;
				addr += 2;
			}
		}
		else if (tile.pixel_size == PIXEL_SIZE_8BPP)
		{
			unsigned tmem_addr = tile.tmem << 2;
			unsigned addr = texture.offset;

			addr += sl * 1;
			addr += tl * 1 * texture.width;

			assert((pixels & 1) == 0);
			assert((addr & 7) == 0);

			// The TMEM has 16 bits, I doubt it would support partial writes,
			// so just go two texels at a time like a bawz.
			for (unsigned x = 0; x < pixels; x += 2, tmem_addr++)
			{
				uint32_t y = t >> 11;

				unsigned flip = y & 1 ? 2 : 0;

				// When t rolls over, we skip a certain number of words in TMEM.
				uint32_t skipwords = y * tile.line;
				uint32_t stride_tmem = tmem_addr + (skipwords << 2);

				uint16_t c0 = READ_DRAM_U16(rdram.base, addr);
				unsigned wrapped = (stride_tmem >> 2) & 0x1ff;
				unsigned bank = (stride_tmem & 3) ^ flip;
				tmem[wrapped][bank] = c0;

				if ((x & 6) == 6)
					t += dxt;
				addr += 2;
			}
		}
		else
			assert(0 && "Unsupported pixel format!");
	}

	// Angrylion copies these values over to tile info.
	// A bit weird, but probably correct.
	tile.sl = sl;
	tile.tl = tl;
	tile.sh = sh;
	tile.th = dxt;

	update_tile_info(tile);
	// Can potentially overwrite every tile due to aliasing.
	// Should probably track ranges, but whatever.
	dirty_tiles |= (1 << TMEM_TILES) - 1;
}

void TMEM::load_tile(uint32_t w1, uint32_t w2)
{
	unsigned index = (w2 >> 24) & 7;
	auto &tile = tiles[index];

	tile.sl = (w1 >> 12) & 0xfff;
	tile.tl = (w1 >> 0) & 0xfff;
	tile.sh = (w2 >> 12) & 0xfff;
	tile.th = (w2 >> 0) & 0xfff;

	if (!load_tile_framebuffer(tile, tile.sl, tile.tl))
	{
#if 0
		assert((tile.sl & 3) == 0);
		assert((tile.tl & 3) == 0);
		assert((tile.sh & 3) == 0);
		assert((tile.th & 3) == 0);
#endif

		unsigned width = (tile.sh >> 2) - (tile.sl >> 2) + 1;
		unsigned height = (tile.th >> 2) - (tile.tl >> 2) + 1;

		//fprintf(stderr, "Load Tile #%u, TMEM: %u, ADDR: %u, pixel size: %u, width %u, height %u, SL %u, TL %u, SH %u, TH %u\n", index, tile.tmem, texture.offset, tile.pixel_size,
		//        width, height, tile.sl, tile.tl, tile.sh, tile.th);

		// Just winging it for now, there's probably a million edge cases.

		// No idea how to deal with this case yet.
		//assert(texture.pixel_size == tile.pixel_size && texture.format == tile.format);

		// RGBA32
		// 32-bit formats are a bit weird.
		if (tile.pixel_size == PIXEL_SIZE_32BPP)
		{
			unsigned word = tile.tmem;
			unsigned addr = texture.offset;

			addr += (tile.sl >> 2) * 4;
			addr += (tile.tl >> 2) * 4 * texture.width;

			for (unsigned y = 0; y < height; y++, word += tile.line, addr += texture.width * 4)
			{
				unsigned step_tmem = word << 2;

				// The TMEM is 4-way (8 due to LMEM/HMEM?) 16-bit banked to avoid bank conflicts
				// when reading a quad footprint. To guarantee this, the
				// bank indices flip around every other scanline.
				// 0, 1, 2, 3, (T = 0)
				// 2, 3, 0, 1, (T = 1)
				// 0, 1, 2, 3, (T = 2)
				// 2, 3, 0, 1, (T = 3)
				unsigned flip = y & 1 ? 2 : 0;

				// Some SIMD shuffling magic could do wonders here :D

				for (unsigned x = 0; x < width; x++, step_tmem++)
				{
					uint32_t c0 = READ_DRAM_U32(rdram.base, addr + 4 * x);

					// RGBA32 is split up where RG are stored in LMEM
					// and the corresponding BA are stored in HMEM.
					unsigned wrapped = (step_tmem >> 2) & 0xff;
					unsigned bank = (step_tmem & 3) ^ flip;
					tmem[wrapped + 0][bank] = c0 >> 16;
					tmem[wrapped + 0x100][bank] = c0 & 0xffff;
				}
			}
		}
		else if (tile.pixel_size == PIXEL_SIZE_16BPP)
		{
			// RGBA16 and IA16
			unsigned word = tile.tmem;
			unsigned addr = texture.offset;

			addr += (tile.sl >> 2) * 2;
			addr += (tile.tl >> 2) * 2 * texture.width;

			for (unsigned y = 0; y < height; y++, word += tile.line, addr += texture.width * 2)
			{
				unsigned step_tmem = word << 2;
				unsigned flip = y & 1 ? 2 : 0;

				for (unsigned x = 0; x < width; x++, step_tmem++)
				{
					uint16_t c0 = READ_DRAM_U16(rdram.base, addr + 2 * x);
					unsigned wrapped = (step_tmem >> 2) & 0x1ff;
					unsigned bank = (step_tmem & 3) ^ flip;
					tmem[wrapped][bank] = c0;
				}
			}
		}
		else if (tile.pixel_size == PIXEL_SIZE_8BPP)
		{
			unsigned word = tile.tmem;
			unsigned addr = texture.offset;

			addr += (tile.sl >> 2) * 1;
			addr += (tile.tl >> 2) * 1 * texture.width;

			// Are there some alignment requirements here internally?
			for (unsigned y = 0; y < height; y++, word += tile.line, addr += texture.width * 1)
			{
				unsigned step_tmem = word << 2;
				unsigned flip = y & 1 ? 2 : 0;

				// The TMEM has 16 bits, I doubt it would support partial writes,
				// so just go two texels at a time like a bawz.
				for (unsigned x = 0; x < width; x += 2, step_tmem++)
				{
					unsigned wrapped = (step_tmem >> 2) & 0x1ff;
					unsigned bank = (step_tmem & 3) ^ flip;

					if (x + 1 < width)
					{
						// Need to read unaligned here since texture.width can be odd (Mario Kart 64).
						uint16_t c0 =
						    (READ_DRAM_U8(rdram.base, addr + x) << 8) | READ_DRAM_U8(rdram.base, addr + x + 1);
						tmem[wrapped][bank] = c0;
					}
					else
					{
						// Partial write, not sure if we need to be this careful, but can't hurt.
						uint16_t c0 = READ_DRAM_U8(rdram.base, addr + x) << 8;
						tmem[wrapped][bank] &= 0xff;
						tmem[wrapped][bank] |= c0;
					}
				}
			}
		}
		else
			assert(0 && "Unsupported pixel format!");
	}

	update_tile_info(tile);
	// Can potentially overwrite every tile due to aliasing.
	// Should probably track ranges, but whatever.
	dirty_tiles |= (1 << TMEM_TILES) - 1;
}

void TMEM::build_tile_descriptor(unsigned index, TileDescriptor *desc) const
{
	auto &tile = tiles[index];

	// Do everything in 10.5 fixed point space.
	desc->base[0] = tile.sl << 3;
	desc->base[1] = tile.tl << 3;

	uint32_t size_mask_x = next_pow2(tile.size[0]) - 1;
	uint32_t size_mask_y = next_pow2(tile.size[1]) - 1;

	desc->mask_mirror_and[0] = min(tile.mask_and[0], size_mask_x);
	desc->mask_mirror_and[1] = min(tile.mask_and[1], size_mask_y);
	desc->mask_mirror_and[2] = tile.mirror_and[0];
	desc->mask_mirror_and[3] = tile.mirror_and[1];

	memcpy(desc->clamped, tile.clamp, sizeof(tile.clamp));
	memcpy(desc->shift, tile.shift, sizeof(tile.shift));

	desc->mid_texel = 0;
}

void TMEM::update_tile_info(Tile &tile)
{
	// Mask and mirror in 10.5 fixed point.
	if (tile.mask[0])
		tile.mask_and[0] = (1 << tile.mask[0]) - 1;
	else
		tile.mask_and[0] = 0xffffffffu;

	if (tile.mask[1])
		tile.mask_and[1] = (1 << tile.mask[1]) - 1;
	else
		tile.mask_and[1] = 0xffffffffu;

	if (tile.mirror_en[0])
		tile.mirror_and[0] = 1 << tile.mask[0];
	else
		tile.mirror_and[0] = 0;

	if (tile.mirror_en[1])
		tile.mirror_and[1] = 1 << tile.mask[1];
	else
		tile.mirror_and[1] = 0;

	// Compute effective tile sizes, i.e. the amount of texels
	// we will need to upload to handle all cases.

	const uint32_t clamped_size[2] = {
		(((tile.sh - tile.sl + 3) >> 2) & 0x3ffu) + 1u, (((tile.th - tile.tl + 3) >> 2) & 0x3ffu) + 1u,
	};

	for (unsigned i = 0; i < 2; i++)
	{
		if (tile.mask[i])
		{
			tile.size[i] = 1 << tile.mask[i];

			if (tile.clamp_en[i])
			{
				// Clamp in 10.5 format.
				tile.clamp[i + 0] = 0;
				tile.clamp[i + 2] = (clamped_size[i] - 1) << 5;
				tile.size[i] = min(tile.size[i], clamped_size[i]);
			}
			else
			{
				// Disable clamping.
				tile.clamp[i + 0] = -0x80000000;
				tile.clamp[i + 2] = 0x7fffffff;
			}
		}
		else
		{
			// If we're not masking, we always clamp.
			// Clamp in 10.5 format.
			tile.clamp[i + 0] = 0;
			tile.clamp[i + 2] = (clamped_size[i] - 1) << 5;

			tile.size[i] = clamped_size[i];
		}
	}

	// Dirty hack. Some microcode seems to like 1k x 1k tiles for some reason ...
	// Visible in certain places in OoT among other places.
	// Technically it's fine for games to do this since we are supposed to get wrapping behavior in TMEM,
	// but when decoding the tile, we have to unwrap this behavior, which becomes unusable.
	unsigned bits_per_pixel = 4 << tile.pixel_size;
	unsigned size = (tile.size[0] * tile.size[1] * bits_per_pixel) >> 3;
	if (size > 4 * 1024) // Detect the difficult case where we need to decode more than 4KiB worth of texel data.
	{
		unsigned horiz_pixels = 0;
		unsigned max_texels = 0;

		switch (tile.pixel_size)
		{
		case PIXEL_SIZE_4BPP:
			horiz_pixels = tile.line * 16;
			max_texels = 8 * 1024;
			break;

		case PIXEL_SIZE_8BPP:
			horiz_pixels = tile.line * 8;
			max_texels = 4 * 1024;
			break;

		case PIXEL_SIZE_16BPP:
			horiz_pixels = tile.line * 4;
			max_texels = 2 * 1024;
			break;

		case PIXEL_SIZE_32BPP:
			horiz_pixels = tile.line * 4;
			max_texels = 1024;
			break;
		}

		// We have a small line parameter, so assume that this is the true width ...
		if (horiz_pixels < tile.size[0])
		{
			tile.size[0] = horiz_pixels;

			// Recompute the size.
			size = (tile.size[0] * tile.size[1] * bits_per_pixel) >> 3;
		}

		// If it's still bad, chop off the height so it can fit inside TMEM, rounded up.
		if (size > 4 * 1024)
			tile.size[1] = (max_texels + tile.size[0] - 1) / tile.size[0];
	}
}

void TMEM::set_tile(uint32_t w1, uint32_t w2)
{
	unsigned index = (w2 & 0x07000000) >> 24;
	auto &tile = tiles[index];

	tile.format = (w1 & 0x00e00000) >> (53 - 32);
	tile.pixel_size = (w1 & 0x00180000) >> (51 - 32);
	tile.line = (w1 & 0x0003fe00) >> (41 - 32);
	tile.tmem = (w1 & 0x000001ff) >> (32 - 32);
	tile.palette = (w2 & 0x00f00000) >> (20 - 0);

	tile.clamp_en[1] = (w2 & 0x00080000) >> (19 - 0);
	tile.mirror_en[1] = (w2 & 0x00040000) >> (18 - 0);
	tile.mask[1] = (w2 & 0x0003c000) >> (14 - 0);
	tile.shift[1] = (w2 & 0x00003c00) >> (10 - 0);

	tile.clamp_en[0] = (w2 & 0x00000200) >> (9 - 0);
	tile.mirror_en[0] = (w2 & 0x00000100) >> (8 - 0);
	tile.mask[0] = (w2 & 0x000000f0) >> (4 - 0);
	tile.shift[0] = (w2 & 0x0000000f) >> (0 - 0);

	tile.mask[0] = min(tile.mask[0], 10u);
	tile.mask[1] = min(tile.mask[1], 10u);

	//fprintf(stderr, "Set Tile #%u, TMEM: %u, format: %u, pixel_size: %u\n", index, tile.tmem, tile.format,
	//        tile.pixel_size);

	if (!tile.mask[0])
		tile.mirror_en[0] = false;
	if (!tile.mask[1])
		tile.mirror_en[1] = false;

	update_tile_info(tile);
	dirty_tiles |= 1 << index;
}

void TileAtlasAllocator::reset()
{
	max_width = 0;
	max_height = 0;
}

void TileAtlasAllocator::add_size(unsigned width, unsigned height)
{
	max_width = max(width, max_width);
	max_height = max(height, max_height);
}

void TileAtlasAllocator::begin_allocator()
{
	current_x = 0;
	current_y = 0;
	current_layer = 0;
	max_height_in_stripe = 0;
}

void TileAtlasAllocator::end_allocator()
{
	num_layers = current_layer + 1;
}

void TileAtlasAllocator::allocate(unsigned *x, unsigned *y, unsigned *layer, unsigned width, unsigned height)
{
	// Allocate in X.
	if (current_x + width <= max_width && current_y + height <= max_height)
	{
		*x = current_x;
		*y = current_y;
		*layer = current_layer;
		current_x += width;

		max_height_in_stripe = max(max_height_in_stripe, height);
		return;
	}

	// End stripe, allocate in Y.
	current_y += max_height_in_stripe;
	current_x = 0;
	max_height_in_stripe = 0;

	if (current_x + width <= max_width && current_y + height <= max_height)
	{
		*x = current_x;
		*y = current_y;
		*layer = current_layer;
		current_x += width;

		max_height_in_stripe = max(max_height_in_stripe, height);
		return;
	}

	// If all fails, Allocate in Z.
	current_layer++;
	current_x = 0;
	current_y = 0;
	max_height_in_stripe = 0;

	*x = current_x;
	*y = current_y;
	*layer = current_layer;
	current_x += width;

	max_height_in_stripe = max(max_height_in_stripe, height);
}
}
