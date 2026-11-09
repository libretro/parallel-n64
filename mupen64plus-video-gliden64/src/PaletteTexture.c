#include "Types.h"
#include "N64.h"
#include "gDP.h"
#include "convert.h"
#include "gli_textures.h"
#include "PaletteTexture.h"

static struct CachedTexture * l_pTexture;
static u16 l_palette[256];
static u64 l_paletteCRC256;

void gli_palette_init(void)
{
	if (!gli_tex_integer_supported())
		return;

	l_paletteCRC256 = 0;
	l_pTexture = gli_tex_lut_create(256, 1, GLI_TEX_UNIT_PALETTE, NULL);
}

void gli_palette_destroy(void)
{
	if (!gli_tex_integer_supported())
		return;

	gli_tex_remove(l_pTexture);
	l_pTexture = NULL;
}

void gli_palette_update(void)
{
	const u16 * src = (const u16*)&TMEM[256];
	u32 i;

	if (!gli_tex_integer_supported())
		return;

	if (l_paletteCRC256 == gDP.paletteCRC256)
		return;

	l_paletteCRC256 = gDP.paletteCRC256;

	for (i = 0; i < 256; ++i)
		l_palette[i] = swapword(src[i * 4]);

	gli_tex_lut_update(l_pTexture, GLI_TEX_UNIT_PALETTE, l_palette);
}
