#ifndef RDP_FRONTEND_HPP
#define RDP_FRONTEND_HPP

#include "rdp.hpp"
#include "vulkan.hpp"
#include <stddef.h>
#include <stdint.h>

namespace RDP
{

struct OtherModes
{
   unsigned cycle_type;
   unsigned z_mode;
   unsigned cvg_dest;
   unsigned tlut_type;
   unsigned convert_one;
   unsigned rgb_dither_sel;
   unsigned alpha_dither_sel;
   bool force_blend;
   bool image_read_en;
   bool antialias_en;
   bool z_source_sel;
   bool z_update_en;
   bool z_compare_en;
   bool persp_tex_en;
   bool bi_lerp0;
   bool bi_lerp1;
   bool key_en;
   bool mid_texel;
   bool alpha_compare_en;
   bool dither_alpha_en;
   bool color_on_cvg;
   bool cvg_times_alpha;
   bool alpha_cvg_select;
   bool en_tlut;
   bool sample_type;
   bool tex_lod_en;
   bool sharpen_tex_en;
   bool detail_tex_en;
};

class Frontend
{
public:
   void set_renderer(Renderer *renderer, Vulkan::Device *device)
   {
      this->renderer = renderer;
      this->device = device;
      device->begin_index(0);
   }

   void command(uint32_t cmd, const uint32_t *args);

   void reset_draw_count()
   {
      draw_count = 0;
   }

   unsigned get_draw_count() const
   {
      return draw_count;
   }

   bool trace = false;

private:
   typedef void (Frontend::*rdp_func_t)(const uint32_t *args);
   static const rdp_func_t rdp_commands[64];

   Renderer *renderer = nullptr;
   Vulkan::Device *device = nullptr;
   OtherModes other_modes = {};
   unsigned draw_count = 0;

   void tri_fill_tex_coeffs(Attribute *attr, const uint32_t *args);
   void tri_fill_z_coeffs(Attribute *attr, const uint32_t *args);
   void tri_fill_shade_coeffs(Attribute *attr, const uint32_t *args);
   void tri_fill_coeffs(Primitive *prim, const uint32_t *args, int32_t *xmin, int32_t *xmax);
   void tri_fill_flags_common(Primitive *prim);
   void tri_fill_tile(Primitive *prim, uint32_t *tile_mask, uint32_t mips, uint32_t tile);
   void tri_fill_novarying_tile(Primitive *prim, Attribute *attr, uint32_t *tile_mask);
   void tri_fill_flags(Primitive *prim, const uint32_t *args, uint32_t *tile_mask, bool zbuffer, bool shade);
   void tri_noshade(const uint32_t *args);
   void tri_noshade_z(const uint32_t *args);
   void tri_tex(const uint32_t *args);
   void tri_tex_z(const uint32_t *args);
   void tri_shade(const uint32_t *args);
   void tri_shade_z(const uint32_t *args);
   void tri_shade_tex(const uint32_t *args);
   void tri_shade_tex_z(const uint32_t *args);
   void tex_rect_common(const uint32_t *args, bool flipped);
   void tex_rect(const uint32_t *args);
   void tex_rect_flip(const uint32_t *args);
   void fill_rect(const uint32_t *args);
   void set_other_modes(const uint32_t *args);
   void set_combine(const uint32_t *args);
   void set_key_r(const uint32_t *args);
   void set_key_gb(const uint32_t *args);
   void set_convert(const uint32_t *args);
   void set_scissor(const uint32_t *args);
   void set_prim_depth(const uint32_t *args);
   void set_fill_color(const uint32_t *args);
   void set_blend_color(const uint32_t *args);
   void set_prim_color(const uint32_t *args);
   void set_env_color(const uint32_t *args);
   void set_fog_color(const uint32_t *args);
   void load_tile(const uint32_t *args);
   void load_block(const uint32_t *args);
   void load_tlut(const uint32_t *args);
   void set_tile(const uint32_t *args);
   void set_texture_image(const uint32_t *args);
   void set_color_image(const uint32_t *args);
   void set_mask_image(const uint32_t *args);
   void set_tile_size(const uint32_t *args);
   void sync_full(const uint32_t *);
   void sync_load(const uint32_t *);
   void sync_pipe(const uint32_t *);
   void sync_tile(const uint32_t *);
   void noop(const uint32_t *);
   void invalid(const uint32_t *);
};
}

#endif
