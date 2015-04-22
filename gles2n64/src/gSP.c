#include <assert.h>
#include <math.h>
#include <stdlib.h>

#include "Common.h"
#include "gles2N64.h"
#include "Debug.h"
#include "Types.h"
#include "RSP.h"
#include "GBI.h"
#include "gSP.h"
#include "gDP.h"
#include "3DMath.h"
#include "OpenGL.h"
#include "CRC.h"
#include <string.h>
#include "convert.h"
#include "S2DEX.h"
#include "VI.h"
#include "DepthBuffer.h"
#include "Config.h"

//Note: 0xC0 is used by 1080 alot, its an unknown command.

#ifdef DEBUG
extern u32 uc_crc, uc_dcrc;
extern char uc_str[256];
#endif

void gSPCombineMatrices(void);

void gSPTriangle(s32 v0, s32 v1, s32 v2)
{
   if ((v0 < INDEXMAP_SIZE) && (v1 < INDEXMAP_SIZE) && (v2 < INDEXMAP_SIZE))
   {

#if 0
      // Don't bother with triangles completely outside clipping frustrum
      if (config.enableClipping)
      {
         if (OGL.triangles.vertices[v0].clip & OGL.triangles.vertices[v1].clip & OGL.triangles.vertices[v2].clip)
         {
            return;
         }
      }
#endif

      OGL_AddTriangle(v0, v1, v2);

   }

   if (depthBuffer.current) depthBuffer.current->cleared = FALSE;
   gDP.colorImage.changed = TRUE;
   gDP.colorImage.height = (u32)(max( gDP.colorImage.height, (u32)gDP.scissor.lry ));
}

void gSP1Triangle( s32 v0, s32 v1, s32 v2)
{
   gSPTriangle( v0, v1, v2);
   gSPFlushTriangles();
}

void gSP2Triangles(const s32 v00, const s32 v01, const s32 v02, const s32 flag0,
                    const s32 v10, const s32 v11, const s32 v12, const s32 flag1 )
{
   gSPTriangle( v00, v01, v02);
   gSPTriangle( v10, v11, v12);
   gSPFlushTriangles();
}

void gSP4Triangles(const s32 v00, const s32 v01, const s32 v02,
                    const s32 v10, const s32 v11, const s32 v12,
                    const s32 v20, const s32 v21, const s32 v22,
                    const s32 v30, const s32 v31, const s32 v32 )
{
   gSPTriangle(v00, v01, v02);
   gSPTriangle(v10, v11, v12);
   gSPTriangle(v20, v21, v22);
   gSPTriangle(v30, v31, v32);
   gSPFlushTriangles();
}


gSPInfo gSP;

f32 identityMatrix[4][4] =
{
    { 1.0f, 0.0f, 0.0f, 0.0f },
    { 0.0f, 1.0f, 0.0f, 0.0f },
    { 0.0f, 0.0f, 1.0f, 0.0f },
    { 0.0f, 0.0f, 0.0f, 1.0f }
};

void gSPClipVertex(u32 v)
{
   SPVertex *vtx = &OGL.triangles.vertices[v];
   vtx->clip = 0;
   if (vtx->x > +vtx->w)   vtx->clip |= CLIP_POSX;
   if (vtx->x < -vtx->w)   vtx->clip |= CLIP_NEGX;
   if (vtx->y > +vtx->w)   vtx->clip |= CLIP_POSY;
   if (vtx->y < -vtx->w)   vtx->clip |= CLIP_NEGY;
   if (vtx->w < 0.1f)      vtx->clip |= CLIP_Z;
}

static void gSPTransformVertex_default(float vtx[4], float mtx[4][4])
{
   float x, y, z, w;
   x = vtx[0];
   y = vtx[1];
   z = vtx[2];
   w = vtx[3];

   vtx[0] = x * mtx[0][0] + y * mtx[1][0] + z * mtx[2][0] + mtx[3][0];
   vtx[1] = x * mtx[0][1] + y * mtx[1][1] + z * mtx[2][1] + mtx[3][1];
   vtx[2] = x * mtx[0][2] + y * mtx[1][2] + z * mtx[2][2] + mtx[3][2];
   vtx[3] = x * mtx[0][3] + y * mtx[1][3] + z * mtx[2][3] + mtx[3][3];
}

static void gSPLightVertex_default(u32 v)
{
   int i;
   f32 r, g, b, intensity;


   r = gSP.lights[gSP.numLights].r;
   g = gSP.lights[gSP.numLights].g;
   b = gSP.lights[gSP.numLights].b;
   for (i = 0; i < gSP.numLights; i++)
   {
      intensity = DotProduct( &OGL.triangles.vertices[v].nx, &gSP.lights[i].x );
      if (intensity < 0.0f)
         intensity = 0.0f;
      r += gSP.lights[i].r * intensity;
      g += gSP.lights[i].g * intensity;
      b += gSP.lights[i].b * intensity;
   }
   OGL.triangles.vertices[v].r = min(1.0f, r);
   OGL.triangles.vertices[v].g = min(1.0f, g);
   OGL.triangles.vertices[v].b = min(1.0f, b);
}

static void gSPBillboardVertex_default(u32 v, u32 i)
{
   OGL.triangles.vertices[v].x += OGL.triangles.vertices[i].x;
   OGL.triangles.vertices[v].y += OGL.triangles.vertices[i].y;
   OGL.triangles.vertices[v].z += OGL.triangles.vertices[i].z;
   OGL.triangles.vertices[v].w += OGL.triangles.vertices[i].w;
}

void gSPCombineMatrices(void)
{
   MultMatrix(gSP.matrix.projection, gSP.matrix.modelView[gSP.matrix.modelViewi], gSP.matrix.combined);
   gSP.changed &= ~CHANGED_MATRIX;
}

void gSPProcessVertex( u32 v )
{
   f32 intensity, r, g, b;

   if (gSP.changed & CHANGED_MATRIX)
      gSPCombineMatrices();

   gSPTransformVertex( &OGL.triangles.vertices[v].x, gSP.matrix.combined );

   if (config.screen.flipVertical)
      OGL.triangles.vertices[v].y = -OGL.triangles.vertices[v].y;

   if (gDP.otherMode.depthSource)
      OGL.triangles.vertices[v].z = gDP.primDepth.z * OGL.triangles.vertices[v].w;

   if (gSP.matrix.billboard)
   {
      int i = 0;

      gSPBillboardVertex(v, i);
   }

   if (!(gSP.geometryMode & G_ZBUFFER))
   {
      OGL.triangles.vertices[v].z = -OGL.triangles.vertices[v].w;
   }

   if (config.enableClipping)
      gSPClipVertex(v);

   if (gSP.geometryMode & G_LIGHTING)
   {
      TransformVectorNormalize( &OGL.triangles.vertices[v].nx, gSP.matrix.modelView[gSP.matrix.modelViewi] );
      if (config.enableLighting)
      {
         gSPLightVertex(v);
      }
      else
      {
         OGL.triangles.vertices[v].r = 1.0f;
         OGL.triangles.vertices[v].g = 1.0f;
         OGL.triangles.vertices[v].b = 1.0f;
      }

      if (gSP.geometryMode & G_TEXTURE_GEN)
      {
         TransformVectorNormalize(&OGL.triangles.vertices[v].nx, gSP.matrix.projection);

         if (gSP.geometryMode & G_TEXTURE_GEN_LINEAR)
         {
            OGL.triangles.vertices[v].s = acosf(OGL.triangles.vertices[v].nx) * 325.94931f;
            OGL.triangles.vertices[v].t = acosf(OGL.triangles.vertices[v].ny) * 325.94931f;
         }
         else // G_TEXTURE_GEN
         {
            OGL.triangles.vertices[v].s = (OGL.triangles.vertices[v].nx + 1.0f) * 512.0f;
            OGL.triangles.vertices[v].t = (OGL.triangles.vertices[v].ny + 1.0f) * 512.0f;
         }
      }
   }
}


void gSPLoadUcodeEx( u32 uc_start, u32 uc_dstart, u16 uc_dsize )
{
   MicrocodeInfo *ucode;
   __RSP.PCi = 0;
   gSP.matrix.modelViewi = 0;
   gSP.changed |= CHANGED_MATRIX;
   gSP.status[0] = gSP.status[1] = gSP.status[2] = gSP.status[3] = 0;

   if ((((uc_start & 0x1FFFFFFF) + 4096) > RDRAMSize) || (((uc_dstart & 0x1FFFFFFF) + uc_dsize) > RDRAMSize))
      return;

   ucode = (MicrocodeInfo*)GBI_DetectMicrocode( uc_start, uc_dstart, uc_dsize );

   if (ucode->type != 0xFFFFFFFF)
      last_good_ucode = ucode->type;

   if (ucode->type != NONE)
      GBI_MakeCurrent( ucode );
   else
   {
      LOG(LOG_WARNING, "Unknown Ucode\n");
   }
}

void gSPNoOp(void)
{
   gSPFlushTriangles();
}

void gSPTriangleUnknown(void)
{
}

void gSPMatrix( u32 matrix, u8 param )
{
   f32 mtx[4][4];
   u32 address = RSP_SegmentToPhysical( matrix );

   if (address + 64 > RDRAMSize)
      return;

   RSP_LoadMatrix( mtx, address );

   if (param & G_MTX_PROJECTION)
   {
      if (param & G_MTX_LOAD)
         CopyMatrix( gSP.matrix.projection, mtx );
      else
         MultMatrix2( gSP.matrix.projection, mtx );
   }
   else
   {
      if ((param & G_MTX_PUSH) && (gSP.matrix.modelViewi < (gSP.matrix.stackSize - 1)))
      {
         CopyMatrix( gSP.matrix.modelView[gSP.matrix.modelViewi + 1], gSP.matrix.modelView[gSP.matrix.modelViewi] );
         gSP.matrix.modelViewi++;
      }
      if (param & G_MTX_LOAD)
         CopyMatrix( gSP.matrix.modelView[gSP.matrix.modelViewi], mtx );
      else
         MultMatrix2( gSP.matrix.modelView[gSP.matrix.modelViewi], mtx );
   }

   gSP.changed |= CHANGED_MATRIX;
}

void gSPDMAMatrix( u32 matrix, u8 index, u8 multiply )
{
   f32 mtx[4][4];
   u32 address = gSP.DMAOffsets.mtx + RSP_SegmentToPhysical( matrix );

   if (address + 64 > RDRAMSize)
      return;

   RSP_LoadMatrix( mtx, address );

   gSP.matrix.modelViewi = index;

   if (multiply)
   {
      //CopyMatrix( gSP.matrix.modelView[gSP.matrix.modelViewi], gSP.matrix.modelView[0] );
      //MultMatrix( gSP.matrix.modelView[gSP.matrix.modelViewi], mtx );
      MultMatrix(gSP.matrix.modelView[0], mtx, gSP.matrix.modelView[gSP.matrix.modelViewi]);
   }
   else
      CopyMatrix( gSP.matrix.modelView[gSP.matrix.modelViewi], mtx );

   CopyMatrix( gSP.matrix.projection, identityMatrix );
   gSP.changed |= CHANGED_MATRIX;
}

void gSPViewport( u32 v )
{
   u32 address = RSP_SegmentToPhysical( v );

   if ((address + 16) > RDRAMSize)
   {
#ifdef DEBUG
      DebugMsg( DEBUG_HIGH | DEBUG_ERROR, "// Attempting to load viewport from invalid address\n" );
      DebugMsg( DEBUG_HIGH | DEBUG_HANDLED, "gSPViewport( 0x%08X );\n", v );
#endif
      return;
   }

   gSP.viewport.vscale[0] = _FIXED2FLOAT( *(s16*)&gfx_info.RDRAM[address +  2], 2 );
   gSP.viewport.vscale[1] = _FIXED2FLOAT( *(s16*)&gfx_info.RDRAM[address     ], 2 );
   gSP.viewport.vscale[2] = _FIXED2FLOAT( *(s16*)&gfx_info.RDRAM[address +  6], 10 );// * 0.00097847357f;
   gSP.viewport.vscale[3] = *(s16*)&gfx_info.RDRAM[address +  4];
   gSP.viewport.vtrans[0] = _FIXED2FLOAT( *(s16*)&gfx_info.RDRAM[address + 10], 2 );
   gSP.viewport.vtrans[1] = _FIXED2FLOAT( *(s16*)&gfx_info.RDRAM[address +  8], 2 );
   gSP.viewport.vtrans[2] = _FIXED2FLOAT( *(s16*)&gfx_info.RDRAM[address + 14], 10 );// * 0.00097847357f;
   gSP.viewport.vtrans[3] = *(s16*)&gfx_info.RDRAM[address + 12];

   gSP.viewport.x      = gSP.viewport.vtrans[0] - gSP.viewport.vscale[0];
   gSP.viewport.y      = gSP.viewport.vtrans[1] - gSP.viewport.vscale[1];
   gSP.viewport.width  = gSP.viewport.vscale[0] * 2;
   gSP.viewport.height = gSP.viewport.vscale[1] * 2;
   gSP.viewport.nearz  = gSP.viewport.vtrans[2] - gSP.viewport.vscale[2];
   gSP.viewport.farz   = (gSP.viewport.vtrans[2] + gSP.viewport.vscale[2]) ;

   gSP.changed |= CHANGED_VIEWPORT;
}

void gSPForceMatrix( u32 mptr )
{
   u32 address = RSP_SegmentToPhysical( mptr );

   if (address + 64 > RDRAMSize)
      return;

   RSP_LoadMatrix( gSP.matrix.combined, RSP_SegmentToPhysical( mptr ) );

   gSP.changed &= ~CHANGED_MATRIX;
}

void gSPLight( u32 l, s32 n )
{
	--n;
	u32 addrByte = RSP_SegmentToPhysical( l );

	if ((addrByte + sizeof( Light )) > RDRAMSize)
   {
#ifdef DEBUG
		DebugMsg( DEBUG_HIGH | DEBUG_ERROR, "// Attempting to load light from invalid address\n" );
		DebugMsg( DEBUG_HIGH | DEBUG_HANDLED, "gSPLight( 0x%08X, LIGHT_%i );\n",
			l, n );
#endif
		return;
	}

	Light *light = (Light*)&gfx_info.RDRAM[addrByte];

	if (n < 8) {
		gSP.lights[n].r = light->r * 0.0039215689f;
		gSP.lights[n].g = light->g * 0.0039215689f;
		gSP.lights[n].b = light->b * 0.0039215689f;

		gSP.lights[n].x = light->x;
		gSP.lights[n].y = light->y;
		gSP.lights[n].z = light->z;

		Normalize( &gSP.lights[n].x );
		u32 addrShort = addrByte >> 1;
		gSP.lights[n].posx = (float)(((short*)gfx_info.RDRAM)[(addrShort+4)^1]);
		gSP.lights[n].posy = (float)(((short*)gfx_info.RDRAM)[(addrShort+5)^1]);
		gSP.lights[n].posz = (float)(((short*)gfx_info.RDRAM)[(addrShort+6)^1]);
		gSP.lights[n].ca = (float)(gfx_info.RDRAM[(addrByte + 3) ^ 3]) / 16.0f;
		gSP.lights[n].la = (float)(gfx_info.RDRAM[(addrByte + 7) ^ 3]);
		gSP.lights[n].qa = (float)(gfx_info.RDRAM[(addrByte + 14) ^ 3]) / 8.0f;
	}

   /* TODO/FIXME - update */
#if 0
	if (config.generalEmulation.enableHWLighting != 0)
		gSP.changed |= CHANGED_LIGHT;
#endif

#ifdef DEBUG
	DebugMsg( DEBUG_DETAIL | DEBUG_HANDLED, "// x = %2.6f    y = %2.6f    z = %2.6f\n",
		_FIXED2FLOAT( light->x, 7 ), _FIXED2FLOAT( light->y, 7 ), _FIXED2FLOAT( light->z, 7 ) );
	DebugMsg( DEBUG_DETAIL | DEBUG_HANDLED, "// r = %3i    g = %3i   b = %3i\n",
		light->r, light->g, light->b );
	DebugMsg( DEBUG_HIGH | DEBUG_HANDLED, "gSPLight( 0x%08X, LIGHT_%i );\n",
		l, n );
#endif
}

void gSPLookAt( u32 _l, u32 _n )
{
   u32 address = RSP_SegmentToPhysical(_l);

   if ((address + sizeof(Light)) > RDRAMSize) {
#ifdef DEBUG
      DebugMsg(DEBUG_HIGH | DEBUG_ERROR, "// Attempting to load light from invalid address\n");
      DebugMsg(DEBUG_HIGH | DEBUG_HANDLED, "gSPLookAt( 0x%08X, LOOKAT_%i );\n",
            l, n);
#endif
      return;
   }

   assert(_n < 2);

   Light *light = (Light*)&gfx_info.RDRAM[address];

   gSP.lookat[_n].x = light->x;
   gSP.lookat[_n].y = light->y;
   gSP.lookat[_n].z = light->z;

   gSP.lookatEnable = (_n == 0) || (_n == 1 && (light->x != 0 || light->y != 0));

   Normalize(&gSP.lookat[_n].x);
}

void gSPVertex( u32 v, u32 n, u32 v0 )
{
   unsigned int i;
   Vertex *vertex;
   //flush batched triangles:

   u32 address = RSP_SegmentToPhysical( v );

   if ((address + sizeof( Vertex ) * n) > RDRAMSize)
      return;

   vertex = (Vertex*)&gfx_info.RDRAM[address];

   if ((n + v0) <= INDEXMAP_SIZE)
   {
      i = v0;
      for (; i < n + v0; i++)
      {
         u32 v = i;
         OGL.triangles.vertices[v].x = vertex->x;
         OGL.triangles.vertices[v].y = vertex->y;
         OGL.triangles.vertices[v].z = vertex->z;
         OGL.triangles.vertices[v].s = _FIXED2FLOAT( vertex->s, 5 );
         OGL.triangles.vertices[v].t = _FIXED2FLOAT( vertex->t, 5 );
         if (gSP.geometryMode & G_LIGHTING)
         {
            OGL.triangles.vertices[v].nx = vertex->normal.x;
            OGL.triangles.vertices[v].ny = vertex->normal.y;
            OGL.triangles.vertices[v].nz = vertex->normal.z;
            OGL.triangles.vertices[v].a = vertex->color.a * 0.0039215689f;
         }
         else
         {
            OGL.triangles.vertices[v].r = vertex->color.r * 0.0039215689f;
            OGL.triangles.vertices[v].g = vertex->color.g * 0.0039215689f;
            OGL.triangles.vertices[v].b = vertex->color.b * 0.0039215689f;
            OGL.triangles.vertices[v].a = vertex->color.a * 0.0039215689f;
         }
         gSPProcessVertex(v);
         vertex++;
      }
   }
   else
   {
      LOG(LOG_ERROR, "Using Vertex outside buffer v0=%i, n=%i\n", v0, n);
   }

}

void gSPCIVertex( u32 v, u32 n, u32 v0 )
{
   unsigned int i;
   PDVertex *vertex;

   u32 address = RSP_SegmentToPhysical( v );

   if ((address + sizeof( PDVertex ) * n) > RDRAMSize)
      return;

   vertex = (PDVertex*)&gfx_info.RDRAM[address];

   if ((n + v0) <= INDEXMAP_SIZE)
   {
      i = v0;
      for(; i < n + v0; i++)
      {
		 u8 *color;
         u32 v = i;
         OGL.triangles.vertices[v].x = vertex->x;
         OGL.triangles.vertices[v].y = vertex->y;
         OGL.triangles.vertices[v].z = vertex->z;
         OGL.triangles.vertices[v].s = _FIXED2FLOAT( vertex->s, 5 );
         OGL.triangles.vertices[v].t = _FIXED2FLOAT( vertex->t, 5 );
         color = (u8*)&gfx_info.RDRAM[gSP.vertexColorBase + (vertex->ci & 0xff)];

         if (gSP.geometryMode & G_LIGHTING)
         {
            OGL.triangles.vertices[v].nx = (s8)color[3];
            OGL.triangles.vertices[v].ny = (s8)color[2];
            OGL.triangles.vertices[v].nz = (s8)color[1];
            OGL.triangles.vertices[v].a = color[0] * 0.0039215689f;
         }
         else
         {
            OGL.triangles.vertices[v].r = color[3] * 0.0039215689f;
            OGL.triangles.vertices[v].g = color[2] * 0.0039215689f;
            OGL.triangles.vertices[v].b = color[1] * 0.0039215689f;
            OGL.triangles.vertices[v].a = color[0] * 0.0039215689f;
         }

         gSPProcessVertex(v);
         vertex++;
      }
   }
   else
   {
      LOG(LOG_ERROR, "Using Vertex outside buffer v0=%i, n=%i\n", v0, n);
   }

}

void gSPDMAVertex( u32 v, u32 n, u32 v0 )
{
   unsigned int i;
   u32 address = gSP.DMAOffsets.vtx + RSP_SegmentToPhysical( v );

   if ((address + 10 * n) > RDRAMSize)
      return;

   if ((n + v0) <= INDEXMAP_SIZE)
   {
      i = v0;
      for (; i < n + v0; i++)
      {
         u32 v = i;

         OGL.triangles.vertices[v].x = *(s16*)&gfx_info.RDRAM[address ^ 2];
         OGL.triangles.vertices[v].y = *(s16*)&gfx_info.RDRAM[(address + 2) ^ 2];
         OGL.triangles.vertices[v].z = *(s16*)&gfx_info.RDRAM[(address + 4) ^ 2];

         if (gSP.geometryMode & G_LIGHTING)
         {
            OGL.triangles.vertices[v].nx = *(s8*)&gfx_info.RDRAM[(address + 6) ^ 3];
            OGL.triangles.vertices[v].ny = *(s8*)&gfx_info.RDRAM[(address + 7) ^ 3];
            OGL.triangles.vertices[v].nz = *(s8*)&gfx_info.RDRAM[(address + 8) ^ 3];
            OGL.triangles.vertices[v].a = *(u8*)&gfx_info.RDRAM[(address + 9) ^ 3] * 0.0039215689f;
         }
         else
         {
            OGL.triangles.vertices[v].r = *(u8*)&gfx_info.RDRAM[(address + 6) ^ 3] * 0.0039215689f;
            OGL.triangles.vertices[v].g = *(u8*)&gfx_info.RDRAM[(address + 7) ^ 3] * 0.0039215689f;
            OGL.triangles.vertices[v].b = *(u8*)&gfx_info.RDRAM[(address + 8) ^ 3] * 0.0039215689f;
            OGL.triangles.vertices[v].a = *(u8*)&gfx_info.RDRAM[(address + 9) ^ 3] * 0.0039215689f;
         }

         gSPProcessVertex(v);
         address += 10;
      }
   }
   else
   {
      LOG(LOG_ERROR, "Using Vertex outside buffer v0=%i, n=%i\n", v0, n);
   }
}

void gSPDisplayList( u32 dl )
{
   u32 address = RSP_SegmentToPhysical( dl );

   if ((address + 8) > RDRAMSize)
      return;

   if (__RSP.PCi < (GBI.PCStackSize - 1))
   {
#ifdef DEBUG
      DebugMsg( DEBUG_HIGH | DEBUG_HANDLED, "\n" );
      DebugMsg( DEBUG_HIGH | DEBUG_HANDLED, "gSPDisplayList( 0x%08X );\n",
            dl );
#endif
      __RSP.PCi++;
      __RSP.PC[__RSP.PCi] = address;
      __RSP.nextCmd = _SHIFTR( *(u32*)&gfx_info.RDRAM[address], 24, 8 );
   }
}

void gSPDMADisplayList( u32 dl, u32 n )
{
   u32 curDL, w0, w1;
   if ((dl + (n << 3)) > RDRAMSize)
      return;

   curDL = __RSP.PC[__RSP.PCi];

   __RSP.PC[__RSP.PCi] = RSP_SegmentToPhysical( dl );

   while ((__RSP.PC[__RSP.PCi] - dl) < (n << 3))
   {
      if ((__RSP.PC[__RSP.PCi] + 8) > RDRAMSize)
         break;

      w0 = *(u32*)&gfx_info.RDRAM[__RSP.PC[__RSP.PCi]];
      w1 = *(u32*)&gfx_info.RDRAM[__RSP.PC[__RSP.PCi] + 4];

      __RSP.PC[__RSP.PCi] += 8;
      __RSP.nextCmd = _SHIFTR( *(u32*)&gfx_info.RDRAM[__RSP.PC[__RSP.PCi]], 24, 8 );

      GBI.cmd[_SHIFTR( w0, 24, 8 )]( w0, w1 );
   }

   __RSP.PC[__RSP.PCi] = curDL;
}

void gSPBranchList( u32 dl )
{
   u32 address = RSP_SegmentToPhysical( dl );

   if ((address + 8) > RDRAMSize)
      return;

   __RSP.PC[__RSP.PCi] = address;
   __RSP.nextCmd = _SHIFTR( *(u32*)&gfx_info.RDRAM[address], 24, 8 );
}

void gSPBranchLessZ( u32 branchdl, u32 vtx, f32 zval )
{
   u32 address = RSP_SegmentToPhysical( branchdl );

   if ((address + 8) > RDRAMSize)
      return;

   if (OGL.triangles.vertices[vtx].z <= zval)
      __RSP.PC[__RSP.PCi] = address;
}

void gSPDlistCount(u32 count, u32 v)
{
	u32 address = RSP_SegmentToPhysical( v );
	if (address == 0 || (address + 8) > RDRAMSize)
   {
#if 0
		DebugMsg( DEBUG_HIGH | DEBUG_ERROR, "// Attempting to branch to display list at invalid address\n" );
		DebugMsg( DEBUG_HIGH | DEBUG_HANDLED, "gSPDlistCnt(%d, 0x%08X );\n", count, v );
#endif
		return;
	}

	if (__RSP.PCi >= 9)
   {
#if 0
		DebugMsg( DEBUG_HIGH | DEBUG_ERROR, "// ** DL stack overflow **\n" );
		DebugMsg( DEBUG_HIGH | DEBUG_HANDLED, "gSPDlistCnt(%d, 0x%08X );\n", count, v );
#endif
		return;
	}

#if 0
	DebugMsg( DEBUG_HIGH | DEBUG_HANDLED, "gSPDlistCnt(%d, 0x%08X );\n", count, v );
#endif

	++__RSP.PCi;  // go to the next PC in the stack
	__RSP.PC[__RSP.PCi] = address;  // jump to the address
	__RSP.nextCmd = _SHIFTR( *(u32*)&gfx_info.RDRAM[address], 24, 8 );
	__RSP.count = count + 1;
}

void gSPSetDMAOffsets( u32 mtxoffset, u32 vtxoffset )
{
   gSP.DMAOffsets.mtx = mtxoffset;
   gSP.DMAOffsets.vtx = vtxoffset;
}

void gSPSetVertexColorBase( u32 base )
{
   gSP.vertexColorBase = RSP_SegmentToPhysical( base );
}

void gSPSprite2DBase( u32 base )
{
}

void gSPCopyVertex( SPVertex *dest, SPVertex *src )
{
   dest->x = src->x;
   dest->y = src->y;
   dest->z = src->z;
   dest->w = src->w;
   dest->r = src->r;
   dest->g = src->g;
   dest->b = src->b;
   dest->a = src->a;
   dest->s = src->s;
   dest->t = src->t;
}

void gSPInterpolateVertex( SPVertex *dest, f32 percent, SPVertex *first, SPVertex *second )
{
   dest->x = first->x + percent * (second->x - first->x);
   dest->y = first->y + percent * (second->y - first->y);
   dest->z = first->z + percent * (second->z - first->z);
   dest->w = first->w + percent * (second->w - first->w);
   dest->r = first->r + percent * (second->r - first->r);
   dest->g = first->g + percent * (second->g - first->g);
   dest->b = first->b + percent * (second->b - first->b);
   dest->a = first->a + percent * (second->a - first->a);
   dest->s = first->s + percent * (second->s - first->s);
   dest->t = first->t + percent * (second->t - first->t);
}

void gSPDMATriangles( u32 tris, u32 n )
{
   unsigned int i;
   s32 v0, v1, v2;
   DKRTriangle *triangles;
   u32 address = RSP_SegmentToPhysical( tris );

   if (address + sizeof( DKRTriangle ) * n > RDRAMSize)
      return;

   triangles = (DKRTriangle*)&gfx_info.RDRAM[address];

   for (i = 0; i < n; i++)
   {
      int mode = 0;
      if (!(triangles->flag & 0x40))
      {
         if (gSP.viewport.vscale[0] > 0)
            mode |= G_CULL_BACK;
         else
            mode |= G_CULL_FRONT;
      }

      if ((gSP.geometryMode&G_CULL_BOTH) != mode)
      {
         gSP.geometryMode &= ~G_CULL_BOTH;
         gSP.geometryMode |= mode;
         gSP.changed |= CHANGED_GEOMETRYMODE;
      }


      v0 = triangles->v0;
      v1 = triangles->v1;
      v2 = triangles->v2;
      OGL.triangles.vertices[v0].s = _FIXED2FLOAT( triangles->s0, 5 );
      OGL.triangles.vertices[v0].t = _FIXED2FLOAT( triangles->t0, 5 );
      OGL.triangles.vertices[v1].s = _FIXED2FLOAT( triangles->s1, 5 );
      OGL.triangles.vertices[v1].t = _FIXED2FLOAT( triangles->t1, 5 );
      OGL.triangles.vertices[v2].s = _FIXED2FLOAT( triangles->s2, 5 );
      OGL.triangles.vertices[v2].t = _FIXED2FLOAT( triangles->t2, 5 );
      gSPTriangle(triangles->v0, triangles->v1, triangles->v2);
      triangles++;
   }

   OGL_DrawTriangles();
}

void gSP1Quadrangle( s32 v0, s32 v1, s32 v2, s32 v3)
{
   gSPTriangle( v0, v1, v2);
   gSPTriangle( v0, v2, v3);
   gSPFlushTriangles();
}

bool gSPCullVertices( u32 v0, u32 vn )
{
   unsigned int i;
   u32 v;
   u32 clip;

   if (!config.enableClipping)
      return FALSE;

   v = v0;
   clip = OGL.triangles.vertices[v].clip;
   if (clip == 0)
      return FALSE;

   for (i = v0 + 1; i <= vn; i++)
   {
      v = i;
      if (OGL.triangles.vertices[v].clip != clip) return FALSE;
   }
   return TRUE;
}

void gSPCullDisplayList( u32 v0, u32 vn )
{
   if (gSPCullVertices( v0, vn ))
   {
      if (__RSP.PCi > 0)
         __RSP.PCi--;
      else
         __RSP.halt = TRUE;
   }
}

void gSPPopMatrixN( u32 param, u32 num )
{
   if (gSP.matrix.modelViewi > num - 1)
   {
      gSP.matrix.modelViewi -= num;

      gSP.changed |= CHANGED_MATRIX;
   }
}

void gSPPopMatrix( u32 param )
{
   if (gSP.matrix.modelViewi > 0)
   {
      gSP.matrix.modelViewi--;

      gSP.changed |= CHANGED_MATRIX;
   }
}

void gSPSegment( s32 seg, s32 base )
{
    if (seg > 0xF)
        return;

    if ((unsigned int)base > RDRAMSize - 1)
        return;

    gSP.segment[seg] = base;
}

void gSPClipRatio( u32 r )
{
}

void gSPInsertMatrix( u32 where, u32 num )
{
   f32 fraction, integer;

   if (gSP.changed & CHANGED_MATRIX)
      gSPCombineMatrices();

   if ((where & 0x3) || (where > 0x3C))
      return;

   if (where < 0x20)
   {
      fraction = modff( gSP.matrix.combined[0][where >> 1], &integer );
      gSP.matrix.combined[0][where >> 1]
       = (f32)_SHIFTR( num, 16, 16 ) + abs( (int)fraction );

      fraction = modff( gSP.matrix.combined[0][(where >> 1) + 1], &integer );
      gSP.matrix.combined[0][(where >> 1) + 1]
       = (f32)_SHIFTR( num, 0, 16 ) + abs( (int)fraction );
   }
   else
   {
      f32 newValue;

      fraction = modff( gSP.matrix.combined[0][(where - 0x20) >> 1], &integer );
      newValue = integer + _FIXED2FLOAT( _SHIFTR( num, 16, 16 ), 16);

      // Make sure the sign isn't lost
      if ((integer == 0.0f) && (fraction != 0.0f))
         newValue = newValue * (fraction / abs( (int)fraction ));

      gSP.matrix.combined[0][(where - 0x20) >> 1] = newValue;

      fraction = modff( gSP.matrix.combined[0][((where - 0x20) >> 1) + 1], &integer );
      newValue = integer + _FIXED2FLOAT( _SHIFTR( num, 0, 16 ), 16 );

      // Make sure the sign isn't lost
      if ((integer == 0.0f) && (fraction != 0.0f))
         newValue = newValue * (fraction / abs( (int)fraction ));

      gSP.matrix.combined[0][((where - 0x20) >> 1) + 1] = newValue;
   }
}

void gSPModifyVertex( u32 vtx, u32 where, u32 val )
{
   s32 v = vtx;

   switch (where)
   {
      case G_MWO_POINT_RGBA:
         OGL.triangles.vertices[v].r = _SHIFTR( val, 24, 8 ) * 0.0039215689f;
         OGL.triangles.vertices[v].g = _SHIFTR( val, 16, 8 ) * 0.0039215689f;
         OGL.triangles.vertices[v].b = _SHIFTR( val, 8, 8 ) * 0.0039215689f;
         OGL.triangles.vertices[v].a = _SHIFTR( val, 0, 8 ) * 0.0039215689f;
         break;
      case G_MWO_POINT_ST:
         OGL.triangles.vertices[v].s = _FIXED2FLOAT( (s16)_SHIFTR( val, 16, 16 ), 5 );
         OGL.triangles.vertices[v].t = _FIXED2FLOAT( (s16)_SHIFTR( val, 0, 16 ), 5 );
         break;
      case G_MWO_POINT_XYSCREEN:
         break;
      case G_MWO_POINT_ZSCREEN:
         break;
   }
}

void gSPNumLights( s32 n )
{
   gSP.numLights = (n <= 8) ? n : 0;
}


void gSPLightColor( u32 lightNum, u32 packedColor )
{
   lightNum--;

   if (lightNum < 8)
   {
      gSP.lights[lightNum].r = _SHIFTR( packedColor, 24, 8 ) * 0.0039215689f;
      gSP.lights[lightNum].g = _SHIFTR( packedColor, 16, 8 ) * 0.0039215689f;
      gSP.lights[lightNum].b = _SHIFTR( packedColor, 8, 8 ) * 0.0039215689f;
   }
}

void gSPFogFactor( s16 fm, s16 fo )
{
   gSP.fog.multiplier = fm;
   gSP.fog.offset = fo;

   gSP.changed |= CHANGED_FOGPOSITION;
}

void gSPPerspNormalize( u16 scale )
{
}

void gSPTexture( f32 sc, f32 tc, s32 level, s32 tile, s32 on )
{
   gSP.texture.scales = sc;
   gSP.texture.scalet = tc;

   if (gSP.texture.scales == 0.0f) gSP.texture.scales = 1.0f;
   if (gSP.texture.scalet == 0.0f) gSP.texture.scalet = 1.0f;

   gSP.texture.level = level;
   gSP.texture.on = on;

   if (gSP.texture.tile != tile)
   {
      gSP.texture.tile = tile;
      gSP.textureTile[0] = &gDP.tiles[tile];
      gSP.textureTile[1] = &gDP.tiles[(tile < 7) ? (tile + 1) : tile];
      gSP.changed |= CHANGED_TEXTURE;
   }

   gSP.changed |= CHANGED_TEXTURESCALE;
}

void gSPEndDisplayList(void)
{
   if (__RSP.PCi > 0)
      __RSP.PCi--;
   else
      __RSP.halt = TRUE;
}

void gSPGeometryMode( u32 clear, u32 set )
{
   gSP.geometryMode = (gSP.geometryMode & ~clear) | set;
   gSP.changed |= CHANGED_GEOMETRYMODE;
}

void gSPSetGeometryMode( u32 mode )
{
   gSP.geometryMode |= mode;
   gSP.changed |= CHANGED_GEOMETRYMODE;
}

void gSPClearGeometryMode( u32 mode )
{
   gSP.geometryMode &= ~mode;
   gSP.changed |= CHANGED_GEOMETRYMODE;
}

void gSPSetOtherMode_H(u32 _length, u32 _shift, u32 _data)
{
	const u32 mask = (((u64)1 << _length) - 1) << _shift;
	gDP.otherMode.h = (gDP.otherMode.h&(~mask)) | _data;

	if (mask & 0x00300000)  // cycle type
		gDP.changed |= CHANGED_CYCLETYPE;
}

void gSPSetOtherMode_L(u32 _length, u32 _shift, u32 _data)
{
	const u32 mask = (((u64)1 << _length) - 1) << _shift;
	gDP.otherMode.l = (gDP.otherMode.l&(~mask)) | _data;

	if (mask & 0x00000003)  // alpha compare
		gDP.changed |= CHANGED_ALPHACOMPARE;

	if (mask & 0xFFFFFFF8)  // rendermode / blender bits
		gDP.changed |= CHANGED_RENDERMODE;
}

void gSPLine3D( s32 v0, s32 v1, s32 flag )
{
   OGL_DrawLine(v0, v1, 1.5f );
}

void gSPLineW3D( s32 v0, s32 v1, s32 wd, s32 flag )
{
   OGL_DrawLine(v0, v1, 1.5f + wd * 0.5f );
}

void gSPBgRect1Cyc( u32 bg )
{
#if 1
   uint16_t imageFlip;
   u32 addr;
   f32 imageX, imageY, imageW, imageH, frameX, frameY, frameW, frameH, scaleW, scaleH,
	   frameX0, frameX1, frameY0, frameY1, frameS0, frameT0;
   addr = RSP_SegmentToPhysical(bg) >> 1;

   imageX = (f32)(((uint16_t*)gfx_info.RDRAM)[(addr+0)^1] >> 5); /* 0 */
   imageY = (f32)(((uint16_t*)gfx_info.RDRAM)[(addr+4)^1] >> 5); /* 4 */
   imageW = (f32)(((uint16_t*)gfx_info.RDRAM)[(addr+1)^1] >> 2); /* 1 */
   imageH = (f32)(((uint16_t*)gfx_info.RDRAM)[(addr+5)^1] >> 2); /* 5 */

   frameX = (f32)((int16_t*)gfx_info.RDRAM)[(addr+2)^1] / 4.0f;  /* 2 */
   frameY = (f32)((int16_t*)gfx_info.RDRAM)[(addr+6)^1] / 4.0f;  /* 6 */
   frameW = (f32)(((uint16_t*)gfx_info.RDRAM)[(addr+3)^1] >> 2); /* 3 */
   frameH = (f32)(((uint16_t*)gfx_info.RDRAM)[(addr+7)^1] >> 2); /* 7 */


   imageFlip = ((uint16_t*)gfx_info.RDRAM)[(addr+13)^1];	// 13;
   //d.flipX 	= (uint8_t)imageFlip&0x01;

   gSP.bgImage.address	= RSP_SegmentToPhysical(((u32*)gfx_info.RDRAM)[(addr+8)>>1]);	// 8,9
   gSP.bgImage.width = (u32)imageW;
   gSP.bgImage.height = (u32)imageH;
   gSP.bgImage.format = ((u8*)gfx_info.RDRAM)[(((addr+11)<<1)+0)^3];
   gSP.bgImage.size = ((u8*)gfx_info.RDRAM)[(((addr+11)<<1)+1)^3];
   gSP.bgImage.palette = ((u16*)gfx_info.RDRAM)[(addr+12)^1];

   scaleW	= ((s16*)gfx_info.RDRAM)[(addr+14)^1] / 1024.0f;	// 14
   scaleH	= ((s16*)gfx_info.RDRAM)[(addr+15)^1] / 1024.0f;	// 15
   gDP.textureMode = TEXTUREMODE_BGIMAGE;

#else
   u32 address = RSP_SegmentToPhysical( bg );
   uObjScaleBg *objScaleBg = (uObjScaleBg*)&gfx_info.RDRAM[address];

   gSP.bgImage.address = RSP_SegmentToPhysical( objScaleBg->imagePtr );
   gSP.bgImage.width = objScaleBg->imageW >> 2;
   gSP.bgImage.height = objScaleBg->imageH >> 2;
   gSP.bgImage.format = objScaleBg->imageFmt;
   gSP.bgImage.size = objScaleBg->imageSiz;
   gSP.bgImage.palette = objScaleBg->imagePal;
   gDP.textureMode = TEXTUREMODE_BGIMAGE;

   f32 imageX = _FIXED2FLOAT( objScaleBg->imageX, 5 );
   f32 imageY = _FIXED2FLOAT( objScaleBg->imageY, 5 );
   f32 imageW = objScaleBg->imageW >> 2;
   f32 imageH = objScaleBg->imageH >> 2;

   f32 frameX = _FIXED2FLOAT( objScaleBg->frameX, 2 );
   f32 frameY = _FIXED2FLOAT( objScaleBg->frameY, 2 );
   f32 frameW = _FIXED2FLOAT( objScaleBg->frameW, 2 );
   f32 frameH = _FIXED2FLOAT( objScaleBg->frameH, 2 );
   f32 scaleW = _FIXED2FLOAT( objScaleBg->scaleW, 10 );
   f32 scaleH = _FIXED2FLOAT( objScaleBg->scaleH, 10 );
#endif

   frameX0 = frameX;
   frameY0 = frameY;
   frameS0 = imageX;
   frameT0 = imageY;

   frameX1 = frameX + min( (imageW - imageX) / scaleW, frameW );
   frameY1 = frameY + min( (imageH - imageY) / scaleH, frameH );
   //f32 frameS1 = imageX + min( (imageW - imageX) * scaleW, frameW * scaleW );
   //f32 frameT1 = imageY + min( (imageH - imageY) * scaleH, frameH * scaleH );

   gDP.otherMode.cycleType = G_CYC_1CYCLE;
   gDP.changed |= CHANGED_CYCLETYPE;
   gSPTexture( 1.0f, 1.0f, 0, 0, TRUE );
   gDPTextureRectangle( frameX0, frameY0, frameX1 - 1, frameY1 - 1, 0, frameS0 - 1, frameT0 - 1, scaleW, scaleH );

   if ((frameX1 - frameX0) < frameW)
   {
      f32 frameX2 = frameW - (frameX1 - frameX0) + frameX1;
      gDPTextureRectangle( frameX1, frameY0, frameX2 - 1, frameY1 - 1, 0, 0, frameT0, scaleW, scaleH );
   }

   if ((frameY1 - frameY0) < frameH)
   {
      f32 frameY2 = frameH - (frameY1 - frameY0) + frameY1;
      gDPTextureRectangle( frameX0, frameY1, frameX1 - 1, frameY2 - 1, 0, frameS0, 0, scaleW, scaleH );
   }

   gDPTextureRectangle( 0, 0, 319, 239, 0, 0, 0, scaleW, scaleH );
}

void gSPBgRectCopy( u32 bg )
{
   u16 imageX, imageY, frameW, frameH;
   s16 frameX, frameY;
   u32 address = RSP_SegmentToPhysical( bg );
   uObjBg *objBg = (uObjBg*)&gfx_info.RDRAM[address];

   gSP.bgImage.address = RSP_SegmentToPhysical( objBg->imagePtr );
   gSP.bgImage.width = objBg->imageW >> 2;
   gSP.bgImage.height = objBg->imageH >> 2;
   gSP.bgImage.format = objBg->imageFmt;
   gSP.bgImage.size = objBg->imageSiz;
   gSP.bgImage.palette = objBg->imagePal;
   gDP.textureMode = TEXTUREMODE_BGIMAGE;

   imageX = objBg->imageX >> 5;
   imageY = objBg->imageY >> 5;

   frameX = objBg->frameX / 4;
   frameY = objBg->frameY / 4;
   frameW = objBg->frameW >> 2;
   frameH = objBg->frameH >> 2;

   gSPTexture( 1.0f, 1.0f, 0, 0, TRUE );

   gDPTextureRectangle(
      frameX,
      frameY,
      frameX + frameW - 1.f,
      frameY + frameH - 1.f,
      0,
      imageX,
      imageY,
      4,
      1
   );
}

void gSPObjRectangle( u32 sp )
{
   u32 address = RSP_SegmentToPhysical( sp );
   uObjSprite *objSprite = (uObjSprite*)&gfx_info.RDRAM[address];

   f32 scaleW = _FIXED2FLOAT( objSprite->scaleW, 10 );
   f32 scaleH = _FIXED2FLOAT( objSprite->scaleH, 10 );
   f32 objX = _FIXED2FLOAT( objSprite->objX, 2 );
   f32 objY = _FIXED2FLOAT( objSprite->objY, 2 );
   u32 imageW = objSprite->imageW >> 2;
   u32 imageH = objSprite->imageH >> 2;

   gDPTextureRectangle( objX, objY, objX + imageW / scaleW - 1, objY + imageH / scaleH - 1, 0, 0.0f, 0.0f, scaleW * (gDP.otherMode.cycleType == G_CYC_COPY ? 4.0f : 1.0f), scaleH );
}

void gSPObjLoadTxtr( u32 tx )
{
   u32 address = RSP_SegmentToPhysical( tx );
   uObjTxtr *objTxtr = (uObjTxtr*)&gfx_info.RDRAM[address];

   if ((gSP.status[objTxtr->block.sid >> 2] & objTxtr->block.mask) != objTxtr->block.flag)
   {
      switch (objTxtr->block.type)
      {
         case G_OBJLT_TXTRBLOCK:
            gDPSetTextureImage( 0, 1, 0, objTxtr->block.image );
            gDPSetTile( 0, 1, 0, objTxtr->block.tmem, 7, 0, 0, 0, 0, 0, 0, 0 );
            gDPLoadBlock( 7, 0, 0, ((objTxtr->block.tsize + 1) << 3) - 1, objTxtr->block.tline );
            break;
         case G_OBJLT_TXTRTILE:
            gDPSetTextureImage( 0, 1, (objTxtr->tile.twidth + 1) << 1, objTxtr->tile.image );
            gDPSetTile( 0, 1, (objTxtr->tile.twidth + 1) >> 2, objTxtr->tile.tmem, 7, 0, 0, 0, 0, 0, 0, 0 );
            gDPLoadTile( 7, 0, 0, (((objTxtr->tile.twidth + 1) << 1) - 1) << 2, (((objTxtr->tile.theight + 1) >> 2) - 1) << 2 );
            break;
         case G_OBJLT_TLUT:
            gDPSetTextureImage( 0, 2, 1, objTxtr->tlut.image );
            gDPSetTile( 0, 2, 0, objTxtr->tlut.phead, 7, 0, 0, 0, 0, 0, 0, 0 );
            gDPLoadTLUT( 7, 0, 0, objTxtr->tlut.pnum << 2, 0 );
            break;
      }
      gSP.status[objTxtr->block.sid >> 2] = (gSP.status[objTxtr->block.sid >> 2] & ~objTxtr->block.mask) | (objTxtr->block.flag & objTxtr->block.mask);
   }
}

void gSPObjSprite( u32 sp )
{
   u32 address = RSP_SegmentToPhysical( sp );
   uObjSprite *objSprite = (uObjSprite*)&gfx_info.RDRAM[address];

   f32 scaleW = _FIXED2FLOAT( objSprite->scaleW, 10 );
   f32 scaleH = _FIXED2FLOAT( objSprite->scaleH, 10 );
   f32 objX = _FIXED2FLOAT( objSprite->objX, 2 );
   f32 objY = _FIXED2FLOAT( objSprite->objY, 2 );
   u32 imageW = objSprite->imageW >> 5;
   u32 imageH = objSprite->imageH >> 5;

   f32 x0 = objX;
   f32 y0 = objY;
   f32 x1 = objX + imageW / scaleW - 1;
   f32 y1 = objY + imageH / scaleH - 1;

   s32 v0=0,v1=1,v2=2,v3=3;


   OGL.triangles.vertices[v0].x = gSP.objMatrix.A * x0 + gSP.objMatrix.B * y0 + gSP.objMatrix.X;
   OGL.triangles.vertices[v0].y = gSP.objMatrix.C * x0 + gSP.objMatrix.D * y0 + gSP.objMatrix.Y;
   OGL.triangles.vertices[v0].z = 0.0f;
   OGL.triangles.vertices[v0].w = 1.0f;
   OGL.triangles.vertices[v0].s = 0.0f;
   OGL.triangles.vertices[v0].t = 0.0f;
   OGL.triangles.vertices[v1].x = gSP.objMatrix.A * x1 + gSP.objMatrix.B * y0 + gSP.objMatrix.X;
   OGL.triangles.vertices[v1].y = gSP.objMatrix.C * x1 + gSP.objMatrix.D * y0 + gSP.objMatrix.Y;
   OGL.triangles.vertices[v1].z = 0.0f;
   OGL.triangles.vertices[v1].w = 1.0f;
   OGL.triangles.vertices[v1].s = imageW - 1.f;
   OGL.triangles.vertices[v1].t = 0.0f;
   OGL.triangles.vertices[v2].x = gSP.objMatrix.A * x1 + gSP.objMatrix.B * y1 + gSP.objMatrix.X;
   OGL.triangles.vertices[v2].y = gSP.objMatrix.C * x1 + gSP.objMatrix.D * y1 + gSP.objMatrix.Y;
   OGL.triangles.vertices[v2].z = 0.0f;
   OGL.triangles.vertices[v2].w = 1.0f;
   OGL.triangles.vertices[v2].s = imageW - 1.f;
   OGL.triangles.vertices[v2].t = imageH - 1.f;
   OGL.triangles.vertices[v3].x = gSP.objMatrix.A * x0 + gSP.objMatrix.B * y1 + gSP.objMatrix.X;
   OGL.triangles.vertices[v3].y = gSP.objMatrix.C * x0 + gSP.objMatrix.D * y1 + gSP.objMatrix.Y;
   OGL.triangles.vertices[v3].z = 0.0f;
   OGL.triangles.vertices[v3].w = 1.0f;
   OGL.triangles.vertices[v3].s = 0;
   OGL.triangles.vertices[v3].t = imageH - 1.f;

   gDPSetTile( objSprite->imageFmt, objSprite->imageSiz, objSprite->imageStride, objSprite->imageAdrs, 0, objSprite->imagePal, G_TX_CLAMP, G_TX_CLAMP, 0, 0, 0, 0 );
   gDPSetTileSize( 0, 0, 0, (imageW - 1) << 2, (imageH - 1) << 2 );
   gSPTexture( 1.0f, 1.0f, 0, 0, TRUE );

   //glOrtho( 0, VI.width, VI.height, 0, 0.0f, 32767.0f );
   OGL.triangles.vertices[v0].x = 2.0f * VI.rwidth * OGL.triangles.vertices[v0].x - 1.0f;
   OGL.triangles.vertices[v0].y = -2.0f * VI.rheight * OGL.triangles.vertices[v0].y + 1.0f;
   OGL.triangles.vertices[v0].z = -1.0f;
   OGL.triangles.vertices[v0].w = 1.0f;
   OGL.triangles.vertices[v1].x = 2.0f * VI.rwidth * OGL.triangles.vertices[v0].x - 1.0f;
   OGL.triangles.vertices[v1].y = -2.0f * VI.rheight * OGL.triangles.vertices[v0].y + 1.0f;
   OGL.triangles.vertices[v1].z = -1.0f;
   OGL.triangles.vertices[v1].w = 1.0f;
   OGL.triangles.vertices[v2].x = 2.0f * VI.rwidth * OGL.triangles.vertices[v0].x - 1.0f;
   OGL.triangles.vertices[v2].y = -2.0f * VI.rheight * OGL.triangles.vertices[v0].y + 1.0f;
   OGL.triangles.vertices[v2].z = -1.0f;
   OGL.triangles.vertices[v2].w = 1.0f;
   OGL.triangles.vertices[v3].x = 2.0f * VI.rwidth * OGL.triangles.vertices[v0].x - 1.0f;
   OGL.triangles.vertices[v3].y = -2.0f * VI.rheight * OGL.triangles.vertices[v0].y + 1.0f;
   OGL.triangles.vertices[v3].z = -1.0f;
   OGL.triangles.vertices[v3].w = 1.0f;

   OGL_AddTriangle(v0, v1, v2);
   OGL_AddTriangle(v0, v2, v3);
   OGL_DrawTriangles();

   if (depthBuffer.current) depthBuffer.current->cleared = FALSE;
   gDP.colorImage.changed = TRUE;
   gDP.colorImage.height = (unsigned int)(max( gDP.colorImage.height, gDP.scissor.lry ));
}

void gSPObjLoadTxSprite( u32 txsp )
{
   gSPObjLoadTxtr( txsp );
   gSPObjSprite( txsp + sizeof( uObjTxtr ) );
}

void gSPObjLoadTxRectR( u32 txsp )
{
   gSPObjLoadTxtr( txsp );
   //gSPObjRectangleR( txsp + sizeof( uObjTxtr ) );
}

void gSPObjMatrix( u32 mtx )
{
   u32 address = RSP_SegmentToPhysical( mtx );
   uObjMtx *objMtx = (uObjMtx*)&gfx_info.RDRAM[address];

   gSP.objMatrix.A = _FIXED2FLOAT( objMtx->A, 16 );
   gSP.objMatrix.B = _FIXED2FLOAT( objMtx->B, 16 );
   gSP.objMatrix.C = _FIXED2FLOAT( objMtx->C, 16 );
   gSP.objMatrix.D = _FIXED2FLOAT( objMtx->D, 16 );
   gSP.objMatrix.X = _FIXED2FLOAT( objMtx->X, 2 );
   gSP.objMatrix.Y = _FIXED2FLOAT( objMtx->Y, 2 );
   gSP.objMatrix.baseScaleX = _FIXED2FLOAT( objMtx->BaseScaleX, 10 );
   gSP.objMatrix.baseScaleY = _FIXED2FLOAT( objMtx->BaseScaleY, 10 );
}

void gSPObjSubMatrix( u32 mtx )
{
}

void (*gSPTransformVertex)(float vtx[4], float mtx[4][4]) =
        gSPTransformVertex_default;
void (*gSPLightVertex)(u32 v) = gSPLightVertex_default;
void (*gSPBillboardVertex)(u32 v, u32 i) = gSPBillboardVertex_default;

