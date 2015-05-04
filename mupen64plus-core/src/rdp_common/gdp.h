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
   uint32_t z;
   uint16_t dz;
   int32_t r, g, b, a, l;
} gdp_color;

typedef struct
{
    int32_t clampdiffs, clampdifft;
    int32_t clampens, clampent;
    int32_t masksclamped, masktclamped;
    int32_t notlutswitch, tlutswitch;
} gdp_faketile;

typedef struct
{
    int32_t format;        /* format: ARGB, IA, ... */
    int32_t size;          /* size: 4, 8, 16, or 32-bit */
    int32_t line;          /* size of one row (x axis) in 64 bit words */
    int32_t tmem;          /* location in texture memory (in 64 bit words, max 512 (4MB)) */
    int32_t palette;       /* palette # to use */
    int32_t ct;            /* clamp_t */
    int32_t mt;            /* mirror_t */
    int32_t cs;            /* clamp_s */
    int32_t ms;            /* mirror_s */
    int32_t mask_t;        /* mask to wrap around (y axis) */
    int32_t shift_t;       /* ??? (scaling) */
    int32_t mask_s;        /* mask to wrap around (x axis) */
    int32_t shift_s;       /* ??? (scaling) */
    int32_t sl;            /* lr_s - lower right s coordinate */
    int32_t tl;            /* lr_t - lower right t coordinate */
    int32_t sh;            /* ul_s - upper left  s coordinate */
    int32_t th;            /* ul_t - upper left  t coordinate */
    gdp_faketile f;
} gdp_tile;

struct gdp_global
{
   int32_t primitive_lod_min;
   int32_t primitive_lod_frac;
   gdp_color prim_color;
   gdp_color fill_color;
   gdp_color blend_color;
   gdp_color fog_color;
   gdp_color env_color;
   gdp_color key_width;
   gdp_color key_scale;
   gdp_color key_center;
   int32_t k0, k1, k2, k3, k4, k5;
   gdp_tile tile[8];
};


void gdp_set_prim_color(uint32_t w0, uint32_t w1);

void gdp_set_prim_depth(uint32_t w1);

void gdp_set_env_color(uint32_t w1);

void gdp_set_fill_color(uint32_t w1);

void gdp_set_fog_color(uint32_t w1);

void gdp_set_blend_color(uint32_t w1);

void gdp_set_convert(uint32_t w0, uint32_t w1);

void gdp_set_key_r(uint32_t w1);

void gdp_set_key_gb(uint32_t w0, uint32_t w1);

int32_t gdp_set_tile(uint32_t w0, uint32_t w1);

int32_t gdp_set_tile_size(uint32_t w0, uint32_t w1);

extern struct gdp_global g_gdp;

#ifdef __cplusplus
}
#endif

#endif
