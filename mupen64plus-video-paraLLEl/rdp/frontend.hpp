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
	unsigned cvg_mode;
	bool force_blend;
	bool aa_enable;
	bool z_source_sel;
	bool z_update;
	bool z_compare;
	bool perspective;
	bool bilerp0;
	bool bilerp1;
	bool alphatest;
	bool alpha_noise;
	bool color_on_cvg;
	bool cvg_times_alpha;
	bool alpha_cvg_select;
	bool sample_type;
	bool lod_enable;
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
