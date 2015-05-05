
#include "gdp.h"

struct gdp_global g_gdp;

void gdp_set_prim_color(uint32_t w0, uint32_t w1)
{
   g_gdp.prim_color.total   =  w1;
   g_gdp.prim_color.r       = (w1 & 0xFF000000) >> 24;
   g_gdp.prim_color.g       = (w1 & 0x00FF0000) >> 16;
   g_gdp.prim_color.b       = (w1 & 0x0000FF00) >>  8;
   g_gdp.prim_color.a       = (w1 & 0x000000FF) >>  0;
   g_gdp.primitive_lod_frac = (w0 & 0x000000FF) >> (32-32);
   g_gdp.primitive_lod_min  = (w0 & 0x00001F00) >> (40-32);
}

void gdp_set_env_color(uint32_t w1)
{
   g_gdp.env_color.total =  w1;
   g_gdp.env_color.r       = (w1 & 0xFF000000) >> 24;
   g_gdp.env_color.g       = (w1 & 0x00FF0000) >> 16;
   g_gdp.env_color.b       = (w1 & 0x0000FF00) >>  8;
   g_gdp.env_color.a       = (w1 & 0x000000FF) >>  0;
}

void gdp_set_prim_depth(uint32_t w1)
{
   g_gdp.prim_color.z     = (w1 & 0xFFFF0000) >> 16;
   g_gdp.prim_color.dz    = (w1 & 0x0000FFFF) >>  0;
}

void gdp_set_fog_color(uint32_t w1)
{
   g_gdp.fog_color.total =  w1;
   g_gdp.fog_color.r       = (w1 & 0xFF000000) >> 24;
   g_gdp.fog_color.g       = (w1 & 0x00FF0000) >> 16;
   g_gdp.fog_color.b       = (w1 & 0x0000FF00) >>  8;
   g_gdp.fog_color.a       = (w1 & 0x000000FF) >>  0;
}

void gdp_set_blend_color(uint32_t w1)
{
   g_gdp.blend_color.total = w1;
   g_gdp.blend_color.r       = (w1 & 0xFF000000) >> 24;
   g_gdp.blend_color.g       = (w1 & 0x00FF0000) >> 16;
   g_gdp.blend_color.b       = (w1 & 0x0000FF00) >>  8;
   g_gdp.blend_color.a       = (w1 & 0x000000FF) >>  0;
}

void gdp_set_fill_color(uint32_t w1)
{
   g_gdp.fill_color.total = w1;
   g_gdp.fill_color.r       = (w1 & 0xFF000000) >> 24;
   g_gdp.fill_color.g       = (w1 & 0x00FF0000) >> 16;
   g_gdp.fill_color.b       = (w1 & 0x0000FF00) >>  8;
   g_gdp.fill_color.a       = (w1 & 0x000000FF) >>  0;
   g_gdp.fill_color.z   = _SHIFTR( w1,  2, 14 );
   g_gdp.fill_color.dz  = _SHIFTR( w1,  0,  2 );
}

void gdp_set_convert(uint32_t w0, uint32_t w1)
{
   g_gdp.k0  = (w0 & 0x003FE000)  >> 13;
   g_gdp.k1  = (w0 & 0x00001FF0)  >>  4;
   g_gdp.k2  = (w0 & 0x0000000F)  <<  5;
   g_gdp.k2 |= (w1 & 0xF8000000)  >> 27;
   g_gdp.k3  = (w1 & 0x07FC0000)  >> 18;
   g_gdp.k4  = (w1 & 0x0003FE00)  >>  9;
   g_gdp.k5  = (w1 & 0x000001FF)  >>  0;
}

void gdp_set_key_gb(uint32_t w0, uint32_t w1)
{
   g_gdp.key_scale.total  = (g_gdp.key_scale.total & 0xFF0000FF) | (((w1 >> 16) & 0xFF) << 16)   | (((w1 & 0xFF)) << 8);
   g_gdp.key_center.total = (g_gdp.key_center.total & 0xFF0000FF) | (((w1 >> 24) & 0xFF) << 16) | (((w1 >> 8) & 0xFF) << 8);

   g_gdp.key_width.g      = (w0 & 0x00FFF000) >> 12;
   g_gdp.key_width.b      = (w0 & 0x00000FFF) >>  0;
   g_gdp.key_center.g     = (w1 & 0xFF000000) >> 24;
   g_gdp.key_scale.g      = (w1 & 0x00FF0000) >> 16;
   g_gdp.key_center.b     = (w1 & 0x0000FF00) >>  8;
   g_gdp.key_scale.b      = (w1 & 0x000000FF) >>  0;
}

void gdp_set_key_r(uint32_t w1)
{
   g_gdp.key_scale.total  = (g_gdp.key_scale.total & 0x00FFFFFF)  | ((w1 & 0xFF) << 24);
   g_gdp.key_center.total = (g_gdp.key_center.total & 0x00FFFFFF) | (((w1 >> 8) & 0xFF) << 24);
   g_gdp.key_center.r     = (w1 & 0x0000FF00) >>  8;
   g_gdp.key_width.r      = (w1 & 0x0FFF0000) >> 16;
   g_gdp.key_scale.r      = (w1 & 0x000000FF) >>  0;
}

int32_t gdp_set_tile(uint32_t w0, uint32_t w1)
{
   int32_t tilenum             = (w1 & 0x07000000) >> 24;

   g_gdp.tile[tilenum].format  = (w0 & 0x00E00000) >> (53 - 32);
   g_gdp.tile[tilenum].size    = (w0 & 0x00180000) >> (51 - 32);
   g_gdp.tile[tilenum].line    = (w0 & 0x0003FE00) >> (41 - 32);
   g_gdp.tile[tilenum].tmem    = (w0 & 0x000001FF) >> (32 - 32);
   g_gdp.tile[tilenum].palette = (w1 & 0x00F00000) >> (20 -  0);
   g_gdp.tile[tilenum].ct      = (w1 & 0x00080000) >> (19 -  0);
   g_gdp.tile[tilenum].mt      = (w1 & 0x00040000) >> (18 -  0);
   g_gdp.tile[tilenum].mask_t  = (w1 & 0x0003C000) >> (14 -  0);
   g_gdp.tile[tilenum].shift_t = (w1 & 0x00003C00) >> (10 -  0);
   g_gdp.tile[tilenum].cs      = (w1 & 0x00000200) >> ( 9 -  0);
   g_gdp.tile[tilenum].ms      = (w1 & 0x00000100) >> ( 8 -  0);
   g_gdp.tile[tilenum].mask_s  = (w1 & 0x000000F0) >> ( 4 -  0);
   g_gdp.tile[tilenum].shift_s = (w1 & 0x0000000F) >> ( 0 -  0);

   return tilenum;
}

int32_t gdp_set_tile_size(uint32_t w0, uint32_t w1)
{
   int32_t tilenum = (w1 & 0x07000000) >> (24 -  0);
   g_gdp.tile[tilenum].sl      = (w0 & 0x00FFF000) >> (44 - 32);
   g_gdp.tile[tilenum].tl      = (w0 & 0x00000FFF) >> (32 - 32);
   g_gdp.tile[tilenum].sh      = (w1 & 0x00FFF000) >> (12 -  0);
   g_gdp.tile[tilenum].th      = (w1 & 0x00000FFF) >> ( 0 -  0);

   return tilenum;
}

void gdp_set_combine(uint32_t w0, uint32_t w1)
{
   g_gdp.combine.sub_a_rgb0 = (w0 & 0x00F00000) >> 20;
   g_gdp.combine.mul_rgb0   = (w0 & 0x000F8000) >> 15;
   g_gdp.combine.sub_a_a0   = (w0 & 0x00007000) >> 12;
   g_gdp.combine.mul_a0     = (w0 & 0x00000E00) >>  9;
   g_gdp.combine.sub_a_rgb1 = (w0 & 0x000001E0) >>  5;
   g_gdp.combine.mul_rgb1   = (w0 & 0x0000001F) >>  0;
   g_gdp.combine.sub_b_rgb0 = (w1 & 0xF0000000) >> 28;
   g_gdp.combine.sub_b_rgb1 = (w1 & 0x0F000000) >> 24;
   g_gdp.combine.sub_a_a1   = (w1 & 0x00E00000) >> 21;
   g_gdp.combine.mul_a1     = (w1 & 0x001C0000) >> 18;
   g_gdp.combine.add_rgb0   = (w1 & 0x00038000) >> 15;
   g_gdp.combine.sub_b_a0   = (w1 & 0x00007000) >> 12;
   g_gdp.combine.add_a0     = (w1 & 0x00000E00) >>  9;
   g_gdp.combine.add_rgb1   = (w1 & 0x000001C0) >>  6;
   g_gdp.combine.sub_b_a1   = (w1 & 0x00000038) >>  3;
   g_gdp.combine.add_a1     = (w1 & 0x00000007) >>  0;
}

void gdp_set_other_modes(uint32_t w0, uint32_t w1)
{
   /* K:  atomic_prim              = (w0 & 0x0080000000000000) >> 55; */
   /* j:  reserved for future use -- (w0 & 0x0040000000000000) >> 54 */
   g_gdp.other_modes.cycle_type       = (w0 & 0x00300000) >> (52 - 32);
   g_gdp.other_modes.persp_tex_en     = !!(w0 & 0x00080000); /* 51 */
   g_gdp.other_modes.detail_tex_en    = !!(w0 & 0x00040000); /* 50 */
   g_gdp.other_modes.sharpen_tex_en   = !!(w0 & 0x00020000); /* 49 */
   g_gdp.other_modes.tex_lod_en       = !!(w0 & 0x00010000); /* 48 */
   g_gdp.other_modes.en_tlut          = !!(w0 & 0x00008000); /* 47 */
   g_gdp.other_modes.tlut_type        = !!(w0 & 0x00004000); /* 46 */
   g_gdp.other_modes.sample_type      = !!(w0 & 0x00002000); /* 45 */
   g_gdp.other_modes.mid_texel        = !!(w0 & 0x00001000); /* 44 */
   g_gdp.other_modes.bi_lerp0         = !!(w0 & 0x00000800); /* 43 */
   g_gdp.other_modes.bi_lerp1         = !!(w0 & 0x00000400); /* 42 */
   g_gdp.other_modes.convert_one      = !!(w0 & 0x00000200); /* 41 */
   g_gdp.other_modes.key_en           = !!(w0 & 0x00000100); /* 40 */
   g_gdp.other_modes.rgb_dither_sel   = (w0 & 0x000000C0) >> (38 - 32);
   g_gdp.other_modes.alpha_dither_sel = (w0 & 0x00000030) >> (36 - 32);
   /* reserved for future, def:15 -- (w1 & 0x0000000F00000000) >> 32 */
   g_gdp.other_modes.blend_m1a_0      = (w1 & 0xC0000000) >> (30 -  0);
   g_gdp.other_modes.blend_m1a_1      = (w1 & 0x30000000) >> (28 -  0);
   g_gdp.other_modes.blend_m1b_0      = (w1 & 0x0C000000) >> (26 -  0);
   g_gdp.other_modes.blend_m1b_1      = (w1 & 0x03000000) >> (24 -  0);
   g_gdp.other_modes.blend_m2a_0      = (w1 & 0x00C00000) >> (22 -  0);
   g_gdp.other_modes.blend_m2a_1      = (w1 & 0x00300000) >> (20 -  0);
   g_gdp.other_modes.blend_m2b_0      = (w1 & 0x000C0000) >> (18 -  0);
   g_gdp.other_modes.blend_m2b_1      = (w1 & 0x00030000) >> (16 -  0);
   /* N:  reserved for future use -- (w1 & 0x0000000000008000) >> 15 */
   g_gdp.other_modes.force_blend      = !!(w1 & 0x00004000); /* 14 */
   g_gdp.other_modes.alpha_cvg_select = !!(w1 & 0x00002000); /* 13 */
   g_gdp.other_modes.cvg_times_alpha  = !!(w1 & 0x00001000); /* 12 */
   g_gdp.other_modes.z_mode           = (w1 & 0x00000C00) >> (10 -  0);
   g_gdp.other_modes.cvg_dest         = (w1 & 0x00000300) >> ( 8 -  0);
   g_gdp.other_modes.color_on_cvg     = !!(w1 & 0x00000080); /*  7 */
   g_gdp.other_modes.image_read_en    = !!(w1 & 0x00000040); /*  6 */
   g_gdp.other_modes.z_update_en      = !!(w1 & 0x00000020); /*  5 */
   g_gdp.other_modes.z_compare_en     = !!(w1 & 0x00000010); /*  4 */
   g_gdp.other_modes.antialias_en     = !!(w1 & 0x00000008); /*  3 */
   g_gdp.other_modes.z_source_sel     = !!(w1 & 0x00000004); /*  2 */
   g_gdp.other_modes.dither_alpha_en  = !!(w1 & 0x00000002); /*  1 */
   g_gdp.other_modes.alpha_compare_en = !!(w1 & 0x00000001); /*  0 */
}
