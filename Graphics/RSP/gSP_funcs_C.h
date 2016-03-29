#ifndef _GSP_FUNCS_C_H
#define _GSP_FUNCS_C_H

#include <stdint.h>

#include "gSP_funcs_prot.h"

#ifdef __cplusplus
extern "C" {
#endif

enum gsp_plugin_type
{
   GSP_PLUGIN_GLIDE64 = 0,
   GSP_PLUGIN_GLN64
};

#ifdef GSP_PLUGIN
#define GSP_DEF_PLUGIN GSP_PLUGIN
#else
#define GSP_DEF_PLUGIN GSP_PLUGIN_GLIDE64
#endif

#define gSPCombineMatrices() GSPCombineMatricesC(GSP_DEF_PLUGIN)
#define gSPClipVertex(v)     GSPClipVertexC(GSP_DEF_PLUGIN, v)
#define gSPLookAt(l, n)      GSPLookAtC(GSP_DEF_PLUGIN, l, n)
#define gSPLight(l, n)       GSPLightC(GSP_DEF_PLUGIN, l, n)
#define gSPLightColor(l, c)  GSPLightColorC(GSP_DEF_PLUGIN, l, c)
#define gSPViewport(v)       GSPViewportC(GSP_DEF_PLUGIN, v)
#define gSPForceMatrix(mptr) GSPForceMatrixC(GSP_DEF_PLUGIN, mptr)


void GSPCombineMatricesC(enum gsp_plugin_type plug_type);
void GSPClipVertexC(enum gsp_plugin_type plug_type, uint32_t v);
void GSPLookAtC(enum gsp_plugin_type plug_type, uint32_t l, uint32_t n);
void GSPLightC(enum gsp_plugin_type plug_type, uint32_t l, int32_t n);
void GSPLightColorC(enum gsp_plugin_type plug_type, uint32_t lightNum, uint32_t packedColor );
void GSPViewportC(enum gsp_plugin_type plug_type, uint32_t v);
void GSPForceMatrixC(enum gsp_plugin_type plug_type, uint32_t mptr);

#ifdef __cplusplus
}
#endif

#endif
