/* test_gliden64_s2dex_fb -- what GLideN64's S2DEX microcode does with the
 * frame buffers, the depth buffers and the drawer.
 *
 * It was C++ glue (S2DEX_gfx.cpp) over the frame buffer list; it is C now
 * (uCodes/S2DEX_gfx.c), on the frame buffers' gli_fb and gli_ctx's table.
 * A background read from a frame buffer samples that buffer on tile 0; a
 * frame buffer image is used only of the image's size, not a depth buffer
 * unless changed, not 8-bit CI, and not a CPU buffer or one RDRAM no longer
 * holds (those are dropped). Background drawing flushes the texrects as it
 * ends. Copying the depth buffer blits it, through a borrowed buffer, into
 * a depth buffer at the color buffer's address, on the read target (on
 * GLES2, the one target), and puts back what it borrowed.
 *
 *   ./test_gliden64_s2dex_fb
 */
#include <stdio.h>
#include <string.h>

#include "gli_s2dex.h"
#include "gli_fb_rdram.h"
#include "gli_framebuffer.h"
#include "DepthBuffer.h"
#include "Graphics/gli_context.h"
#include "Config.h"
#include "DisplayWindow.h"
#include "GBI.h"
#include "gDP.h"
#include "gSP.h"

static int failures;

static void check(int _ok, const char *_what)
{
	if (!_ok) {
		printf("FAIL: %s\n", _what);
		failures++;
	}
}

/* ---- the world ---- */

Config gliden64Config;
gDPInfo gDP;
gSPInfo gSP;
gli_window g_window;
gli_context gli_ctx;

static gli_fb current, other, tmp;
static gli_fb *found;
static DepthBuffer copyDepth, tmpDepth;
static int valid, flushed, bgMode;
static u32 removedAt, depthSaves[4], numSaves;
static struct FrameBuffer *copyBuffer;
static u32 attachFbo[4], attachTarget[4];
static DepthBuffer *attachDepth[4];
static u32 numAttach;

struct FrameBuffer *gli_fb_current(void) { return (struct FrameBuffer *)&current; }
struct FrameBuffer *gli_fb_find(u32 _a) { (void)_a; return (struct FrameBuffer *)found; }
struct FrameBuffer *gli_fb_find_tmp(u32 _a) { return _a == current.m_startAddress ? (struct FrameBuffer *)&tmp : NULL; }
struct gli_fb *gli_fb_of(struct FrameBuffer *_p) { return (struct gli_fb *)_p; }
int gli_fb_is_valid(gli_fb *_fb, int _force, u32 _swap) { (void)_fb; (void)_swap; return !_force && valid; }
void gli_fb_remove(u32 _a) { removedAt = _a; }
void gli_fb_set_copy_buffer(struct FrameBuffer *_p) { copyBuffer = _p; }
bool gli_texrect_draw(void) { flushed++; return 0; }
void gli_drawer_set_background_mode(bool _on) { bgMode = _on; }
void gli_depth_save(u32 _a) { if (numSaves < 4) depthSaves[numSaves] = _a; numSaves++; }
struct DepthBuffer *gli_depth_find(u32 _a) { return _a == 0x300000 ? &copyDepth : NULL; }
void gli_depth_set_attachment(DepthBuffer *_d, u32 _fbo, u32 _target)
{
	if (numAttach < 4) {
		attachDepth[numAttach] = _d;
		attachFbo[numAttach] = _fbo;
		attachTarget[numAttach] = _target;
	}
	numAttach++;
}

/* ---- the backend ---- */

static gli_context_ops ops;
static gli_blit_params lastBlit;
static u32 blits, lastBindTarget = 0xdead, lastBindName = 0xdead;

static bool blit(void *_i, const gli_blit_params *_p) { (void)_i; lastBlit = *_p; blits++; return 1; }
static void bind(void *_i, u32 _t, u32 _n) { (void)_i; lastBindTarget = _t; lastBindName = _n; }

static void reset(void)
{
	memset(&current, 0, sizeof(current));
	memset(&other, 0, sizeof(other));
	memset(&tmp, 0, sizeof(tmp));
	memset(&gDP, 0, sizeof(gDP));
	memset(&gSP, 0, sizeof(gSP));
	memset(&config, 0, sizeof(config));
	current.m_startAddress = 0x100000;
	current.m_FBO = 31;
	other.m_startAddress = 0x200000;
	other.m_size = G_IM_SIZ_16b;
	tmp.m_FBO = 32;
	tmp.m_pDepthBuffer = &tmpDepth;
	found = &other;
	valid = 1;
	flushed = bgMode = 0;
	removedAt = 0;
	numSaves = numAttach = blits = 0;
	copyBuffer = NULL;
	memset(&ops, 0, sizeof(ops));
	ops.blitFramebuffers = blit;
	ops.bindFramebuffer = bind;
	gli_ctx.ops = &ops;
	gli_ctx.blitFramebuffer = 1;
	gli_ctx.drawFramebuffer = GLI_BT_DRAW_FRAMEBUFFER;
	gli_ctx.readFramebuffer = GLI_BT_READ_FRAMEBUFFER;
	g_window.width = 640;
	g_window.height = 480;
	config.frameBufferEmulation.enable = 1;
}

static void test_images(void)
{
	reset();
	gli_s2dex_mark_current_ob_screen();
	check(current.m_isOBScreen == 1, "the current buffer is marked an object screen");

	gSP.bgImage.address = 0x200000;
	gli_s2dex_load_fb_image();
	check(gDP.tiles[0].frameBufferAddress == 0x200000 && gDP.tiles[0].textureMode == TEXTUREMODE_FRAMEBUFFER_BG &&
		gDP.tiles[0].loadType == LOADTYPE_TILE && (gDP.changed & CHANGED_TMEM) != 0,
		"a background from a frame buffer samples it on tile 0");
	check(copyBuffer == NULL, "no copy buffer without the Zelda MM hack");
	config.generalEmulation.hacks = hack_ZeldaMM;
	gDP.colorImage.address = gDP.depthImageAddress = 0x400000;
	gli_s2dex_load_fb_image();
	check(copyBuffer == (struct FrameBuffer *)&current, "with it, drawing into the depth buffer copies the current one");

	reset();
	check(gli_s2dex_use_fb_image(0x200000, G_IM_SIZ_16b, G_IM_FMT_RGBA) == 1, "a frame buffer of the image's size is used");
	check(gli_s2dex_use_fb_image(0x200000, G_IM_SIZ_32b, G_IM_FMT_RGBA) == 0, "not of another size");
	other.m_size = G_IM_SIZ_8b;
	check(gli_s2dex_use_fb_image(0x200000, G_IM_SIZ_8b, G_IM_FMT_CI) == 0, "not for 8-bit CI");
	other.m_size = G_IM_SIZ_16b;
	other.m_isDepthBuffer = 1;
	check(gli_s2dex_use_fb_image(0x200000, G_IM_SIZ_16b, G_IM_FMT_RGBA) == 0, "not a depth buffer");
	other.m_changed = 1;
	check(gli_s2dex_use_fb_image(0x200000, G_IM_SIZ_16b, G_IM_FMT_RGBA) == 1, "unless changed");
	other.m_isDepthBuffer = 0;
	check(removedAt == 0, "nothing dropped so far");
	other.m_cfb = 1;
	check(gli_s2dex_use_fb_image(0x200000, G_IM_SIZ_16b, G_IM_FMT_RGBA) == 0 && removedAt == 0x200000,
		"a CPU buffer is dropped, not used");
	other.m_cfb = 0;
	removedAt = 0;
	valid = 0;
	check(gli_s2dex_use_fb_image(0x200000, G_IM_SIZ_16b, G_IM_FMT_RGBA) == 0 && removedAt == 0x200000,
		"one RDRAM no longer holds is dropped");
	valid = 1;
	found = NULL;
	check(gli_s2dex_use_fb_image(0x200000, G_IM_SIZ_16b, G_IM_FMT_RGBA) == 0, "no buffer, no image");
	found = &other;
	config.frameBufferEmulation.enable = 0;
	check(gli_s2dex_use_fb_image(0x200000, G_IM_SIZ_16b, G_IM_FMT_RGBA) == 0, "no frame buffer emulation, no image");
}

static void test_background(void)
{
	reset();
	gli_s2dex_background_drawing(1);
	check(bgMode == 1 && flushed == 0, "background drawing starts without a flush");
	gli_s2dex_background_drawing(0);
	check(bgMode == 0 && flushed == 1, "and ends flushing the texrects");
}

static void test_copy_depth(u32 _readTarget)
{
	reset();
	gli_ctx.readFramebuffer = _readTarget;
	gSP.bgImage.address = 0x300000;
	gDP.colorImage.address = 0x100000;
	gDP.depthImageAddress = 0x500000;
	gli_s2dex_copy_depth_buffer();
	check(numSaves == 2 && depthSaves[0] == 0x100000 && depthSaves[1] == 0x500000,
		"a depth buffer at the color buffer's address, then the depth buffer back");
	check(numAttach == 2 && attachDepth[0] == &copyDepth && attachFbo[0] == 32 && attachTarget[0] == _readTarget,
		"the source attached to the borrowed buffer, on the read target");
	check(blits == 1 && lastBlit.readBuffer == 32 && lastBlit.drawBuffer == 31 && lastBlit.mask == GLI_BM_DEPTH_BUFFER &&
		lastBlit.filter == GLI_TP_FILTER_NEAREST && lastBlit.srcX1 == 640 && lastBlit.srcY1 == 480 &&
		lastBlit.dstX1 == 640 && lastBlit.dstY1 == 480 && lastBlit.srcX0 == 0 && lastBlit.dstY0 == 0,
		"blitted into the current buffer, depth alone, the window's size");
	check(attachDepth[1] == &tmpDepth && attachFbo[1] == 31 && attachTarget[1] == _readTarget,
		"the borrowed buffer's depth put back");
	check(lastBindTarget == _readTarget && lastBindName == 0, "the read target let go");

	reset();
	gli_ctx.blitFramebuffer = 0;
	gli_s2dex_copy_depth_buffer();
	check(numSaves == 0 && blits == 0, "without blits, nothing");
}

int main(void)
{
	test_images();
	test_background();
	test_copy_depth(GLI_BT_READ_FRAMEBUFFER);
	test_copy_depth(GLI_BT_FRAMEBUFFER);
	if (failures) {
		printf("%d failed\n", failures);
		return 1;
	}
	printf("s2dex frame buffers: ok\n");
	return 0;
}
