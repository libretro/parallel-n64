/* test_gliden64_texture_params -- the blocks GLideN64 hands its backends
 * to make, fill, sample and bind a texture.
 *
 * They were C++ classes whose fields defaulted to "unset", which each
 * backend read to leave that state as it was. They are C structs now
 * (Graphics/gli_context.h), made from their _DEFAULT, and the backends
 * read GLI_PARAM_INVALID (or an anisotropy of 0) as unset. A default that
 * moved would change every texture made without naming it: a 2D texture
 * of one level, on unit 0 (Vulkan binds it there), with no format given;
 * sampler parameters all left as they are; nothing bound.
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
