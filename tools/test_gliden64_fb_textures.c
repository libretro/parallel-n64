/* test_gliden64_fb_textures -- a GLideN64 frame buffer's GPU side
 * (FrameBufferTextures.c) on stub modules and a stub graphics context.
 *
 * Made, a buffer is zero but for a block load, its texture (multisampled
 * with multisampling) and its frame buffer; let go, every frame buffer and
 * texture it made goes. Set up, it takes its address, width, size, a
 * colour buffer's VI height or 1, its end, the scale (1 for an auxiliary
 * buffer copied to RDRAM, the native factor, else the window's), not
 * cleared, the swap; its texture as wide and as tall as the VI's tallest
 * at that scale, in the colour or monochrome format, nearest, attached,
 * and with multisampling a multisampled one and its resolve; RDRAM copy
 * storage for the image lent it. Resolved with multisampling, once until
 * forced. The texture a tile samples: its own, the depth buffer's for a
 * depth image drawn as its colour image, a copy where it is drawn into
 * (made once, blitted once until drawn again) or nothing copied with a
 * texture barrier, at the offset of the tile's address, a load tile's
 * origin added, its scales and shift scales; where the tile wraps the part
 * it samples, blitted from the resolve where blits are weak, made again
 * for another size; the background's at its image's offset.
 *
 *   ./test_gliden64_fb_textures
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gli_fb_rdram.h"
#include "gli_fb_list.h"
#include "gli_framebuffer.h"
#include "gli_textures.h"
#include "gli_tex.h"
#include "DepthBuffer.h"
#include "DisplayWindow.h"
#include "Config.h"
#include "GBI.h"
#include "VI.h"
#include "gDP.h"
#include "gSP.h"
#include "Graphics/gli_context.h"

static int failures;

static void check(int _ok, const char *_what)
{
	if (!_ok) {
		printf("FAIL: %s\n", _what);
		failures++;
	}
}

Config gliden64Config;
gDPInfo gDP;
gSPInfo gSP;
VIInfo VI;
gli_window g_window;
gli_context gli_ctx;

static char log_[4096];
static void say(const char *_s)
{
	if (strlen(log_) + strlen(_s) + 2 < sizeof(log_)) {
		strcat(log_, _s);
		strcat(log_, " ");
	}
}
static void clear_log(void) { log_[0] = '\0'; }
static int has(const char *_s) { return strstr(log_, _s) != NULL; }

/* ---- textures ---- */

/* a pool handed out in turn; texturesMade counts since the last counts() */
static gli_tex textures[64];
static int texIndex;
static gli_tex *made_[8];
static u32 texTargets[8];
static int texturesMade, texturesFreed;
static const gli_tex *freed[8];
gli_tex *gli_textures_add_fb(u32 _target)
{
	gli_tex *t = &textures[texIndex++ & 63];
	memset(t, 0xFF, sizeof(*t));
	t->name = 100 + (u32)texturesMade;
	made_[texturesMade & 7] = t;
	texTargets[texturesMade & 7] = _target;
	texturesMade++;
	return t;
}
void gli_textures_remove_fb(gli_tex *_t) { freed[texturesFreed++ & 7] = _t; }

/* ---- the buffer's other sides ---- */

static int ended, auxiliary;
void gli_fb_update_end_address(gli_fb *_fb) { (void)_fb; ended++; }
int gli_fb_is_auxiliary(const gli_fb *_fb) { (void)_fb; return auxiliary; }
u16 gli_VI_GetMaxBufferHeight(u16 _w) { return _w > 320 ? 480 : 240; }
static const gli_fb *lentTo;
static u32 lent;
static struct gli_fb_list *lentFrom;
static int listToken;
void gli_fb_list_lend_copy(struct gli_fb_list *_l, gli_fb *_fb, u32 _capacity) { lentFrom = _l; lentTo = _fb; lent = _capacity; }
void gli_fb_set_current_draw_buffer(void) { say("draw-buffer"); }
struct FrameBuffer { gli_fb fb; };
static struct FrameBuffer buffers[2];
static struct FrameBuffer *current;
struct FrameBuffer *gli_fb_current(void) { return current; }
struct gli_fb *gli_fb_of(struct FrameBuffer *_p) { return &_p->fb; }
static DepthBuffer depth;
static gli_tex depthTexture, depthCopy;
struct gli_tex *gli_depth_copy_texture(DepthBuffer *_d, const struct gli_fb *_b)
{
	check(_d == &depth && _b == &buffers[0].fb, "its own depth copied");
	say("depth-copy");
	return &depthCopy;
}

/* ---- the context ---- */

static u32 nextFbo = 70;
static u32 deleted[8];
static int fbosDeleted;
static gli_init_texture_params inits[4];
static int initsMade;
static gli_tex_parameters params[4];
static int paramsSet;
static gli_fb_render_target targets[4];
static int targetsAdded;
static gli_blit_params blits[4];
static int blitsMade;
static u32 boundTarget, boundName;
static u32 c_create_fb(void *_i) { (void)_i; say("create-fb"); return nextFbo++; }
static void c_delete_fb(void *_i, u32 _n) { (void)_i; deleted[fbosDeleted++ & 7] = _n; }
static void c_init(void *_i, const gli_init_texture_params *_p) { (void)_i; inits[initsMade++ & 3] = *_p; say("init-tex"); }
static void c_params(void *_i, const gli_tex_parameters *_p) { (void)_i; params[paramsSet++ & 3] = *_p; say("tex-params"); }
static void c_target(void *_i, const gli_fb_render_target *_p) { (void)_i; targets[targetsAdded++ & 3] = *_p; say("attach"); }
static bool c_false(const void *_i) { (void)_i; return 0; }
static void c_bind(void *_i, u32 _t, u32 _n) { (void)_i; boundTarget = _t; boundName = _n; say("bind-fb"); }
static bool c_blit(void *_i, const gli_blit_params *_p) { (void)_i; blits[blitsMade++ & 3] = *_p; say("blit"); return 1; }
static void c_barrier(void *_i) { (void)_i; say("barrier"); }
static gli_context_ops ops;

static void setup(void)
{
	memset(&ops, 0, sizeof(ops));
	ops.createFramebuffer = c_create_fb;
	ops.deleteFramebuffer = c_delete_fb;
	ops.init2DTexture = c_init;
	ops.setTextureParameters = c_params;
	ops.addFrameBufferRenderTarget = c_target;
	ops.isFramebufferError = c_false;
	ops.bindFramebuffer = c_bind;
	ops.blitFramebuffers = c_blit;
	ops.textureBarrier = c_barrier;
	gli_ctx.ops = &ops;
	gli_ctx.readFramebuffer = GLI_BT_READ_FRAMEBUFFER;
	gli_ctx.drawFramebuffer = GLI_BT_DRAW_FRAMEBUFFER;
	gli_ctx.formats.colorInternalFormat = 0x3001;
	gli_ctx.formats.colorFormat = 0x3002;
	gli_ctx.formats.colorType = 0x3003;
	gli_ctx.formats.colorFormatBytes = 4;
	gli_ctx.formats.monochromeInternalFormat = 0x1001;
	gli_ctx.formats.monochromeFormat = 0x1002;
	gli_ctx.formats.monochromeType = 0x1003;
	gli_ctx.formats.monochromeFormatBytes = 1;
	g_window.scaleX = 2.0f;
	g_window.buffersSwapCount = 9;
	VI.height = 224;
	gSP.textureTile[0] = &gDP.tiles[0];
	gSP.textureTile[1] = &gDP.tiles[1];
}

static void counts(void)
{
	texturesMade = texturesFreed = initsMade = paramsSet = targetsAdded = blitsMade = fbosDeleted = 0;
	clear_log();
}

static gli_fb *made(void)
{
	gli_fb *fb = &buffers[0].fb;
	counts();
	gli_fb_make(fb);
	return fb;
}

static void test_make(void)
{
	gli_fb *fb = &buffers[0].fb;
	memset(fb, 0x5a, sizeof(*fb));
	config.video.multisampling = 0;
	nextFbo = 70;
	counts();
	gli_fb_make(fb);
	check(fb->m_FBO == 70 && fb->m_pTexture == made_[0] && texTargets[0] == GLI_TT_TEXTURE_2D && texturesMade == 1,
		"made: its frame buffer and texture");
	check(fb->m_loadType == LOADTYPE_BLOCK && fb->m_width == 0 && fb->m_resolveFBO == 0 && fb->m_pSubTexture == NULL
		&& fb->m_cleared == 0 && fb->m_RdramCopy == NULL, "the rest zero, a block load");
	config.video.multisampling = 4;
	counts();
	gli_fb_make(fb);
	check(texTargets[0] == GLI_TT_TEXTURE_2D_MULTISAMPLE, "multisampled with multisampling");
	config.video.multisampling = 0;

	fb->m_resolveFBO = 71;
	fb->m_SubFBO = 72;
	fb->m_copyFBO = 73;
	fb->m_pResolveTexture = &textures[60];
	fb->m_pSubTexture = &textures[61];
	fb->m_pFrameBufferCopyTexture = &textures[62];
	counts();
	gli_fb_free(fb);
	check(fbosDeleted == 4 && deleted[0] == fb->m_FBO && deleted[1] == 71 && deleted[2] == 72 && deleted[3] == 73,
		"let go: all its frame buffers");
	check(texturesFreed == 4 && freed[0] == fb->m_pTexture && freed[1] == &textures[60] && freed[2] == &textures[61]
		&& freed[3] == &textures[62], "and textures");
}

static void test_init(void)
{
	gli_fb *fb;
	config.frameBufferEmulation.enable = 1;
	config.frameBufferEmulation.nativeResFactor = 0;
	fb = made();
	ended = 0;
	fb->m_cleared = 1;
	fb->m_fingerprint = 1;
	gli_fb_init(fb, (struct gli_fb_list *)&listToken, 0x100000, G_IM_FMT_RGBA, G_IM_SIZ_16b, 320, 1);
	check(fb->m_startAddress == 0x100000 && fb->m_width == 320 && fb->m_height == 224 && fb->m_size == G_IM_SIZ_16b
		&& ended == 1, "set up: its address, width, the VI's height, size and end");
	check(fb->m_scale == 2.0f && fb->m_cfb == 1 && fb->m_cleared == 0 && fb->m_fingerprint == 0 && fb->m_swapCount == 9,
		"at the window's scale, not cleared, this swap");
	check(fb->m_pTexture->width == 640 && fb->m_pTexture->height == 480 && fb->m_pTexture->realWidth == 640
		&& fb->m_pTexture->realHeight == 480 && fb->m_pTexture->clampWidth == 320 && fb->m_pTexture->clampHeight == 240
		&& fb->m_pTexture->format == G_IM_FMT_RGBA && fb->m_pTexture->size == G_IM_SIZ_16b
		&& fb->m_pTexture->address == 0x100000 && fb->m_pTexture->textureBytes == 640 * 480 * 4,
		"its texture as wide and tall as the VI's tallest at its scale");
	check(fb->m_pTexture->clampS == 1 && fb->m_pTexture->clampT == 1 && fb->m_pTexture->frameBufferTexture == GLI_TEX_FB_ONE_SAMPLE
		&& !fb->m_pTexture->maskS && !fb->m_pTexture->maskT && !fb->m_pTexture->mirrorS && !fb->m_pTexture->mirrorT,
		"clamped, a frame buffer's");
	check(initsMade == 1 && inits[0].handle == 100 && inits[0].textureUnitIndex == GLI_TU_TEX0 && inits[0].width == 640
		&& inits[0].height == 480 && inits[0].internalFormat == 0x3001 && inits[0].format == 0x3002 && inits[0].dataType == 0x3003
		&& inits[0].msaaLevel == 0, "in the colour format");
	check(paramsSet == 1 && params[0].handle == 100 && params[0].target == GLI_TT_TEXTURE_2D && params[0].textureUnitIndex == GLI_TU_TEX0
		&& params[0].minFilter == GLI_TP_FILTER_NEAREST && params[0].magFilter == GLI_TP_FILTER_NEAREST, "nearest");
	check(targetsAdded == 1 && targets[0].bufferHandle == fb->m_FBO && targets[0].bufferTarget == GLI_BT_FRAMEBUFFER
		&& targets[0].attachment == GLI_BA_COLOR_ATTACHMENT0 && targets[0].textureTarget == GLI_TT_TEXTURE_2D
		&& targets[0].textureHandle == 100, "attached to its frame buffer");
	check(lentTo == fb && lentFrom == (struct gli_fb_list *)&listToken && lent == 320 * 240 * 2, "RDRAM copy storage for its tallest image, from the list");
	check(fb->m_resolveFBO == 0 && fb->m_pResolveTexture == NULL, "no resolve without multisampling");

	fb = made();
	gli_fb_init(fb, (struct gli_fb_list *)&listToken, 0x100000, G_IM_FMT_I, G_IM_SIZ_8b, 640, 0);
	check(fb->m_height == 1 && fb->m_cfb == 0 && inits[0].internalFormat == 0x1001 && inits[0].format == 0x1002
		&& inits[0].dataType == 0x1003 && fb->m_pTexture->textureBytes == 1280 * 960 && lent == 640 * 480,
		"not a colour buffer: 1 high; 8 bits: monochrome");
	fb = made();
	gli_fb_init(fb, (struct gli_fb_list *)&listToken, 0x100000, G_IM_FMT_RGBA, G_IM_SIZ_32b, 320, 0);
	check(lent == 320 * 240 * 4, "32 bits: twice the storage");

	config.frameBufferEmulation.nativeResFactor = 3;
	fb = made();
	gli_fb_init(fb, (struct gli_fb_list *)&listToken, 0x100000, G_IM_FMT_RGBA, G_IM_SIZ_16b, 320, 1);
	check(fb->m_scale == 3.0f && fb->m_pTexture->width == 960, "the native factor");
	config.frameBufferEmulation.enable = 0;
	fb = made();
	gli_fb_init(fb, (struct gli_fb_list *)&listToken, 0x100000, G_IM_FMT_RGBA, G_IM_SIZ_16b, 320, 1);
	check(fb->m_scale == 2.0f, "without frame buffer emulation: the window's");
	config.frameBufferEmulation.enable = 1;
	auxiliary = 1;
	config.frameBufferEmulation.copyAuxToRDRAM = 1;
	fb = made();
	gli_fb_init(fb, (struct gli_fb_list *)&listToken, 0x100000, G_IM_FMT_RGBA, G_IM_SIZ_16b, 320, 1);
	check(fb->m_scale == 1.0f, "an auxiliary buffer copied to RDRAM: 1");
	config.frameBufferEmulation.copyAuxToRDRAM = 0;
	fb = made();
	gli_fb_init(fb, (struct gli_fb_list *)&listToken, 0x100000, G_IM_FMT_RGBA, G_IM_SIZ_16b, 320, 1);
	check(fb->m_scale == 3.0f, "one not copied: as the others");
	auxiliary = 0;
	config.frameBufferEmulation.copyAuxToRDRAM = 1;
	fb = made();
	gli_fb_init(fb, (struct gli_fb_list *)&listToken, 0x100000, G_IM_FMT_RGBA, G_IM_SIZ_16b, 320, 1);
	check(fb->m_scale == 3.0f, "not auxiliary: as the others");
	config.frameBufferEmulation.copyAuxToRDRAM = 0;
	config.frameBufferEmulation.nativeResFactor = 0;

	/* multisampled: and its resolve */
	config.video.multisampling = 4;
	nextFbo = 80;
	fb = made();
	gli_fb_init(fb, (struct gli_fb_list *)&listToken, 0x100000, G_IM_FMT_RGBA, G_IM_SIZ_16b, 320, 1);
	check(fb->m_pTexture->frameBufferTexture == GLI_TEX_FB_MULTI_SAMPLE && inits[0].msaaLevel == 4
		&& targets[0].textureTarget == GLI_TT_TEXTURE_2D_MULTISAMPLE && targets[0].bufferHandle == 80,
		"multisampled: its texture so, attached as such");
	check(paramsSet == 1 && params[0].handle == 101, "its own not filtered, the resolve nearest");
	check(fb->m_pResolveTexture == made_[1] && texTargets[1] == GLI_TT_TEXTURE_2D && fb->m_resolveFBO == 81
		&& fb->m_pResolveTexture->width == 640 && fb->m_pResolveTexture->frameBufferTexture == GLI_TEX_FB_ONE_SAMPLE
		&& targets[1].bufferHandle == 81 && targets[1].textureHandle == 101 && targets[1].textureTarget == GLI_TT_TEXTURE_2D
		&& inits[1].msaaLevel == 0, "its resolve: a texture of its own size, attached to its own frame buffer");
	check(boundTarget == GLI_BT_FRAMEBUFFER && boundName == 80, "its frame buffer bound after");
	config.video.multisampling = 0;
}

static void test_resolve(void)
{
	gli_fb *fb;
	config.video.multisampling = 4;
	nextFbo = 80;
	fb = made();
	gli_fb_init(fb, (struct gli_fb_list *)&listToken, 0x100000, G_IM_FMT_RGBA, G_IM_SIZ_16b, 320, 1);
	config.video.multisampling = 0;
	gli_ctx.multisampling = 1;
	counts();
	gli_fb_resolve_texture(fb, 0);
	check(blitsMade == 1 && blits[0].readBuffer == 80 && blits[0].drawBuffer == 81 && blits[0].srcX0 == 0 && blits[0].srcY0 == 0
		&& blits[0].srcX1 == 640 && blits[0].srcY1 == 480 && blits[0].dstX0 == 0 && blits[0].dstY0 == 0 && blits[0].dstX1 == 640
		&& blits[0].dstY1 == 480 && blits[0].mask == GLI_BM_COLOR_BUFFER && blits[0].filter == GLI_TP_FILTER_NEAREST,
		"resolved: all of it blitted into its resolve");
	check(has("blit bind-fb draw-buffer") && boundTarget == GLI_BT_READ_FRAMEBUFFER && boundName == 0 && fb->m_resolved,
		"the default read, the draw buffer back");
	counts();
	gli_fb_resolve_texture(fb, 0);
	check(blitsMade == 0, "once");
	gli_fb_resolve_texture(fb, 1);
	check(blitsMade == 1, "again when forced");
	gli_ctx.multisampling = 0;
	counts();
	gli_fb_resolve_texture(fb, 1);
	check(blitsMade == 0, "no multisampling: nothing");
}

static void test_textures(void)
{
	gli_fb *fb;
	gli_tex *t;
	gDPTile *tile = &gDP.tiles[0];
	nextFbo = 90;
	fb = made();
	gli_fb_init(fb, (struct gli_fb_list *)&listToken, 0x100000, G_IM_FMT_RGBA, G_IM_SIZ_16b, 320, 1);
	current = &buffers[1];
	memset(tile, 0, sizeof(*tile));
	tile->clamps = tile->clampt = 1;
	tile->imageAddress = 0x100000 + (320 * 3 + 7) * 2;
	counts();
	t = gli_fb_get_texture(fb, 0, current == &buffers[0]);
	check(t == fb->m_pTexture && t->offsetS == 7.0f && t->offsetT == 3.0f && blitsMade == 0, "its own, at the tile's offset");
	check(t->scaleS == 2.0f / 640.0f && t->scaleT == 2.0f / 480.0f && t->shiftScaleS == 1.0f && t->shiftScaleT == 1.0f,
		"its scales, no shift");
	fb->m_loadType = LOADTYPE_TILE;
	fb->m_loadTileOrigin.uls = 10;
	fb->m_loadTileOrigin.ult = 20;
	t = gli_fb_get_texture(fb, 0, current == &buffers[0]);
	check(t->offsetS == 17.0f && t->offsetT == 23.0f, "a load tile's origin added");
	fb->m_loadType = LOADTYPE_BLOCK;
	fb->m_size = G_IM_SIZ_32b;
	fb->m_width = 160;
	tile->imageAddress = 0x100000 + (160 * 3 + 7) * 4;
	t = gli_fb_get_texture(fb, 0, current == &buffers[0]);
	check(t->offsetS == 7.0f && t->offsetT == 3.0f, "in its pixels' size, rows of its width");
	fb->m_size = G_IM_SIZ_16b;
	fb->m_width = 320;
	tile->imageAddress = 0x100000 + (320 * 3 + 7) * 2;
	{
		static const u32 shifts[6] = { 0, 1, 3, 10, 11, 15 };
		static const f32 want[6] = { 1.0f, 0.5f, 0.125f, 1.0f / 1024.0f, 32.0f, 2.0f };
		int i;
		for (i = 0; i < 6; ++i) {
			tile->shifts = shifts[i];
			tile->shiftt = shifts[5 - i];
			t = gli_fb_get_texture(fb, 0, current == &buffers[0]);
			check(t->shiftScaleS == want[i] && t->shiftScaleT == want[5 - i], "its shifts: down to 10, up past it");
		}
		tile->shifts = tile->shiftt = 0;
	}
	gSP.textureTile[1] = &gDP.tiles[1];
	gDP.tiles[1] = *tile;
	gDP.tiles[1].shifts = 2;
	t = gli_fb_get_texture(fb, 1, current == &buffers[0]);
	check(t->shiftScaleS == 0.25f, "tile 1's own");

	/* a depth image drawn as the colour image: the depth buffer's */
	depth.m_pDepthBufferTexture = &depthTexture;
	fb->m_pDepthBuffer = &depth;
	fb->m_isDepthBuffer = 1;
	gDP.colorImage.address = gDP.depthImageAddress = 0x100000;
	depthTexture.realWidth = 320;
	depthTexture.realHeight = 240;
	t = gli_fb_get_texture(fb, 0, current == &buffers[0]);
	check(t == &depthTexture && t->scaleS == 2.0f / 320.0f, "a depth image drawn as the colour image: the depth buffer's");
	tile->clamps = 0;
	counts();
	t = gli_fb_get_texture(fb, 0, current == &buffers[0]);
	check(t == &depthTexture && blitsMade == 0, "whatever the tile's clamp");
	tile->clamps = 1;
	{
		int i;
		for (i = 0; i < 4; ++i) {
			fb->m_isDepthBuffer = i != 0;
			gDP.colorImage.address = i == 1 ? 0x200000 : 0x100000;
			fb->m_pDepthBuffer = i == 2 ? NULL : &depth;
			config.generalEmulation.hacks = i == 3 ? hack_ZeldaMM : 0;
			t = gli_fb_get_texture(fb, 0, current == &buffers[0]);
			check(t == fb->m_pTexture, "not a depth image drawn, no depth buffer or Majora's Mask: its own");
		}
		config.generalEmulation.hacks = 0;
		fb->m_isDepthBuffer = 0;
	}

	/* drawn into: a barrier, or a copy */
	current = &buffers[0];
	gli_ctx.textureBarrier = 1;
	counts();
	t = gli_fb_get_texture(fb, 0, current == &buffers[0]);
	check(t == fb->m_pTexture && has("barrier") && blitsMade == 0, "drawn into with a texture barrier: its own, a barrier");
	gli_ctx.textureBarrier = 0;
	gli_ctx.blitFramebuffer = 1;
	nextFbo = 95;
	counts();
	t = gli_fb_get_texture(fb, 0, current == &buffers[0]);
	check(t == fb->m_pFrameBufferCopyTexture && t == made_[0] && fb->m_copyFBO == 95 && texTargets[0] == GLI_TT_TEXTURE_2D
		&& t->width == 640 && t->height == 480 && targets[0].bufferHandle == 95, "drawn into: a copy, made at its size");
	check(blitsMade == 1 && blits[0].readBuffer == fb->m_FBO && blits[0].drawBuffer == 95 && blits[0].srcX1 == 640
		&& blits[0].srcY1 == 480 && blits[0].dstX1 == 640 && blits[0].dstY1 == 480 && fb->m_copied && has("blit bind-fb draw-buffer"),
		"all of it blitted into it");
	check(t->offsetS == 7.0f && t->scaleS == 2.0f / 640.0f, "at the tile's offset, its scales");
	counts();
	t = gli_fb_get_texture(fb, 0, current == &buffers[0]);
	check(t == fb->m_pFrameBufferCopyTexture && blitsMade == 0 && texturesMade == 0, "once until drawn again");
	fb->m_copied = 0;
	t = gli_fb_get_texture(fb, 0, current == &buffers[0]);
	check(blitsMade == 1 && texturesMade == 0, "drawn again: blitted again, not made");
	fb->m_isDepthBuffer = 1;
	gDP.colorImage.address = 0x100000;
	counts();
	t = gli_fb_get_texture(fb, 0, current == &buffers[0]);
	check(t == &depthCopy && has("depth-copy") && blitsMade == 0, "a depth image drawn into: a copy of the depth");
	fb->m_isDepthBuffer = 0;
	gli_ctx.blitFramebuffer = 0;
	counts();
	t = gli_fb_get_texture(fb, 0, current == &buffers[0]);
	check(t == fb->m_pTexture && blitsMade == 0, "no blits: its own");
	current = &buffers[1];

	/* multisampled copies */
	config.video.multisampling = 4;
	gli_ctx.blitFramebuffer = 1;
	fb = made();
	gli_fb_init(fb, (struct gli_fb_list *)&listToken, 0x100000, G_IM_FMT_RGBA, G_IM_SIZ_16b, 320, 1);
	current = &buffers[0];
	counts();
	t = gli_fb_get_texture(fb, 0, current == &buffers[0]);
	check(texTargets[0] == GLI_TT_TEXTURE_2D_MULTISAMPLE && t->frameBufferTexture == GLI_TEX_FB_MULTI_SAMPLE
		&& targets[0].textureTarget == GLI_TT_TEXTURE_2D_MULTISAMPLE && inits[0].msaaLevel == 4, "multisampled: a multisampled copy");
	config.video.multisampling = 0;
	current = &buffers[1];
}

static void test_sub_textures(void)
{
	gli_fb *fb;
	gli_tex *t;
	gDPTile *tile = &gDP.tiles[1];
	nextFbo = 110;
	fb = made();
	gli_fb_init(fb, (struct gli_fb_list *)&listToken, 0x100000, G_IM_FMT_RGBA, G_IM_SIZ_16b, 320, 1);
	current = &buffers[1];
	gSP.textureTile[1] = tile;
	memset(tile, 0, sizeof(*tile));
	tile->clamps = 0;
	tile->clampt = 1;
	tile->uls = 4;
	tile->lrs = 35;
	tile->ult = 2;
	tile->lrt = 17;
	tile->imageAddress = 0x100000 + (320 * 3 + 7) * 2;
	gli_ctx.blitFramebuffer = 1;
	counts();
	t = gli_fb_get_texture(fb, 1, current == &buffers[0]);
	check(t == fb->m_pSubTexture && fb->m_SubFBO == 111 && texTargets[0] == GLI_TT_TEXTURE_2D && t->clampWidth == 32
		&& t->clampHeight == 16 && t->width == 64 && t->height == 32 && t->clampS == 0 && t->clampT == 1
		&& t->offsetS == 0.0f && t->offsetT == 0.0f, "a wrapping tile: the part it samples, its clamps");
	check(targets[0].bufferHandle == 111 && inits[0].textureUnitIndex == GLI_TU_TEX1 && params[0].textureUnitIndex == GLI_TU_TEX1,
		"attached to its own frame buffer, in the tile's unit");
	check(blitsMade == 1 && blits[0].readBuffer == fb->m_FBO && blits[0].drawBuffer == 111 && blits[0].srcX0 == 14
		&& blits[0].srcY0 == 6 && blits[0].srcX1 == 14 + 64 && blits[0].srcY1 == 6 + 32 && blits[0].dstX1 == 64 && blits[0].dstY1 == 32,
		"blitted from where the tile is, at the scale");
	check(t->scaleS == 2.0f / 64.0f && t->scaleT == 2.0f / 32.0f, "its scales");
	counts();
	t = gli_fb_get_texture(fb, 1, current == &buffers[0]);
	check(texturesMade == 0 && blitsMade == 1 && fb->m_SubFBO == 111, "the same size: kept, blitted again, its frame buffer kept");
	tile->lrs = 40;
	t = gli_fb_get_texture(fb, 1, current == &buffers[0]);
	check(texturesMade == 1 && texturesFreed == 1 && t->clampWidth == 37, "another size: made again");
	tile->lrt = 20;
	tile->clampt = 0;
	counts();
	t = gli_fb_get_texture(fb, 1, current == &buffers[0]);
	check(texturesMade == 1 && t->clampHeight == 19 && t->clampT == 0, "another height: made again, with the tile's clamp");
	tile->lrt = 17;
	tile->clampt = 1;
	t = gli_fb_get_texture(fb, 1, current == &buffers[0]);
	tile->clamps = 1;
	tile->clampt = 0;
	t = gli_fb_get_texture(fb, 1, current == &buffers[0]);
	check(t == fb->m_pSubTexture, "a wrap in t too");
	fb->m_pTexture->size = G_IM_SIZ_32b;
	counts();
	t = gli_fb_get_texture(fb, 1, current == &buffers[0]);
	check(texturesMade == 1 && t->size == G_IM_SIZ_32b, "another size of pixel: made again");
	fb->m_pTexture->size = G_IM_SIZ_16b;
	/* cut to the texture */
	tile->imageAddress = 0x100000 + (320 * 230 + 300) * 2;
	counts();
	t = gli_fb_get_texture(fb, 1, current == &buffers[0]);
	check(blits[0].srcX0 == 600 && blits[0].srcX1 == 640 && blits[0].dstX1 == 40 && blits[0].srcY0 == 460
		&& blits[0].srcY1 == 480 && blits[0].dstY1 == 20, "cut to its texture");
	tile->imageAddress = 0x100000 + (320 * 3 + 7) * 2;
	/* an empty tile, or no blits: its own */
	tile->lrs = 2;
	counts();
	t = gli_fb_get_texture(fb, 1, current == &buffers[0]);
	check(t == fb->m_pTexture && blitsMade == 0, "an empty tile: its own");
	tile->lrs = 40;
	tile->lrt = 1;
	t = gli_fb_get_texture(fb, 1, current == &buffers[0]);
	check(t == fb->m_pTexture, "empty in t: its own");
	tile->lrt = 17;
	gli_ctx.blitFramebuffer = 0;
	t = gli_fb_get_texture(fb, 1, current == &buffers[0]);
	check(t == fb->m_pTexture && blitsMade == 0, "no blits: its own");
	gli_ctx.blitFramebuffer = 1;

	/* weak blits from a multisampled one: from its resolve */
	config.video.multisampling = 4;
	nextFbo = 120;
	fb = made();
	gli_fb_init(fb, (struct gli_fb_list *)&listToken, 0x100000, G_IM_FMT_RGBA, G_IM_SIZ_16b, 320, 1);
	config.video.multisampling = 0;
	gli_ctx.multisampling = 1;
	gli_ctx.weakBlitFramebuffer = 1;
	counts();
	t = gli_fb_get_texture(fb, 1, current == &buffers[0]);
	check(blitsMade == 2 && blits[0].drawBuffer == 121 && blits[1].readBuffer == 121 && blits[1].drawBuffer == fb->m_SubFBO,
		"weak blits: resolved, from the resolve");
	gli_ctx.weakBlitFramebuffer = 0;
	fb->m_resolved = 0;
	counts();
	t = gli_fb_get_texture(fb, 1, current == &buffers[0]);
	check(blitsMade == 1 && blits[0].readBuffer == 120, "else from itself");
	gli_ctx.weakBlitFramebuffer = 1;
	fb->m_pTexture->frameBufferTexture = GLI_TEX_FB_ONE_SAMPLE;
	counts();
	t = gli_fb_get_texture(fb, 1, current == &buffers[0]);
	check(blitsMade == 1 && blits[0].readBuffer == 120, "one sample: from itself");
	gli_ctx.weakBlitFramebuffer = 0;
	gli_ctx.multisampling = 0;
}

static void test_bg(void)
{
	gli_fb *fb;
	gli_tex *t;
	nextFbo = 130;
	fb = made();
	gli_fb_init(fb, (struct gli_fb_list *)&listToken, 0x100000, G_IM_FMT_RGBA, G_IM_SIZ_16b, 320, 1);
	current = &buffers[1];
	gSP.bgImage.imageX = 12.5f;
	gSP.bgImage.imageY = 7.25f;
	fb->m_pTexture->shiftScaleS = 3.0f;
	t = gli_fb_get_texture_bg(fb, 0, current == &buffers[0]);
	check(t == fb->m_pTexture && t->offsetS == 12.5f && t->offsetT == 7.25f && t->scaleS == 2.0f / 640.0f
		&& t->scaleT == 2.0f / 480.0f && t->shiftScaleS == 1.0f && t->shiftScaleT == 1.0f,
		"the background: its own, at its image's offset, no shift");
	current = &buffers[0];
	gli_ctx.textureBarrier = 1;
	counts();
	t = gli_fb_get_texture_bg(fb, 0, current == &buffers[0]);
	check(t == fb->m_pTexture && has("barrier"), "drawn into with a texture barrier: a barrier");
	gli_ctx.textureBarrier = 0;
	gli_ctx.blitFramebuffer = 1;
	counts();
	t = gli_fb_get_texture_bg(fb, 0, current == &buffers[0]);
	check(t == fb->m_pFrameBufferCopyTexture && blitsMade == 1 && t->offsetS == 12.5f, "drawn into: a copy");
	gli_ctx.blitFramebuffer = 0;
	t = gli_fb_get_texture_bg(fb, 0, current == &buffers[0]);
	check(t == fb->m_pTexture, "no blits: its own");
	current = NULL;
	t = gli_fb_get_texture_bg(fb, 0, current == &buffers[0]);
	check(t == fb->m_pTexture, "no current buffer: its own");

	/* a texture attached on its own */
	{
		gli_tex own;
		counts();
		memset(&own, 0, sizeof(own));
		own.name = 140;
		gli_fb_attach_texture(&own, 141, 320, 240, 1.5f, G_IM_FMT_RGBA, G_IM_SIZ_32b);
		check(own.width == 480 && own.height == 360 && own.address == 0 && own.clampWidth == 320 && own.textureBytes == 480 * 360 * 4
			&& targets[0].bufferHandle == 141 && targets[0].textureHandle == 140 && paramsSet == 1 && params[0].minFilter == GLI_TP_FILTER_NEAREST
			&& targets[0].textureTarget == GLI_TT_TEXTURE_2D, "a texture attached on its own: at its scale, nearest, one sample");
	}
}

int main(void)
{
	setup();
	test_make();
	test_init();
	test_resolve();
	test_textures();
	test_sub_textures();
	test_bg();
	if (failures) {
		printf("%d failed\n", failures);
		return 1;
	}
	printf("fb textures: ok\n");
	return 0;
}
