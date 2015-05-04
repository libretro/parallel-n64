
#include "gdp.h"

struct gdp_global g_gdp;

void gdp_set_prim_color(uint32_t w0, uint32_t w1)
{
   g_gdp.prim_color.total   =  w1;
   g_gdp.prim_color.r       = _SHIFTR( w1, 24, 8 );
   g_gdp.prim_color.g       = _SHIFTR( w1, 16, 8 );
   g_gdp.prim_color.b       = _SHIFTR( w1,  8, 8 );
   g_gdp.prim_color.a       = _SHIFTR( w1,  0, 8 );
   g_gdp.primitive_lod_frac = _SHIFTL( w0,  0, 8 );
   g_gdp.primitive_lod_min  = _SHIFTL( w0,  8, 8 );
}

void gdp_set_env_color(uint32_t w1)
{
   g_gdp.env_color.total =  w1;
   g_gdp.env_color.r     = _SHIFTR( w1, 24, 8);
   g_gdp.env_color.g     = _SHIFTR( w1, 16, 8);
   g_gdp.env_color.b     = _SHIFTR( w1,  8, 8);
   g_gdp.env_color.a     = _SHIFTR( w1,  0, 8);
}

void gdp_set_prim_depth(uint32_t w1)
{
   g_gdp.prim_color.z       = _SHIFTR(w1, 16, 16);
   g_gdp.prim_color.dz      = _SHIFTR(w1,  0, 16);
}

void gdp_set_fog_color(uint32_t w1)
{
   g_gdp.fog_color.total =  w1;
   g_gdp.fog_color.r     = _SHIFTR( w1, 24, 8 );
   g_gdp.fog_color.g     = _SHIFTR( w1, 16, 8 );
   g_gdp.fog_color.b     = _SHIFTR( w1,  8, 8 );
   g_gdp.fog_color.a     = _SHIFTR( w1,  0, 8 );
}

void gdp_set_blend_color(uint32_t w1)
{
   g_gdp.blend_color.total = w1;
   g_gdp.blend_color.r = (w1 & 0xFF000000) >> 24;
   g_gdp.blend_color.g = (w1 & 0x00FF0000) >> 16;
   g_gdp.blend_color.b = (w1 & 0x0000FF00) >>  8;
   g_gdp.blend_color.a = (w1 & 0x000000FF) >>  0;
}

void gdp_set_fill_color(uint32_t w1)
{
   g_gdp.fill_color.total = w1;
   g_gdp.fill_color.r   = _SHIFTR( w1, 24, 8 );
   g_gdp.fill_color.g   = _SHIFTR( w1, 16, 8 );
   g_gdp.fill_color.b   = _SHIFTR( w1,  8, 8 );
   g_gdp.fill_color.a   = _SHIFTR( w1,  0, 8 );
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
   g_gdp.key_scale.total = (g_gdp.key_scale.total & 0xFF0000FF) | (((w1 >> 16) & 0xFF) << 16)   | (((w1 & 0xFF)) << 8);
   g_gdp.key_center.total = (g_gdp.key_center.total & 0xFF0000FF) | (((w1 >> 24) & 0xFF) << 16) | (((w1 >> 8) & 0xFF) << 8);
   g_gdp.key_width.g  = _SHIFTR( w0, 12, 12 );
   g_gdp.key_width.b  = _SHIFTR( w0,  0, 12 );
   g_gdp.key_center.g = _SHIFTR( w1, 24,  8 );
   g_gdp.key_scale.g  = _SHIFTR( w1, 16,  8 );
   g_gdp.key_center.b = _SHIFTR( w1,  8,  8 );
   g_gdp.key_scale.b  = _SHIFTR( w1,  0,  8 );
}

void gdp_set_key_r(uint32_t w1)
{
   g_gdp.key_scale.total  = (g_gdp.key_scale.total & 0x00FFFFFF)  | ((w1 & 0xFF) << 24);
   g_gdp.key_center.total = (g_gdp.key_center.total & 0x00FFFFFF) | (((w1 >> 8) & 0xFF) << 24);
   g_gdp.key_center.r     = _SHIFTR( w1,  8,  8 );
   g_gdp.key_width.r      = _SHIFTR( w1,  0,  8 );
   g_gdp.key_scale.r      = _SHIFTR( w1, 16, 12 );
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
