#include "gSP_funcs.h"

void GSPCombineMatrices(enum gsp_plugin_type plug_type)
{
   switch (plug_type)
   {
      case GSP_PLUGIN_GLIDE64:
         glide64gSPCombineMatrices();
         break;
      case GSP_PLUGIN_GLN64:
         break;
   }
}
