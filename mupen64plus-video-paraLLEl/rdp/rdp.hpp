#ifndef RDP_HPP
#define RDP_HPP

#include "common.hpp"
#include "tmem.hpp"
#include "vulkan_util.hpp"
#include <stdint.h>
#include <unordered_map>
#include <cstring>
#include <cstdio>
#include <vector>

#define RDP_MAX_PRIMITIVES 	1024
#define RDP_MAX_PRIMITIVES_LOG2 12
#define RDP_MAX_COMBINERS 	64
#define TILE_SIZE_X 		8
#define TILE_SIZE_Y 		8

#define RDP_FLAG_FLIP		      (1 << 0)
#define RDP_FLAG_DO_OFFSET	      (1 << 1)
#define RDP_FLAG_SAMPLE_TEX 	      (1 << 2)
#define RDP_FLAG_FORCE_BLEND 	      (1 << 3)
#define RDP_FLAG_AA_ENABLE 	      (1 << 4)
#define RDP_FLAG_SHADE 		      (1 << 5)
#define RDP_FLAG_INTERPOLATE_Z        (1 << 6)
#define RDP_FLAG_Z_UPDATE 	      (1 << 7)
#define RDP_FLAG_Z_COMPARE            (1 << 8)
#define RDP_FLAG_PERSPECTIVE          (1 << 9)
#define RDP_FLAG_BILERP0              (1 << 10)
#define RDP_FLAG_BILERP1              (1 << 11)
#define RDP_FLAG_ALPHATEST            (1 << 12)
#define RDP_FLAG_COLOR_ON_CVG         (1 << 13)
#define RDP_FLAG_CVG_TIMES_ALPHA      (1 << 14)
#define RDP_FLAG_ALPHA_CVG_SELECT     (1 << 15)
#define RDP_FLAG_ALPHA_NOISE          (1 << 16)
#define RDP_FLAG_SAMPLE_TEX_PIPELINED (1 << 17)
#define RDP_FLAG_SAMPLE_TEX_LOD       (1 << 18)

#define RDP_FLAG_CVG_MODE_SHIFT       20
#define RDP_FLAG_TILE_SHIFT           22
#define RDP_FLAG_LEVELS_SHIFT         25
#define RDP_FLAG_CYCLE_TYPE_SHIFT     30
#define RDP_FLAG_Z_MODE_SHIFT         28

#define BLENDMODE_FLAG_DITHER_SEL     16

#define LOD_INFO_PRIMITIVE_DETAIL        (1 << 16)
#define LOD_INFO_PRIMITIVE_SHARPEN       (1 << 17)
#define LOD_INFO_PRIMITIVE_MIN_LOD_SHIFT 18
#define LOD_INFO_PRIMITIVE_MIN_LOD_MASK  (31 << 18)

#define SEXT(x, bits) (int32_t((x) << (32 - (bits))) >> (32 - (bits)))

namespace RDP
{
struct Primitive
{
   int32_t xl, xm, xh;
   int32_t yl, ym, yh;
   int32_t DxLDy, DxMDy, DxHDy;
   uint32_t flags;
};

struct Attribute
{
   int32_t rgba[4];
   int32_t d_rgba_dx[4];
   int32_t d_rgba_de[4];
   int32_t d_rgba_dy[4];

   int32_t stwz[4];
   int32_t d_stwz_dx[4];
   int32_t d_stwz_de[4];
   int32_t d_stwz_dy[4];
};

class Renderer
{
public:
	Renderer(Vulkan::Device &device);

	void set_scissor(int xh, int yh, int xl, int yl);

	void set_fill_color(uint32_t color)
	{
		state.fill_color = color;
	}

	void set_blend_word(uint32_t word, uint32_t dither_sel)
	{
		state.blend_word = word | (dither_sel << BLENDMODE_FLAG_DITHER_SEL);
	}

	void set_key_r(uint32_t w1, uint32_t w2);
	void set_key_gb(uint32_t w1, uint32_t w2);
	void set_dithering(unsigned type);
	void set_combine(uint32_t w1, uint32_t w2);
	void set_prim_color(uint32_t w1, uint32_t w2);
	void set_env_color(uint32_t w2);
	void set_fog_color(uint32_t w2);
	void set_convert(uint32_t w1, uint32_t w2);
	void set_primitive_z(uint32_t w2);
	void set_lod_modes(bool detail, bool sharpen);

	bool combiner_reads_secondary_tile(unsigned cycle) const;
	bool combiner_reads_pipelined_tile() const;
	bool combiner_reads_tile(CycleType type) const;

	void log_combiner() const;

	void set_blend_color(uint32_t color)
	{
		state.blend_color = color;
	}

	// Either sync_full or async_full must be called in a frame before ending it to
	// ensure that all pending command buffer have been submitted.
	// sync_full is typically called when CPU needs to invalidate GPU memory.
	// Ensures that RDRAM sees a coherent view of memory written by the GPU.
	// Must be called if emulator accesses RDRAM which is covered by color buffer or depth buffer.
	void sync_full();

	// Flush out all outstanding work, but don't sync back to CPU yet.
	void complete_frame();

	// Draw RDP primitive with bounding box.
	void draw_primitive(const Primitive &tri, const Attribute *attr, uint32_t tile_mask, int min_x, int max_x,
	                    int min_y, int max_y);

	// Optimization for clear colors.
	// If our framebuffers are on CPU (start of frame), it's kinda pointless to sync to GPU and back.
	void fill_rect_cpu(int min_x, int max_x, int min_y, int max_y);

	void set_color_image(uint32_t addr, unsigned format, unsigned pixel_size, unsigned width);
	void set_depth_image(uint32_t addr);

	void set_rdram(uint8_t *dram, size_t size)
	{
		rdram.base = dram;
		rdram.size = size;
		tmem.set_rdram(dram, size);
	}

   void set_scissor_variables(const char *name)
   {
      fprintf(stderr, "ROM name: %s\n", name);

      width_greater_than            = 320;
      width_greater_than_max_height = 480;

      /*
       * We need to detect the following games by 
       * CRC because the names are non-ASCII:
       *
       * Wrestlemania 2000 (Japan)
       */

      if (
            !strcmp(name, "GOLDENEYE")             ||
            !strcmp(name, "ALL STAR TENNIS '99")   ||
            !strcmp(name, "All-Star Baseball 99")  ||
            !strcmp(name, "All-Star Baseball '0")  ||
            !strcmp(name, "All-Star Baseball 20")  ||
            !strcmp(name, "Armorines Project S.")  ||
            !strcmp(name, "Banjo-Kazooie")         ||
            !strcmp(name, "BANJO KAZOOIE 2")       ||
            !strcmp(name, "BANJO TOOIE")           ||
            !strcmp(name, "BATTLEZONE")            ||
            !strcmp(name, "Command&Conquer")       ||
            !strcmp(name, "CONKER BFD")            ||
            !strcmp(name, "CBFD ECTS")             ||
            !strcmp(name, "CyberTiger")            ||
            !strcmp(name, "Donald Duck Goin' Qu")  ||
            !strcmp(name, "Donald Duck Quack At")  ||
            !strcmp(name, "DUKE NUKEM ZERO HOUR")  ||
            !strcmp(name, "ECW Hardcore Revolut")  ||
            !strcmp(name, "EXCITEBIKE64")          ||
            !strcmp(name, "F1RacingChampionship")  ||
            !strcmp(name, "FIFA: RTWC 98")         ||
            !strcmp(name, "RoadToWorldCup98")      ||
            !strcmp(name, "FIFA 99")               ||
            !strcmp(name, "Fox Sports Hoops 99")   ||
            !strcmp(name, "GAUNTLET LEGENDS")      ||
            !strcmp(name, "Indiana Jones")         ||
            !strcmp(name, "Jeremy McGrath Super")  ||
            !strcmp(name, "KEN GRIFFEY SLUGFEST")  ||
            !strcmp(name, "Knockout Kings 2000")   ||
            !strcmp(name, "NBA COURTSIDE")         ||
            !strcmp(name, "LEGORacers")            ||
            !strcmp(name, "Madden NFL 2000")       ||
            !strcmp(name, "Madden NFL 2001")       ||
            !strcmp(name, "Madden NFL 2002")       ||
            !strcmp(name, "MLB FEATURING K G JR")  ||
            !strcmp(name, "MarioTennis")           ||
            !strcmp(name, "MarioTennis64")         ||
            !strcmp(name, "Monaco GP Racing 2")    ||
            !strcmp(name, "MORTAL KOMBAT 4")       ||
            !strcmp(name, "NBA Courtside 2")       ||
            !strcmp(name, "NBA JAM 2000")          ||
            !strcmp(name, "NBA JAM 99")            ||
            !strcmp(name, "NBA LIVE 2000")         ||
            !strcmp(name, "NBA Live 99")           ||
            !strcmp(name, "NBA SHOWTIME")          ||
            !strcmp(name, "NEWTETRIS")             ||
            !strcmp(name, "NFL BLITZ")             ||
            !strcmp(name, "NFL BLITZ SPECIAL ED")  ||
            !strcmp(name, "blitz2k")               ||
            !strcmp(name, "NFL BLITZ 2001")        ||
            !strcmp(name, "NFL Quarterback Club")  ||
            !strcmp(name, "NFL QBC 2000")          ||
            !strcmp(name, "NHL 99")                ||
            !strcmp(name, "NHL_BREAKAWAY_98")      ||
            !strcmp(name, "NHL Breakaway '99")     ||
            !strcmp(name, "NIGHTMARE CREATURES")   ||
            !strcmp(name, "NUCLEARSTRIKE64")       ||
            !strcmp(name, "Perfect Dark")          ||
            !strcmp(name, "Premier Manager 64")    ||
            !strcmp(name, "Racing Simulation 2")   ||
            !strcmp(name, "RAMPAGE")               ||
            !strcmp(name, "RAMPAGE2")              ||
            !strcmp(name, "RAT ATTACK")            ||
            !strcmp(name, "Rayman 2")              ||
            !strcmp(name, "RUGRATSTREASUREHUNT")   ||
            !strcmp(name, "ROADSTERS TROPHY")      ||
            !strcmp(name, "Rush 2049")             ||
            !strcmp(name, "SCOOBY-DOO")            ||
            !strcmp(name, "South Park")            ||
            !strcmp(name, "Rogue Squadron")        ||
            !strcmp(name, "rogue squadron")        ||
            !strcmp(name, "Battle for Naboo")      ||
            !strcmp(name, "STAR WARS EP1 RACER")   ||
            !strcmp(name, "SUPERROBOTSPIRITS")     ||
            !strcmp(name, "Supercross")            ||
            !strcmp(name, "TARZAN")                ||
            !strcmp(name, "Taz Express")           ||
            !strcmp(name, "TG RALLY 2")            ||
            !strcmp(name, "Tigger's Honey Hunt")   ||
            !strcmp(name, "TRIPLE PLAY 2000")      ||
            !strcmp(name, "Turok: Rage Wars")      ||
            !strcmp(name, "Turok 2: Seeds of Ev")  ||
            !strcmp(name, "Turok 3: Shadow of O")  ||
            !strcmp(name, "VIOLENCEKILLER")        ||
            !strcmp(name, "WCW BACKSTAGE")         ||
            !strcmp(name, "WCW MAYHEM")            ||
            !strcmp(name, "WCWvs.NWO:World Tour")  ||
            !strcmp(name, "WCW / nWo  REVENGE")    ||
            !strcmp(name, "World Cup 98")          ||
            !strcmp(name, "WWF: Attitude")         ||
            !strcmp(name, "WWF No Mercy")          ||
            !strcmp(name, "WWF War Zone")          ||
            !strcmp(name, "WRESTLEMANIA 2000")     ||
            !strcmp(name, "PLACEHOLDER")
            )
      {
         width_greater_than            = 280;
         width_greater_than_max_height = 200;
      }
#if 0
      /* We need to come up with some proper heuristics
       * for Resident Evil 2 - see NOTE below -
       * Without the expansion pak, the resolution ran at 320x240p, 
       * same as the PS1. However, with the expansion, the resolution 
       * adjusted based on CPU load and outputted a maximum 
       * resolution of 512x384.
       * */
      else if
         (
          !strcmp(name, "BioHazard II")      ||
          !strcmp(name, "Resident Evil II")
         )
         {
            width_greater_than            = 280;
            width_greater_than_max_height = 384;
         }
#endif
#if 0
      /* These games require separate tweaking still
       *
       * Bottom of the 9th (Bottom of the 9th)
       * Resident Evil 2/Biohazard (BioHazard II) (Resident Evil II) -
       * Army Men: Sarge's Heroes 2 (ARMYMEN SARGE 2)
       * Brunswick Circuit Pro Bowling (BRUNSWICKBOWLING)
       * Lode Runner 3D 
       * Mario no Photopie
       * NBA In The Zone 2000
       * NFL Quarterback Club (mostly correct but not yet perfect)
       * NFL Quarterback Club 2000 (mostly correct but not yet perfect)
       * Perfect Dark (mostly correct but weapon selection screen can make
       * screen shift around)
       * Top Gear Rally (might need better heuristics )
       * PD Ultraman Battle Collection 64 ("Ultraman Battle JAPA") (flickers)
       * Supercross 2000 (screen can shift and pan around badly on title screen)
       * Vigilante 8
       */
#endif
   }

	void set_synchronous(bool enable)
	{
		// Make sure we don't end up with a situation where we inadvertently write old data to RDRAM.
		if (enable && !synchronous)
			async_transfers.clear();

		synchronous = enable;
	}

	struct VIOutput
	{
		Framebuffer framebuffer;
		Vulkan::ImageHandle image;
	};

	const Framebuffer &get_current_framebuffer() const
	{
		return framebuffer;
	}

	std::vector<VIOutput> &get_vi_outputs()
	{
		return vi_outputs;
	}

	void check_tmem_feedback();

	TMEM &get_tmem()
	{
		return tmem;
	}

	void begin_index(unsigned index);

private:
	Vulkan::Device &device;

	std::vector<AsyncFramebuffer> async_transfers;
	std::vector<VIOutput> vi_outputs;
	void sync_framebuffer_to_cpu(AsyncFramebuffer &async);
	unsigned current_sync_index = 0;
	unsigned rdp_dithering;
   unsigned width_greater_than            = 320;
   unsigned width_greater_than_max_height = 480;

	struct
	{
		Vulkan::CommandBuffer cmd = {};
		Vulkan::DescriptorSet lut_set;
		Vulkan::DescriptorSet buffer_set;

		Vulkan::ImageHandle dither_lut;
		Vulkan::ImageHandle centroid_lut;
		Vulkan::Buffer z_lut;

		Vulkan::Buffer framebuffer = {};
		Vulkan::Buffer framebuffer_depth = {};

		struct
		{
			unsigned width, height, layers;
		} tile_map;
	} vulkan;

	void begin_command_buffer();
	void begin_framebuffer();
	void begin_framebuffer_depth();
	Vulkan::Fence submit(const Vulkan::Semaphore *semaphore = nullptr);

	struct BufferWorkDescriptor
	{
		uint32_t base[2];
		uint32_t primitive;
		uint32_t padding;
	};
	static_assert(sizeof(BufferWorkDescriptor) == 16, "Work Descriptor must be 16 bytes.");

	// SoA for improved cache behavior.
	struct BufferTile
	{
		uint32_t uv[TILE_SIZE_X * TILE_SIZE_Y];
		uint32_t uv_next[TILE_SIZE_X * TILE_SIZE_Y];
		uint32_t uv_prev_y[TILE_SIZE_X * TILE_SIZE_Y];
		int32_t z[TILE_SIZE_X * TILE_SIZE_Y];
		uint32_t rgba[TILE_SIZE_X * TILE_SIZE_Y];
		uint32_t coverage[TILE_SIZE_X * TILE_SIZE_Y];
	};
	static const unsigned FlushBufferTileCount = Vulkan::MaxBlockSize / (2 * sizeof(BufferTile));

	struct BufferPrimitiveAttribute
	{
		int32_t rgba[4];
		int32_t d_rgba_dx[4];
		int32_t d_rgba_de[4];
		int32_t d_rgba_dy[4];

		int32_t stwz[4];
		int32_t d_stwz_dx[4];
		int32_t d_stwz_de[4];
		int32_t d_stwz_dy[4];

		int32_t d_rgba_diff[4];
		int32_t d_stwz_diff[4];
		uint32_t tile_descriptors[8];
	};

	struct BufferPrimitive
	{
		int32_t xl, xm, xh;
		uint32_t primitive_z;
		int32_t yl, ym, yh;
		uint32_t span_stride_combiner;
		int32_t DxLDy, DxMDy, DxHDy;
		uint32_t blend_color;
		uint32_t flags, scissor_x, scissor_y, fill_color_blend;
		// scissor filled in by Renderer.
		// fill_color filled in by Renderer.

		BufferPrimitiveAttribute attr;
	};
	static_assert(sizeof(BufferPrimitive) == 256, "Primitive struct is not 256 bytes.");

	struct Combiner
	{
		uint32_t sub_a, sub_b, mul, add;
	};

	struct CombinerBaked
	{
		int32_t sub_a[4];
		int32_t sub_b[4];
		int32_t mul[4];
		int32_t add[4];
	};

	// Figure out better way to pack this.
	struct BufferCombiner
	{
		CombinerBaked cycle[2];
		Combiner color[2];
		Combiner alpha[2];
		int32_t padding[16];
	};
	static_assert(sizeof(BufferCombiner) == 256, "BufferCombiner is not 256 bytes.");

	struct TileInfo
	{
		uint32_t head = -1u;
		uint32_t tail = -1u;
	};

	struct TileNode
	{
		TileNode(uint32_t primitive, uint32_t next)
		    : primitive(primitive)
		    , next(next)
		{
		}
		TileNode() = default;

		uint32_t primitive = -1u;
		uint32_t next = -1u;
	};
	static_assert(sizeof(TileNode) == 8, "TileListHeader struct is not 8 bytes.");

	std::vector<BufferPrimitive> primitive_data;
	std::vector<BufferCombiner> combiner_data;
	std::vector<TileInfo> tile_lists;
	std::vector<TileNode> tile_nodes;
	std::vector<BufferWorkDescriptor> work_data;
	uint32_t tile_count = 0;

	std::vector<uint8_t> tile_data;
	struct Tile
	{
		TileDescriptor desc;
		size_t offset;

		unsigned width, height;
		unsigned off_x, off_y, off_z;

		TMEM::TransferInfo hw_fbe_info;
		bool hw_fbe;
	};
	std::vector<Tile> tile_descriptors;
	bool tile_hw_fbe = false;
	void allocate_tiles();
	void update_tiles(uint32_t tile_mask);
	void append_tile_list(TileInfo &tile_info, unsigned prim, unsigned tile);

	unsigned tiles_x = 0, tiles_y = 0;

	void flush_tile_lists();

	struct
	{
		int xh, yh, xl, yl;
	} scissor;

	struct
	{
		uint32_t fill_color = 0;
		uint32_t blend_word = 0;
		uint32_t blend_color = 0;

		uint32_t fog_color = 0;
		uint32_t prim_color = 0;
		uint32_t env_color = 0;
		uint32_t prim_lod_frac = 0;
		int32_t k0_tf = 0;
		int32_t k1_tf = 0;
		int32_t k2_tf = 0;
		int32_t k3_tf = 0;
		uint32_t k4 = 0;
		uint32_t k5 = 0;

      uint32_t key_scale_r = 0;
      uint32_t key_scale_g = 0;
      uint32_t key_scale_b = 0;

      uint32_t key_center_r = 0;
      uint32_t key_center_g = 0;
      uint32_t key_center_b = 0;

		uint32_t primitive_z = 0;

		BufferCombiner combiners = {};
		bool combiners_dirty = true;
		bool combiner_reads_secondary_tile[2] = {};
		bool combiner_reads_tile[2] = {};
		bool combiner_reads_pipelined_tile = false;

		std::unordered_map<size_t, unsigned> combiner_map;
		unsigned last_combiner = 0;
		uint32_t lod_flags = 0;
	} state;

	struct RDRAM
	{
		RDRAM()
		{
			shadow_base.resize(RDRAM_SIZE);
			hidden_bits.resize(RDRAM_SIZE);
		}

		uint8_t *base = nullptr;
		size_t size = 0;
		std::vector<uint8_t> shadow_base;
		std::vector<uint8_t> hidden_bits;
	} rdram;

	Framebuffer framebuffer;

	void set_framebuffer_size(unsigned width, unsigned height);
	void sync_gpu_to_dram(bool blocking);
	void sync_gpu_to_vi(Vulkan::CommandBuffer &cmd);
	void sync_color_dram_to_gpu();
	void sync_depth_dram_to_gpu();

	TMEM tmem;
	uint32_t tile_instances[TMEM_TILES] = {};

	void reset_buffers();

	uint64_t update_static_combiner();
	void init_dither_lut();
	void init_centroid_lut();
	void init_z_lut();
	void clip_scissor(int &xmin, int &xmax, int &ymin, int &ymax);

	// Just a counter to seed the RNG with.
	int32_t rng_frame_count = 0;

	bool coarse_conservative_raster(int x, int y, int min_x, int max_x, int min_y, int max_y,
	                                const Primitive &prim) const;
	uint64_t raster_tile_count = 0;
	uint64_t reject_tile_count = 0;
	bool synchronous = false;

	uint8_t *framebuffer_data()
	{
		return synchronous ? rdram.base : rdram.shadow_base.data();
	}
};
}

#endif
