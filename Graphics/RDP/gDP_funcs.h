#ifndef _GDP_FUNCS_H
#define _GDP_FUNCS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum gdp_plugin_type
{
   GDP_PLUGIN_GLIDE64 = 0,
   GDP_PLUGIN_GLN64
};

#define gDPSetScissor(mode, ulx, uly, lrx, lry) GDPSetScissor(GDP_PLUGIN_GLIDE64, mode, ulx, uly, lrx, lry)

void GDPSetScissor(enum gdp_plugin_type plug_type, uint32_t mode, float ulx, float uly, float lrx, float lry );

/* Glide64 prototypes */
void glide64gDPSetScissor( uint32_t mode, float ulx, float uly, float lrx, float lry );

/* GLN64 prototypes */
void gln64gDPSetScissor( uint32_t mode, float ulx, float uly, float lrx, float lry );

#ifdef __cplusplus
}
#endif

#endif
