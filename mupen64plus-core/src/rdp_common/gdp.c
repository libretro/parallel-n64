
#include "gdp.h"

struct gdp_global g_gdp;

void gdp_set_prim_color(uint32_t w0, uint32_t w1)
{
   int32_t r = _SHIFTR( w1, 24, 8 );
   int32_t g = _SHIFTR( w1, 16, 8 );
   int32_t b = _SHIFTR( w1,  8, 8 );
   int32_t a = _SHIFTR( w1,  0, 8 );
   int32_t l = _SHIFTL( w0,  0, 8 );
   g_gdp.prim_color.r       = r;
   g_gdp.prim_color.g       = g;
   g_gdp.prim_color.b       = b;
   g_gdp.prim_color.a       = a;
   g_gdp.prim_color.total   = w1;
   g_gdp.primitive_lod_frac = l;
}

