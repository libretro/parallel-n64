#include <stdlib.h>
#include "Types.h"
#include "gli_textures.h"
#include "ZlutTexture.h"

#define ZLUT_SIZE 0x40000

static struct CachedTexture * l_pTexture;

void gli_zlut_init(void)
{
	const u16 * zLUT16;
	u32 * zLUT;
	u32 i;

	if (!gli_tex_integer_supported())
		return;

	zLUT = (u32*)malloc(ZLUT_SIZE * sizeof(u32));
	if (zLUT == NULL)
		return;
	zLUT16 = gli_depth_zlut();
	for (i = 0; i < ZLUT_SIZE; ++i)
		zLUT[i] = zLUT16[i];

	l_pTexture = gli_tex_lut_create(512, 512, GLI_TEX_UNIT_ZLUT, zLUT);
	free(zLUT);
}

void gli_zlut_destroy(void)
{
	if (!gli_tex_integer_supported())
		return;

	gli_tex_remove(l_pTexture);
	l_pTexture = NULL;
}
