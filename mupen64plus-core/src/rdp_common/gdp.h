#ifndef _RDP_COMMON_GDP_H
#define _RDP_COMMON_GDP_H

#include <stdint.h>

struct gdp_global
{
   struct
   {
      uint32_t total;
      int32_t r, g, b, a, l;
   } prim_color;

   struct
   {
      uint32_t total;
      int32_t r, g, b, a;
   }
};

extern struct gdp_global g_gdp;

#endif
