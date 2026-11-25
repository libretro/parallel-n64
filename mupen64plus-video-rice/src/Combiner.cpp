/*
Copyright (C) 2002 Rice1964

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
#include <stdint.h>
#include "Combiner.h"
#include "Config.h"
#include "RenderBase.h"


//========================================================================
//========================================================================

void CColorCombiner::InitCombinerMode(void)
{

    if (currentRomOptions.bNormalCombiner)
        DisableCombiner();
    else if (gRDP.otherMode.cycle_type  == G_CYC_COPY)
    {
        InitCombinerCycleCopy();
        m_bCycleChanged = true;
    }
    else if (gRDP.otherMode.cycle_type == G_CYC_FILL)
    {
        InitCombinerCycleFill();
        m_bCycleChanged = true;
    }
    else
    {
        InitCombinerCycle12();
        m_bCycleChanged = false;
    }
}


bool bConkerHideShadow=false;

void CColorCombiner::UpdateCombiner(uint32_t dwMux0, uint32_t dwMux1)
{

   DecodedMux &m_decodedMux = *m_pDecodedMux;
   if (m_decodedMux.m_dwMux0 != dwMux0 || m_decodedMux.m_dwMux1 != dwMux1)
   {
      if (options.enableHackForGames == HACK_FOR_DR_MARIO)
      {
         /* Hack for Dr. Mario */
         if (dwMux1 == 0xfffcf239 && 
               ((m_decodedMux.m_dwMux0 == dwMux0 && dwMux0 == 0x00ffffff && 
                 m_decodedMux.m_dwMux1 != dwMux1 && m_decodedMux.m_dwMux1 == 0xfffcf279) || 
                (m_decodedMux.m_dwMux0 == 0x00ffb3ff && m_decodedMux.m_dwMux1 == 0xff64fe7f && dwMux0 == 0x00ffffff )))
         {
            //dwMux1 = 0xffcf23A;
            dwMux1 = 0xfffcf438;
         }
      }
      uint64_t mux64 = (((uint64_t)dwMux1)<<32)+dwMux0;
      int index=m_DecodedMuxList.find(mux64);

      if (options.enableHackForGames == HACK_FOR_CONKER)
      {
         // Conker's shadow, to disable the shadow
         //Mux=0x00ffe9ff    Used in CONKER BFD
         //Color0: (0 - 0) * 0 + SHADE
         //Color1: (0 - 0) * 0 + SHADE
         //Alpha0: (1 - TEXEL0) * SHADE + 0
         //Alpha1: (1 - TEXEL0) * SHADE + 0              
         if (dwMux1 == 0xffd21f0f && dwMux0 == 0x00ffe9ff)
            bConkerHideShadow = true;
         else
            bConkerHideShadow = false;
      }

      if (index >= 0)
      {
         m_decodedMux = m_DecodedMuxList[index];
      }
      else
      {
         m_decodedMux.Decode(dwMux0, dwMux1);
         m_decodedMux.splitType[0] = CM_FMT_TYPE_NOT_CHECKED;
         m_decodedMux.splitType[1] = CM_FMT_TYPE_NOT_CHECKED;
         m_decodedMux.splitType[2] = CM_FMT_TYPE_NOT_CHECKED;
         m_decodedMux.splitType[3] = CM_FMT_TYPE_NOT_CHECKED;

         m_decodedMux.Hack();

         m_decodedMux.Simplify();

         m_DecodedMuxList.add(m_decodedMux.m_u64Mux, *m_pDecodedMux);
      }

      m_bTex0Enabled = m_decodedMux.m_bTexel0IsUsed;
      m_bTex1Enabled = m_decodedMux.m_bTexel1IsUsed;
      m_bTexelsEnable = m_bTex0Enabled||m_bTex1Enabled;

      gRSP.bProcessDiffuseColor = (m_decodedMux.m_dwShadeColorChannelFlag != MUX_0 || m_decodedMux.m_dwShadeAlphaChannelFlag != MUX_0);
      gRSP.bProcessSpecularColor = false;
   }
}



