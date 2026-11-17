/* test_gliden64_texture_cache -- GLideN64's texture cache, against a backend
 * that records what it is asked.
 *
 * The cache was a C++ singleton (Textures.cpp); it is C now (Textures.c).
 * Its frame buffer textures come from fixed slots, last freed first taken,
 * each made of no state but its GPU texture; destroying the cache deletes
 * the GPU texture of every slot in use and no other. A tile's texture is
 * found by its key: the same key twice makes one texture, and a key again
 * after another finds its own, with no new GPU texture. Sampling hands the
 * backend the tile's unit and the sampler's filters and wraps in GL's
 * numbers, anisotropy only on triangles, and a multisampled frame buffer
 * texture on its own units with no sampler state.
 *
 *   ./test_gliden64_texture_cache
 */
#include <stdio.h>
#include <string.h>

#include "gli_textures.h"
#include "gli_tex.h"
#include "gli_texdecode.h"
#include "Graphics/gli_context.h"
#include "Config.h"
#include "N64.h"
#include "gDP.h"
#include "gSP.h"
#include "GLideNHQ/Ext_TxFilter.h"

static int failures;

static void check(int _ok, const char *_what)
{
	if (!_ok) {
		printf("FAIL: %s\n", _what);
		failures++;
	}
}

/* ---- what the cache reads ---- */

Config gliden64Config;
gDPInfo gDP;
gSPInfo gSP;
u8 *RDRAM;
gli_context gli_ctx;

static int triangles;
static u64 tileCrc;
static gli_tex_sampler sampler;
static u32 activatedFb;
static u32 tileWidth = 8;

void CRC_Init(void) {}
bool gli_TFH_is_inited(void) { return 0; }
bool gli_combiner_uses_lod(void) { return 0; }
bool gli_drawer_is_triangle(void) { return triangles != 0; }
void gli_fb_activate_texture(u32 _t, u32 _address) { (void)_t; (void)_address; activatedFb++; }
void gli_fb_activate_texture_bg(u32 _t, u32 _address) { (void)_t; (void)_address; activatedFb++; }
int gli_tex_filter_skips(void) { return 0; }

static const gli_tex_format format16 = { NULL, NULL, GLI_TEXT_5551, GLI_TEXF_RGB5A1, GLI_TEXT_U8, GLI_TEXF_RGBA8, GLI_TEXF_RGB5A1, 0, 0 };
const gli_tex_format *gli_tex_format_get(u32 tlut, u32 size, u32 format) { (void)tlut; (void)size; (void)format; return &format16; }
void gli_tex_decode_tmem(const struct gli_tex *t, const gli_tex_format *f, int out32, u32 tlut, u16 *line, void *dst)
{ (void)t; (void)f; (void)out32; (void)tlut; (void)line; (void)dst; }
void gli_tex_decode_bg(const struct gli_tex *t, const gli_tex_format *f, int out32, u32 address, u32 bpl, void *dst)
{ (void)t; (void)f; (void)out32; (void)address; (void)bpl; (void)dst; }
void gli_tex_decode_bg_rows(const struct gli_tex *t, const gli_tex_format *f, int out32, u32 address, u32 bpl, u32 y, u32 rows, void *dst)
{ (void)t; (void)f; (void)out32; (void)address; (void)bpl; (void)y; (void)rows; (void)dst; }
u16 *gli_tex_depth_to_half(u16 *buf, u32 n) { (void)n; return buf; }
void gli_tex_tile_sizes(u32 _t, gli_tile_sizes *_s, struct gDPTile *_l)
{
	(void)_t; (void)_l;
	memset(_s, 0, sizeof(*_s));
	_s->width = _s->realWidth = tileWidth;
	_s->height = _s->realHeight = 4;
}
void gli_tex_from_tile(struct gli_tex *_tex, const struct gDPTile *_tile, const gli_tile_sizes *_s)
{
	(void)_tile;
	_tex->width = _tex->realWidth = (u16)_s->width;
	_tex->height = _tex->realHeight = (u16)_s->height;
}
u8 gli_tex_max_level(const struct gli_tex *_tex, u32 _tile) { (void)_tex; (void)_tile; return 0; }
u32 gli_tex_mip_step(struct gli_tex *_tex, u32 _l, u16 *_line, u32 _s) { (void)_tex; (void)_l; (void)_line; (void)_s; return 0; }
u64 gli_tex_crc(u32 _t, const gli_tile_sizes *_s) { (void)_t; (void)_s; return tileCrc; }
u64 gli_tex_bg_crc(void) { return 0; }
void gli_tex_from_bg(struct gli_tex *_tex) { (void)_tex; }
void gli_tex_sampler_get(const struct gli_tex *_tex, int _lod, gli_tex_sampler *_s) { (void)_tex; (void)_lod; *_s = sampler; }
u32 gli_tex_reverse_dxt(u32 val, u32 lrs, u32 width, u32 size) { (void)lrs; (void)width; (void)size; return val; }

boolean txfilter_filter(uint8 *src, int w, int h, uint16 f, uint64 c, GHQTexInfo *i)
{ (void)src; (void)w; (void)h; (void)f; (void)c; (void)i; return 0; }
boolean txfilter_hirestex(uint64 c, uint64 r, uint16 *p, GHQTexInfo *i) { (void)c; (void)r; (void)p; (void)i; return 0; }
uint64 txfilter_checksum(uint8 *s, int w, int h, int z, int r, uint8 *p) { (void)s; (void)w; (void)h; (void)z; (void)r; (void)p; return 0; }
boolean txfilter_dmptx(uint8 *s, int w, int h, int r, uint16 g, uint16 n, uint64 c)
{ (void)s; (void)w; (void)h; (void)r; (void)g; (void)n; (void)c; return 0; }

/* ---- the backend ---- */

static u32 nextName = 1;
static u32 created, deleted, inits;
static u32 deletedNames[4096];
static gli_tex_parameters lastParams;
static gli_init_texture_params lastInit;
static u32 setParams;
static u8 scratch[64];

static u32 fake_create(void *_i, u32 _target) { (void)_i; (void)_target; created++; return nextName++; }
static void fake_delete(void *_i, u32 _name) { (void)_i; if (deleted < 4096) deletedNames[deleted] = _name; deleted++; }
static void fake_init(void *_i, const gli_init_texture_params *_p) { (void)_i; lastInit = *_p; inits++; }
static void fake_update(void *_i, const gli_update_texture_params *_p) { (void)_i; (void)_p; }
static void *fake_lend(void *_i, size_t _bytes) { (void)_i; return _bytes <= sizeof(scratch) ? scratch : NULL; }
static void fake_set(void *_i, const gli_tex_parameters *_p) { (void)_i; lastParams = *_p; setParams++; }
static void fake_bind(void *_i, const gli_bind_texture_params *_p) { (void)_i; (void)_p; }
static void fake_unpack(void *_i, s32 _a) { (void)_i; (void)_a; }
static s32 fake_get_unpack(const void *_i) { (void)_i; return 4; }
static u32 fake_convert(const void *_i, u32 _f) { (void)_i; return _f; }
static bool fake_error(const void *_i) { (void)_i; return 0; }

static gli_context_ops ops;

static void reset_backend(void)
{
	memset(&ops, 0, sizeof(ops));
	ops.createTexture = fake_create;
	ops.deleteTexture = fake_delete;
	ops.init2DTexture = fake_init;
	ops.update2DTexture = fake_update;
	ops.lendTextureData = fake_lend;
	ops.setTextureParameters = fake_set;
	ops.bindTexture = fake_bind;
	ops.setTextureUnpackAlignment = fake_unpack;
	ops.getTextureUnpackAlignment = fake_get_unpack;
	ops.convertInternalTextureFormat = fake_convert;
	ops.isError = fake_error;
	gli_ctx.ops = &ops;
	{
		static const gli_fb_formats unset = GLI_FB_FORMATS_DEFAULT;
		gli_ctx.formats = unset;
	}
	gli_fb_formats_gl(&gli_ctx.formats, GLI_FB_GL, 0);
}

static int is_clean(const gli_tex *_tex)
{
	gli_tex blank;
	memset(&blank, 0, sizeof(blank));
	blank.name = _tex->name;
	return memcmp(&blank, _tex, sizeof(blank)) == 0;
}

static void test_fb_slots(void)
{
	gli_tex *a, *b, *c, *again;
	u32 i, liveBefore, names[3];
	int found;

	gli_textures_init();
	check(created == 1 && inits == 1, "init makes the dummy texture");
	check(lastInit.width == 2 && lastInit.height == 2 && lastInit.data != NULL &&
		lastInit.format == GLI_CF_RGBA && lastInit.internalFormat == GLI_ICF_RGBA8, "the dummy is 2x2 RGBA8 with data");
	check(setParams == 2 && lastParams.textureUnitIndex == GLI_TU_TEX1 &&
		lastParams.minFilter == GLI_TP_FILTER_NEAREST && lastParams.magFilter == GLI_TP_FILTER_NEAREST,
		"init samples the dummy on both units, nearest");
	check(gli_texture_current[0] == NULL && gli_texture_current[1] == NULL, "init leaves no current texture");

	a = gli_textures_add_fb(GLI_TT_TEXTURE_2D);
	b = gli_textures_add_fb(GLI_TT_TEXTURE_2D);
	check(a != NULL && b != NULL && a != b && a->name != b->name, "frame buffer textures are distinct");
	check(is_clean(a) && a->name != 0, "a frame buffer texture is its GPU texture and no state");
	a->width = 320;
	a->frameBufferTexture = GLI_TEX_FB_ONE_SAMPLE;
	gli_textures_remove_fb(a);
	check(deleted == 1 && deletedNames[0] != 0, "removing deletes its GPU texture");
	again = gli_textures_add_fb(GLI_TT_TEXTURE_2D);
	check(again == a, "the slot freed last is taken first");
	check(is_clean(again), "a slot taken again keeps no state");

	c = gli_textures_add_fb(GLI_TT_TEXTURE_2D_MULTISAMPLE);
	names[0] = b->name;
	names[1] = c->name;
	names[2] = again->name;
	liveBefore = deleted;
	gli_textures_destroy();
	/* the dummy, again, b and c */
	check(deleted - liveBefore == 4, "destroying deletes every slot in use, once");
	found = 0;
	for (i = liveBefore; i < deleted; ++i)
		found += deletedNames[i] == names[0] || deletedNames[i] == names[1] || deletedNames[i] == names[2];
	check(found == 3, "destroying deletes the slots' own GPU textures");
}

static void test_tiles(void)
{
	static gDPTile tile, other;
	gli_tex *first, *second;
	u32 createdBefore;

	gli_textures_init();
	memset(&tile, 0, sizeof(tile));
	memset(&other, 0, sizeof(other));
	gSP.textureTile[0] = &tile;
	gSP.textureTile[1] = &other;
	gDP.loadTile = &other;
	sampler.minFilter = GLI_TF_LINEAR_MIP_NEAREST;
	sampler.magFilter = GLI_TF_LINEAR;
	sampler.wrapS = GLI_WRAP_MIRRORED_REPEAT;
	sampler.wrapT = GLI_WRAP_CLAMP_TO_EDGE;
	sampler.maxLevel = 3;
	config.texture.maxAnisotropyF = 8.0f;

	createdBefore = created;
	tileCrc = 0x1234;
	triangles = 1;
	gli_textures_update(0);
	first = gli_texture_current[0];
	check(first != NULL && first->crc == 0x1234 && created == createdBefore + 1, "a new key makes one texture");
	check(lastInit.handle == first->name && lastInit.width == 8 && lastInit.height == 4 &&
		lastInit.textureUnitIndex == GLI_TU_TEX0 && lastInit.mutableStorage, "the texture is loaded on its tile's unit");
	check(lastParams.handle == first->name && lastParams.target == GLI_TT_TEXTURE_2D &&
		lastParams.textureUnitIndex == GLI_TU_TEX0, "it is sampled on its tile's unit");
	check(lastParams.minFilter == GLI_TP_FILTER_LINEAR_MIPMAP_NEAREST && lastParams.magFilter == GLI_TP_FILTER_LINEAR,
		"the sampler's filters, in GL's numbers");
	check(lastParams.wrapS == GLI_TP_WRAP_MIRRORED_REPEAT && lastParams.wrapT == GLI_TP_WRAP_CLAMP_TO_EDGE,
		"the sampler's wraps, in GL's numbers");
	check(lastParams.maxMipmapLevel == 3, "the sampler's last level");
	check(lastParams.maxAnisotropy == 8.0f, "anisotropy on triangles");

	gli_textures_update(0);
	check(created == createdBefore + 1 && gli_texture_current[0] == first, "the same key again makes nothing");

	tileCrc = 0x5678;
	triangles = 0;
	gli_textures_update(0);
	second = gli_texture_current[0];
	check(second != first && second->crc == 0x5678 && created == createdBefore + 2, "another key makes another");
	check(!(lastParams.maxAnisotropy > 0.0f), "no anisotropy off triangles");

	tileCrc = 0x1234;
	gli_textures_update(0);
	check(gli_texture_current[0] == first && created == createdBefore + 2, "the first key again finds its own");

	/* the same key at another size is dropped; its slot is taken again
	 * with the GPU texture it had */
	{
		const u32 name = first->name;
		gli_textures_activate(0, second);
		tileWidth = 16;
		gli_textures_update(0);
		check(gli_texture_current[0]->width == 16 && gli_texture_current[0]->name == name &&
			created == createdBefore + 2, "a dropped texture's GPU texture is used again");
		tileWidth = 8;
	}

	tileCrc = 0x9abc;
	gli_textures_update(1);
	check(gli_texture_current[1] != NULL && gli_texture_current[1]->crc == 0x9abc &&
		lastParams.textureUnitIndex == GLI_TU_TEX1 && lastInit.textureUnitIndex == GLI_TU_TEX1,
		"tile 1's texture is loaded and sampled on its unit");

	tile.textureMode = TEXTUREMODE_FRAMEBUFFER;
	gli_textures_update(0);
	check(activatedFb == 1, "a frame buffer tile goes to the frame buffer");
	tile.textureMode = 0;

	{
		gli_tex *ms = gli_textures_add_fb(GLI_TT_TEXTURE_2D_MULTISAMPLE);
		ms->frameBufferTexture = GLI_TEX_FB_MULTI_SAMPLE;
		config.video.multisampling = 4;
		gli_textures_activate(1, ms);
		check(lastParams.target == GLI_TT_TEXTURE_2D_MULTISAMPLE && lastParams.textureUnitIndex == GLI_TU_MSTEX1 &&
			lastParams.minFilter == GLI_PARAM_INVALID && lastParams.wrapS == GLI_PARAM_INVALID,
			"a multisampled texture: its own units, no sampler state");
		check(gli_texture_current[1] == ms, "the texture activated is the tile's");
		config.video.multisampling = 0;
	}

	{
		static const u16 zlut[4] = { 0 };
		gli_tex *lut = gli_tex_lut_create(2, 2, GLI_TEX_UNIT_ZLUT, zlut);
		check(lastInit.internalFormat == GLI_ICF_R16UI && lastInit.format == GLI_CF_RED_INTEGER &&
			lastInit.dataType == GLI_DT_UNSIGNED_SHORT, "a LUT takes the backend's LUT format");
		check(lastParams.textureUnitIndex == GLI_TU_ZLUTTex && lastParams.wrapS == GLI_TP_WRAP_CLAMP_TO_EDGE,
			"the depth LUT on its unit, clamped");
		check(lut->textureBytes == 2 * 2 * 2 && lut->frameBufferTexture == GLI_TEX_FB_ONE_SAMPLE, "a LUT's size and kind");
	}

	gli_textures_destroy();
	check(gli_texture_current[0] == NULL && gli_texture_current[1] == NULL, "destroying leaves no current texture");
}

int main(void)
{
	reset_backend();
	test_fb_slots();
	test_tiles();
	if (failures) {
		printf("%d failed\n", failures);
		return 1;
	}
	printf("texture cache: ok\n");
	return 0;
}
