#ifndef _GDP_FUNCS_C_H
#define _GDP_FUNCS_C_H

#include <stdint.h>

#include "gDP_funcs_prot.h"

enum gdp_plugin_type
{
   GDP_PLUGIN_GLIDE64 = 0,
   GDP_PLUGIN_GLN64
};

#ifdef GDP_PLUGIN
#define GDP_DEF_PLUGIN GDP_PLUGIN
#else
#define GDP_DEF_PLUGIN GDP_PLUGIN_GLIDE64
#endif

#define gDPSetScissor(mode, ulx, uly, lrx, lry) GDPSetScissorC(GDP_DEF_PLUGIN, mode, ulx, uly, lrx, lry)

void GDPSetScissorC(enum gdp_plugin_type plug_type, uint32_t mode, float ulx, float uly, float lrx, float lry );

#endif
