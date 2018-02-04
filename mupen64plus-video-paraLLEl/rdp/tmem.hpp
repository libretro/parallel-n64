#ifndef TMEM_HPP
#define TMEM_HPP

#include "common.hpp"
#include <stddef.h>
#include <stdint.h>

namespace RDP
{

#define TMEM_BANKS 4
#define TMEM_WORDS 512
#define TMEM_TILES 8

enum Format
{
   FORMAT_RGBA = 0,
   FORMAT_YUV  = 1,
   FORMAT_CI   = 2,
   FORMAT_IA   = 3,
   FORMAT_I    = 4
};

enum PixelSize
{
   PIXEL_SIZE_4BPP = 0,
   PIXEL_SIZE_8BPP = 1,
   PIXEL_SIZE_16BPP = 2,
   PIXEL_SIZE_32BPP = 3
};

struct TileDescriptor
{
   uint32_t mask_mirror_and[4];
   int32_t clamped[4];

   float offset[2];
   int32_t base[2];

   int32_t shift[2];
   float layer;
   int mid_texel;
};
static_assert(sizeof(TileDescriptor) == 64, "TileDescriptor is not 64 bytes.");

struct Tile
{
   unsigned format;
   unsigned pixel_size;
   unsigned line;
   unsigned tmem;
   unsigned palette;

   int32_t sl, tl, sh, th;

   unsigned size[2];

   // Raw RDP values.
   unsigned mask[2];
   uint32_t shift[2];
   bool mirror_en[2];
   bool clamp_en[2];

   // Pre-compute values that are convenient for shader to work with.
   int32_t clamp[4];
   uint32_t mirror_and[2];
   uint32_t mask_and[2];
};

class TMEM
{
public:
   TMEM();

   void set_enable_tlut(bool state);
   void set_tlut_type(bool type);

   void set_tile(uint32_t w0, uint32_t w1);
   void load_tile(uint32_t w0, uint32_t w1);
   void load_block(uint32_t w0, uint32_t w1);
   void load_tlut(uint32_t w0, uint32_t w1);
   void set_tile_size(uint32_t w0, uint32_t w1);

   uint32_t get_dirty_tiles() const
   {
      return dirty_tiles;
   }

   void invalidate()
   {
      dirty_tiles = (1 << TMEM_TILES) - 1;
   }

   void clear_dirty_tiles(uint32_t mask)
   {
      dirty_tiles &= ~mask;
   }

   void get_effective_size(unsigned index, unsigned *width, unsigned *height) const
   {
      *width = tiles[index].size[0];
      *height = tiles[index].size[1];
   }

   void build_tile_descriptor(unsigned index, TileDescriptor *desc) const;

   void decode_tile(unsigned index, uint8_t *buffer, size_t stride);

   bool tmem_is_framebuffer() const
   {
      return fbe.tmem_is_framebuffer;
   }

   enum Transfer
   {
      TRANSFER_INVALID = 0,
      TRANSFER_I8 = 1,
      TRANSFER_IA8 = 2,
      TRANSFER_RGBA16 = 3,
      TRANSFER_RGBA32 = 4,
      TRANSFER_IA16 = 5
   };

   struct TransferInfo
   {
      Transfer transfer;
      Vulkan::Buffer buffer;
      uint32_t offset_pixels;
      uint32_t stride_pixels;
      uint32_t range_pixels;
   };
   bool get_framebuffer_transfer(unsigned index, TransferInfo *info) const;

   void set_texture_image(uint32_t offset, unsigned format, unsigned pixel_size, unsigned width);
   uint32_t get_texture_image_offset() const
   {
      return texture.offset;
   }

   void set_rdram(uint8_t *base, size_t size)
   {
      rdram.base = base;
      rdram.size = size;
   }

   void set_async_framebuffers(const std::vector<AsyncFramebuffer> *framebuffers)
   {
      async_framebuffers = framebuffers;
   }

   const Tile &get_tile(unsigned i) const
   {
      return tiles[i];
   }

private:
   uint16_t tmem[TMEM_WORDS][TMEM_BANKS];
   Tile tiles[TMEM_TILES];
   uint32_t dirty_tiles = (1 << TMEM_TILES) - 1;
   const std::vector<AsyncFramebuffer> *async_framebuffers = nullptr;

   bool enable_tlut = false;
   bool tlut_type = false;

   void update_tile_info(Tile &tile);

   struct
   {
      uint32_t offset = 0;
      unsigned format = 0;
      unsigned pixel_size = 0;
      unsigned width = 0;
   } texture;

   struct
   {
      // TMEM is currently assumed to alias with a framebuffer.
      // Used for HWFBE effects.
      bool tmem_is_framebuffer = false;

      // The offset into the buffer which was loaded into TMEM.
      uint32_t offset_pixels = 0;

      // The TMEM addr that the framebuffer was loaded into.
      uint32_t tmem_base = 0;

      Framebuffer hardware_framebuffer;
      Vulkan::Buffer color_buffer;
   } fbe;

   struct
   {
      uint8_t *base = nullptr;
      size_t size = 0;
   } rdram;

   void read_rgba32(uint8_t *buffer, unsigned word, unsigned x, unsigned y, unsigned line, unsigned palette);
   void read_rgba16(uint8_t *buffer, unsigned word, unsigned x, unsigned y, unsigned line, unsigned palette);
   void read_ia16(uint8_t *buffer, unsigned word, unsigned x, unsigned y, unsigned line, unsigned palette);
   void read_ia8(uint8_t *buffer, unsigned word, unsigned x, unsigned y, unsigned line, unsigned palette);
   void read_ia4(uint8_t *buffer, unsigned word, unsigned x, unsigned y, unsigned line, unsigned palette);
   void read_i16(uint8_t *buffer, unsigned word, unsigned x, unsigned y, unsigned line, unsigned palette);
   void read_i8(uint8_t *buffer, unsigned word, unsigned x, unsigned y, unsigned line, unsigned palette);
   void read_i4(uint8_t *buffer, unsigned word, unsigned x, unsigned y, unsigned line, unsigned palette);
   void read_ci16(uint8_t *buffer, unsigned word, unsigned x, unsigned y, unsigned line, unsigned palette);
   void read_ci8(uint8_t *buffer, unsigned word, unsigned x, unsigned y, unsigned line, unsigned palette);
   void read_ci4(uint8_t *buffer, unsigned word, unsigned x, unsigned y, unsigned line, unsigned palette);

   bool load_tile_framebuffer(const Tile &tile, uint32_t sl, uint32_t tl);
};

class TileAtlasAllocator
{
public:
   void reset();
   void add_size(unsigned width, unsigned height);
   void begin_allocator();
   void allocate(unsigned *x, unsigned *y, unsigned *layer, unsigned width, unsigned height);
   void end_allocator();

   void get_atlas_size(unsigned *width, unsigned *height, unsigned *layers)
   {
      *width = max_width;
      *height = max_height;
      *layers = num_layers;
   }

private:
   unsigned max_width = 0;
   unsigned max_height = 0;

   unsigned current_x = 0;
   unsigned current_y = 0;
   unsigned max_height_in_stripe = 0;
   unsigned current_layer = 0;
   unsigned num_layers = 0;
};
}
#endif
