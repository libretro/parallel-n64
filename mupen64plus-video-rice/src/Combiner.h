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

#ifndef _COMBINER_H_
#define _COMBINER_H_

#include "typedefs.h"
#include "CombinerDefs.h"
#include "CSortedList.h"
#include "DecodedMux.h"

class CRender;

extern const char* cycleTypeStrs[];

class CColorCombiner
{
    friend class CRender;
public:
    virtual ~CColorCombiner() {};
    virtual void InitCombinerMode(void);

    virtual bool Initialize(void)=0;
    virtual void CleanUp(void) {};
    virtual void UpdateCombiner(uint32_t dwMux0, uint32_t dwMux1);
    virtual void InitCombinerBlenderForSimpleTextureDraw(uint32_t tile)=0;
    virtual void DisableCombiner(void)=0;


    DecodedMux *m_pDecodedMux;
protected:
    CColorCombiner(CRender *pRender) : 
        m_pDecodedMux(NULL), m_bTex0Enabled(false),m_bTex1Enabled(false),m_bTexelsEnable(false),
        m_bCycleChanged(false), m_pRender(pRender)
    {
    }

    virtual void InitCombinerCycleCopy(void)=0;
    virtual void InitCombinerCycleFill(void)=0;
    virtual void InitCombinerCycle12(void)=0;

    bool    m_bTex0Enabled;
    bool    m_bTex1Enabled;
    bool    m_bTexelsEnable;

    bool    m_bCycleChanged;    // A flag will be set if cycle is changed to FILL or COPY

    CRender *m_pRender;

    CSortedList<uint64_t, DecodedMux> m_DecodedMuxList;
};


inline bool isEqual(uint8_t val1, uint8_t val2)
{
   if( (val1&MUX_MASK) == (val2&MUX_MASK) )
      return true;
   return false;
}

inline bool isTexel(uint8_t val)
{
   if( (val&MUX_MASK) == MUX_TEXEL0 || (val&MUX_MASK) == MUX_TEXEL1 )
      return true;
   return false;
}

#endif

