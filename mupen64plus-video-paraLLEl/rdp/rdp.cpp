#include "rdp.hpp"
#include "common.hpp"
#include <algorithm>
#include <assert.h>
#include <string.h>

#if 0
#define TMEM_DEBUG
#define ENABLE_LOGS
#endif

#ifdef TMEM_DEBUG
#include "stb/stb_image_write.h"
#include <functional>
#include <unordered_set>
#endif

#ifndef SE
#define SE(i, b)        ((i) | -SB((i), (b)))
#endif

#if (~0 >> 1 < 0)
#ifndef SRA
#define SRA(exp, sa)    ((signed)(exp) >> (sa))
#endif
#else
#ifndef SRA
#define SRA(exp, sa)    (SE((exp) >> (sa), (sa) ^ 31))
#endif
#endif

#ifndef SIGN
#define SIGN(i, b)      SRA((i) << (32 - (b)), (32 - (b)))
#endif

using namespace std;
using namespace Vulkan;

extern "C" unsigned setting_get_dithering();

namespace RDP
{

Renderer::Renderer(Vulkan::Device &device)
    : device(device)
{
   rdp_dithering = setting_get_dithering();

   reset_buffers();
   init_dither_lut();
   init_centroid_lut();
   init_z_lut();


   tmem.set_async_framebuffers(&async_transfers);
}

static uint16_t decompress_from_byte(uint8_t x)
{
   uint16_t y = (x & 1) | ((x & 2) << 4) | (x & 4) | ((x & 8) << 4) | ((x & 0x10) << 4) | ((x & 0x20) << 8) |
	             ((x & 0x40) << 4) | ((x & 0x80) << 8);
   return y;
}

void Renderer::set_dithering(unsigned type)
{
   rdp_dithering = type;
}

void Renderer::check_tmem_feedback()
{
   uint32_t addr = tmem.get_texture_image_offset();

   if (framebuffer.color_state != FRAMEBUFFER_CPU)
   {
	   uint32_t wrap = WRAP_ADDR(addr - framebuffer.addr);
	   bool within = wrap < framebuffer.color_size();
	   if (within)
	   {
#ifdef ENABLE_LOGS
		   fprintf(stderr, "TMEM feedback detected.\n");
#endif
		   complete_frame();
		   return;
	   }
   }

   if (framebuffer.depth_state != FRAMEBUFFER_CPU)
   {
	   uint32_t wrap = WRAP_ADDR(addr - framebuffer.depth_addr);
	   bool within = wrap < framebuffer.depth_size();
	   if (within)
	   {
#ifdef ENABLE_LOGS
		   fprintf(stderr, "TMEM feedback detected.\n");
#endif
		   complete_frame();
		   return;
	   }
   }
}

void Renderer::init_z_lut()
{
	vulkan.z_lut = device.request_buffer(BufferType::Dynamic, 0x88 * 16);
	auto *lut = static_cast<uint32_t *>(vulkan.z_lut.map());

	for (unsigned high = 0; high < 0x80; high++)
	{
		uint32_t shift, exp;

		if (high < 0x40)
		{
			shift = 6;
			exp = 0;
		}
		else if (high < 0x60)
		{
			shift = 5;
			exp = 1;
		}
		else if (high < 0x70)
		{
			shift = 4;
			exp = 2;
		}
		else if (high < 0x78)
		{
			shift = 3;
			exp = 3;
		}
		else if (high < 0x7c)
		{
			shift = 2;
			exp = 4;
		}
		else if (high < 0x7e)
		{
			shift = 1;
			exp = 5;
		}
		else if (high == 0x7e)
		{
			shift = 0;
			exp = 6;
		}
		else
		{
			shift = 0;
			exp = 7;
		}

		lut[4 * high + 0] = shift;
		lut[4 * high + 1] = exp;
	}

	lut[4 * (0x80 + 0) + 0] = 6;
	lut[4 * (0x80 + 0) + 1] = 0x00000;
	lut[4 * (0x80 + 1) + 0] = 5;
	lut[4 * (0x80 + 1) + 1] = 0x20000;
	lut[4 * (0x80 + 2) + 0] = 4;
	lut[4 * (0x80 + 2) + 1] = 0x30000;
	lut[4 * (0x80 + 3) + 0] = 3;
	lut[4 * (0x80 + 3) + 1] = 0x38000;
	lut[4 * (0x80 + 4) + 0] = 2;
	lut[4 * (0x80 + 4) + 1] = 0x3c000;
	lut[4 * (0x80 + 5) + 0] = 1;
	lut[4 * (0x80 + 5) + 1] = 0x3e000;
	lut[4 * (0x80 + 6) + 0] = 0;
	lut[4 * (0x80 + 6) + 1] = 0x3f000;
	lut[4 * (0x80 + 7) + 0] = 0;
	lut[4 * (0x80 + 7) + 1] = 0x3f800;

	vulkan.z_lut.unmap();

	CommandBuffer cmd = device.request_command_buffer();
	cmd.begin_stream();
	cmd.sync_buffer_to_gpu(vulkan.z_lut);
	cmd.end_stream();
	device.submit(cmd);
}

void Renderer::init_centroid_lut()
{
   unsigned i;
   /* Use texture here instead of UBO or LUT inside shader since we can get format expansion for free and have tight packing. */
   /* Could use texture buffer, but it's not that well supported, and we don't need the large buffers. */
   vulkan.centroid_lut = device.create_image_2d(VK_FORMAT_R8G8_UINT, 256, 1);
   Buffer staging = device.request_buffer(BufferType::Staging, 0x200);
   auto *offsets = static_cast<uint8_t *>(staging.map());

   for (i = 0; i < 0x100; i++)
   {
      unsigned k;
      uint16_t mask = decompress_from_byte(i);
      uint16_t mask_y = 0;
      uint16_t mask_x = 0;
      uint8_t off_x = 0;
      uint8_t off_y = 0;

      static const uint8_t xarray[16] = { 0, 3, 2, 2, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0 };
      static const uint8_t yarray[16] = { 0, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0 };
      for (k = 0; k < 4; k++)
         mask_y |= ((mask & (0xf000 >> (k << 2))) > 0) << k;

      off_y = yarray[mask_y];
      mask_x = (mask & (0xf000 >> (off_y << 2))) >> ((off_y ^ 3) << 2);
      off_x = xarray[mask_x];
      offsets[2 * i + 0] = off_x;
      offsets[2 * i + 1] = off_y;
   }

   /* Make sure that offsets for full coverage is zero. */
   offsets[0x1fe] = 0;
   offsets[0x1ff] = 0;

   CommandBuffer cmd = device.request_command_buffer();
   cmd.begin_stream();

   staging.unmap();
   cmd.prepare_image(*vulkan.centroid_lut);
   cmd.copy_to_image(*vulkan.centroid_lut, staging, 0, 0, 0, 0, 256, 1, 256);
   cmd.complete_image(*vulkan.centroid_lut);
   device.submit(cmd);
}

void Renderer::init_dither_lut()
{
   static const int8_t magic[16] = {
		0, 6, 1, 7, 4, 2, 5, 3, 3, 5, 2, 4, 7, 1, 6, 0,
	};

   static const int8_t bayer[16] = {
		0, 4, 1, 5, 4, 0, 5, 1, 3, 7, 2, 6, 7, 3, 6, 2,
	};

	vulkan.dither_lut = device.create_image_2d_array(VK_FORMAT_R8G8_UINT, 4, 4, 16);
	CommandBuffer cmd = device.request_command_buffer();
	Buffer staging = device.request_buffer(BufferType::Staging, (16 * 2) * 16);
	cmd.begin_stream();

	auto *built = static_cast<int8_t *>(staging.map());

	for (unsigned i = 0; i < 16; i++, built += 16 * 2)
	{
		/* Get RGB dither. */
		switch ((i >> 2) & 3)
		{
		case 0:
			for (unsigned x = 0; x < 16; x++)
				built[2 * x] = magic[x];
			break;

		case 1:
		case 2: /* Should be noise, but just use bayer for now. */
			for (unsigned x = 0; x < 16; x++)
				built[2 * x] = bayer[x];
			break;

		case 3:
			for (unsigned x = 0; x < 16; x++)
				built[2 * x] =
				    7; /* Apparently so that inv pattern alpha becomes 0 here, we won't actually dither with this value. */
			break;
		}

		/* Create alpha dither */
		switch ((i >> 0) & 3)
		{
		case 0: /* Same pattern as RGB */
			for (unsigned x = 0; x < 16; x++)
				built[2 * x + 1] = built[2 * x];
			break;

		case 1: /* Inverse pattern as RGB */
			for (unsigned x = 0; x < 16; x++)
				built[2 * x + 1] = (~built[2 * x]) & 7;
			break;

		case 2: /* Noise, just use bayer for now. */
			for (unsigned x = 0; x < 16; x++)
				built[2 * x + 1] = bayer[x];
			break;

		case 3: /* No noise. */
			for (unsigned x = 0; x < 16; x++)
				built[2 * x + 1] = 0;
			break;
		}
	}

	staging.unmap();
	cmd.prepare_image(*vulkan.dither_lut);
	for (unsigned i = 0; i < 16; i++)
	{
		cmd.copy_to_image(*vulkan.dither_lut, staging, 16 * 2 * i, 0, 0, i, 4, 4, 4);
	}
	cmd.complete_image(*vulkan.dither_lut);
	device.submit(cmd);
}

void Renderer::set_framebuffer_size(unsigned width, unsigned height)
{
   unsigned old_tiles_x;

   /* If we change framebuffer size mid-frame, we have no choice but to sync up ... */
   if (framebuffer.color_state == FRAMEBUFFER_GPU || framebuffer.depth_state == FRAMEBUFFER_GPU)
      sync_full();

   framebuffer.allocated_width = width;
   framebuffer.allocated_height = height;

   /* Cannot change framebuffer sizes while GPU has outstanding writes in flight. */
   assert(framebuffer.color_state != FRAMEBUFFER_GPU && framebuffer.depth_state != FRAMEBUFFER_GPU);

   old_tiles_x = tiles_x;
   tiles_x     = (width + TILE_SIZE_X - 1) / TILE_SIZE_X;
   tiles_y     = (height + TILE_SIZE_Y - 1) / TILE_SIZE_Y;

   /* If we're outside a render pass or we did a full flush (only way we can have changed tiles_x),
    * it's okay to clear out tile list data. */
   if ((framebuffer.color_state == FRAMEBUFFER_CPU && framebuffer.depth_state == FRAMEBUFFER_CPU) ||
		   tiles_x != old_tiles_x)
	   tile_lists.clear();

   tile_lists.resize(tiles_x * tiles_y);
}

void Renderer::set_scissor(int xh, int yh, int xl, int yl)
{
   scissor.xh = xh;
   scissor.yh = yh;
   scissor.xl = xl;
   scissor.yl = yl;
}

void Renderer::set_key_r(uint32_t w1, uint32_t w2)
{
   state.key_scale_r  = (w2 & 0x000000FF) >>  0;
   state.key_center_r = (w2 & 0x0000FF00) >>  8;
}

void Renderer::set_key_gb(uint32_t w1, uint32_t w2)
{
   state.key_center_g = (w2 & 0xFF000000) >> 24;
   state.key_scale_g  = (w2 & 0x00FF0000) >> 16;
   state.key_center_b = (w2 & 0x0000FF00) >>  8;
   state.key_scale_b  = (w2 & 0x000000FF) >>  0;
}

void Renderer::set_combine(uint32_t w1, uint32_t w2)
{
   state.combiners.color[0].sub_a = (w1 & 0x00f00000) >> (52 - 32);
   state.combiners.color[0].mul = (w1 & 0x000f8000) >> (47 - 32);
   state.combiners.alpha[0].sub_a = (w1 & 0x00007000) >> (44 - 32);
   state.combiners.alpha[0].mul = (w1 & 0x00000e00) >> (41 - 32);
   state.combiners.color[1].sub_a = (w1 & 0x000001e0) >> (37 - 32);
   state.combiners.color[1].mul = (w1 & 0x0000001f) >> (32 - 32);
   state.combiners.color[0].sub_b = (w2 & 0xf0000000) >> (28 - 0);
   state.combiners.color[1].sub_b = (w2 & 0x0f000000) >> (24 - 0);
   state.combiners.alpha[1].sub_a = (w2 & 0x00e00000) >> (21 - 0);
   state.combiners.alpha[1].mul = (w2 & 0x001c0000) >> (18 - 0);
   state.combiners.color[0].add = (w2 & 0x00038000) >> (15 - 0);
   state.combiners.alpha[0].sub_b = (w2 & 0x00007000) >> (12 - 0);
   state.combiners.alpha[0].add = (w2 & 0x00000e00) >> (9 - 0);
   state.combiners.color[1].add = (w2 & 0x000001c0) >> (6 - 0);
   state.combiners.alpha[1].sub_b = (w2 & 0x00000038) >> (3 - 0);
   state.combiners.alpha[1].add = (w2 & 0x00000007) >> (0 - 0);
   state.combiners_dirty = true;

   // RDP 2-cycle combiner is pipelined so texel 0 becomes texel 1 in second cycle.
   // Similarly, texel 1 becomes the next texel for the neighbor pixel (ouch).
   // Of course, this depends on the winding direction ... Argh :v
   auto color_is_secondary = [](uint32_t c, uint32_t cycle) { return c == (2 - cycle) || c == (9 - cycle); };
   auto alpha_is_secondary = [](uint32_t c, uint32_t cycle) { return c == (2 - cycle); };
   auto color_is_pipelined = [](uint32_t c) { return c == 2 || c == 9; };
   auto alpha_is_pipelined = [](uint32_t c) { return c == 2; };

   auto color_is_tex = [](uint32_t c) { return c == 2 || c == 1 || c == 9 || c == 8; };
   auto alpha_is_tex = [](uint32_t c) { return c == 2 || c == 1; };

   // FIXME: This is silly. Broadcast bools based on c directly instead.
   state.combiner_reads_tile[0] =
	   color_is_tex(state.combiners.color[0].sub_a) || color_is_tex(state.combiners.color[0].sub_b) ||
	   color_is_tex(state.combiners.color[0].mul) || color_is_tex(state.combiners.color[0].add) ||
	   alpha_is_tex(state.combiners.alpha[0].sub_a) || alpha_is_tex(state.combiners.alpha[0].sub_b) ||
	   alpha_is_tex(state.combiners.alpha[0].mul) || alpha_is_tex(state.combiners.alpha[0].add);

   state.combiner_reads_tile[1] =
	   color_is_tex(state.combiners.color[1].sub_a) || color_is_tex(state.combiners.color[1].sub_b) ||
	   color_is_tex(state.combiners.color[1].mul) || color_is_tex(state.combiners.color[1].add) ||
	   alpha_is_tex(state.combiners.alpha[1].sub_a) || alpha_is_tex(state.combiners.alpha[1].sub_b) ||
	   alpha_is_tex(state.combiners.alpha[1].mul) || alpha_is_tex(state.combiners.alpha[1].add);

   state.combiner_reads_secondary_tile[0] =
	   color_is_secondary(state.combiners.color[0].sub_a, 0) ||
	   color_is_secondary(state.combiners.color[0].sub_b, 0) || color_is_secondary(state.combiners.color[0].mul, 0) ||
	   color_is_secondary(state.combiners.color[0].add, 0) || alpha_is_secondary(state.combiners.alpha[0].sub_a, 0) ||
	   alpha_is_secondary(state.combiners.alpha[0].sub_b, 0) || alpha_is_secondary(state.combiners.alpha[0].mul, 0) ||
	   alpha_is_secondary(state.combiners.alpha[0].add, 0);

   state.combiner_reads_secondary_tile[1] =
	   color_is_secondary(state.combiners.color[1].sub_a, 1) ||
	   color_is_secondary(state.combiners.color[1].sub_b, 1) || color_is_secondary(state.combiners.color[1].mul, 1) ||
	   color_is_secondary(state.combiners.color[1].add, 1) || alpha_is_secondary(state.combiners.alpha[1].sub_a, 1) ||
	   alpha_is_secondary(state.combiners.alpha[1].sub_b, 1) || alpha_is_secondary(state.combiners.alpha[1].mul, 1) ||
	   alpha_is_secondary(state.combiners.alpha[1].add, 1);

   state.combiner_reads_pipelined_tile =
	   color_is_pipelined(state.combiners.color[1].sub_a) || color_is_pipelined(state.combiners.color[1].sub_b) ||
	   color_is_pipelined(state.combiners.color[1].mul) || color_is_pipelined(state.combiners.color[1].add) ||
	   alpha_is_pipelined(state.combiners.alpha[1].sub_a) || alpha_is_pipelined(state.combiners.alpha[1].sub_b) ||
	   alpha_is_pipelined(state.combiners.alpha[1].mul) || alpha_is_pipelined(state.combiners.alpha[1].add);
}

void Renderer::log_combiner() const
{
#ifdef ENABLE_LOGS
   unsigned i;
   for (i = 0; i < 2; i++)
   {
      fprintf(stderr, "Cycle %u:\n", i);
      fprintf(stderr, "  Color: (%2u - %2u) * %2u + %2u\n", state.combiners.color[i].sub_a,
		      state.combiners.color[i].sub_b, state.combiners.color[i].mul, state.combiners.color[i].add);
      fprintf(stderr, "  Alpha: (%2u - %2u) * %2u + %2u\n", state.combiners.alpha[i].sub_a,
		      state.combiners.alpha[i].sub_b, state.combiners.alpha[i].mul, state.combiners.alpha[i].add);
   }
#endif
}

bool Renderer::combiner_reads_secondary_tile(unsigned cycle) const
{
   return state.combiner_reads_secondary_tile[cycle];
}

bool Renderer::combiner_reads_pipelined_tile() const
{
   return state.combiner_reads_pipelined_tile;
}

bool Renderer::combiner_reads_tile(CycleType type) const
{
   switch (type)
   {
      case CYCLE_TYPE_1:
         return state.combiner_reads_tile[1];
      case CYCLE_TYPE_2:
         return state.combiner_reads_tile[0] || state.combiner_reads_tile[1];
      default:
         break;
   }

   return false;
}

void Renderer::set_primitive_z(uint32_t w2)
{
   /* Top 15 bits => Primitive Z, lower 16 are dZ.
    * We should encode it appropriately ahead of time. */
   state.primitive_z = w2 & 0x7fffffff;
}

void Renderer::update_tiles(uint32_t tile_mask)
{
   unsigned i;
   uint32_t dirty_tiles = tmem.get_dirty_tiles();
   uint32_t needs_update = dirty_tiles & tile_mask;
   if (!needs_update)
      return;

   for (i = 0; i < TMEM_TILES; i++)
   {
	   if (!(needs_update & (1 << i)))
		   continue;

	   //fprintf(stderr, "Updating tile #%u.\n", i);

	   // Allocate new tile descriptor.
	   tile_instances[i] = tile_descriptors.size();
	   tile_descriptors.emplace_back();
	   auto &tile = tile_descriptors.back();

	   // Build descriptors.
	   tmem.get_effective_size(i, &tile.width, &tile.height);
	   tmem.build_tile_descriptor(i, &tile.desc);

	   // Decode into pixel buffer.
	   tile.hw_fbe = false;
	   if (tmem.tmem_is_framebuffer())
	   {
		   if (tmem.get_framebuffer_transfer(i, &tile.hw_fbe_info))
			   tile.hw_fbe = true;
		   else
			   fprintf(stderr, "WARNING: Attempted HWFBE transfer, but found incompatibility.\n");
	   }

	   if (!tile.hw_fbe)
	   {
		   size_t required_size = tile.width * tile.height * 4;
		   tile.offset = tile_data.size();
		   tile_data.insert(end(tile_data), required_size, uint8_t(0));
		   tmem.decode_tile(i, tile_data.data() + tile.offset, tile.width * 4);
	   }
	   else
	   {
		   // We're doing at least one image load store compute job, so
		   // we need to keep the image in general layout.
		   tile_hw_fbe = true;
	   }

#ifdef TMEM_DEBUG
	   if (!tile.hw_fbe)
	   {
		   size_t required_size = tile.width * tile.height * 4;
		   static unordered_set<size_t> seen_tile;
		   static unordered_set<size_t> blank_tile;

		   hash<uint8_t> hasher;
		   hash<uint32_t> hasher32;
		   size_t v = 0xdeadbeef;
		   for (unsigned i = 0; i < required_size; i++)
			   v = (v * 12515) ^ hasher(tile_data[tile.offset + i]);
		   v = (v * 1241251) ^ hasher32(tile.width);
		   v = (v * 12314) ^ hasher32(tile.height);

		   if (blank_tile.find(v) != end(blank_tile))
		   {
			   memset(tile_data.data() + tile.offset, 0xff, required_size);
		   }
		   else
		   {
			   auto itr = seen_tile.find(v);
			   if (itr == end(seen_tile))
			   {
				   static unsigned tile_count;

				   const char *range = getenv("SKIP_TILE_RANGE");
				   unsigned start, end;
				   if (range && sscanf(range, "%u-%u", &start, &end) == 2 && start <= tile_count && end >= tile_count)
				   {
#ifdef ENABLE_LOGS
					   fprintf(stderr, "Skipping tile %u.\n", tile_count);
#endif
					   memset(tile_data.data() + tile.offset, 0xff, required_size);
					   blank_tile.insert(v);
					   tile_count++;
				   }
				   else
				   {
					   char tile_path[512];
					   fprintf(stderr, "Dumping tile: %u\n", tile_count);

					   snprintf(tile_path, sizeof(tile_path),
							   "/tmp/tile_%04u_%ux%u_format%u_pixelsize%u_shift%u_%u_tmem_0x%04x_line%u.png",
							   tile_count++, tile.width, tile.height, tmem.get_tile(i).format,
							   tmem.get_tile(i).pixel_size, tmem.get_tile(i).shift[0], tmem.get_tile(i).shift[1],
							   tmem.get_tile(i).tmem, tmem.get_tile(i).line);

					   if (!stbi_write_png(tile_path, tile.width, tile.height, 4, tile_data.data() + tile.offset,
								   tile.width * 4))
						   fprintf(stderr, "Failed to write image file: %s\n", tile_path);
					   seen_tile.insert(v);
				   }
			   }
		   }
	   }
#endif
   }

   tmem.clear_dirty_tiles(needs_update);
}

/* Angrylion does this, seems like it's 
 * rounding up to nearest POT, with some weird
 * rules thrown in. */
static uint16_t normalize_dz(uint16_t dz)
{
   unsigned count;
   if (dz & 0xc000)
      return 0x8000;
   if (dz == 0)
      return 1;
   if (dz == 1)
      return 3;
   for (count = 0x2000; count > 0; count >>= 1)
      if (dz & count)
         return count << 1;
   return 0;
}

void Renderer::clip_scissor(int &min_x, int &max_x, int &min_y, int &max_y)
{
   /* Clip to scissor box. */
   min_x = max(min_x, scissor.xh >> 2);
   max_x = min(max_x, scissor.xl >> 2);
   min_y = max(min_y, scissor.yh >> 2);

   /* y < clip is used to test scissor, so if scissor Y == 240, make sure we don't include that line. */
   max_y = min(max_y, (scissor.yl - 1) >> 2);
}

void Renderer::fill_rect_cpu(int xmin, int xmax, int ymin, int ymax)
{
   int y, x;
   int shift = 0;
   auto base = framebuffer_data();

   /* If we are clearing the framebuffer on CPU,
    * we don't want any pending readbacks to this framebuffer to happen after we wrote CPU, so just invalidate them.
    * We should make sure we're clearing the whole region and track by sub-region (uggggh), but this should suffice. */
   auto itr = remove_if(begin(async_transfers), end(async_transfers), [this](const AsyncFramebuffer &async) {
		   return async.framebuffer.addr == framebuffer.addr || async.framebuffer.depth_addr == framebuffer.addr;
		   });
#ifdef ENABLE_LOGS
   if (itr != end(async_transfers))
	   fprintf(stderr, "Invalidating old frames.\n");
#endif
   async_transfers.erase(itr, end(async_transfers));

   clip_scissor(xmin, xmax, ymin, ymax);
   xmin = max(0, xmin);
   xmax = min(int(framebuffer.width - 1), xmax);

   switch (framebuffer.pixel_size)
   {
      case 1:
         shift = 2;
	 break;
      case 2:
         shift = 1;
	 break;
   }

   // Not sure how correct this is, would only matter if we clear at less than 32-bit alignment.
   xmin >>= shift;
   xmax >>= shift;
   uint32_t stride = framebuffer.width << (2 - shift);

   uint32_t fill = state.fill_color;
   uint32_t addr = framebuffer.addr + ymin * stride;
   assert((addr & 3) == 0);

   uint32_t max_addr = addr + (ymax + ymin - 1) * stride + 4 * xmax;

   if (max_addr <= RDRAM_SIZE) /* No wrapping case. */
   {
      for (y = ymin; y <= ymax; y++, addr += stride)
         for (x = xmin; x <= xmax; x++)
            WRITE_DRAM_U32_NOWRAP(base, addr + 4 * x, fill);
   }
   else
   {
      for (y = ymin; y <= ymax; y++, addr += stride)
         for (x = xmin; x <= xmax; x++)
            WRITE_DRAM_U32(base, addr + 4 * x, fill);
   }
}

void Renderer::draw_primitive(const Primitive &prim, const Attribute *attr, uint32_t tile_mask, int min_x, int max_x,
                              int min_y, int max_y)
{
   BufferPrimitive buffer_prim;
   unsigned span_stride = 1;

   clip_scissor(min_x, max_x, min_y, max_y);

   // When computing coverage in copy pipe,
   // we make the decisions a certain number of 64-bits worth of pixels at a time.
   if (((prim.flags >> RDP_FLAG_CYCLE_TYPE_SHIFT) & 3) == CYCLE_TYPE_COPY)
      span_stride = 16 >> max(framebuffer.pixel_size, 1u);

   // Fiddle with max_x to account for splatted coverage.
   max_x = ((max_x - min_x + span_stride) & ~(span_stride - 1)) + min_x - 1;

   int min_tile_x = min_x / TILE_SIZE_X;
   int min_tile_y = min_y / TILE_SIZE_Y;
   int max_tile_x = max_x / TILE_SIZE_X;
   int max_tile_y = max_y / TILE_SIZE_Y;

   // :( Expand the buffer.
   if (max_y >= int(framebuffer.allocated_height))
   {
      // Resize the framebuffer.
      // While building a render pass, this is not in use.
      set_framebuffer_size(framebuffer.allocated_width, max_y + 1);

#ifdef ENABLE_LOGS
      fprintf(stderr, "RESIZING FRAMEBUFFER!\n");
#endif
   }

   update_tiles(tile_mask);

   min_tile_x = max(min_tile_x, 0);
   max_tile_x = min(max_tile_x, int(tiles_x) - 1);

   // Can we render at negative Y? Assume not ...
   // Would get pretty funky.
   assert(min_tile_y >= 0);

   // We cannot clamp rendering in Y blindly,
   // we should have allocated enough size.
   assert(max_tile_y < int(tiles_y));

   unsigned num_tris = primitive_data.size();

   // Push out data to buffer.
   buffer_prim.xl = prim.xl;
   buffer_prim.xm = prim.xm;
   buffer_prim.xh = prim.xh;
   buffer_prim.yl = prim.yl;
   buffer_prim.ym = prim.ym;
   buffer_prim.yh = prim.yh;
   buffer_prim.DxLDy = prim.DxLDy;
   buffer_prim.DxMDy = prim.DxMDy;
   buffer_prim.DxHDy = prim.DxHDy;
   buffer_prim.flags = prim.flags;

   // Add in extra data.
   buffer_prim.scissor_x = scissor.xh | (scissor.xl << 16);
   buffer_prim.scissor_y = scissor.yh | (scissor.yl << 16);

   unsigned cycle_type = (prim.flags >> RDP_FLAG_CYCLE_TYPE_SHIFT) & 3;
   if (cycle_type == CYCLE_TYPE_FILL)
	   buffer_prim.fill_color_blend = state.fill_color;
   else
	   buffer_prim.fill_color_blend = state.blend_word;

   buffer_prim.blend_color = state.blend_color;
   buffer_prim.primitive_z = state.primitive_z;

   if (attr)
   {
	   memcpy(buffer_prim.attr.rgba, attr->rgba, 4 * sizeof(int32_t));
	   memcpy(buffer_prim.attr.d_rgba_dx, attr->d_rgba_dx, 4 * sizeof(int32_t));
	   memcpy(buffer_prim.attr.d_rgba_de, attr->d_rgba_de, 4 * sizeof(int32_t));
	   memcpy(buffer_prim.attr.d_rgba_dy, attr->d_rgba_dy, 4 * sizeof(int32_t));

	   memcpy(buffer_prim.attr.stwz, attr->stwz, 4 * sizeof(int32_t));
	   memcpy(buffer_prim.attr.d_stwz_dx, attr->d_stwz_dx, 4 * sizeof(int32_t));
	   memcpy(buffer_prim.attr.d_stwz_de, attr->d_stwz_de, 4 * sizeof(int32_t));
	   memcpy(buffer_prim.attr.d_stwz_dy, attr->d_stwz_dy, 4 * sizeof(int32_t));

	   uint32_t sign = !!(prim.DxHDy < 0) ^ !!(prim.flags & RDP_FLAG_FLIP);
	   if (sign)
	   {
		   memset(buffer_prim.attr.d_rgba_diff, 0, 4 * sizeof(int32_t));
		   memset(buffer_prim.attr.d_stwz_diff, 0, 4 * sizeof(int32_t));
	   }
	   else
	   {
		   // Apply 3/4th pixel offset.
		   for (unsigned i = 0; i < 4; i++)
		   {
			   int32_t d_rgba_deh = attr->d_rgba_de[i] & ~0x1ff;
			   int32_t d_rgba_dyh = attr->d_rgba_dy[i] & ~0x1ff;
			   int32_t d_stwz_deh = attr->d_stwz_de[i] & ~0x1ff;
			   int32_t d_stwz_dyh = attr->d_stwz_dy[i] & ~0x1ff;
			   buffer_prim.attr.d_rgba_diff[i] = d_rgba_deh - d_rgba_dyh;
			   buffer_prim.attr.d_rgba_diff[i] -= buffer_prim.attr.d_rgba_diff[i] >> 2;

			   buffer_prim.attr.d_stwz_diff[i] = d_stwz_deh - d_stwz_dyh;
			   buffer_prim.attr.d_stwz_diff[i] -= buffer_prim.attr.d_stwz_diff[i] >> 2;
		   }

		   buffer_prim.flags |= RDP_FLAG_DO_OFFSET;
	   }

	   if (buffer_prim.flags & RDP_FLAG_INTERPOLATE_Z)
	   {
		   // Compute dZ and replace it.

		   int32_t dzdx = buffer_prim.attr.d_stwz_dx[3] >> 16;
		   int32_t dzdy = buffer_prim.attr.d_stwz_dy[3] >> 16;

		   // Angrylion does this, but why not just abs()?
		   // This will be off by one, but who knows, there's a reason for everything.
		   dzdx ^= dzdx >> 31;
		   dzdy ^= dzdy >> 31;

		   uint16_t dz = normalize_dz(dzdx + dzdy);

		   buffer_prim.primitive_z &= 0x7fff0000u;
		   buffer_prim.primitive_z |= dz;
	   }
   }
   else
	   memset(&buffer_prim.attr, 0, sizeof(buffer_prim.attr));

   // Only emit Z buffer variant if we need it.
   // Also, only go into PENDING_GPU state if we came from SYNCED state.
   if (framebuffer.color_state == FRAMEBUFFER_CPU)
	   framebuffer.color_state = FRAMEBUFFER_STALE_GPU;
   if (framebuffer.depth_state == FRAMEBUFFER_CPU && (buffer_prim.flags & (RDP_FLAG_Z_UPDATE | RDP_FLAG_Z_COMPARE)))
	   framebuffer.depth_state = FRAMEBUFFER_STALE_GPU;

   memcpy(buffer_prim.attr.tile_descriptors, tile_instances, sizeof(tile_instances));

   // Create a new combiner instance if necessary.
   bool flush = false;

   if (cycle_type == CYCLE_TYPE_1 || cycle_type == CYCLE_TYPE_2)
   {
	   if (state.combiners_dirty)
	   {
		   state.combiners_dirty = false;
		   size_t hash = update_static_combiner();
		   auto itr = state.combiner_map.find(hash);

		   if (itr != end(state.combiner_map))
		   {
			   buffer_prim.span_stride_combiner = itr->second;
			   state.last_combiner = buffer_prim.span_stride_combiner;
		   }
		   else
		   {
			   combiner_data.push_back(state.combiners);
			   if (combiner_data.size() >= RDP_MAX_COMBINERS)
			   {
#ifdef ENABLE_LOGS
				   fprintf(stderr, "Flushing due to combiners.\n");
#endif
				   flush = true;
			   }

			   buffer_prim.span_stride_combiner = combiner_data.size() - 1;
			   state.last_combiner = buffer_prim.span_stride_combiner;
			   state.combiner_map.emplace(hash, state.last_combiner);
		   }
	   }
	   else
		   buffer_prim.span_stride_combiner = state.last_combiner;

	   assert(buffer_prim.span_stride_combiner < combiner_data.size());
   }
   else
	   buffer_prim.span_stride_combiner = 0;

   buffer_prim.span_stride_combiner |= (~(span_stride - 1) & 0xffffu) << 16u;

   // Push primitive to buffer.
   primitive_data.push_back(buffer_prim);
   if (primitive_data.size() >= RDP_MAX_PRIMITIVES)
   {
#ifdef ENABLE_LOGS
	   fprintf(stderr, "Flushing due to primitives.\n");
#endif
	   flush = true;
   }

   // Bin to tiles.
   for (int y = min_tile_y; y <= max_tile_y; y++)
   {
	   for (int x = min_tile_x; x <= max_tile_x; x++)
	   {
		   if (coarse_conservative_raster(x, y, min_x, max_x, min_y, max_y, prim))
		   {
			   raster_tile_count++;
			   auto &tile = tile_lists[y * tiles_x + x];
			   append_tile_list(tile, num_tris, tile_count);

			   work_data.push_back({ { uint32_t(x), uint32_t(y) }, num_tris | state.lod_flags, state.fog_color });
			   tile_count++;

			   if (tile_count > FlushBufferTileCount)
			   {
#ifdef ENABLE_LOGS
				   fprintf(stderr, "Flushing due to tile memory.\n");
#endif
				   flush = true;
			   }
		   }
		   else
			   reject_tile_count++;
	   }
   }

   if (flush)
   {
#ifdef ENABLE_LOGS
	   fprintf(stderr, "Flushing!\n");
#endif
	   flush_tile_lists();
   }
}

void Renderer::append_tile_list(TileInfo &tile_info, unsigned primitive, unsigned tile)
{
   if (tile_info.head == -1u)
   {
      tile_info.head = tile_nodes.size();
      tile_info.tail = tile_nodes.size();
   }
   else
   {
      unsigned next = tile_nodes.size();
      tile_nodes[tile_info.tail].next = next;
      tile_info.tail = next;
   }
   tile_nodes.emplace_back(primitive | (tile << RDP_MAX_PRIMITIVES_LOG2), -1u);
}

bool Renderer::coarse_conservative_raster(int x, int y, int min_x, int max_x, int min_y, int max_y,
                                          const Primitive &prim) const
{
	min_x = std::max((x + 0) * TILE_SIZE_X, min_x);
	max_x = std::min((x + 1) * TILE_SIZE_X - 1, max_x);

	min_y = std::max((y + 0) * TILE_SIZE_Y, min_y) * 4;
	max_y = std::min((y + 1) * TILE_SIZE_Y - 1, max_y) * 4 + 3;

	bool flip = (prim.flags & RDP_FLAG_FLIP) != 0;
	int32_t xh0 = (prim.xh + (min_y - (prim.yh & ~3)) * prim.DxHDy) >> 16;
	int32_t xh1 = (prim.xh + (max_y - (prim.yh & ~3)) * prim.DxHDy) >> 16;

	int32_t xm0 = (prim.xm + (min_y - (prim.yh & ~3)) * prim.DxMDy) >> 16;
	int32_t xm1 = (prim.xm + (max_y - (prim.yh & ~3)) * prim.DxMDy) >> 16;

	int32_t xl0 = (prim.xl + (min_y - prim.ym) * prim.DxLDy) >> 16;
	int32_t xl1 = (prim.xl + (max_y - prim.ym) * prim.DxLDy) >> 16;

	bool skip_m = min_y >= prim.ym;
	bool skip_l = max_y < prim.ym;
	bool cull_m, cull_l;

	if (flip)
	{
		int32_t xh = std::min(xh0, xh1);
		int32_t xm = std::max(xm0, xm1);
		int32_t xl = std::max(xl0, xl1);

		if (xh > max_x)
			return false;

		cull_m = skip_m || (xm < min_x);
		cull_l = skip_l || (xl < min_x);
	}
	else
	{
		int32_t xh = std::max(xh0, xh1);
		int32_t xm = std::min(xm0, xm1);
		int32_t xl = std::min(xl0, xl1);

		if (xh < min_x)
			return false;

		cull_m = skip_m || (xm > max_x);
		cull_l = skip_l || (xl > max_x);
	}
	return !cull_m || !cull_l;
}

void Renderer::set_lod_modes(bool detail, bool sharpen)
{
   state.lod_flags &= ~(LOD_INFO_PRIMITIVE_DETAIL | LOD_INFO_PRIMITIVE_SHARPEN);
   state.lod_flags |= detail ? LOD_INFO_PRIMITIVE_DETAIL : 0;
   state.lod_flags |= sharpen ? LOD_INFO_PRIMITIVE_SHARPEN : 0;
}

void Renderer::set_prim_color(uint32_t w1, uint32_t w2)
{
   uint32_t min_lod       = (w1 & 0x1f00) >> 8;

   state.prim_color       = w2;
   state.prim_lod_frac    = w1 & 0xff;
   state.combiners_dirty  = true;

   state.lod_flags       &= ~LOD_INFO_PRIMITIVE_MIN_LOD_MASK;
   state.lod_flags       |= min_lod << LOD_INFO_PRIMITIVE_MIN_LOD_SHIFT;
}

void Renderer::set_env_color(uint32_t w2)
{
   state.env_color = w2;
   state.combiners_dirty = true;
}

void Renderer::set_fog_color(uint32_t w2)
{
   state.fog_color = w2;
}

void Renderer::set_convert(uint32_t w1, uint32_t w2)
{
   // TODO/FIXME - Ignore texture filter converts for now ...
   int32_t k0 = (w1 >> 13) & 0x1ff;
   int32_t k1 = (w1 >> 4) & 0x1ff;
   int32_t k2 = ((w1 & 0xf) << 5) | ((w2 >> 27) & 0x1f);
   int32_t k3 = (w2 >> 18) & 0x1ff;
   state.k0_tf = (SIGN(k0, 9) << 1) + 1;
   state.k1_tf = (SIGN(k1, 9) << 1) + 1;
   state.k2_tf = (SIGN(k2, 9) << 1) + 1;
   state.k3_tf = (SIGN(k3, 9) << 1) + 1;
   state.k4 = (w2 >> 9) & 0x1ff;
   state.k5 = w2 & 0x1ff;
   state.combiners_dirty = true;
}

uint64_t Renderer::update_static_combiner()
{
   auto &c = state.combiners;
   for (unsigned base = 0; base < 2; base++)
   {
	   auto &cycle = c.cycle[base];
	   auto &color = c.color[base];
	   auto &alpha = c.alpha[base];

	   // Pre-resolve color inputs.
	   switch (color.sub_a)
	   {
		   case 3:
			   cycle.sub_a[0] = uint8_t(state.prim_color >> 24);
			   cycle.sub_a[1] = uint8_t(state.prim_color >> 16);
			   cycle.sub_a[2] = uint8_t(state.prim_color >> 8);
			   break;

		   case 5:
			   cycle.sub_a[0] = uint8_t(state.env_color >> 24);
			   cycle.sub_a[1] = uint8_t(state.env_color >> 16);
			   cycle.sub_a[2] = uint8_t(state.env_color >> 8);
			   break;

		   case 6:
			   cycle.sub_a[0] = 0x100;
			   cycle.sub_a[1] = 0x100;
			   cycle.sub_a[2] = 0x100;
			   break;

		   default:
			   cycle.sub_a[0] = 0x0;
			   cycle.sub_a[1] = 0x0;
			   cycle.sub_a[2] = 0x0;
			   break;
	   }

	   switch (color.sub_b)
	   {
		   case 3:
			   cycle.sub_b[0] = uint8_t(state.prim_color >> 24);
			   cycle.sub_b[1] = uint8_t(state.prim_color >> 16);
			   cycle.sub_b[2] = uint8_t(state.prim_color >> 8);
			   break;

		   case 5:
			   cycle.sub_b[0] = uint8_t(state.env_color >> 24);
			   cycle.sub_b[1] = uint8_t(state.env_color >> 16);
			   cycle.sub_b[2] = uint8_t(state.env_color >> 8);
			   break;

		   case 6:
			   /* Key center */
			   cycle.sub_b[0] = state.key_center_r;
			   cycle.sub_b[1] = state.key_center_g;
			   cycle.sub_b[2] = state.key_center_b;
			   break;

		   case 7:
			   cycle.sub_b[0] = state.k4;
			   cycle.sub_b[1] = state.k4;
			   cycle.sub_b[2] = state.k4;
			   break;

		   default:
			   cycle.sub_b[0] = 0x0;
			   cycle.sub_b[1] = 0x0;
			   cycle.sub_b[2] = 0x0;
			   break;
	   }

	   switch (color.mul)
	   {
		   case 3:
			   cycle.mul[0] = uint8_t(state.prim_color >> 24);
			   cycle.mul[1] = uint8_t(state.prim_color >> 16);
			   cycle.mul[2] = uint8_t(state.prim_color >> 8);
			   break;

		   case 5:
			   cycle.mul[0] = uint8_t(state.env_color >> 24);
			   cycle.mul[1] = uint8_t(state.env_color >> 16);
			   cycle.mul[2] = uint8_t(state.env_color >> 8);
			   break;

		   case 6:
			   /* Key scale */
			   cycle.mul[0] = uint8_t(state.key_scale_r);
			   cycle.mul[1] = uint8_t(state.key_scale_g);
			   cycle.mul[2] = uint8_t(state.key_scale_b);
			   break;

		   case 10:
			   cycle.mul[0] = cycle.mul[1] = cycle.mul[2] = uint8_t(state.prim_color >> 0);
			   break;

		   case 12:
			   cycle.mul[0] = cycle.mul[1] = cycle.mul[2] = uint8_t(state.env_color >> 0);
			   break;

		   case 14:
			   cycle.mul[0] = cycle.mul[1] = cycle.mul[2] = state.prim_lod_frac;
			   break;

		   case 15:
			   cycle.mul[0] = state.k5;
			   cycle.mul[1] = state.k5;
			   cycle.mul[2] = state.k5;
			   break;

		   default:
			   cycle.mul[0] = 0;
			   cycle.mul[1] = 0;
			   cycle.mul[2] = 0;
			   break;
	   }

	   switch (color.add)
	   {
		   case 3:
			   cycle.add[0] = uint8_t(state.prim_color >> 24);
			   cycle.add[1] = uint8_t(state.prim_color >> 16);
			   cycle.add[2] = uint8_t(state.prim_color >> 8);
			   break;

		   case 5:
			   cycle.add[0] = uint8_t(state.env_color >> 24);
			   cycle.add[1] = uint8_t(state.env_color >> 16);
			   cycle.add[2] = uint8_t(state.env_color >> 8);
			   break;

		   case 6:
			   cycle.add[0] = 0x100;
			   cycle.add[1] = 0x100;
			   cycle.add[2] = 0x100;
			   break;

		   default:
			   cycle.add[0] = 0x0;
			   cycle.add[1] = 0x0;
			   cycle.add[2] = 0x0;
			   break;
	   }

	   // Alpha
	   switch (alpha.sub_a)
	   {
		   case 3:
			   cycle.sub_a[3] = uint8_t(state.prim_color >> 0);
			   break;

		   case 5:
			   cycle.sub_a[3] = uint8_t(state.env_color >> 0);
			   break;

		   case 6:
			   cycle.sub_a[3] = 0x100;
			   break;

		   default:
			   cycle.sub_a[3] = 0;
			   break;
	   }

	   switch (alpha.sub_b)
	   {
		   case 3:
			   cycle.sub_b[3] = uint8_t(state.prim_color >> 0);
			   break;

		   case 5:
			   cycle.sub_b[3] = uint8_t(state.env_color >> 0);
			   break;

		   case 6:
			   cycle.sub_b[3] = 0x100;
			   break;

		   default:
			   cycle.sub_b[3] = 0;
			   break;
	   }

	   switch (alpha.mul)
	   {
		   case 3:
			   cycle.mul[3] = uint8_t(state.prim_color >> 0);
			   break;

		   case 5:
			   cycle.mul[3] = uint8_t(state.env_color >> 0);
			   break;

		   case 6:
			   cycle.mul[3] = state.prim_lod_frac;
			   break;

		   default:
			   cycle.mul[3] = 0;
			   break;
	   }

	   switch (alpha.add)
	   {
		   case 3:
			   cycle.add[3] = uint8_t(state.prim_color >> 0);
			   break;

		   case 5:
			   cycle.add[3] = uint8_t(state.env_color >> 0);
			   break;

		   case 6:
			   cycle.add[3] = 0x100;
			   break;

		   default:
			   cycle.add[3] = 0;
			   break;
	   }
   }

   return hash64(&c, sizeof(c));
}

void Renderer::set_depth_image(uint32_t addr)
{
   if (framebuffer.depth_addr == addr)
      return;

   // Keep the async frame around and update it when it's done.
   if (framebuffer.depth_state == FRAMEBUFFER_GPU)
      complete_frame();

   framebuffer.depth_addr = addr;
}

void Renderer::set_color_image(uint32_t addr, unsigned format, unsigned pixel_size, unsigned width)
{
   unsigned max_height    = width;

   if (framebuffer.addr == addr && framebuffer.format == format && framebuffer.width == width &&
      framebuffer.pixel_size == pixel_size)
      return;

   /* Keep the async frame around and update it when it's done. */
   complete_frame();

   framebuffer.addr       = addr;
   framebuffer.format     = format;
   framebuffer.width      = width;
   framebuffer.pixel_size = pixel_size;

   // Unfortunately, the N64 RDP doesn't *need* to know the framebuffer
   // height since it's implied by the scissor box.
   // It only renders scanlines.
   // We will need to estimate the real height and potentially flush out
   // everything if we guess wrong.
   // Just employ some crappy heuristic to make this sort of work.
   if (width > width_greater_than)
      max_height = width_greater_than_max_height;
   else if (width == 320)
      max_height = 240;

   // Allocate new framebuffer if needed.
   if (framebuffer.allocated_width != width || framebuffer.allocated_height != max_height)
      set_framebuffer_size(width, max_height);
}

void Renderer::reset_buffers()
{
   primitive_data.clear();
   combiner_data.clear();
   tile_descriptors.clear();
   tile_data.clear();
   work_data.clear();
   state.combiner_map.clear();
   state.last_combiner = 0;
   tile_count = 0;
   tile_hw_fbe = false;

   memset(tile_instances, 0, sizeof(tile_instances));

   for (auto &tile : tile_lists)
   {
      tile.head = -1u;
      tile.tail = -1u;
   }
   tile_nodes.clear();

   // Invalidate state that is flushed out when needed.
   tmem.invalidate();
   state.combiners_dirty = true;
}

void Renderer::begin_framebuffer()
{
   if (!vulkan.framebuffer.staging.block)
   {
      unsigned pixels = framebuffer.allocated_width * framebuffer.allocated_height;
      // Needs to be dynamic shared since we will do VI uploads from alt_queue concurrently.
      vulkan.framebuffer = device.request_buffer(BufferType::DynamicShared, pixels * sizeof(uint32_t));
   }
}

void Renderer::begin_framebuffer_depth()
{
   if (!vulkan.framebuffer_depth.staging.block)
   {
      unsigned pixels = framebuffer.allocated_width * framebuffer.allocated_height;
      vulkan.framebuffer_depth = device.request_buffer(BufferType::Dynamic, pixels * sizeof(uint32_t));
   }
}

void Renderer::sync_color_dram_to_gpu()
{
#ifdef ENABLE_LOGS
   fprintf(stderr, "sync_color_dram_to_gpu()\n");
#endif

   // Check if the last writer to this region was actually the GPU. In this case, we can copy from GPU -> GPU.
   // This usually happens when clear screen happens with CYCLE1 pipeline instead of FILL, which
   // blocks us from using CPU-side fill optimization.
   int old_index = -1;
   bool matches_depth = false;
   bool matches_color = false;

   for (int i = async_transfers.size() - 1; i >= 0; i--)
   {
      auto &async = async_transfers[i];
      matches_depth = async.framebuffer.depth_addr == framebuffer.addr && async.depth_buffer.staging.block &&
	      framebuffer.pixel_size == PIXEL_SIZE_16BPP;
      matches_color = async.framebuffer.addr == framebuffer.addr && async.color_buffer.staging.block &&
	      async.framebuffer.pixel_size == framebuffer.pixel_size;

      if ((matches_depth || matches_color) && async.framebuffer.allocated_width == framebuffer.allocated_width &&
		      async.framebuffer.allocated_height == framebuffer.allocated_height)
      {
	      old_index = i;
	      break;
      }
   }

   if (old_index >= 0)
   {
      if (!vulkan.framebuffer.staging.block)
      {
         // We have already waited for earlier compute to complete when we called sync_gpu_to_dram().
         begin_framebuffer();
	 if (matches_color)
		 vulkan.cmd.copy_buffer(vulkan.framebuffer, async_transfers[old_index].color_buffer);
	 else if (matches_depth)
		 vulkan.cmd.copy_buffer(vulkan.framebuffer, async_transfers[old_index].depth_buffer);
      }
   }
   else
   {
      unsigned pixels;
      auto base = framebuffer_data();

      begin_framebuffer();
      pixels = framebuffer.allocated_width * framebuffer.allocated_height;
      auto *dst = static_cast<uint32_t *>(vulkan.framebuffer.map());

      if (framebuffer.pixel_size == PIXEL_SIZE_32BPP)
      {
	      uint32_t max_addr = framebuffer.addr + 4 * pixels;
	      assert((framebuffer.addr & 3) == 0);
	      if (max_addr <= RDRAM_SIZE)
		      memcpy(dst, base + framebuffer.addr, pixels * sizeof(uint32_t));
	      else
	      {
		      for (unsigned i = 0; i < pixels; i++)
			      dst[i] = READ_DRAM_U32(base, framebuffer.addr + 4 * i);
	      }
      }
      else if (framebuffer.pixel_size == PIXEL_SIZE_16BPP)
      {
	      uint32_t max_addr = framebuffer.addr + 2 * pixels;
	      assert((framebuffer.addr & 1) == 0);
	      if (max_addr <= RDRAM_SIZE)
	      {
		      for (unsigned i = 0; i < pixels; i++)
		      {
			      // Estimate the hidden bits based on the alpha bit.
			      uint32_t c = READ_DRAM_U16_NOWRAP(base, framebuffer.addr + 2 * i);
			      dst[i] = (c << 2) | ((c & 1) * 3);
		      }
	      }
	      else
	      {
		      for (unsigned i = 0; i < pixels; i++)
		      {
			      // Estimate the hidden bits based on the alpha bit.
			      uint32_t c = READ_DRAM_U16(base, framebuffer.addr + 2 * i);
			      dst[i] = (c << 2) | ((c & 1) * 3);
		      }
	      }
      }
      else if (framebuffer.pixel_size == PIXEL_SIZE_8BPP)
      {
	      uint32_t max_addr = framebuffer.addr + 1 * pixels;
	      if (max_addr <= RDRAM_SIZE)
	      {
		      for (unsigned i = 0; i < pixels; i++)
		      {
			      // Estimate the hidden bits based on the alpha bit.
			      uint32_t c = READ_DRAM_U8_NOWRAP(base, framebuffer.addr + 1 * i);
			      dst[i] = (c << 3) | ((c & 1) * 7);
		      }
	      }
	      else
	      {
		      for (unsigned i = 0; i < pixels; i++)
		      {
			      // Estimate the hidden bits based on the alpha bit.
			      uint32_t c = READ_DRAM_U8(base, framebuffer.addr + 1 * i);
			      dst[i] = (c << 3) | ((c & 1) * 7);
		      }
	      }
      }

      vulkan.framebuffer.unmap();
      vulkan.cmd.sync_buffer_to_gpu(vulkan.framebuffer);
   }

   framebuffer.color_state = FRAMEBUFFER_GPU;
}

void Renderer::sync_depth_dram_to_gpu()
{
   // If color buffer and depth buffer alias each other, we won't use a depth buffer per-se, as
   // we'll do the aliasing internally.
   
   if (framebuffer.addr == framebuffer.depth_addr)
   {
      framebuffer.depth_state = FRAMEBUFFER_GPU;
      return;
   }

#ifdef ENABLE_LOGS
   fprintf(stderr, "sync_depth_dram_to_gpu()\n");
#endif
   auto base = framebuffer_data();

   // Check if the last writer to this region was actually the GPU. In this case, we can copy from GPU -> GPU.
   // This usually happens when clear screen happens with CYCLE1 pipeline instead of FILL, which
   // blocks us from using CPU-side fill optimization.
   int old_index = -1;
   bool matches_depth = false, matches_color = false;

   for (int i = async_transfers.size() - 1; i >= 0; i--)
   {
	   auto &async = async_transfers[i];

	   matches_depth = async.framebuffer.depth_addr == framebuffer.depth_addr && async.depth_buffer.staging.block;

	   matches_color = async.framebuffer.addr == framebuffer.depth_addr && async.color_buffer.staging.block &&
		   async.framebuffer.pixel_size == PIXEL_SIZE_16BPP;

	   if ((matches_depth || matches_color) && async.framebuffer.allocated_width == framebuffer.allocated_width &&
			   async.framebuffer.allocated_height == framebuffer.allocated_height)
	   {
		   old_index = i;
		   break;
	   }
   }

   if (old_index >= 0)
   {
	   if (!vulkan.framebuffer_depth.staging.block)
	   {
		   begin_framebuffer_depth();
		   // We have already waited for earlier compute to complete when we called sync_gpu_to_dram().
		   if (matches_depth)
		   {
#ifdef ENABLE_LOGS
			   fprintf(stderr, "Reusing old depth buffer.\n");
#endif
			   vulkan.cmd.copy_buffer(vulkan.framebuffer_depth, async_transfers[old_index].depth_buffer);
		   }
		   else if (matches_color)
		   {
#ifdef ENABLE_LOGS
			   fprintf(stderr, "Reusing old color buffer.\n");
#endif
			   vulkan.cmd.copy_buffer(vulkan.framebuffer_depth, async_transfers[old_index].color_buffer);
		   }
	   }
   }
   else
   {
	   begin_framebuffer_depth();
	   unsigned pixels = framebuffer.allocated_width * framebuffer.allocated_height;
	   auto *dst = static_cast<uint32_t *>(vulkan.framebuffer_depth.map());

	   for (unsigned i = 0; i < pixels; i++)
		   dst[i] = READ_DRAM_U16(base, framebuffer.depth_addr + 2 * i) << 2;

	   vulkan.framebuffer_depth.unmap();
	   vulkan.cmd.sync_buffer_to_gpu(vulkan.framebuffer_depth);
   }

   framebuffer.depth_state = FRAMEBUFFER_GPU;
}

void Renderer::begin_index(unsigned index)
{
   unsigned i;
   /* Complete async transfers which are complete. */
   unsigned end = 0;

   for (i = 0; i < async_transfers.size(); i++)
      if (async_transfers[i].sync_index == index)
         end = i + 1;

   for (i = 0; i < end; i++)
      sync_framebuffer_to_cpu(async_transfers[i]);

   if (end)
      async_transfers.erase(begin(async_transfers), begin(async_transfers) + end);

   current_sync_index = index;
}

void Renderer::sync_framebuffer_to_cpu(AsyncFramebuffer &async)
{
	// Wait for GPU to complete.
	device.wait(async.fence);
	auto &framebuffer = async.framebuffer;

	// A bit hacky, but if we reading back framebuffers async, we cannot safely write
	// to normal RDRAM since that might have been recycled with other critical data.
	// Instead, we maintain a shadowed RDRAM which contains GPU rendered data only.
	auto base = framebuffer_data();

	// Reads back GPU buffer and updates DRAM with newly rendered data.
	// For now, just make every call synchronous, but we really
	// want async readbacks for content which does not need FB emulation and forward GPU -> FB -> GPU when
	// possible.

	unsigned pixels = framebuffer.allocated_width * framebuffer.allocated_height;

	if (framebuffer.color_state == FRAMEBUFFER_GPU)
	{
		const auto *src = static_cast<const uint32_t *>(async.color_buffer.map());

		if (framebuffer.pixel_size == PIXEL_SIZE_32BPP)
		{
			uint32_t max_addr = framebuffer.addr + 4 * pixels;
			if (max_addr <= RDRAM_SIZE)
			{
				memcpy(base + framebuffer.addr, src, pixels * sizeof(uint32_t));
			}
			else
			{
				for (unsigned i = 0; i < pixels; i++)
					WRITE_DRAM_U32(base, framebuffer.addr + 4 * i, src[i]);
			}
		}
		else if (framebuffer.pixel_size == PIXEL_SIZE_16BPP)
		{
			uint32_t max_addr = framebuffer.addr + 2 * pixels;
			assert((framebuffer.addr & 1) == 0);

			if (max_addr <= RDRAM_SIZE)
			{
				for (unsigned i = 0; i < pixels; i++)
					WRITE_DRAM_U16_NOWRAP(base, framebuffer.addr + 2 * i, src[i] >> 2);
			}
			else
			{
				for (unsigned i = 0; i < pixels; i++)
					WRITE_DRAM_U16(base, framebuffer.addr + 2 * i, src[i] >> 2);
			}
		}
		else if (framebuffer.pixel_size == PIXEL_SIZE_8BPP)
		{
			uint32_t max_addr = framebuffer.addr + 1 * pixels;

			if (max_addr <= RDRAM_SIZE)
			{
				for (unsigned i = 0; i < pixels; i++)
					WRITE_DRAM_U8_NOWRAP(base, framebuffer.addr + 1 * i, src[i] >> 3);
			}
			else
			{
				for (unsigned i = 0; i < pixels; i++)
					WRITE_DRAM_U8(base, framebuffer.addr + 1 * i, src[i] >> 3);
			}
		}
		async.color_buffer.unmap();
	}

	// Don't read back the depth buffer if it shares the address with color buffer.
	if (framebuffer.depth_state == FRAMEBUFFER_GPU && framebuffer.addr != framebuffer.depth_addr)
	{
		const auto *src = static_cast<const uint32_t *>(async.depth_buffer.map());
		uint32_t max_addr = framebuffer.depth_addr + 2 * pixels;
		assert((framebuffer.depth_addr & 1) == 0);

		if (max_addr <= RDRAM_SIZE)
		{
			for (unsigned i = 0; i < pixels; i++)
				WRITE_DRAM_U16_NOWRAP(base, framebuffer.depth_addr + 2 * i, src[i] >> 2);
		}
		else
		{
			for (unsigned i = 0; i < pixels; i++)
				WRITE_DRAM_U16(base, framebuffer.depth_addr + 2 * i, src[i] >> 2);
		}
		async.depth_buffer.unmap();
	}
}

struct PushConstant
{
   uint32_t width, height;
};

void Renderer::sync_gpu_to_vi(CommandBuffer &cmd)
{
   VIOutput output;
   // Remove now stale VI outputs.
   vi_outputs.erase(remove_if(begin(vi_outputs), end(vi_outputs),
			   [this](VIOutput &output) { return output.framebuffer.addr == framebuffer.addr; }),
		   end(vi_outputs));

   output.framebuffer = framebuffer;
   output.image =
	   device.create_image_2d(VK_FORMAT_R8G8B8A8_UNORM, framebuffer.allocated_width, framebuffer.allocated_height);

   auto set = device.request_blit_descriptor_set(Vulkan::Blit::DescriptorSetType::Buffers);
   switch (framebuffer.pixel_size)
   {
      case 3:
         cmd.bind_pipeline(device.get_blit_pipeline(Vulkan::Blit::PipelineType::Blit_32bit));
	 break;
      case 2:
	 cmd.bind_pipeline(device.get_blit_pipeline(Vulkan::Blit::PipelineType::Blit_16bit));
	 break;
      default:
      case 1:
	 cmd.bind_pipeline(device.get_blit_pipeline(Vulkan::Blit::PipelineType::Blit_8bit));
	 break;
   }

   PushConstant push = { framebuffer.allocated_width, framebuffer.allocated_height };
   cmd.push_constants(&push, sizeof(push));

   set.set_storage_buffer(static_cast<unsigned>(Vulkan::Blit::BufferLayout::Color), vulkan.framebuffer);
   set.set_storage_image(static_cast<unsigned>(Vulkan::Blit::BufferLayout::Image), *output.image);
   cmd.bind_descriptor_set(static_cast<unsigned>(Vulkan::Blit::DescriptorSetType::Buffers), set);

   cmd.prepare_storage_image(*output.image);
   cmd.dispatch((framebuffer.allocated_width + 7) / 8, (framebuffer.allocated_height + 7) / 8, 1);
   cmd.complete_storage_image(*output.image);

   vi_outputs.push_back(move(output));
}

void Renderer::sync_gpu_to_dram(bool blocking)
{
   CommandBuffer alt_cmd;
   bool depth_is_aliased           = false;

   /* We cannot be in a transient state when doing this. */
   assert(framebuffer.color_state != FRAMEBUFFER_STALE_GPU);
   assert(framebuffer.depth_state != FRAMEBUFFER_STALE_GPU);

#ifdef ENABLE_LOGS
   fprintf(stderr, "sync_gpu_to_dram()\n");
#endif

   depth_is_aliased = framebuffer.depth_state == FRAMEBUFFER_GPU && framebuffer.addr == framebuffer.depth_addr;

   vulkan.cmd.begin_readback();

   if (framebuffer.color_state == FRAMEBUFFER_GPU)
	   vulkan.cmd.sync_buffer_to_cpu(vulkan.framebuffer);
   if (framebuffer.depth_state == FRAMEBUFFER_GPU && !depth_is_aliased)
	   vulkan.cmd.sync_buffer_to_cpu(vulkan.framebuffer_depth);

   vulkan.cmd.end_readback();

   auto sem = device.request_semaphore();

   if (framebuffer.color_state == FRAMEBUFFER_GPU)
   {
      alt_cmd = device.request_alt_command_buffer();
      if (!depth_is_aliased)
         sync_gpu_to_vi(alt_cmd);
   }

   /* If we're blocking, wait immediately and read back buffers. */
   if (blocking)
   {
      /* Make sure we read to RDRAM here. */
      AsyncFramebuffer async;
      bool old_sync = true;

      swap(old_sync, synchronous);

      async.sync_index   = current_sync_index;
      async.framebuffer  = framebuffer;
      async.color_buffer = vulkan.framebuffer;

      if (framebuffer.depth_state == FRAMEBUFFER_GPU && !depth_is_aliased)
         async.depth_buffer = vulkan.framebuffer_depth;

      async.fence        = submit(&sem);

      if (framebuffer.color_state == FRAMEBUFFER_GPU)
	      device.submit_alt_queue(alt_cmd, &sem, nullptr);

      sync_framebuffer_to_cpu(async);

      swap(old_sync, synchronous);
   }
   else
   {
      /* If we're completing frame async, just queue up 
       * a transfer back to client memory. */
      AsyncFramebuffer async;

      async.sync_index   = current_sync_index;
      async.framebuffer  = framebuffer;

      async.color_buffer = vulkan.framebuffer;

      if (framebuffer.depth_state == FRAMEBUFFER_GPU && !depth_is_aliased)
         async.depth_buffer = vulkan.framebuffer_depth;

      async.fence        = submit(&sem);

      if (framebuffer.color_state == FRAMEBUFFER_GPU)
         device.submit_alt_queue(alt_cmd, &sem, nullptr);

      async_transfers.push_back(move(async));
   }

   framebuffer.color_state = FRAMEBUFFER_CPU;
   framebuffer.depth_state = FRAMEBUFFER_CPU;
}

void Renderer::sync_full()
{
   /* Flush out all async framebuffers and synchronize with DRAM. */
   for (auto &async : async_transfers)
      sync_framebuffer_to_cpu(async);
   async_transfers.clear();

   flush_tile_lists();
   if (vulkan.cmd.cmd != VK_NULL_HANDLE)
      sync_gpu_to_dram(true);
}

void Renderer::complete_frame()
{
   if (synchronous)
      sync_full();
   else
   {
      flush_tile_lists();
      if (vulkan.cmd.cmd != VK_NULL_HANDLE)
         sync_gpu_to_dram(false);
   }
}

void Renderer::allocate_tiles()
{
   TileAtlasAllocator atlas;
   unsigned width, height, layers;
   float inv_width, inv_height;

   atlas.reset();

   // Find max tile size, this will become the size of array layer.
   for (auto &tile : tile_descriptors)
	   atlas.add_size(tile.width, tile.height);

   // Allocate space in 2D array for all tiles.
   atlas.begin_allocator();
   for (auto &tile : tile_descriptors)
	   atlas.allocate(&tile.off_x, &tile.off_y, &tile.off_z, tile.width, tile.height);
   atlas.end_allocator();

   // Get the final size.
   atlas.get_atlas_size(&width, &height, &layers);

#ifdef ENABLE_LOGS
   fprintf(stderr, "Atlas: %u x %u x %u\n", width, height, layers);
#endif

   inv_width  = 1.0f / width;
   inv_height = 1.0f / height;

   /* Set correct UV offsets for the tiles. */
   for (auto &tile : tile_descriptors)
   {
      tile.desc.offset[0] = (tile.off_x + 0.5f) * inv_width;
      tile.desc.offset[1] = (tile.off_y + 0.5f) * inv_height;
      tile.desc.layer     = float(tile.off_z);
   }

   vulkan.tile_map.width  = width;
   vulkan.tile_map.height = height;
   vulkan.tile_map.layers = layers;
}

void Renderer::begin_command_buffer()
{
   if (vulkan.cmd.cmd == VK_NULL_HANDLE)
      vulkan.cmd = device.request_command_buffer();
}

Vulkan::Fence Renderer::submit(const Vulkan::Semaphore *sem)
{
   assert(vulkan.cmd.cmd != VK_NULL_HANDLE);
   auto fence = device.submit(vulkan.cmd, nullptr, sem);

   vulkan.cmd = {};
   vulkan.framebuffer = {};
   vulkan.framebuffer_depth = {};

   return fence;
}

void Renderer::flush_tile_lists()
{
   if (primitive_data.empty() || work_data.empty())
      return;

#ifdef ENABLE_LOGS
   fprintf(stderr, "Flushing %u primitives.\n", unsigned(primitive_data.size()));
   fprintf(stderr, "Rejection rate: %.3f %%\n",
		   100.0 * double(reject_tile_count) / double(reject_tile_count + raster_tile_count));
#endif
   begin_command_buffer();

   /* Allocate descriptor sets. */
   vulkan.lut_set = device.request_rdp_descriptor_set(Vulkan::RDP::DescriptorSetType::LUT);
   vulkan.buffer_set = device.request_rdp_descriptor_set(Vulkan::RDP::DescriptorSetType::Buffers);
   if (rdp_dithering)
	   vulkan.lut_set.set_image(0, *vulkan.dither_lut);
   vulkan.lut_set.set_image(1, *vulkan.centroid_lut);
   vulkan.lut_set.set_uniform_buffer(2, vulkan.z_lut);

   vulkan.cmd.begin_stream();

   /* TODO: We could detect if we have a typical clear screen scenario and
    * avoid uploading DRAM to GPU every frame. */
   if (framebuffer.color_state == FRAMEBUFFER_STALE_GPU)
	   sync_color_dram_to_gpu();
   if (framebuffer.depth_state == FRAMEBUFFER_STALE_GPU)
	   sync_depth_dram_to_gpu();
   bool depth_is_aliased = framebuffer.depth_state == FRAMEBUFFER_GPU && framebuffer.addr == framebuffer.depth_addr;
   bool pass_uses_depth = framebuffer.depth_state == FRAMEBUFFER_GPU && !depth_is_aliased;

   /* Primitive data. */
   {
	   size_t primitive_data_size = primitive_data.size() * sizeof(BufferPrimitive);
	   Buffer tmp = device.request_dynamic_buffer(vulkan.cmd, vulkan.buffer_set,
			   Vulkan::RDP::BufferLayout::PrimitiveData, primitive_data_size);

	   memcpy(tmp.map(), primitive_data.data(), primitive_data_size);
	   tmp.unmap();
   }

   /* Upload tile list headers */
   {
	   size_t tile_list_header_size = sizeof(uint32_t) * tiles_x * tiles_y;
	   Buffer tmp = device.request_dynamic_buffer(vulkan.cmd, vulkan.buffer_set,
			   Vulkan::RDP::BufferLayout::TileListHeader, tile_list_header_size);

	   auto *header = static_cast<uint32_t *>(tmp.map());
	   for (auto &tile : tile_lists)
		   *header++ = tile.head;

	   tmp.unmap();
   }

   /* Upload tile list. */
   {
	   size_t tile_node_size = tile_nodes.size() * sizeof(TileNode);
	   Buffer tmp = device.request_dynamic_buffer(vulkan.cmd, vulkan.buffer_set, Vulkan::RDP::BufferLayout::TileList,
			   tile_node_size);

	   memcpy(tmp.map(), tile_nodes.data(), tile_node_size);
	   tmp.unmap();
   }

   vulkan.buffer_set.set_storage_buffer(static_cast<unsigned>(Vulkan::RDP::BufferLayout::Color), vulkan.framebuffer);

   if (pass_uses_depth)
   {
	   vulkan.buffer_set.set_storage_buffer(static_cast<unsigned>(Vulkan::RDP::BufferLayout::Depth),
			   vulkan.framebuffer_depth);
   }
   else
   {
	   /* Avoid tripping assertions. */
	   vulkan.buffer_set.set_storage_buffer(static_cast<unsigned>(Vulkan::RDP::BufferLayout::Depth),
			   vulkan.framebuffer);
   }

   /* Upload combiner data. */
   if (!combiner_data.empty())
   {
	   size_t combiner_size = combiner_data.size() * sizeof(BufferCombiner);
	   Buffer tmp = device.request_dynamic_buffer(vulkan.cmd, vulkan.buffer_set, Vulkan::RDP::BufferLayout::Combiners,
			   combiner_size);
	   memcpy(tmp.map(), combiner_data.data(), combiner_size);
	   tmp.unmap();
   }
   else
   {
	   /* Make validators shut up. */
	   device.request_dynamic_buffer(vulkan.cmd, vulkan.buffer_set, Vulkan::RDP::BufferLayout::Combiners, 64);
   }

   if (!tile_descriptors.empty())
   {
	   allocate_tiles();

	   /* Upload tile descriptors. */
	   {
		   size_t tile_descriptor_size = tile_descriptors.size() * sizeof(TileDescriptor);
		   Buffer tmp = device.request_dynamic_buffer(vulkan.cmd, vulkan.buffer_set,
				   Vulkan::RDP::BufferLayout::TileDescriptor, tile_descriptor_size);
		   auto *desc = static_cast<TileDescriptor *>(tmp.map());
		   for (auto &tile : tile_descriptors)
			   *desc++ = tile.desc;
		   tmp.unmap();
	   }

	   /* Upload tile atlas. */
	   auto image = device.create_image_2d_array(VK_FORMAT_R8G8B8A8_UINT, vulkan.tile_map.width,
			   vulkan.tile_map.height, vulkan.tile_map.layers);

	   Buffer staging = device.request_buffer(BufferType::Staging, tile_data.size());
	   memcpy(staging.map(), tile_data.data(), tile_data.size());
	   staging.unmap();

	   if (tile_hw_fbe)
		   vulkan.cmd.prepare_mixed_image(*image);
	   else
		   vulkan.cmd.prepare_image(*image);

	   for (auto &tile : tile_descriptors)
	   {
		   if (tile.hw_fbe)
		   {
			   auto set = device.request_blit_descriptor_set(Vulkan::Blit::DescriptorSetType::Buffers);
			   set.set_storage_buffer(static_cast<unsigned>(Vulkan::Blit::BufferLayout::Color),
					   tile.hw_fbe_info.buffer);
			   set.set_storage_image(static_cast<unsigned>(Vulkan::Blit::BufferLayout::Image), *image);

			   switch (tile.hw_fbe_info.transfer)
			   {
				   case TMEM::TRANSFER_RGBA32:
					   vulkan.cmd.bind_pipeline(device.get_blit_pipeline(Vulkan::Blit::PipelineType::TMEM_RGBA32));
					   break;
				   case TMEM::TRANSFER_RGBA16:
					   vulkan.cmd.bind_pipeline(device.get_blit_pipeline(Vulkan::Blit::PipelineType::TMEM_RGBA16));
					   break;
				   case TMEM::TRANSFER_I8:
					   vulkan.cmd.bind_pipeline(device.get_blit_pipeline(Vulkan::Blit::PipelineType::TMEM_I8));
					   break;
				   case TMEM::TRANSFER_IA8:
					   vulkan.cmd.bind_pipeline(device.get_blit_pipeline(Vulkan::Blit::PipelineType::TMEM_IA8));
					   break;
				   case TMEM::TRANSFER_IA16:
					   vulkan.cmd.bind_pipeline(device.get_blit_pipeline(Vulkan::Blit::PipelineType::TMEM_IA16));
					   break;
				   default:
					   break;
			   }

			   struct PushConstant
			   {
				   uint32_t off[4];
				   uint32_t size[2];
				   uint32_t offset;
				   uint32_t range;
				   uint32_t stride;
			   };

			   const PushConstant push = {
				   { tile.off_x, tile.off_y, tile.off_z, 0u },
				   { tile.width, tile.height },
				   tile.hw_fbe_info.offset_pixels,
				   tile.hw_fbe_info.range_pixels,
				   tile.hw_fbe_info.stride_pixels,
			   };

			   vulkan.cmd.push_constants(&push, sizeof(push));
			   vulkan.cmd.bind_descriptor_set(static_cast<unsigned>(Vulkan::Blit::DescriptorSetType::Buffers), set);
			   vulkan.cmd.dispatch((tile.width + 7) / 8, (tile.height + 7) / 8, 1);
		   }
		   else
		   {
			   vulkan.cmd.copy_to_image(*image, staging, tile.offset, tile.off_x, tile.off_y, tile.off_z, tile.width,
					   tile.height, tile.width);
		   }
	   }

	   if (tile_hw_fbe)
		   vulkan.cmd.complete_mixed_image(*image);
	   else
		   vulkan.cmd.complete_image(*image);

	   /* Image is deleted here,
	    * but it's fine since the deletion is deferred. */
	   vulkan.buffer_set.set_image(static_cast<unsigned>(Vulkan::RDP::BufferLayout::TileAtlas), *image);
   }
   else
   {
	   /* Make validators shut up. */
	   device.request_dynamic_buffer(vulkan.cmd, vulkan.buffer_set, Vulkan::RDP::BufferLayout::TileDescriptor, 64);
	   vulkan.buffer_set.set_image(static_cast<unsigned>(Vulkan::RDP::BufferLayout::TileAtlas), *vulkan.dither_lut);
   }

   /* Work Descriptors. */
   {
#ifdef ENABLE_LOGS
	   fprintf(stderr, "Rendering 8x8 tiles: %u\n", unsigned(work_data.size()));
#endif
	   size_t work_data_size = work_data.size() * sizeof(BufferWorkDescriptor);
	   Buffer tmp = device.request_dynamic_buffer(vulkan.cmd, vulkan.buffer_set,
			   Vulkan::RDP::BufferLayout::WorkDescriptor, work_data_size);

	   memcpy(tmp.map(), work_data.data(), work_data_size);
	   tmp.unmap();
   }

   /* Tile Buffer.
    * Lives solely on device. */
   {
	   size_t tile_buffer_size = sizeof(BufferTile) * tile_count;
	   Buffer tmp = device.request_buffer(BufferType::Device, tile_buffer_size);
	   vulkan.buffer_set.set_storage_buffer(static_cast<unsigned>(Vulkan::RDP::BufferLayout::TileBuffer), tmp);
   }

   vulkan.cmd.end_stream();

   struct PushConstant
   {
	   uint32_t framebuffer[2];
	   float inv_size_tilemap[2];
	   uint32_t tiles_x;
	   int32_t seed;
   };

   PushConstant push = {
	   { framebuffer.allocated_width, framebuffer.allocated_height },
	   { 1.0f / vulkan.tile_map.width, 1.0f / vulkan.tile_map.height },
	   tiles_x,
	   rng_frame_count++,
   };

   /* Rasterize + Varying stage. */
   vulkan.cmd.bind_pipeline(device.get_rdp_pipeline(Vulkan::RDP::PipelineType::Varying));
   vulkan.cmd.push_constants(&push, sizeof(push));

   vulkan.cmd.bind_descriptor_set(static_cast<unsigned>(Vulkan::RDP::DescriptorSetType::LUT), vulkan.lut_set);
   vulkan.cmd.bind_descriptor_set(static_cast<unsigned>(Vulkan::RDP::DescriptorSetType::Buffers), vulkan.buffer_set);

   vulkan.cmd.dispatch(tile_count, 1, 1);
   vulkan.cmd.flush_barrier();

   /* Texture stage. */
   vulkan.cmd.bind_pipeline(device.get_rdp_pipeline(Vulkan::RDP::PipelineType::Texture));
   vulkan.cmd.dispatch(tile_count, 1, 1);
   vulkan.cmd.flush_barrier();

   /* Combiners + Pre-Blender for 2-cycle mode. */
   vulkan.cmd.bind_pipeline(device.get_rdp_pipeline(Vulkan::RDP::PipelineType::Combiner));
   vulkan.cmd.dispatch(tile_count, 1, 1);
   vulkan.cmd.flush_barrier();

   if (depth_is_aliased)
   {
      if (framebuffer.pixel_size != PIXEL_SIZE_16BPP)
         assert(0 && "Trying to alias depth and 16-bit color, but this does not make any sense.");

      vulkan.cmd.bind_pipeline(device.get_rdp_pipeline(Vulkan::RDP::PipelineType::ColorDepthAlias_16bit));
   }
   else
   {
      /* Framebuffer pipeline. */
      switch (framebuffer.pixel_size)
      {
         case 3:
            if (pass_uses_depth)
               vulkan.cmd.bind_pipeline(device.get_rdp_pipeline(Vulkan::RDP::PipelineType::Z_32bit));
	    else
	       vulkan.cmd.bind_pipeline(device.get_rdp_pipeline(Vulkan::RDP::PipelineType::NoZ_32bit));
	    break;
	 case 2:
	    if (pass_uses_depth)
               vulkan.cmd.bind_pipeline(device.get_rdp_pipeline(Vulkan::RDP::PipelineType::Z_16bit));
	    else
               vulkan.cmd.bind_pipeline(device.get_rdp_pipeline(Vulkan::RDP::PipelineType::NoZ_16bit));
	    break;

	 case 1:
	    if (pass_uses_depth)
               vulkan.cmd.bind_pipeline(device.get_rdp_pipeline(Vulkan::RDP::PipelineType::Z_8bit));
	    else
               vulkan.cmd.bind_pipeline(device.get_rdp_pipeline(Vulkan::RDP::PipelineType::NoZ_8bit));
	    break;
      }
   }

   vulkan.cmd.dispatch(tiles_x, tiles_y, 1);
   /* We want to be able to overlap raster in next batch with Z/blend. */

   reset_buffers();
}
}
