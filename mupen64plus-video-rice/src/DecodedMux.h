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

#ifndef _DECODEDMUX_H_
#define _DECODEDMUX_H_

#include <string.h>
#include <stdio.h>

#include "typedefs.h"
#include "CombinerDefs.h"

#define CM_IGNORE 0
#define CM_IGNORE_BYTE 0xFF

typedef enum 
{
    N64Cycle0RGB   = 0,
    N64Cycle0Alpha = 1,
    N64Cycle1RGB   = 2,
    N64Cycle1Alpha = 3,
} N64StageNumberType;

typedef union
{
    struct
    {
        uint32_t dwMux0;
        uint32_t dwMux1;
    };
    uint64_t Mux64;
} MuxType;

typedef struct
{
    MuxType ori_mux;
    MuxType simple_mux;
} SimpleMuxMapType;

class DecodedMux
{
public:
    union
    {
        struct
        {
            uint8_t aRGB0;
            uint8_t bRGB0;
            uint8_t cRGB0;
            uint8_t dRGB0;
            
            uint8_t aA0;
            uint8_t bA0;
            uint8_t cA0;
            uint8_t dA0;
            
            uint8_t aRGB1;
            uint8_t bRGB1;
            uint8_t cRGB1;
            uint8_t dRGB1;
            
            uint8_t aA1;
            uint8_t bA1;
            uint8_t cA1;
            uint8_t dA1;
        };
        uint8_t  m_bytes[16];
        uint32_t m_dWords[4];
        N64CombinerType m_n64Combiners[4];
    };
    
    union
    {
        struct
        {
            uint32_t m_dwMux0;
            uint32_t m_dwMux1;
        };
        uint64_t m_u64Mux;
    };

    CombinerFormatType splitType[4];
    CombinerFormatType mType;
    
    bool m_bTexel0IsUsed;
    bool m_bTexel1IsUsed;

    void Decode(uint32_t dwMux0, uint32_t dwMux1);
    void Hack(void);
    bool IsUsed(uint8_t fac, uint8_t mask);
    bool IsUsedInCycle(uint8_t fac, int cycle, CombineChannel channel, uint8_t mask);
    void CheckCombineInCycle1(void);
    void Simplify(void);
    void Reformat(bool do_complement);
    void ReplaceVal(uint8_t val1, uint8_t val2, int cycle, uint8_t mask);

    DecodedMux()
    {
        memset(m_bytes, 0, sizeof(m_bytes));
        mType=CM_FMT_TYPE_NOT_CHECKED;
        for( int i=0; i<4; i++ )
        {
            splitType[i] = CM_FMT_TYPE_NOT_CHECKED;
        }
    }
};

#endif


