#ifndef _GSP_FUNCS_H
#define _GSP_FUNCS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum gsp_plugin_type
{
   GSP_PLUGIN_GLIDE64 = 0,
   GSP_PLUGIN_GLN64
};

#define gSPCombineMatrices() GSPCombineMatrices(GSP_PLUGIN_GLIDE64)
#define gSPClipVertex(v)     GSPClipVertex(GSP_PLUGIN_GLIDE64, v)
#define gSPLookAt(l, n)      GSPLookAt(GSP_PLUGIN_GLIDE64, l, n)
#define gSPLight(l, n)       GSPLight(GSP_PLUGIN_GLIDE64, l, n)
#define gSPLightColor(l, c)  GSPLightColor(GSP_PLUGIN_GLIDE64, l, c)

void GSPCombineMatrices(enum gsp_plugin_type plug_type);
void GSPClipVertex(enum gsp_plugin_type plug_type, uint32_t v);
void GSPLookAt(enum gsp_plugin_type plug_type, uint32_t l, uint32_t n);
void GSPLight(enum gsp_plugin_type plug_type, uint32_t l, int32_t n);
void GSPLightColor(enum gsp_plugin_type plug_type, uint32_t lightNum, uint32_t packedColor );

#ifdef __cplusplus
}
#endif

#endif
