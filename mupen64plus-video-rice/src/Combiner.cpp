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
#include <string.h>

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


void CColorCombiner::UpdateCombiner(uint32_t dwMux0, uint32_t dwMux1)
{
   if (DecodedMux_Update(m_pDecodedMux, &m_DecodedMuxList, dwMux0, dwMux1))
   {
      m_bTex0Enabled = m_pDecodedMux->m_bTexel0IsUsed;
      m_bTex1Enabled = m_pDecodedMux->m_bTexel1IsUsed;
      m_bTexelsEnable = m_bTex0Enabled||m_bTex1Enabled;
   }
}
