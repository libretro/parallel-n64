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

#ifndef _DEVICE_BUILDER_H
#define _DEVICE_BUILDER_H

#include "Blender.h"
#include "Combiner.h"
#include "Config.h"
#include "GraphicsContext.h"
#include "TextureManager.h"

//========================================================================

class CDeviceBuilder
{
public:
    static CDeviceBuilder* GetBuilder(void);
    static void DeleteBuilder(void);

    CGraphicsContext * CreateGraphicsContext(void);
    void DeleteGraphicsContext(void);

    CRender * CreateRender(void);
    void DeleteRender(void);

    CColorCombiner * CreateColorCombiner(CRender *pRender);
    void DeleteColorCombiner(void);

    CTexture * CreateTexture(uint32 dwWidth, uint32 dwHeight, TextureUsage usage = AS_NORMAL);

protected:
    CDeviceBuilder();
    virtual ~CDeviceBuilder();

    static CDeviceBuilder* m_pInstance;

    CRender* m_pRender;
    CGraphicsContext* m_pGraphicsContext;
    CColorCombiner* m_pColorCombiner;
};

#endif


