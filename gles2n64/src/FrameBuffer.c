#include <stdlib.h>

#include "OpenGL.h"
#include "FrameBuffer.h"
#include "RSP.h"
#include "RDP.h"
#include "Textures.h"
#include "ShaderCombiner.h"
#include "Types.h"

struct FrameBufferInfo frameBuffer;

void FrameBuffer_Init(void)
{
   frameBuffer.current = NULL;
   frameBuffer.top = NULL;
   frameBuffer.bottom = NULL;
   frameBuffer.numBuffers = 0;
}

void FrameBuffer_RemoveBottom(void)
{
   struct FrameBuffer *newBottom = (struct FrameBuffer*)
      frameBuffer.bottom->higher;

   TextureCache_Remove( frameBuffer.bottom->texture );

   if (frameBuffer.bottom == frameBuffer.top)
      frameBuffer.top = NULL;

   free( frameBuffer.bottom );

   frameBuffer.bottom = newBottom;

   if (frameBuffer.bottom != NULL)
      frameBuffer.bottom->lower = NULL;

   frameBuffer.numBuffers--;
}

void FrameBuffer_Remove( struct FrameBuffer *buffer )
{
   if ((buffer == frameBuffer.bottom) &&
         (buffer == frameBuffer.top))
   {
      frameBuffer.top = NULL;
      frameBuffer.bottom = NULL;
   }
   else if (buffer == frameBuffer.bottom)
   {
      frameBuffer.bottom = buffer->higher;

      if (frameBuffer.bottom)
         frameBuffer.bottom->lower = NULL;
   }
   else if (buffer == frameBuffer.top)
   {
      frameBuffer.top = buffer->lower;

      if (frameBuffer.top)
         frameBuffer.top->higher = NULL;
   }
   else
   {
      buffer->higher->lower = buffer->lower;
      buffer->lower->higher = buffer->higher;
   }

   if (buffer->texture)
      TextureCache_Remove( buffer->texture );

   free( buffer );

   frameBuffer.numBuffers--;
}

void FrameBuffer_RemoveBuffer( u32 address )
{
   struct FrameBuffer *current = (struct FrameBuffer*)frameBuffer.bottom;

   while (current != NULL)
   {
      if (current->startAddress == address)
      {
         current->texture = NULL;
         FrameBuffer_Remove( current );
         return;
      }
      current = current->higher;
   }
}

struct FrameBuffer *FrameBuffer_AddTop(void)
{
   struct FrameBuffer *newtop = (struct FrameBuffer*)malloc( sizeof(struct FrameBuffer ) );

   newtop->texture = TextureCache_AddTop();

   newtop->lower = frameBuffer.top;
   newtop->higher = NULL;

   if (frameBuffer.top)
      frameBuffer.top->higher = newtop;

   if (!frameBuffer.bottom)
      frameBuffer.bottom = newtop;

   frameBuffer.top = newtop;

   frameBuffer.numBuffers++;

   return newtop;
}

void FrameBuffer_MoveToTop( struct FrameBuffer *newtop )
{
   if (newtop == frameBuffer.top)
      return;

   if (newtop == frameBuffer.bottom)
   {
      frameBuffer.bottom = newtop->higher;
      frameBuffer.bottom->lower = NULL;
   }
   else
   {
      newtop->higher->lower = newtop->lower;
      newtop->lower->higher = newtop->higher;
   }

   newtop->higher = NULL;
   newtop->lower = frameBuffer.top;
   frameBuffer.top->higher = newtop;
   frameBuffer.top = newtop;

   TextureCache_MoveToTop( newtop->texture );
}

void FrameBuffer_Destroy(void)
{
   while (frameBuffer.bottom)
      FrameBuffer_RemoveBottom();
}

void FrameBuffer_SaveBuffer( u32 address, u16 size, u16 width, u16 height )
{
   struct FrameBuffer *current = frameBuffer.top;

   /* Search through saved frame buffers */
   while (current != NULL)
   {
      if ((current->startAddress == address) &&
            (current->width == width) &&
            (current->height == height) &&
            (current->size == size))
      {
         if ((current->scaleX != OGL.scaleX) ||
               (current->scaleY != OGL.scaleY))
         {
            FrameBuffer_Remove( current );
            break;
         }

         /* code goes here */
         *(u32*)&gfx_info.RDRAM[current->startAddress] = current->startAddress;

         current->changed = TRUE;

         FrameBuffer_MoveToTop( current );

         gSP.changed |= CHANGED_TEXTURE;
         return;
      }
      current = current->lower;
   }

   /* Wasn't found, create a new one */
   current = FrameBuffer_AddTop();

   current->startAddress = address;
   current->endAddress = address + ((width * height << size >> 1) - 1);
   current->width = width;
   current->height = height;
   current->size = size;
   current->scaleX = OGL.scaleX;
   current->scaleY = OGL.scaleY;

   current->texture->width = current->width * OGL.scaleX;
   current->texture->height = current->height * OGL.scaleY;
   current->texture->clampS = 1;
   current->texture->clampT = 1;
   current->texture->address = current->startAddress;
   current->texture->clampWidth = current->width;
   current->texture->clampHeight = current->height;
   current->texture->frameBufferTexture = TRUE;
   current->texture->maskS = 0;
   current->texture->maskT = 0;
   current->texture->mirrorS = 0;
   current->texture->mirrorT = 0;
   current->texture->realWidth = pow2( current->width * OGL.scaleX );
   current->texture->realHeight = pow2( current->height * OGL.scaleY );
   current->texture->textureBytes = current->texture->realWidth * current->texture->realHeight * 4;
   cache.cachedBytes += current->texture->textureBytes;

   /* code goes here - just bind texture and copy it over */
   *(u32*)&gfx_info.RDRAM[current->startAddress] = current->startAddress;

   current->changed = TRUE;

   gSP.changed |= CHANGED_TEXTURE;
}

void FrameBuffer_RenderBuffer( u32 address )
{
   struct FrameBuffer *current = (struct FrameBuffer*)frameBuffer.top;

   while (current != NULL)
   {
      if ((current->startAddress <= address) &&
            (current->endAddress >= address))
      {

         /* code goes here */

         current->changed = FALSE;

         FrameBuffer_MoveToTop( current );

         gSP.changed |= CHANGED_TEXTURE | CHANGED_VIEWPORT;
         gDP.changed |= CHANGED_COMBINE;
         return;
      }
      current = current->lower;
   }
}

void FrameBuffer_RestoreBuffer( u32 address, u16 size, u16 width )
{
   struct FrameBuffer *current = (struct FrameBuffer*)frameBuffer.top;

   while (current != NULL)
   {
      if ((current->startAddress == address) &&
            (current->width == width) &&
            (current->size == size))
      {

         /* code goes here */

         FrameBuffer_MoveToTop( current );

         gSP.changed |= CHANGED_TEXTURE | CHANGED_VIEWPORT;
         gDP.changed |= CHANGED_COMBINE;
         return;
      }
      current = current->lower;
   }
}

struct FrameBuffer *FrameBuffer_FindBuffer( u32 address )
{
   struct FrameBuffer *current = (struct FrameBuffer*)frameBuffer.top;

   while (current)
   {
      if ((current->startAddress <= address) &&
            (current->endAddress >= address))
         return current;
      current = current->lower;
   }

   return NULL;
}

void FrameBuffer_ActivateBufferTexture( s16 t, struct FrameBuffer *buffer )
{
   buffer->texture->scaleS = OGL.scaleX / (float)buffer->texture->realWidth;
   buffer->texture->scaleT = OGL.scaleY / (float)buffer->texture->realHeight;

   if (gSP.textureTile[t]->shifts > 10)
      buffer->texture->shiftScaleS = (float)(1 << (16 - gSP.textureTile[t]->shifts));
   else if (gSP.textureTile[t]->shifts > 0)
      buffer->texture->shiftScaleS = 1.0f / (float)(1 << gSP.textureTile[t]->shifts);
   else
      buffer->texture->shiftScaleS = 1.0f;

   if (gSP.textureTile[t]->shiftt > 10)
      buffer->texture->shiftScaleT = (float)(1 << (16 - gSP.textureTile[t]->shiftt));
   else if (gSP.textureTile[t]->shiftt > 0)
      buffer->texture->shiftScaleT = 1.0f / (float)(1 << gSP.textureTile[t]->shiftt);
   else
      buffer->texture->shiftScaleT = 1.0f;

   if (gDP.loadTile->loadType == LOADTYPE_TILE)
   {
      buffer->texture->offsetS = gDP.loadTile->uls;
      buffer->texture->offsetT = (float)buffer->height - 
         (gDP.loadTile->ult + (gDP.textureImage.address - buffer->startAddress) / (buffer->width << buffer->size >> 1));
   }
   else
   {
      buffer->texture->offsetS = 0.0f;
      buffer->texture->offsetT = (float)buffer->height - (gDP.textureImage.address - buffer->startAddress) / (buffer->width << buffer->size >> 1);
   }

   FrameBuffer_MoveToTop( buffer );
   TextureCache_ActivateTexture( t, buffer->texture );
	gDP.changed |= CHANGED_FB_TEXTURE;
}

void FrameBuffer_ActivateBufferTextureBG(s16 t, struct FrameBuffer *buffer )
{
	if (buffer == NULL || buffer->texture == NULL)
		return;

   buffer->texture->scaleS = OGL.scaleX / (float)buffer->texture->realWidth;
   buffer->texture->scaleT = OGL.scaleY / (float)buffer->texture->realHeight;

	buffer->texture->shiftScaleS = 1.0f;
	buffer->texture->shiftScaleT = 1.0f;

	buffer->texture->offsetS = gSP.bgImage.imageX;
	buffer->texture->offsetT = (float)buffer->height - gSP.bgImage.imageY;

   FrameBuffer_MoveToTop( buffer );
   TextureCache_ActivateTexture( t, buffer->texture );
	gDP.changed |= CHANGED_FB_TEXTURE;
}
