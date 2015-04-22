
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <direct.h>
#else
#include <unistd.h>
#endif

#include "Common.h"
#include "gles2N64.h"
#include "OpenGL.h"
#include "Types.h"
#include "N64.h"
#include "gSP.h"
#include "gDP.h"
#include "Textures.h"
#include "ShaderCombiner.h"
#include "VI.h"
#include "RSP.h"
#include "Config.h"

void OGL_UpdateDepthUpdate(void);

GLInfo OGL;

void OGL_EnableRunfast(void)
{
}

int OGL_IsExtSupported( const char *extension )
{
   const GLubyte *extensions = NULL;
   const GLubyte *start;
   GLubyte *where, *terminator;

   where = (GLubyte *) strchr(extension, ' ');
   if (where || *extension == '\0')
      return 0;

   extensions = glGetString(GL_EXTENSIONS);

   if (!extensions) return 0;

   start = extensions;
   for (;;)
   {
      where = (GLubyte *) strstr((const char *) start, extension);
      if (!where)
         break;

      terminator = where + strlen(extension);
      if (where == start || *(where - 1) == ' ')
         if (*terminator == ' ' || *terminator == '\0')
            return 1;

      start = terminator;
   }

   return 0;
}

extern void _glcompiler_error(GLint shader);

void OGL_InitStates(void)
{
   glEnable( GL_CULL_FACE );
   glEnableVertexAttribArray( SC_POSITION );
   glEnable( GL_DEPTH_TEST );
   glDepthFunc( GL_ALWAYS );
   glDepthMask( GL_FALSE );
   glEnable( GL_SCISSOR_TEST );

   glDepthRange(0.0f, 1.0f);
   glPolygonOffset(-0.2f, -0.2f);
   glViewport(0, 0, config.screen.width, config.screen.height);
}

void OGL_UpdateScale(void)
{
   OGL.scaleX = (float)config.screen.width / (float)VI.width;
   OGL.scaleY = (float)config.screen.height / (float)VI.height;
}

bool OGL_Start(void)
{
   float f;
   OGL_InitStates();

   //check extensions
   if ((config.texture.maxAnisotropy>0) && !OGL_IsExtSupported("GL_EXT_texture_filter_anistropic"))
   {
      LOG(LOG_WARNING, "Anistropic Filtering is not supported.\n");
      config.texture.maxAnisotropy = 0;
   }

   f = 0;
   glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &f);
   if (config.texture.maxAnisotropy > ((int)f))
   {
      LOG(LOG_WARNING, "Clamping max anistropy to %ix.\n", (int)f);
      config.texture.maxAnisotropy = (int)f;
   }

   //Print some info
   LOG(LOG_VERBOSE, "[gles2n64]: Enable Runfast... \n");

   OGL_EnableRunfast();

   //We must have a shader bound before binding any textures:
   ShaderCombiner_Init();
   ShaderCombiner_Set(EncodeCombineMode(0, 0, 0, TEXEL0, 0, 0, 0, TEXEL0, 0, 0, 0, TEXEL0, 0, 0, 0, TEXEL0), -1);
   ShaderCombiner_Set(EncodeCombineMode(0, 0, 0, SHADE, 0, 0, 0, 1, 0, 0, 0, SHADE, 0, 0, 0, 1), -1);

   TextureCache_Init();

   memset(OGL.triangles.vertices, 0, VERTBUFF_SIZE * sizeof(SPVertex));
   memset(OGL.triangles.elements, 0, ELEMBUFF_SIZE * sizeof(GLubyte));
   OGL.triangles.num = 0;

   OGL.renderingToTexture = false;
   OGL.renderState = RS_NONE;
   gSP.changed = gDP.changed = 0xFFFFFFFF;
   VI.displayNum = 0;
   glGetError();

   return TRUE;
}

void OGL_Stop(void)
{
   LOG(LOG_MINIMAL, "Stopping OpenGL\n");

   ShaderCombiner_Destroy();
   TextureCache_Destroy();
}

void OGL_UpdateCullFace(void)
{
   if (config.enableFaceCulling && (gSP.geometryMode & G_CULL_BOTH))
   {
      glEnable( GL_CULL_FACE );
      if ((gSP.geometryMode & G_CULL_BACK) && (gSP.geometryMode & G_CULL_FRONT))
         glCullFace(GL_FRONT_AND_BACK);
      else if (gSP.geometryMode & G_CULL_BACK)
         glCullFace(GL_BACK);
      else
         glCullFace(GL_FRONT);
   }
   else
      glDisable(GL_CULL_FACE);
}

void OGL_UpdateViewport(void)
{
   int x, y, w, h;
   x = (int)(gSP.viewport.x * OGL.scaleX);
   y = (int)((VI.height - (gSP.viewport.y + gSP.viewport.height)) * OGL.scaleY);
   w = (int)(gSP.viewport.width * OGL.scaleX);
   h = (int)(gSP.viewport.height * OGL.scaleY);
   glViewport(x, y, w, h);
}

void OGL_UpdateDepthUpdate(void)
{
   if (gDP.otherMode.depthUpdate)
      glDepthMask(GL_TRUE);
   else
      glDepthMask(GL_FALSE);
}

void OGL_UpdateScissor(void)
{
   int x, y, w, h;
   x = (int)(gDP.scissor.ulx * OGL.scaleX);
   y = (int)((VI.height - gDP.scissor.lry) * OGL.scaleY);
   w = (int)((gDP.scissor.lrx - gDP.scissor.ulx) * OGL.scaleX);
   h = (int)((gDP.scissor.lry - gDP.scissor.uly) * OGL.scaleY);

   glScissor(x, y, w, h);
}

//copied from RICE VIDEO
void OGL_SetBlendMode(void)
{

   u32 blender = gDP.otherMode.l >> 16;
   u32 blendmode_1 = blender&0xcccc;
   u32 blendmode_2 = blender&0x3333;

   glEnable(GL_BLEND);
   switch(gDP.otherMode.cycleType)
   {
      case G_CYC_FILL:
         glDisable(GL_BLEND);
         break;

      case G_CYC_COPY:
         glBlendFunc(GL_ONE, GL_ZERO);
         break;

      case G_CYC_2CYCLE:
         if (gDP.otherMode.forceBlender && gDP.otherMode.depthCompare)
         {
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            break;
         }

         switch(blendmode_1+blendmode_2)
         {
            case BLEND_PASS+(BLEND_PASS>>2):    // In * 0 + In * 1
            case BLEND_FOG_APRIM+(BLEND_PASS>>2):
            case BLEND_FOG_MEM_FOG_MEM + (BLEND_OPA>>2):
            case BLEND_FOG_APRIM + (BLEND_OPA>>2):
            case BLEND_FOG_ASHADE + (BLEND_OPA>>2):
            case BLEND_BI_AFOG + (BLEND_OPA>>2):
            case BLEND_FOG_ASHADE + (BLEND_NOOP>>2):
            case BLEND_NOOP + (BLEND_OPA>>2):
            case BLEND_NOOP4 + (BLEND_NOOP>>2):
            case BLEND_FOG_ASHADE+(BLEND_PASS>>2):
            case BLEND_FOG_3+(BLEND_PASS>>2):
               glDisable(GL_BLEND);
               break;

            case BLEND_PASS+(BLEND_OPA>>2):
               if (gDP.otherMode.cvgXAlpha && gDP.otherMode.alphaCvgSel)
               glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
               else
                  glDisable(GL_BLEND);
               break;

            case BLEND_PASS + (BLEND_XLU>>2):
            case BLEND_FOG_ASHADE + (BLEND_XLU>>2):
            case BLEND_FOG_APRIM + (BLEND_XLU>>2):
            case BLEND_FOG_MEM_FOG_MEM + (BLEND_PASS>>2):
            case BLEND_XLU + (BLEND_XLU>>2):
            case BLEND_BI_AFOG + (BLEND_XLU>>2):
            case BLEND_XLU + (BLEND_FOG_MEM_IN_MEM>>2):
            case BLEND_PASS + (BLEND_FOG_MEM_IN_MEM>>2):
               glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
               break;

            case BLEND_FOG_ASHADE+0x0301:
               glBlendFunc(GL_SRC_ALPHA, GL_ZERO);
               break;

            case 0x0c08+0x1111:
               glBlendFunc(GL_ZERO, GL_DST_ALPHA);
               break;

            default:
               if (blendmode_2 == (BLEND_PASS>>2))
                  glDisable(GL_BLEND);
               else
                  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
               break;
         }
         break;

      default:

         if (gDP.otherMode.forceBlender && gDP.otherMode.depthCompare && blendmode_1 != BLEND_FOG_ASHADE )
         {
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            break;
         }

         switch (blendmode_1)
         {
            case BLEND_XLU:
            case BLEND_BI_AIN:
            case BLEND_FOG_MEM:
            case BLEND_FOG_MEM_IN_MEM:
            case BLEND_BLENDCOLOR:
            case 0x00c0:
               glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
               break;

            case BLEND_MEM_ALPHA_IN:
               glBlendFunc(GL_ZERO, GL_DST_ALPHA);
               break;

            case BLEND_OPA:
               //if( options.enableHackForGames == HACK_FOR_MARIO_TENNIS )
               //{
               //   glBlendFunc(BLEND_SRCALPHA, BLEND_INVSRCALPHA);
               //}

               glDisable(GL_BLEND);
               break;

            case BLEND_PASS:
            case BLEND_NOOP:
            case BLEND_FOG_ASHADE:
            case BLEND_FOG_MEM_3:
            case BLEND_BI_AFOG:
               glDisable(GL_BLEND);
               break;

            case BLEND_FOG_APRIM:
               glBlendFunc(GL_ONE_MINUS_SRC_ALPHA, GL_ZERO);
               break;

            case BLEND_NOOP3:
            case BLEND_NOOP5:
            case BLEND_MEM:
               glBlendFunc(GL_ZERO, GL_ONE);
               break;

            default:
               glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
         }
   }

}

void OGL_UpdateStates(void)
{
   if (gDP.otherMode.cycleType == G_CYC_COPY)
      ShaderCombiner_Set(EncodeCombineMode(0, 0, 0, TEXEL0, 0, 0, 0, TEXEL0, 0, 0, 0, TEXEL0, 0, 0, 0, TEXEL0), -1);
   else if (gDP.otherMode.cycleType == G_CYC_FILL)
      ShaderCombiner_Set(EncodeCombineMode(0, 0, 0, SHADE, 0, 0, 0, 1, 0, 0, 0, SHADE, 0, 0, 0, 1), -1);
   else
      ShaderCombiner_Set(gDP.combine.mux, -1);

   if (gSP.changed & CHANGED_GEOMETRYMODE)
   {
      OGL_UpdateCullFace();

      if (gSP.geometryMode & G_ZBUFFER)
         glEnable(GL_DEPTH_TEST);
      else
         glDisable(GL_DEPTH_TEST);

   }

   if (gDP.changed & CHANGED_CONVERT)
   {
      SC_SetUniform1f(uK4, gDP.convert.k4);
      SC_SetUniform1f(uK5, gDP.convert.k5);
   }

   if (gDP.changed & CHANGED_RENDERMODE || gDP.changed & CHANGED_CYCLETYPE)
   {
      if (gDP.otherMode.cycleType == G_CYC_1CYCLE || gDP.otherMode.cycleType == G_CYC_2CYCLE)
      {
         //glDepthFunc((gDP.otherMode.depthCompare) ? GL_GEQUAL : GL_ALWAYS);
         glDepthFunc((gDP.otherMode.depthCompare) ? GL_LESS : GL_ALWAYS);
         glDepthMask((gDP.otherMode.depthUpdate) ? GL_TRUE : GL_FALSE);

         if (gDP.otherMode.depthMode == ZMODE_DEC)
            glEnable(GL_POLYGON_OFFSET_FILL);
         else
            glDisable(GL_POLYGON_OFFSET_FILL);
      }
      else
      {
         glDepthFunc(GL_ALWAYS);
         glDepthMask(GL_FALSE);
      }
   }

   if ((gDP.changed & CHANGED_BLENDCOLOR) || (gDP.changed & CHANGED_RENDERMODE))
      SC_SetUniform1f(uAlphaRef, (gDP.otherMode.cvgXAlpha) ? 0.5f : gDP.blendColor.a);

   if (gDP.changed & CHANGED_SCISSOR)
      OGL_UpdateScissor();

   if (gSP.changed & CHANGED_VIEWPORT)
      OGL_UpdateViewport();

   if (gSP.changed & CHANGED_FOGPOSITION)
   {
      SC_SetUniform1f(uFogMultiplier, (float) gSP.fog.multiplier / 255.0f);
      SC_SetUniform1f(uFogOffset, (float) gSP.fog.offset / 255.0f);
   }

   if (gSP.changed & CHANGED_TEXTURESCALE)
   {
      if (scProgramCurrent->usesT0 || scProgramCurrent->usesT1)
         SC_SetUniform2f(uTexScale, gSP.texture.scales, gSP.texture.scalet);
   }

   if ((gSP.changed & CHANGED_TEXTURE) || (gDP.changed & CHANGED_TILE) || (gDP.changed & CHANGED_TMEM))
   {
      //For some reason updating the texture cache on the first frame of LOZ:OOT causes a NULL Pointer exception...
      if (scProgramCurrent)
      {
         if (scProgramCurrent->usesT0)
         {
            TextureCache_Update(0);
            SC_ForceUniform2f(uTexOffset[0], gSP.textureTile[0]->fuls, gSP.textureTile[0]->fult);
            SC_ForceUniform2f(uCacheShiftScale[0], cache.current[0]->shiftScaleS, cache.current[0]->shiftScaleT);
            SC_ForceUniform2f(uCacheScale[0], cache.current[0]->scaleS, cache.current[0]->scaleT);
            SC_ForceUniform2f(uCacheOffset[0], cache.current[0]->offsetS, cache.current[0]->offsetT);
         }
         //else TextureCache_ActivateDummy(0);

         //Note: enabling dummies makes some F-zero X textures flicker.... strange.

         if (scProgramCurrent->usesT1)
         {
            TextureCache_Update(1);
            SC_ForceUniform2f(uTexOffset[1], gSP.textureTile[1]->fuls, gSP.textureTile[1]->fult);
            SC_ForceUniform2f(uCacheShiftScale[1], cache.current[1]->shiftScaleS, cache.current[1]->shiftScaleT);
            SC_ForceUniform2f(uCacheScale[1], cache.current[1]->scaleS, cache.current[1]->scaleT);
            SC_ForceUniform2f(uCacheOffset[1], cache.current[1]->offsetS, cache.current[1]->offsetT);
         }
         //else TextureCache_ActivateDummy(1);
      }
   }

   if ((gDP.changed & CHANGED_FOGCOLOR) && config.enableFog)
      SC_SetUniform4fv(uFogColor, &gDP.fogColor.r );

   if (gDP.changed & CHANGED_ENV_COLOR)
      SC_SetUniform4fv(uEnvColor, &gDP.envColor.r);

   if (gDP.changed & CHANGED_PRIM_COLOR)
   {
      SC_SetUniform4fv(uPrimColor, &gDP.primColor.r);
      SC_SetUniform1f(uPrimLODFrac, gDP.primColor.l);
   }

   if ((gDP.changed & CHANGED_RENDERMODE) || (gDP.changed & CHANGED_CYCLETYPE))
   {
#ifndef OLD_BLENDMODE
      OGL_SetBlendMode();
#else
      if ((gDP.otherMode.forceBlender) &&
            (gDP.otherMode.cycleType != G_CYC_COPY) &&
            (gDP.otherMode.cycleType != G_CYC_FILL) &&
            !(gDP.otherMode.alphaCvgSel))
      {
         glEnable( GL_BLEND );

         switch (gDP.otherMode.l >> 16)
         {
            case 0x0448: // Add
            case 0x055A:
               glBlendFunc( GL_ONE, GL_ONE );
               break;
            case 0x0C08: // 1080 Sky
            case 0x0F0A: // Used LOTS of places
               glBlendFunc( GL_ONE, GL_ZERO );
               break;

            case 0x0040: // Fzero
            case 0xC810: // Blends fog
            case 0xC811: // Blends fog
            case 0x0C18: // Standard interpolated blend
            case 0x0C19: // Used for antialiasing
            case 0x0050: // Standard interpolated blend
            case 0x0055: // Used for antialiasing
               glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
               break;

            case 0x0FA5: // Seems to be doing just blend color - maybe combiner can be used for this?
            case 0x5055: // Used in Paper Mario intro, I'm not sure if this is right...
               glBlendFunc( GL_ZERO, GL_ONE );
               break;

            default:
               LOG(LOG_VERBOSE, "Unhandled blend mode=%x", gDP.otherMode.l >> 16);
               glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
               break;
         }
      }
      else
      {
         glDisable( GL_BLEND );
      }

      if (gDP.otherMode.cycleType == G_CYC_FILL)
      {
         glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
         glEnable( GL_BLEND );
      }
#endif
   }

   gDP.changed &= CHANGED_TILE | CHANGED_TMEM;
   gSP.changed &= CHANGED_TEXTURE | CHANGED_MATRIX;
}

void OGL_DrawTriangle(SPVertex *vertices, int v0, int v1, int v2)
{
}

void OGL_AddTriangle(int v0, int v1, int v2)
{
   u32 i;
   SPVertex *vtx = NULL;

   OGL.triangles.elements[OGL.triangles.num++] = v0;
   OGL.triangles.elements[OGL.triangles.num++] = v1;
   OGL.triangles.elements[OGL.triangles.num++] = v2;

	if ((gSP.geometryMode & G_SHADE) == 0)
   {
      /* Prim shading */
      for (i = OGL.triangles.num - 3; i < OGL.triangles.num; ++i)
      {
         vtx = (SPVertex*)&OGL.triangles.vertices[OGL.triangles.elements[i]];
         vtx->r = gDP.primColor.r;
         vtx->g = gDP.primColor.g;
         vtx->b = gDP.primColor.b;
         vtx->a = gDP.primColor.a;
      }
   }
   else if ((gSP.geometryMode & G_SHADING_SMOOTH) == 0)
   {
      /* Flat shading */
      SPVertex *vtx0 = (SPVertex*)&OGL.triangles.vertices[v0];

      for (i = OGL.triangles.num - 3; i < OGL.triangles.num; ++i)
      {
         vtx = (SPVertex*)&OGL.triangles.vertices[OGL.triangles.elements[i]];
         vtx->r = vtx0->r;
         vtx->g = vtx0->g;
         vtx->b = vtx0->b;
         vtx->a = vtx0->a;
      }
   }

	if (gDP.otherMode.depthSource == G_ZS_PRIM)
   {
		for (i = OGL.triangles.num - 3; i < OGL.triangles.num; ++i)
      {
			vtx = (SPVertex*)&OGL.triangles.vertices[OGL.triangles.elements[i]];
			vtx->z = gDP.primDepth.z * vtx->w;
		}
	}
}

void OGL_SetColorArray(void)
{
   if (scProgramCurrent->usesCol)
      glEnableVertexAttribArray(SC_COLOR);
   else
      glDisableVertexAttribArray(SC_COLOR);
}

void OGL_SetTexCoordArrays(void)
{
   if (scProgramCurrent->usesT0)
      glEnableVertexAttribArray(SC_TEXCOORD0);
   else
      glDisableVertexAttribArray(SC_TEXCOORD0);

   if (scProgramCurrent->usesT1)
      glEnableVertexAttribArray(SC_TEXCOORD1);
   else
      glDisableVertexAttribArray(SC_TEXCOORD1);
}

static void OGL_prepareDrawTriangle(bool _dma)
{
   if (gSP.changed || gDP.changed)
      OGL_UpdateStates();

   if (OGL.renderState != RS_TRIANGLE || scProgramChanged)
   {
      OGL_SetColorArray();
      OGL_SetTexCoordArrays();
      glDisableVertexAttribArray(SC_TEXCOORD1);
      SC_ForceUniform1f(uRenderState, RS_TRIANGLE);
   }

   if (OGL.renderState != RS_TRIANGLE)
   {
      glVertexAttribPointer(SC_POSITION, 4, GL_FLOAT, GL_FALSE, sizeof(SPVertex), &OGL.triangles.vertices[0].x);
      glEnableVertexAttribArray(SC_POSITION);
      glVertexAttribPointer(SC_COLOR, 4, GL_FLOAT, GL_FALSE, sizeof(SPVertex), &OGL.triangles.vertices[0].r);
      glEnableVertexAttribArray(SC_COLOR);
      glVertexAttribPointer(SC_TEXCOORD0, 2, GL_FLOAT, GL_FALSE, sizeof(SPVertex), &OGL.triangles.vertices[0].s);
      glEnableVertexAttribArray(SC_TEXCOORD0);

      OGL_UpdateCullFace();
      OGL_UpdateViewport();
      glEnable(GL_SCISSOR_TEST);
      OGL.renderState = RS_TRIANGLE;
   }
}

void OGL_DrawTriangles(void)
{
   if (OGL.renderingToTexture && config.ignoreOffscreenRendering)
   {
      OGL.triangles.num = 0;
      return;
   }

   if (OGL.triangles.num == 0) return;

   OGL_prepareDrawTriangle(false);

   glDrawElements(GL_TRIANGLES, OGL.triangles.num, GL_UNSIGNED_BYTE, OGL.triangles.elements);
   OGL.triangles.num = 0;
}

void OGL_DrawLine(int v0, int v1, float width )
{
   unsigned short elem[2];
   if (OGL.renderingToTexture && config.ignoreOffscreenRendering) return;

   if (gSP.changed || gDP.changed)
      OGL_UpdateStates();

   if (OGL.renderState != RS_LINE || scProgramChanged)
   {
      OGL_SetColorArray();
      glDisableVertexAttribArray(SC_TEXCOORD0);
      glDisableVertexAttribArray(SC_TEXCOORD1);
      glVertexAttribPointer(SC_POSITION, 4, GL_FLOAT, GL_FALSE, sizeof(SPVertex), &OGL.triangles.vertices[0].x);
      glVertexAttribPointer(SC_COLOR, 4, GL_FLOAT, GL_FALSE, sizeof(SPVertex), &OGL.triangles.vertices[0].r);

      SC_ForceUniform1f(uRenderState, RS_LINE);
      OGL_UpdateCullFace();
      OGL_UpdateViewport();
      OGL.renderState = RS_LINE;
   }

   elem[0] = v0;
   elem[1] = v1;
   glLineWidth( width * OGL.scaleX );
   glDrawElements(GL_LINES, 2, GL_UNSIGNED_SHORT, elem);
}

void OGL_DrawRect( int ulx, int uly, int lrx, int lry, float *color)
{
   if (OGL.renderingToTexture && config.ignoreOffscreenRendering) return;

   if (gSP.changed || gDP.changed)
      OGL_UpdateStates();

   if (OGL.renderState != RS_RECT || scProgramChanged)
   {
      glDisableVertexAttribArray(SC_COLOR);
      glDisableVertexAttribArray(SC_TEXCOORD0);
      glDisableVertexAttribArray(SC_TEXCOORD1);
      SC_ForceUniform1f(uRenderState, RS_RECT);
   }

   if (OGL.renderState != RS_RECT)
   {
      glVertexAttrib4f(SC_POSITION, 0, 0, gSP.viewport.nearz, 1.0);
      glVertexAttribPointer(SC_POSITION, 2, GL_FLOAT, GL_FALSE, sizeof(GLVertex), &OGL.rect[0].x);
      OGL.renderState = RS_RECT;
   }

   glViewport(0, 0, config.screen.width, config.screen.height);
   glDisable(GL_SCISSOR_TEST);
   glDisable(GL_CULL_FACE);

   OGL.rect[0].x = (float)ulx * (2.0f * VI.rwidth) - 1.0f;
   OGL.rect[0].y = (float)uly * (-2.0f * VI.rheight) + 1.0f;
   OGL.rect[1].x = (float)(lrx+1) * (2.0f * VI.rwidth) - 1.0f;
   OGL.rect[1].y = OGL.rect[0].y;
   OGL.rect[2].x = OGL.rect[0].x;
   OGL.rect[2].y = (float)(lry+1) * (-2.0f * VI.rheight) + 1.0f;
   OGL.rect[3].x = OGL.rect[1].x;
   OGL.rect[3].y = OGL.rect[2].y;

   glVertexAttrib4fv(SC_COLOR, color);
   glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
   glEnable(GL_SCISSOR_TEST);
   OGL_UpdateViewport();

}

void OGL_DrawTexturedRect( float ulx, float uly, float lrx, float lry, float uls, float ult, float lrs, float lrt, bool flip )
{
   if (config.hackBanjoTooie)
   {
      if (gDP.textureImage.width == gDP.colorImage.width &&
            gDP.textureImage.format == G_IM_FMT_CI &&
            gDP.textureImage.size == G_IM_SIZ_8b)
         return;
   }

   if (OGL.renderingToTexture && config.ignoreOffscreenRendering) return;

   if (gSP.changed || gDP.changed)
      OGL_UpdateStates();

   if (OGL.renderState != RS_TEXTUREDRECT || scProgramChanged)
   {
      glDisableVertexAttribArray(SC_COLOR);
      OGL_SetTexCoordArrays();
      SC_ForceUniform1f(uRenderState, RS_TEXTUREDRECT);
   }

   if (OGL.renderState != RS_TEXTUREDRECT)
   {
      glVertexAttrib4f(SC_COLOR, 0, 0, 0, 0);
      glVertexAttrib4f(SC_POSITION, 0, 0, (gDP.otherMode.depthSource == G_ZS_PRIM) ? gDP.primDepth.z : gSP.viewport.nearz, 1.0);
      glVertexAttribPointer(SC_POSITION, 2, GL_FLOAT, GL_FALSE, sizeof(GLVertex), &OGL.rect[0].x);
      glVertexAttribPointer(SC_TEXCOORD0, 2, GL_FLOAT, GL_FALSE, sizeof(GLVertex), &OGL.rect[0].s0);
      glVertexAttribPointer(SC_TEXCOORD1, 2, GL_FLOAT, GL_FALSE, sizeof(GLVertex), &OGL.rect[0].s1);
      OGL.renderState = RS_TEXTUREDRECT;
   }

   glViewport(0, 0, config.screen.width, config.screen.height);
   glDisable(GL_CULL_FACE);

   OGL.rect[0].x = (float) ulx * (2.0f * VI.rwidth) - 1.0f;
   OGL.rect[0].y = (float) uly * (-2.0f * VI.rheight) + 1.0f;
   OGL.rect[1].x = (float) (lrx) * (2.0f * VI.rwidth) - 1.0f;
   OGL.rect[1].y = OGL.rect[0].y;
   OGL.rect[2].x = OGL.rect[0].x;
   OGL.rect[2].y = (float) (lry) * (-2.0f * VI.rheight) + 1.0f;
   OGL.rect[3].x = OGL.rect[1].x;
   OGL.rect[3].y = OGL.rect[2].y;

   if (scProgramCurrent->usesT0 && cache.current[0] && gSP.textureTile[0])
   {
      OGL.rect[0].s0 = uls * cache.current[0]->shiftScaleS - gSP.textureTile[0]->fuls;
      OGL.rect[0].t0 = ult * cache.current[0]->shiftScaleT - gSP.textureTile[0]->fult;
      OGL.rect[3].s0 = (lrs + 1.0f) * cache.current[0]->shiftScaleS - gSP.textureTile[0]->fuls;
      OGL.rect[3].t0 = (lrt + 1.0f) * cache.current[0]->shiftScaleT - gSP.textureTile[0]->fult;

      if ((cache.current[0]->maskS) && !(cache.current[0]->mirrorS) && (fmod( OGL.rect[0].s0, cache.current[0]->width ) == 0.0f))
      {
         OGL.rect[3].s0 -= OGL.rect[0].s0;
         OGL.rect[0].s0 = 0.0f;
      }

      if ((cache.current[0]->maskT)  && !(cache.current[0]->mirrorT) && (fmod( OGL.rect[0].t0, cache.current[0]->height ) == 0.0f))
      {
         OGL.rect[3].t0 -= OGL.rect[0].t0;
         OGL.rect[0].t0 = 0.0f;
      }

      glActiveTexture( GL_TEXTURE0);
      if ((OGL.rect[0].s0 >= 0.0f) && (OGL.rect[3].s0 <= cache.current[0]->width))
         glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );

      if ((OGL.rect[0].t0 >= 0.0f) && (OGL.rect[3].t0 <= cache.current[0]->height))
         glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );

      OGL.rect[0].s0 *= cache.current[0]->scaleS;
      OGL.rect[0].t0 *= cache.current[0]->scaleT;
      OGL.rect[3].s0 *= cache.current[0]->scaleS;
      OGL.rect[3].t0 *= cache.current[0]->scaleT;
   }

   if (scProgramCurrent->usesT1 && cache.current[1] && gSP.textureTile[1])
   {
      OGL.rect[0].s1 = uls * cache.current[1]->shiftScaleS - gSP.textureTile[1]->fuls;
      OGL.rect[0].t1 = ult * cache.current[1]->shiftScaleT - gSP.textureTile[1]->fult;
      OGL.rect[3].s1 = (lrs + 1.0f) * cache.current[1]->shiftScaleS - gSP.textureTile[1]->fuls;
      OGL.rect[3].t1 = (lrt + 1.0f) * cache.current[1]->shiftScaleT - gSP.textureTile[1]->fult;

      if ((cache.current[1]->maskS) && (fmod( OGL.rect[0].s1, cache.current[1]->width ) == 0.0f) && !(cache.current[1]->mirrorS))
      {
         OGL.rect[3].s1 -= OGL.rect[0].s1;
         OGL.rect[0].s1 = 0.0f;
      }

      if ((cache.current[1]->maskT) && (fmod( OGL.rect[0].t1, cache.current[1]->height ) == 0.0f) && !(cache.current[1]->mirrorT))
      {
         OGL.rect[3].t1 -= OGL.rect[0].t1;
         OGL.rect[0].t1 = 0.0f;
      }

      glActiveTexture( GL_TEXTURE1);
      if ((OGL.rect[0].s1 == 0.0f) && (OGL.rect[3].s1 <= cache.current[1]->width))
         glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );

      if ((OGL.rect[0].t1 == 0.0f) && (OGL.rect[3].t1 <= cache.current[1]->height))
         glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );

      OGL.rect[0].s1 *= cache.current[1]->scaleS;
      OGL.rect[0].t1 *= cache.current[1]->scaleT;
      OGL.rect[3].s1 *= cache.current[1]->scaleS;
      OGL.rect[3].t1 *= cache.current[1]->scaleT;
   }

   if ((gDP.otherMode.cycleType == G_CYC_COPY) && !config.texture.forceBilinear)
   {
      glActiveTexture(GL_TEXTURE0);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
   }

   if (flip)
   {
      OGL.rect[1].s0 = OGL.rect[0].s0;
      OGL.rect[1].t0 = OGL.rect[3].t0;
      OGL.rect[1].s1 = OGL.rect[0].s1;
      OGL.rect[1].t1 = OGL.rect[3].t1;
      OGL.rect[2].s0 = OGL.rect[3].s0;
      OGL.rect[2].t0 = OGL.rect[0].t0;
      OGL.rect[2].s1 = OGL.rect[3].s1;
      OGL.rect[2].t1 = OGL.rect[0].t1;
   }
   else
   {
      OGL.rect[1].s0 = OGL.rect[3].s0;
      OGL.rect[1].t0 = OGL.rect[0].t0;
      OGL.rect[1].s1 = OGL.rect[3].s1;
      OGL.rect[1].t1 = OGL.rect[0].t1;
      OGL.rect[2].s0 = OGL.rect[0].s0;
      OGL.rect[2].t0 = OGL.rect[3].t0;
      OGL.rect[2].s1 = OGL.rect[0].s1;
      OGL.rect[2].t1 = OGL.rect[3].t1;
   }

   glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void OGL_ClearDepthBuffer(void)
{
   float depth;
   if (OGL.renderingToTexture && config.ignoreOffscreenRendering) return;

   //float depth = 1.0 - (gDP.fillColor.z / ((float)0x3FFF)); // broken on OMAP3
   depth = gDP.fillColor.z ;

   /////// paulscode, graphics bug-fixes
   glDisable( GL_SCISSOR_TEST );
   glDepthMask( GL_TRUE );  // fixes side-bar graphics glitches
   glClearDepth( 1.0f );  // fixes missing graphics on Qualcomm Adreno
   glClearColor( 0, 0, 0, 1 );
   glClear( GL_DEPTH_BUFFER_BIT );
   OGL_UpdateDepthUpdate();
   glEnable( GL_SCISSOR_TEST );
   ////////
}

void OGL_ClearColorBuffer( float *color )
{
   if (OGL.renderingToTexture && config.ignoreOffscreenRendering) return;

   glScissor(0, 0, config.screen.width, config.screen.height);
   glClearColor( color[0], color[1], color[2], color[3] );
   glClear( GL_COLOR_BUFFER_BIT );
   OGL_UpdateScissor();

}

int OGL_CheckError(void)
{
   GLenum e = glGetError();
   if (e != GL_NO_ERROR)
   {
      printf("GL Error: ");
      switch(e)
      {
         case GL_INVALID_ENUM:   printf("INVALID ENUM"); break;
         case GL_INVALID_VALUE:  printf("INVALID VALUE"); break;
         case GL_INVALID_OPERATION:  printf("INVALID OPERATION"); break;
         case GL_OUT_OF_MEMORY:  printf("OUT OF MEMORY"); break;
      }
      printf("\n");
      return 1;
   }
   return 0;
}

void OGL_SwapBuffers(void)
{
   // if emulator defined a render callback function, call it before
   // buffer swap
   if (renderCallback) (*renderCallback)();

   //OGL_DrawTriangles();
   scProgramChanged = 0;

   retro_return(true);

   OGL.screenUpdate = false;

   if (config.forceBufferClear)
   {
      /////// paulscode, graphics bug-fixes
      float depth = gDP.fillColor.z ;
      glDisable( GL_SCISSOR_TEST );
      glDepthMask( GL_TRUE );  // fixes side-bar graphics glitches
      glClearDepth( 1.0f );  // fixes missing graphics on Qualcomm Adreno
      glClearColor( 0, 0, 0, 1 );
      glClear( GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT );
      OGL_UpdateDepthUpdate();
      glEnable( GL_SCISSOR_TEST );
      ///////
   }

}

void OGL_ReadScreen( void *dest, int *width, int *height )
{
   if (width)
      *width = config.screen.width;
   if (height)
      *height = config.screen.height;

   if (dest == NULL)
      return;

   glReadPixels(0, 0,
         config.screen.width, config.screen.height,
         GL_RGBA, GL_UNSIGNED_BYTE, dest );
}
