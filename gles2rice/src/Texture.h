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
/* A texture Rice draws with: its size, what was made of it, and the
 * pixels it is converted into, which the backend (OGLTexture.h) puts on
 * the GPU. C89. */
#ifndef __SURFACEHANDLER_H__
#define __SURFACEHANDLER_H__

#include <stdint.h>
#include <boolean.h>

#ifdef __cplusplus
extern "C" {
#endif

/* where an update writes: the texture's pixels, as made */
typedef struct
{
    unsigned short int  dwWidth;            /* the texture's own width; lPitch moves between rows */
    unsigned short int  dwHeight;           /* the texture's own height */
    unsigned short int  dwCreatedWidth;     /* the width made, a power of 2 */
    unsigned short int  dwCreatedHeight;    /* the height made, a power of 2 */
    int                 lPitch;             /* bytes a row */
    void               *lpSurface;          /* the top left pixel */
} DrawInfo;

typedef enum TextureFmt
{
    TEXTURE_FMT_A8R8G8B8,
    TEXTURE_FMT_A4R4G4B4,
    TEXTURE_FMT_UNKNOWN
} TextureFmt;

typedef enum TextureUsage
{
    AS_NORMAL,
    AS_RENDER_TARGET,
    AS_BACK_BUFFER_SAVE
} TextureUsage;

typedef struct CTexture CTexture;

/* what a backend does with a texture */
typedef struct CTextureOps
{
    /* the pixels put on the GPU */
    void (*upload)(CTexture *tex);
    /* the backend's part let go */
    void (*release)(CTexture *tex);
} CTextureOps;

struct CTexture
{
    const CTextureOps *ops;

    uint32_t      m_dwWidth;                 /* the size asked for */
    uint32_t      m_dwHeight;

    unsigned int  m_dwCreatedTextureWidth;   /* the size made */
    unsigned int  m_dwCreatedTextureHeight;

    float         m_fXScale;                 /* made / asked for */
    float         m_fYScale;

    bool          m_bScaledS;
    bool          m_bScaledT;

    bool          m_bClampedS;
    bool          m_bClampedT;

    bool          m_bIsEnhancedTexture;

    TextureUsage  m_Usage;

    /* while held, an update's end leaves the upload for later: a texture
     * converted and then expanded goes to the GPU once, when let go */
    bool          m_bHoldUpload;
    bool          m_bUploadPending;
    void         *m_pTexture;                /* the pixels */
    TextureFmt    m_dwTextureFmt;
};

/* @tex, a backend's, made @dwWidth x @dwHeight for @usage, its size made
 * the same, nothing more */
void CTexture_Init(CTexture *tex, const CTextureOps *ops, uint32_t dwWidth, uint32_t dwHeight,
      TextureUsage usage);
/* @tex let go, and freed */
void CTexture_Delete(CTexture *tex);

uint32_t   CTexture_GetPixelSize(const CTexture *tex);
TextureFmt CTexture_GetSurfaceFormat(const CTexture *tex);
#define CTexture_GetTexture(tex) ((tex)->m_pTexture)

/* @di where @tex's pixels are, to write; false where there are none */
bool CTexture_StartUpdate(CTexture *tex, DrawInfo *di);
/* the pixels written: on the GPU, or once let go while held */
void CTexture_EndUpdate(CTexture *tex);
void CTexture_HoldUpload(CTexture *tex, bool hold);

/* scaled, clamped or not, as its size made says */
#define CTexture_SetOthersVariables(tex) \
    do { \
        (tex)->m_bClampedS = (tex)->m_bScaledS = ((tex)->m_dwWidth == (tex)->m_dwCreatedTextureWidth); \
        (tex)->m_bClampedT = (tex)->m_bScaledT = ((tex)->m_dwHeight == (tex)->m_dwCreatedTextureHeight); \
    } while (0)

#ifdef __cplusplus
}
#endif

#endif
