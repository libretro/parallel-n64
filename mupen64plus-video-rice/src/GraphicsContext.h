/*
Copyright (C) 2003 Rice1964

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

#ifndef GFXCONTEXT_H
#define GFXCONTEXT_H

#include "typedefs.h"
#include "CritSect.h"

enum ClearFlag
{
    CLEAR_COLOR_BUFFER=0x01,
    CLEAR_DEPTH_BUFFER=0x02,
    CLEAR_COLOR_AND_DEPTH_BUFFER=0x03,
};

// This class basically provides an extra level of security for our
// multithreaded code. Threads can Grab the CGraphicsContext to prevent
// other threads from changing/releasing any of the pointers while it is
// running.

// It is based on CCritSect for Lock() and Unlock()

class CGraphicsContext : public CCritSect
{    
public:
    CGraphicsContext();
    ~CGraphicsContext();


    bool Ready() { return m_bReady; }

    bool Initialize(uint32 dwWidth, uint32 dwHeight);
    void CleanUp();

    void ResetOpenGL();

    void Clear(ClearFlag flags, uint32 color=0xFF000000, float depth=1.0f);
    void UpdateFrame(bool swaponly=false);

    static void InitDeviceParameters();

public:
    bool m_bReady;
    bool m_bSupportMultiTexture;
    bool m_bSupportFogCoord;

public:
    static CGraphicsContext *g_pGraphicsContext;
    static CGraphicsContext * Get(void);
    static bool needCleanScene;
};

#endif

