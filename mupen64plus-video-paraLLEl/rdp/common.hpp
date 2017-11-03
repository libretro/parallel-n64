#ifndef RDP_COMMON_HPP
#define RDP_COMMON_HPP

#include "vulkan_util.hpp"
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

/* Little endian only. */
#define U16_FLIP 	2
#define U8_FLIP 	3

/* This should probably depend on whether or not we have expansion slot installed, but AFAIK
 * mupen doesn't expose this information, so just always assume 8 MiB. */
#define RDRAM_SIZE	0x800000
#define RDRAM_MASK_8	(RDRAM_SIZE - 1)
#define RDRAM_MASK_16	(RDRAM_SIZE - 2)
#define RDRAM_MASK_32	(RDRAM_SIZE - 4)

#define WRAP_ADDR(addr) 			((addr)&RDRAM_MASK_8)
#define READ_DRAM_U32(base, addr) 		(*reinterpret_cast<const uint32_t *>(base + ((addr)&RDRAM_MASK_32)))
#define READ_DRAM_U32_NOWRAP(base, addr) 	(*reinterpret_cast<const uint32_t *>(base + (addr)))
#define READ_DRAM_U16(base, addr) 		(*reinterpret_cast<const uint16_t *>(base + (((addr) ^ U16_FLIP) & RDRAM_MASK_16)))
#define READ_DRAM_U16_NOWRAP(base, addr) 	(*reinterpret_cast<const uint16_t *>(base + ((addr) ^ U16_FLIP)))
#define READ_DRAM_U8(base, addr) 		(*reinterpret_cast<const uint8_t *>(base + (((addr) ^ U8_FLIP) & RDRAM_MASK_8)))
#define READ_DRAM_U8_NOWRAP(base, addr) 	(*reinterpret_cast<const uint8_t *>(base + ((addr) ^ U8_FLIP)))

#define WRITE_DRAM_U32(base, addr, v) 		((*reinterpret_cast<uint32_t *>(base + ((addr)&RDRAM_MASK_8))) = v)
#define WRITE_DRAM_U32_NOWRAP(base, addr, v) 	((*reinterpret_cast<uint32_t *>(base + (addr))) = v)
#define WRITE_DRAM_U16(base, addr, v) 		((*reinterpret_cast<uint16_t *>(base + (((addr) ^ U16_FLIP) & RDRAM_MASK_16))) = v)
#define WRITE_DRAM_U16_NOWRAP(base, addr, v) 	((*reinterpret_cast<uint16_t *>(base + ((addr) ^ U16_FLIP))) = v)
#define WRITE_DRAM_U8(base, addr, v) 		((*reinterpret_cast<uint8_t *>(base + (((addr) ^ U8_FLIP) & RDRAM_MASK_8))) = v)
#define WRITE_DRAM_U8_NOWRAP(base, addr, v) 	((*reinterpret_cast<uint8_t *>(base + (((addr) ^ U8_FLIP)))) = v)

static inline uint64_t hash64(const void *data_, size_t size)
{
   /* FNV-1. */
   size_t i;
   const uint8_t *data = static_cast<const uint8_t *>(data_);
   uint64_t h = 0xcbf29ce484222325ull;
   for (i = 0; i < size; i++)
      h = (h * 0x100000001b3ull) ^ data[i];
   return h;
}

static inline uint32_t next_pow2(uint32_t v)
{
   v--;
   v |= v >> 16;
   v |= v >> 8;
   v |= v >> 4;
   v |= v >> 2;
   v |= v >> 1;
   return v + 1;
}

#if defined(_MSC_VER)
#define CLOCK_MONOTONIC 0

#if _MSC_VER < 1910
struct timespec { long tv_sec; long tv_nsec; };    //header part
#else
#define NOMINMAX
#endif

#include <Windows.h>

int clock_gettime(int, struct timespec *spec)      //C-file part
{  __int64 wintime; GetSystemTimeAsFileTime((FILETIME*)&wintime);
   wintime      -=116444736000000000i64;  //1jan1601 to 1jan1970
   spec->tv_sec  =wintime / 10000000i64;           //seconds
   spec->tv_nsec =wintime % 10000000i64 *100;      //nano-seconds
   return 0;
}
#endif

static inline double gettime()
{
   timespec tv;
   clock_gettime(CLOCK_MONOTONIC, &tv);
   return tv.tv_sec + 1e-9 * tv.tv_nsec;
}

namespace RDP
{
enum CycleType
{
   CYCLE_TYPE_1    = 0,
   CYCLE_TYPE_2    = 1,
   CYCLE_TYPE_COPY = 2,
   CYCLE_TYPE_FILL = 3
};

enum FramebufferState
{
   /* The color or depth image is up-to-date in RDRAM, the GPU does not need to see our RDRAM buffer yet. */
   FRAMEBUFFER_CPU = 0,
   /* The color or depth image has registered an access to RDRAM, we need to copy RDRAM over to GPU here. */
   FRAMEBUFFER_STALE_GPU = 1,
   /* The color or depth image has been written to on GPU and needs to be synced back to RDRAM. */
   FRAMEBUFFER_GPU = 2
};

struct Framebuffer
{
   uint32_t addr = 0;
   uint32_t depth_addr = 0;
   unsigned width = 0;
   unsigned format = 0;
   unsigned pixel_size = 0;

   unsigned allocated_width = 0;
   unsigned allocated_height = 0;

   FramebufferState color_state = FRAMEBUFFER_CPU;
   FramebufferState depth_state = FRAMEBUFFER_CPU;

   inline uint32_t bytes_to_pixels(uint32_t bytes) const
   {
      assert(pixel_size > 0);
      return bytes >> (pixel_size - 1);
   }

   inline uint32_t color_size() const
   {
      if (color_state != FRAMEBUFFER_CPU)
      {
         uint32_t size = allocated_width * allocated_height;
	 assert(pixel_size > 0);
	 return size << (pixel_size - 1);
      }
      return 0;
   }

   inline uint32_t depth_size() const
   {
      if (depth_state != FRAMEBUFFER_CPU)
      {
         uint32_t size = allocated_width * allocated_height;
	 return size << 1;
      }
      return 0;
   }
};

struct AsyncFramebuffer
{
   Framebuffer framebuffer;
   Vulkan::Buffer color_buffer;
   Vulkan::Buffer depth_buffer;
   Vulkan::Fence fence;
   unsigned sync_index;
};
}

#endif
