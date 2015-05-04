#ifndef _RDP_COMMON_GDP_H
#define _RDP_COMMON_GDP_H

#include <stdint.h>

/* Useful macros for decoding GBI command's parameters */

#ifndef _SHIFTL
#define _SHIFTL( v, s, w )  (((uint32_t)v & ((0x01 << w) - 1)) << s)
#endif

#ifndef _SHIFTR
#define _SHIFTR( v, s, w )  (((uint32_t)v >> s) & ((0x01 << w) - 1))
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
   uint32_t total;
   int32_t r, g, b, a, l;
} gdp_color;

struct gdp_global
{
   int32_t primitive_lod_min;
   int32_t primitive_lod_frac;
   gdp_color prim_color;
   gdp_color fog_color;
   gdp_color env_color;
   gdp_color key_width;
   gdp_color key_scale;
   gdp_color key_center;
   uint32_t primitive_z;
   uint16_t primitive_delta_z;
   int32_t k0, k1, k2, k3, k4, k5;
};

void gdp_set_prim_color(uint32_t w0, uint32_t w1);

void gdp_set_prim_depth(uint32_t w1);

void gdp_set_fog_color(uint32_t w1);

void gdp_set_convert(uint32_t w0, uint32_t w1);

void gdp_set_key_r(uint32_t w1);

extern struct gdp_global g_gdp;

#ifdef __cplusplus
}
#endif

#endif
