#include "Types.h"
#include "gli_textures.h"
#include "ZlutTexture.h"

static struct CachedTexture * l_pTexture;

void gli_zlut_init(void)
{
	if (!gli_tex_integer_supported())
		return;

	/* 16-bit texels: the depth buffer's table uploads as it is */
	l_pTexture = gli_tex_lut_create(512, 512, GLI_TEX_UNIT_ZLUT, gli_depth_zlut());
}

void gli_zlut_destroy(void)
{
	if (!gli_tex_integer_supported())
		return;

	gli_tex_remove(l_pTexture);
	l_pTexture = NULL;
}
