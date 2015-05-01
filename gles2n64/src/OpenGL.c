
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

GLInfo OGL;

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

static void _initStates(void)
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
   if (VI.width == 0 || VI.height == 0)
      return;
   OGL.scaleX = (float)config.screen.width / (float)VI.width;
   OGL.scaleY = (float)config.screen.height / (float)VI.height;
}

bool OGL_Start(void)
{
   float f;
   _initStates();

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

   //We must have a shader bound before binding any textures:
   Combiner_Init();
   Combiner_Set(EncodeCombineMode(0, 0, 0, TEXEL0, 0, 0, 0, TEXEL0, 0, 0, 0, TEXEL0, 0, 0, 0, TEXEL0), -1);
   Combiner_Set(EncodeCombineMode(0, 0, 0, SHADE, 0, 0, 0, 1, 0, 0, 0, SHADE, 0, 0, 0, 1), -1);

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

   Combiner_Destroy();
   TextureCache_Destroy();
}

static void _updateCullFace(void)
{
   if (gSP.geometryMode & G_CULL_BOTH)
   {
      glEnable( GL_CULL_FACE );

      if (gSP.geometryMode & G_CULL_BACK)
         glCullFace(GL_BACK);
      else
         glCullFace(GL_FRONT);
   }
   else
      glDisable(GL_CULL_FACE);
}

/* TODO/FIXME - not complete yet */
static void _updateViewport(void)
{
   const u32 VI_height = VI.height;
   const f32 scaleX = OGL.scaleX;
   const f32 scaleY = OGL.scaleY;
   float Xf = gSP.viewport.vscale[0] < 0 ? (gSP.viewport.x + gSP.viewport.vscale[0] * 2.0f) : gSP.viewport.x;
   const GLint X = (GLint)(Xf * scaleX);
   const GLint Y = gSP.viewport.vscale[1] < 0 ? (GLint)((gSP.viewport.y + gSP.viewport.vscale[1] * 2.0f) * scaleY) : (GLint)((VI_height - (gSP.viewport.y + gSP.viewport.height)) * scaleY);
   
   glViewport(X,
         Y,
         max((GLint)(gSP.viewport.width * scaleX), 0),
         max((GLint)(gSP.viewport.height * scaleY), 0));

	gSP.changed &= ~CHANGED_VIEWPORT;
}

static void _updateDepthUpdate(void)
{
   if (gDP.otherMode.depthUpdate)
      glDepthMask(GL_TRUE);
   else
      glDepthMask(GL_FALSE);
}

/* TODO/FIXME - not complete */
static void _updateScissor(void)
{
   u32 heightOffset, screenHeight;
   f32 scaleX, scaleY;
   float SX0 = gDP.scissor.ulx;
   float SX1 = gDP.scissor.lrx;

   scaleX       = OGL.scaleX;
   scaleY       = OGL.scaleY;
   heightOffset = 0;
   screenHeight = VI.height; 

   glScissor(
         (GLint)(SX0 * scaleX),
         (GLint)((screenHeight - gDP.scissor.lry) * scaleY + heightOffset),
         max((GLint)((SX1 - gDP.scissor.ulx) * scaleX), 0),
         max((GLint)((gDP.scissor.lry - gDP.scissor.uly) * scaleY), 0)
         );
	gDP.changed &= ~CHANGED_SCISSOR;
}

static void _setBlendMode(void)
{
	const u32 blendmode = gDP.otherMode.l >> 16;
	// 0x7000 = CVG_X_ALPHA|ALPHA_CVG_SEL|FORCE_BL
	if (gDP.otherMode.alphaCvgSel != 0 && (gDP.otherMode.l & 0x7000) != 0x7000) {
		switch (blendmode) {
		case 0x4055: // Mario Golf
		case 0x5055: // Paper Mario intro clr_mem * a_in + clr_mem * a_mem
			glEnable(GL_BLEND);
			glBlendFunc(GL_ZERO, GL_ONE);
			break;
		default:
			glDisable(GL_BLEND);
		}
		return;
	}

	if (gDP.otherMode.forceBlender != 0 && gDP.otherMode.cycleType < G_CYC_COPY) {
		glEnable( GL_BLEND );

		switch (blendmode)
		{
			// Mace objects
			case 0x0382:
			// Mace special blend mode, see GLSLCombiner.cpp
			case 0x0091:
			// 1080 Sky
			case 0x0C08:
			// Used LOTS of places
			case 0x0F0A:
			//DK64 blue prints
			case 0x0302:
			// Bomberman 2 special blend mode, see GLSLCombiner.cpp
			case 0xA500:
			//Sin and Punishment
			case 0xCB02:
			// Battlezone
			// clr_in * a + clr_in * (1-a)
			case 0xC800:
			// Conker BFD
			// clr_in * a_fog + clr_fog * (1-a)
			// clr_in * 0 + clr_in * 1
			case 0x07C2:
			case 0x00C0:
			//ISS64
			case 0xC302:
			// Donald Duck
			case 0xC702:
				glBlendFunc(GL_ONE, GL_ZERO);
				break;

			case 0x0F1A:
				if (gDP.otherMode.cycleType == G_CYC_1CYCLE)
					glBlendFunc(GL_ONE, GL_ZERO);
				else
					glBlendFunc(GL_ZERO, GL_ONE);
				break;

			//Space Invaders
			case 0x0448: // Add
			case 0x055A:
				glBlendFunc( GL_ONE, GL_ONE );
				break;

			case 0xc712: // Pokemon Stadium?
			case 0xAF50: // LOT in Zelda: MM
			case 0x0F5A: // LOT in Zelda: MM
			case 0x0FA5: // Seems to be doing just blend color - maybe combiner can be used for this?
			case 0x5055: // Used in Paper Mario intro, I'm not sure if this is right...
				//clr_in * 0 + clr_mem * 1
				glBlendFunc( GL_ZERO, GL_ONE );
				break;

			case 0x5F50: //clr_mem * 0 + clr_mem * (1-a)
				glBlendFunc( GL_ZERO, GL_ONE_MINUS_SRC_ALPHA );
				break;

			case 0xF550: //clr_fog * a_fog + clr_mem * (1-a)
			case 0x0150: // spiderman
			case 0x0550: // bomberman 64
			case 0x0D18: //clr_in * a_fog + clr_mem * (1-a)
				glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
				break;

			case 0xC912: //40 winks, clr_in * a_fog + clr_mem * 1
				glBlendFunc(GL_SRC_ALPHA, GL_ONE);
				break;

			case 0x0040: // Fzero
			case 0xC810: // Blends fog
			case 0xC811: // Blends fog
			case 0x0C18: // Standard interpolated blend
			case 0x0C19: // Used for antialiasing
			case 0x0050: // Standard interpolated blend
			case 0x0051: // Standard interpolated blend
			case 0x0055: // Used for antialiasing
				glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
				break;


			default:
				LOG(LOG_VERBOSE, "Unhandled blend mode=%x", gDP.otherMode.l >> 16);
				glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
				break;
		}
	}
   else if ((config.generalEmulation.hacks & hack_pilotWings) != 0 && (gDP.otherMode.l & 0x80) != 0) { //CLR_ON_CVG without FORCE_BL
		glEnable(GL_BLEND);
		glBlendFunc(GL_ZERO, GL_ONE);
	}
   /* TODO/FIXME: update */
   else if ((config.generalEmulation.hacks & hack_blastCorps) != 0 && gSP.texture.on == 0 /* && currentCombiner()->usesTex() */)
   {
      // Blast Corps
		glEnable(GL_BLEND);
		glBlendFunc(GL_ZERO, GL_ONE);
	}
   else
   {
		glDisable( GL_BLEND );
	}
}

static void _updateStates(void)
{
   if (gDP.otherMode.cycleType == G_CYC_COPY)
      Combiner_Set(EncodeCombineMode(0, 0, 0, TEXEL0, 0, 0, 0, TEXEL0, 0, 0, 0, TEXEL0, 0, 0, 0, TEXEL0), -1);
   else if (gDP.otherMode.cycleType == G_CYC_FILL)
      Combiner_Set(EncodeCombineMode(0, 0, 0, SHADE, 0, 0, 0, 1, 0, 0, 0, SHADE, 0, 0, 0, 1), -1);
   else
      Combiner_Set(gDP.combine.mux, -1);

   if (gSP.changed & CHANGED_GEOMETRYMODE)
   {
      _updateCullFace();
		gSP.changed &= ~CHANGED_GEOMETRYMODE;
   }

   if (gDP.changed & CHANGED_CONVERT)
   {
      SC_SetUniform1f(uK4, gDP.convert.k4);
      SC_SetUniform1f(uK5, gDP.convert.k5);
   }

   if (gDP.changed & CHANGED_RENDERMODE || gDP.changed & CHANGED_CYCLETYPE)
   {
      if (((gSP.geometryMode & G_ZBUFFER) || gDP.otherMode.depthSource == G_ZS_PRIM) && gDP.otherMode.cycleType <= G_CYC_2CYCLE)
      {
         if (gDP.otherMode.depthCompare != 0)
         {
            switch (gDP.otherMode.depthMode)
            {
               case ZMODE_OPA:
                  glDisable(GL_POLYGON_OFFSET_FILL);
                  glDepthFunc(GL_LEQUAL);
                  break;
               case ZMODE_INTER:
                  glDisable(GL_POLYGON_OFFSET_FILL);
                  glDepthFunc(GL_LEQUAL);
                  break;
               case ZMODE_XLU:
                  // Max || Infront;
                  glDisable(GL_POLYGON_OFFSET_FILL);
                  if (gDP.otherMode.depthSource == G_ZS_PRIM && gDP.primDepth.z == 1.0f)
                     // Max
                     glDepthFunc(GL_LEQUAL);
                  else
                     // Infront
                     glDepthFunc(GL_LESS);
                  break;
               case ZMODE_DEC:
                  glEnable(GL_POLYGON_OFFSET_FILL);
                  glDepthFunc(GL_LEQUAL);
                  break;
            }
         }
         else
         {
            glDisable(GL_POLYGON_OFFSET_FILL);
            glDepthFunc(GL_ALWAYS);
         }

         _updateDepthUpdate();

         glEnable(GL_DEPTH_TEST);
#ifdef NEW
         if (!GBI.isNoN())
            glDisable(GL_DEPTH_CLAMP);
#endif
      }
      else
      {
         glDisable(GL_DEPTH_TEST);
#ifdef NEW
         if (!GBI.isNoN())
            glEnable(GL_DEPTH_CLAMP);
#endif
      }
   }

   if ((gDP.changed & CHANGED_BLENDCOLOR) || (gDP.changed & CHANGED_RENDERMODE))
      SC_SetUniform1f(uAlphaRef, (gDP.otherMode.cvgXAlpha) ? 0.5f : gDP.blendColor.a);

   if (gDP.changed & CHANGED_SCISSOR)
      _updateScissor();

   if (gSP.changed & CHANGED_VIEWPORT)
      _updateViewport();

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

   if ((gDP.changed & CHANGED_FOGCOLOR))
      SC_SetUniform4fv(uFogColor, &gDP.fogColor.r );

   if (gDP.changed & CHANGED_PRIM_COLOR)
   {
      SC_SetUniform4fv(uPrimColor, &gDP.primColor.r);
      SC_SetUniform1f(uPrimLODFrac, gDP.primColor.l);
   }

   if ((gDP.changed & (CHANGED_RENDERMODE | CHANGED_CYCLETYPE)))
   {
      _setBlendMode();
      gDP.changed &= ~(CHANGED_RENDERMODE | CHANGED_CYCLETYPE);
   }

   gDP.changed &= CHANGED_TILE | CHANGED_TMEM;
   gSP.changed &= CHANGED_TEXTURE | CHANGED_MATRIX;
}

void OGL_DrawTriangle(SPVertex *vertices, int v0, int v1, int v2)
{
}

static void _setColorArray(void)
{
   if (scProgramCurrent->usesCol)
      glEnableVertexAttribArray(SC_COLOR);
   else
      glDisableVertexAttribArray(SC_COLOR);
}

void OGL_SetTexCoordArrays(void)
{
#ifdef NEw
   if (OGL.renderState == RS_TRIANGLE)
   {
      glDisableVertexAttribArray(SC_TEXCOORD1);
      if (scProgramCurrent->usesT0 || scProgramCurrent->usesT1)
         glEnableVertexAttribArray(SC_TEXCOORD0);
      else
         glDisableVertexAttribArray(SC_TEXCOORD0);
   }
   else
#endif
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
}

static void OGL_prepareDrawTriangle(bool _dma)
{
   if (gSP.changed || gDP.changed)
      _updateStates();

   if (OGL.renderState != RS_TRIANGLE || scProgramChanged)
   {
      _setColorArray();
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

      _updateCullFace();
      _updateViewport();
      glEnable(GL_SCISSOR_TEST);
      OGL.renderState = RS_TRIANGLE;
   }
}

void OGL_DrawLLETriangle(u32 _numVtx)
{
   float scaleX, scaleY;
   u32 i;
	if (_numVtx == 0)
		return;

	gSP.changed &= ~CHANGED_GEOMETRYMODE; // Don't update cull mode
	OGL_prepareDrawTriangle(false);
	glDisable(GL_CULL_FACE);

#if 0
	OGLVideo & ogl = video();
	FrameBuffer * pCurrentBuffer = frameBufferList().getCurrent();
	if (pCurrentBuffer == NULL)
		glViewport( 0, ogl.getHeightOffset(), ogl.getScreenWidth(), ogl.getScreenHeight());
	else
		glViewport(0, 0, pCurrentBuffer->m_width*pCurrentBuffer->m_scaleX, pCurrentBuffer->m_height*pCurrentBuffer->m_scaleY);

	scaleX = pCurrentBuffer != NULL ? 1.0f / pCurrentBuffer->m_width : VI.rwidth;
	scaleY = pCurrentBuffer != NULL ? 1.0f / pCurrentBuffer->m_height : VI.rheight;
#endif

	for (i = 0; i < _numVtx; ++i)
   {
      SPVertex *vtx = (SPVertex*)&OGL.triangles.vertices[i];

      vtx->HWLight = 0;
      vtx->x  = vtx->x * (2.0f * scaleX) - 1.0f;
      vtx->x *= vtx->w;
      vtx->y  = vtx->y * (-2.0f * scaleY) + 1.0f;
      vtx->y *= vtx->w;
      vtx->z *= vtx->w;

      if (gDP.otherMode.texturePersp == 0)
      {
         vtx->s *= 2.0f;
         vtx->t *= 2.0f;
      }
   }

	glDrawArrays(GL_TRIANGLE_STRIP, 0, _numVtx);
	OGL.triangles.num = 0;

#if 0
	frameBufferList().setBufferChanged();
#endif
	gSP.changed |= CHANGED_VIEWPORT | CHANGED_GEOMETRYMODE;
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

void OGL_DrawDMATriangles(u32 _numVtx)
{
   if (_numVtx == 0)
      return;

   OGL_prepareDrawTriangle(true);
	glDrawArrays(GL_TRIANGLES, 0, _numVtx);
}

void OGL_DrawTriangles(void)
{
   if (OGL.triangles.num == 0)
      return;

   OGL_prepareDrawTriangle(false);

   glDrawElements(GL_TRIANGLES, OGL.triangles.num, GL_UNSIGNED_BYTE, OGL.triangles.elements);
   OGL.triangles.num = 0;
}

void OGL_DrawLine(int v0, int v1, float width )
{
   unsigned short elem[2];

   if (gSP.changed || gDP.changed)
      _updateStates();

   if (OGL.renderState != RS_LINE || scProgramChanged)
   {
      _setColorArray();
      glDisableVertexAttribArray(SC_TEXCOORD0);
      glDisableVertexAttribArray(SC_TEXCOORD1);
      glVertexAttribPointer(SC_POSITION, 4, GL_FLOAT, GL_FALSE, sizeof(SPVertex), &OGL.triangles.vertices[0].x);
      glVertexAttribPointer(SC_COLOR, 4, GL_FLOAT, GL_FALSE, sizeof(SPVertex), &OGL.triangles.vertices[0].r);

      SC_ForceUniform1f(uRenderState, RS_LINE);
      _updateCullFace();
      _updateViewport();
      OGL.renderState = RS_LINE;
   }

   elem[0] = v0;
   elem[1] = v1;
   glLineWidth( width * OGL.scaleX );
   glDrawElements(GL_LINES, 2, GL_UNSIGNED_SHORT, elem);
}

void OGL_DrawRect( int ulx, int uly, int lrx, int lry, float *color)
{
   float scaleX, scaleY, Z, W;
   bool updateArrays;

   gSP.changed &= ~CHANGED_GEOMETRYMODE; // Don't update cull mode
   if (gSP.changed || gDP.changed)
      _updateStates();

   updateArrays = OGL.renderState != RS_RECT;

   if (updateArrays || scProgramChanged)
   {
      glDisableVertexAttribArray(SC_COLOR);
      glDisableVertexAttribArray(SC_TEXCOORD0);
      glDisableVertexAttribArray(SC_TEXCOORD1);
      SC_ForceUniform1f(uRenderState, RS_RECT);
   }

   if (updateArrays)
   {
      glVertexAttrib4f(SC_POSITION, 0, 0, gSP.viewport.nearz, 1.0);
      glVertexAttribPointer(SC_POSITION, 2, GL_FLOAT, GL_FALSE, sizeof(GLVertex), &OGL.rect[0].x);
      OGL.renderState = RS_RECT;
   }

   glViewport(0, 0, config.screen.width, config.screen.height);
   glDisable(GL_SCISSOR_TEST);
   glDisable(GL_CULL_FACE);

   scaleX = VI.rwidth;
   scaleY = VI.rheight;
	Z      = (gDP.otherMode.depthSource == G_ZS_PRIM) ? gDP.primDepth.z : gSP.viewport.nearz;
	W      = 1.0f;

   OGL.rect[0].x = (float)ulx * (2.0f * scaleX) - 1.0f;
   OGL.rect[0].y = (float)uly * (-2.0f * scaleY) + 1.0f;
   OGL.rect[0].z = Z;
   OGL.rect[0].w = W;

   OGL.rect[1].x = (float)lrx * (2.0f * scaleX) - 1.0f;
   OGL.rect[1].y = OGL.rect[0].y;
   OGL.rect[1].z = Z;
   OGL.rect[1].w = W;

   OGL.rect[2].x = OGL.rect[0].x;
   OGL.rect[2].y = (float)lry * (-2.0f * scaleY) + 1.0f;
   OGL.rect[2].z = Z;
   OGL.rect[2].w = W;

   OGL.rect[3].x = OGL.rect[1].x;
   OGL.rect[3].y = OGL.rect[2].y;
   OGL.rect[3].z = Z;
   OGL.rect[3].w = W;

	if (gDP.otherMode.cycleType == G_CYC_FILL)
		glVertexAttrib4fv(SC_COLOR, color);
   else
		glVertexAttrib4f(SC_COLOR, 0.0f, 0.0f, 0.0f, 0.0f);

   glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
   glEnable(GL_SCISSOR_TEST);
	gSP.changed |= CHANGED_GEOMETRYMODE | CHANGED_VIEWPORT;
}

void OGL_DrawTexturedRect( float ulx, float uly, float lrx, float lry, float uls, float ult, float lrs, float lrt, bool flip )
{
   float scaleX, scaleY, Z, W;
   bool updateArrays;

   if (gSP.changed || gDP.changed)
      _updateStates();

   updateArrays = OGL.renderState != RS_TEXTUREDRECT;
   if (updateArrays || scProgramChanged)
   {
      glDisableVertexAttribArray(SC_COLOR);
      OGL_SetTexCoordArrays();
      SC_ForceUniform1f(uRenderState, RS_TEXTUREDRECT);
   }

   if (updateArrays)
   {
      glVertexAttrib4f(SC_COLOR, 0, 0, 0, 0);
      glVertexAttrib4f(SC_POSITION, 0, 0, (gDP.otherMode.depthSource == G_ZS_PRIM) ? gDP.primDepth.z : gSP.viewport.nearz, 1.0);
      glVertexAttribPointer(SC_POSITION, 2, GL_FLOAT, GL_FALSE, sizeof(GLVertex), &OGL.rect[0].x);
      glVertexAttribPointer(SC_TEXCOORD0, 2, GL_FLOAT, GL_FALSE, sizeof(GLVertex), &OGL.rect[0].s0);
      glVertexAttribPointer(SC_TEXCOORD1, 2, GL_FLOAT, GL_FALSE, sizeof(GLVertex), &OGL.rect[0].s1);
      OGL.renderState = RS_TEXTUREDRECT;
   }

#ifdef NEW
	if (__RSP.cmd == 0xE4 && texturedRectSpecial != NULL && texturedRectSpecial(_params))
   {
      gSP.changed |= CHANGED_GEOMETRYMODE;
      return;
   }
#endif

   glViewport(0, 0, config.screen.width, config.screen.height);
   glDisable(GL_CULL_FACE);

   scaleX = VI.rwidth;
   scaleY = VI.rheight;

   OGL.rect[0].x = (float) ulx * (2.0f * scaleX) - 1.0f;
   OGL.rect[0].y = (float) uly * (-2.0f * scaleY) + 1.0f;
   OGL.rect[0].z = Z;
   OGL.rect[0].w = W;

   OGL.rect[1].x = (float) (lrx) * (2.0f * scaleX) - 1.0f;
   OGL.rect[1].y = OGL.rect[0].y;
   OGL.rect[1].z = Z;
   OGL.rect[1].w = W;

   OGL.rect[2].x = OGL.rect[0].x;
   OGL.rect[2].y = (float) (lry) * (-2.0f * scaleY) + 1.0f;
   OGL.rect[2].z = Z;
   OGL.rect[2].w = W;

   OGL.rect[3].x = OGL.rect[1].x;
   OGL.rect[3].y = OGL.rect[2].y;
   OGL.rect[3].z = Z;
   OGL.rect[3].w = W;

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

   if (gDP.otherMode.cycleType == G_CYC_COPY)
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
	gSP.changed |= CHANGED_GEOMETRYMODE | CHANGED_VIEWPORT;
}

/* TODO/FIXME - not complete */
void OGL_ClearDepthBuffer(bool _fullsize)
{
   glDisable( GL_SCISSOR_TEST );
   glDepthMask( GL_TRUE ); 
   glClear( GL_DEPTH_BUFFER_BIT );

   _updateDepthUpdate();

   glEnable( GL_SCISSOR_TEST );
}

void OGL_ClearColorBuffer(float *color)
{
	glDisable( GL_SCISSOR_TEST );

   glClearColor( color[0], color[1], color[2], color[3] );
   glClear( GL_COLOR_BUFFER_BIT );

	glEnable( GL_SCISSOR_TEST );
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
   retro_return(true);

   scProgramChanged = 0;
	gDP.otherMode.l = 0;
	gDPSetTextureLUT(G_TT_NONE);
	++__RSP.DList;
}

void OGL_ReadScreen( void *dest, int *width, int *height )
{
   if (width)
      *width = config.screen.width;
   if (height)
      *height = config.screen.height;

   dest = malloc(config.screen.height * config.screen.width * 3);
   if (dest == NULL)
      return;

   glReadPixels(0, 0,
         config.screen.width, config.screen.height,
         GL_RGBA, GL_UNSIGNED_BYTE, dest );
}
