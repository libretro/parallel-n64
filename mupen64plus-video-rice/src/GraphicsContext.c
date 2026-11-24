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
/* Rice's GL context state (GraphicsContext.h). C89. */
#include <stdio.h>

#include "osal_opengl.h"
#include <glsm/glsm.h>

#define M64P_PLUGIN_PROTOTYPES 1
#include "m64p_plugin.h"
#include "Config.h"
#include "Debugger.h"
#include "FrameBuffer.h"
#include "GraphicsContext.h"
#include "Video.h"
#include "../../libretro/libretro_private.h"

bool rice_gc_need_clean_scene = false;

/* Vertex array object kept bound so rice's client-side vertex arrays draw
 * under a core-profile GL context (see gc_init_state). */
static GLuint gc_vao = 0;

static void gc_init_state(void)
{
    /* A vertex array object must be bound for any draw to be valid under a
     * core-profile GL context.  rice feeds geometry through client-side vertex
     * arrays (glVertexAttribPointer with app pointers, no VBO); under a core
     * profile, with no VAO bound the driver rejects every draw and nothing
     * reaches the framebuffer.  RetroArch's "glcore" driver hands back a
     * core-profile context, so a VAO is needed there.  The "gl" (gl2) driver
     * hands back a compatibility context, where client arrays are valid and a
     * VAO is neither required nor wanted.
     *
     * Use the context type RetroArch requested (glsm_context_is_core) rather
     * than querying GL_CONTEXT_PROFILE_MASK: NVIDIA reports the core-profile
     * bit even on a compatibility context, so the mask query would bind a VAO
     * on the "gl" driver and black-screen it.  Resolve the entry points through
     * the frontend's get_proc_address rather than glsm's wrappers
     * (rglBindVertexArray also re-binds a framebuffer, wrong this early). */
    if (gc_vao == 0 && glsm_context_is_core())
    {
        void (*genVAO)(GLsizei, GLuint*) =
            (void (*)(GLsizei, GLuint*))glsm_get_proc_address("glGenVertexArrays");
        void (*bindVAO)(GLuint) =
            (void (*)(GLuint))glsm_get_proc_address("glBindVertexArray");
        if (genVAO && bindVAO)
        {
            genVAO(1, &gc_vao);
            bindVAO(gc_vao);
        }
    }

    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClearDepth(1.0f);

    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_BLEND);

    glFrontFace(GL_CCW);
    glDisable(GL_CULL_FACE);

    glDepthFunc(GL_LEQUAL);
    glEnable(GL_DEPTH_TEST);

    glEnable(GL_BLEND);
    glDepthRange(-1.0f, 1.0f);
}

void RiceGC_Initialize(void)
{
    RiceFB_Initialize();
    gc_init_state();

    /* two cleared, handed-off frames, so neither buffer starts with garbage */
    RiceGC_Clear(CLEAR_COLOR_AND_DEPTH_BUFFER, 0xFF000000, 1.0f);
    RiceGC_UpdateFrame();
    RiceGC_Clear(CLEAR_COLOR_AND_DEPTH_BUFFER, 0xFF000000, 1.0f);
    RiceGC_UpdateFrame();
}

void RiceGC_Release(void)
{
    gc_vao = 0;
}

void RiceGC_Clear(unsigned flags, uint32_t color, float depth)
{
    GLbitfield flag = 0;
    float r         = ((color>>16)&0xFF)/255.0f;
    float g         = ((color>> 8)&0xFF)/255.0f;
    float b         = ((color    )&0xFF)/255.0f;
    float a         = ((color>>24)&0xFF)/255.0f;

    if (flags & CLEAR_COLOR_BUFFER)
       flag |= GL_COLOR_BUFFER_BIT;
    if (flags & CLEAR_DEPTH_BUFFER)
       flag |= GL_DEPTH_BUFFER_BIT;

    glClearColor(r, g, b, a);
    glClearDepth(depth);
    glClear(flag);
}

void RiceGC_UpdateFrame(void)
{
    status.gFrameCount++;

    /* (no glFlush: the frame is drawn into the frontend's framebuffer,
     * and the frontend submits it) */

    /* if the emulator set a render callback, it is called before the swap */
    if (renderCallback)
        (*renderCallback)(status.bScreenIsDrawn);

    retro_return(true);

    glDepthMask(GL_TRUE);
    glClearDepth(1.0f);
    if (!g_curRomInfo.bForceScreenClear)
        glClear(GL_DEPTH_BUFFER_BIT);
    else
        rice_gc_need_clean_scene = true;

    status.bScreenIsDrawn = false;
}
