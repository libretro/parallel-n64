/* test_gliden64_texture_params -- the blocks GLideN64 hands its backends
 * to make, fill, sample and bind a texture, to make and blit frame
 * buffers, and to draw.
 *
 * They were C++ classes whose fields defaulted to "unset", which each
 * backend read to leave that state as it was. They are C structs now
 * (Graphics/gli_context.h), made from their _DEFAULT, and the backends
 * read GLI_PARAM_INVALID (or an anisotropy of 0) as unset. A default that
 * moved would change every texture made without naming it: a 2D texture
 * of one level, on unit 0 (Vulkan binds it there), with no format given;
 * sampler parameters all left as they are; nothing bound; a frame
 * buffer block naming nothing; a rect textured unless it says not.
 *
 *   ./test_gliden64_texture_params
 */
#include <stdio.h>
#include <stddef.h>

#include "Graphics/gli_context.h"

static int failures;

static void check(int ok, const char *what)
{
   if (!ok)
   {
      printf("FAIL: %s\n", what);
      failures++;
   }
}

int main(void)
{
   const gli_init_texture_params init = GLI_INIT_TEXTURE_PARAMS_DEFAULT;
   const gli_update_texture_params update = GLI_UPDATE_TEXTURE_PARAMS_DEFAULT;
   const gli_tex_parameters tex = GLI_TEX_PARAMETERS_DEFAULT;
   const gli_bind_texture_params bind = GLI_BIND_TEXTURE_PARAMS_DEFAULT;

   /* what an unset C++ Parameter holds, so one handed over reads unset */
   check(GLI_PARAM_INVALID == 0xFFFFFFFFu, "unset is an unset Parameter");

   check(init.handle == 0, "init: no handle");
   check(init.textureUnitIndex == 0, "init: unit 0");
   check(init.target == GLI_TT_TEXTURE_2D, "init: a 2D texture");
   check(init.msaaLevel == 0, "init: not multisampled");
   check(init.width == 0 && init.height == 0, "init: no size");
   check(init.mipMapLevel == 0, "init: level 0");
   check(init.mipMapLevels == 1, "init: one level");
   check(init.format == GLI_PARAM_INVALID, "init: no format");
   check(init.internalFormat == GLI_PARAM_INVALID, "init: no internal format");
   check(init.dataType == GLI_PARAM_INVALID, "init: no data type");
   check(init.data == NULL, "init: no data");
   check(!init.mutableStorage, "init: immutable storage");

   check(update.handle == 0, "update: no handle");
   check(update.textureUnitIndex == 0, "update: unit 0");
   check(update.x == 0 && update.y == 0, "update: at the origin");
   check(update.width == 0 && update.height == 0, "update: no size");
   check(update.mipMapLevel == 0, "update: level 0");
   check(update.format == GLI_PARAM_INVALID, "update: no format");
   check(update.internalFormat == GLI_PARAM_INVALID, "update: no internal format");
   check(update.dataType == GLI_PARAM_INVALID, "update: no data type");
   check(update.data == NULL, "update: no data");

   check(tex.handle == 0, "parameters: no handle");
   check(tex.textureUnitIndex == 0, "parameters: unit 0");
   check(tex.target == GLI_PARAM_INVALID, "parameters: no target");
   check(tex.magFilter == GLI_PARAM_INVALID, "parameters: magnification left");
   check(tex.minFilter == GLI_PARAM_INVALID, "parameters: minification left");
   check(tex.wrapS == GLI_PARAM_INVALID, "parameters: s wrap left");
   check(tex.wrapT == GLI_PARAM_INVALID, "parameters: t wrap left");
   check(tex.maxMipmapLevel == GLI_PARAM_INVALID, "parameters: top level left");
   check(!(tex.maxAnisotropy > 0.0f), "parameters: anisotropy left");

   check(bind.texture == 0, "bind: no texture");
   check(bind.textureUnitIndex == GLI_PARAM_INVALID, "bind: no unit");
   check(bind.target == GLI_PARAM_INVALID, "bind: no target");

   {
      const gli_bind_image_texture_params image = GLI_BIND_IMAGE_TEXTURE_PARAMS_DEFAULT;
      const gli_init_renderbuffer_params rb = GLI_INIT_RENDERBUFFER_PARAMS_DEFAULT;
      const gli_fb_render_target target = GLI_FB_RENDER_TARGET_DEFAULT;
      const gli_blit_params blit = GLI_BLIT_PARAMS_DEFAULT;
      const gli_draw_triangles_params tri = GLI_DRAW_TRIANGLES_PARAMS_DEFAULT;
      const gli_draw_rects_params rect = GLI_DRAW_RECTS_PARAMS_DEFAULT;

      check(image.imageUnit == GLI_PARAM_INVALID && image.texture == 0 &&
         image.accessMode == GLI_PARAM_INVALID && image.textureFormat == GLI_PARAM_INVALID, "image: nothing given");
      check(rb.handle == 0 && rb.target == GLI_PARAM_INVALID && rb.format == GLI_PARAM_INVALID &&
         rb.width == 0 && rb.height == 0, "renderbuffer: nothing given");
      check(target.bufferHandle == 0 && target.bufferTarget == GLI_PARAM_INVALID && target.attachment == GLI_PARAM_INVALID &&
         target.textureTarget == GLI_PARAM_INVALID && target.textureHandle == 0, "render target: nothing given");
      check(blit.readBuffer == 0 && blit.drawBuffer == 0 && blit.mask == GLI_PARAM_INVALID &&
         blit.filter == GLI_PARAM_INVALID, "blit: no buffers, mask or filter");
      check(blit.srcX0 == 0 && blit.srcY1 == 0 && blit.dstX0 == 0 && blit.dstY1 == 0, "blit: empty rectangles");
      check(tri.mode == GLI_PARAM_INVALID && tri.elementsType == GLI_PARAM_INVALID && tri.verticesCount == 0 &&
         tri.elementsCount == 0 && !tri.flatColors && tri.vertices == NULL && tri.elements == NULL &&
         tri.combiner == NULL, "triangles: nothing to draw, smooth colors");
      check(rect.mode == GLI_PARAM_INVALID && rect.texrect && rect.verticesCount == 0 && rect.vertices == NULL &&
         rect.combiner == NULL, "rects: textured, nothing to draw");
   }

   /* the fields the backends take as numbers are 32 bits, as the
    * Parameters they were */
   check(sizeof(tex.magFilter) == 4 && sizeof(init.handle) == 4, "fields are 32 bits");

   if (failures)
   {
      printf("%d failed\n", failures);
      return 1;
   }
   printf("texture parameter defaults: ok\n");
   return 0;
}
