/* test_gliden64_depth_to_rdram -- GLideN64's copy of a frame buffer's
 * depth to RDRAM (BufferCopy/DepthBufferToRDRAM.c) on stub modules.
 *
 * Started, it has a read-back buffer for each copy the colour copy's
 * setting leaves in flight, one to three, as many as the context gives,
 * none and nothing else on GLES 2; its colour and depth textures, the
 * monochrome and depth formats, nearest, and its frame buffer with both.
 * A copy finds the frame buffer at the address, its depth buffer cleared
 * and as wide, and the buffer that depth buffer is, that one copied; not
 * an auxiliary buffer, nor one past RDRAM, nor with the VI's size 0. It
 * blits the depth into its frame buffer, from the resolve with
 * multisampling, once a frame for chunks; reads it back, all of it in
 * the synchronous mode, written at once, or else left in flight and
 * written once landed at a later copy; a chunk is 4K, written at once.
 * The write maps what was read and converts it, and fails the copy when
 * it cannot be mapped. A copy marks both buffers not cleared and the
 * scissor to set again. Software rendered depth copies nothing.
 *
 *   ./test_gliden64_depth_to_rdram
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "BufferCopy/DepthBufferToRDRAM.h"
#include "gli_framebuffer.h"
#include "gli_fb_rdram.h"
#include "gli_rdram_write.h"
#include "gli_textures.h"
#include "gli_tex.h"
#include "DepthBuffer.h"
#include "Config.h"
#include "GBI.h"
#include "N64.h"
#include "VI.h"
#include "gDP.h"
#include "Graphics/gli_context.h"
#include "Graphics/PixelBuffer.h"

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
VIInfo VI;
u32 RDRAMSize = 0x7FFFFF;
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

/* ---- frame and depth buffers ---- */

struct FrameBuffer { gli_fb fb; };
static struct FrameBuffer buffers[2];
static DepthBuffer depth;
static gli_tex fbTexture;
static u32 swapCount;
static int resolved;

struct FrameBuffer *gli_fb_find(u32 _address)
{
	u32 i;
	for (i = 0; i < 2; ++i)
		if (buffers[i].fb.m_width != 0 && _address >= buffers[i].fb.m_startAddress && _address < buffers[i].fb.m_endAddress)
			return &buffers[i];
	return NULL;
}
struct gli_fb *gli_fb_of(struct FrameBuffer *_p) { return &_p->fb; }
int gli_fb_is_auxiliary(const gli_fb *_fb) { return _fb->m_width != VI.width; }
u32 gli_window_swap_count(void) { return swapCount; }
void gli_fb_set_current_draw_buffer(void) { say("draw-buffer"); }
struct gli_tex *gli_depth_resolve_texture(DepthBuffer *_d, const struct gli_fb *_b)
{
	check(_d == &depth && _b == &buffers[1].fb, "the copied buffer's depth resolved");
	resolved++;
	say("resolve");
	return NULL;
}
u32 cutHeight(u32 _address, u32 _height, u32 _stride)
{
	if (_address > RDRAMSize)
		return 0;
	if (_address + _stride * _height > (RDRAMSize + 1))
		return (RDRAMSize + 1 - _address) / _stride;
	return _height;
}

/* ---- the conversion ---- */

static gli_rdram_write lastWrite;
static const f32 *lastSrc;
static u32 writes;
void gli_rdram_write_depth(const f32 *_src, const gli_rdram_write *_w)
{
	lastSrc = _src;
	lastWrite = *_w;
	writes++;
	say("write");
}

/* ---- textures ---- */

static gli_tex textures[2];
static int texturesMade, texturesFreed;
gli_tex *gli_textures_add_fb(u32 _target)
{
	gli_tex *t = &textures[texturesMade & 1];
	check(_target == GLI_TT_TEXTURE_2D, "2D textures");
	memset(t, 0xFF, sizeof(*t));
	t->name = 50 + (u32)texturesMade++;
	return t;
}
void gli_textures_remove_fb(gli_tex *_t) { (void)_t; texturesFreed++; }

/* ---- read-back buffers ---- */

typedef struct stub_prb
{
	gli_prb base;
	int id;
	int landed;
	int mapFails;
	f32 pixels[16];
	u32 mapRange;
	s32 x, y;
	u32 w, h, format, type;
} stub_prb;

static stub_prb prbs[4];
static int prbsMade, prbsFreed, prbLimit = 9, readyWaits;
static void p_read(gli_prb *_b, s32 _x, s32 _y, u32 _w, u32 _h, u32 _f, u32 _t)
{
	stub_prb *p = (stub_prb *)_b;
	p->x = _x; p->y = _y; p->w = _w; p->h = _h; p->format = _f; p->type = _t;
	p->landed = 0;
	say("read");
}
static bool p_ready(gli_prb *_b, bool _wait)
{
	stub_prb *p = (stub_prb *)_b;
	if (_wait) {
		readyWaits++;
		p->landed = 1;
	}
	return p->landed != 0;
}
static void *p_map(gli_prb *_b, u32 _o, u32 _r)
{
	stub_prb *p = (stub_prb *)_b;
	check(_o == 0, "mapped from the start");
	p->mapRange = _r;
	say("map");
	return p->mapFails ? NULL : p->pixels;
}
static void p_unmap(gli_prb *_b) { (void)_b; say("unmap"); }
static void p_bind(gli_prb *_b) { (void)_b; say("bind"); }
static void p_unbind(gli_prb *_b) { (void)_b; say("unbind"); }
static void p_destroy(gli_prb *_b) { (void)_b; prbsFreed++; }
static const gli_prb_ops prbOps = { p_read, p_ready, p_map, p_unmap, p_bind, p_unbind, p_destroy };

/* ---- the context ---- */

static gli_init_texture_params inits[2];
static int initsMade;
static gli_tex_parameters params[2];
static int paramsSet;
static gli_fb_render_target targets[2];
static int targetsAdded;
static gli_blit_params blit;
static int blits, fbDeleted;
static u32 boundTarget, boundName;

static struct gli_prb *c_prb(void *_p, size_t _bytes)
{
	stub_prb *p;
	(void)_p;
	check(_bytes == 640 * 580 * sizeof(float), "a read-back buffer for 640x580 floats");
	if (prbsMade >= prbLimit)
		return NULL;
	p = &prbs[prbsMade];
	memset(p, 0, sizeof(*p));
	p->base.ops = &prbOps;
	p->id = prbsMade++;
	return &p->base;
}
static void c_init(void *_p, const gli_init_texture_params *_q) { (void)_p; inits[initsMade++ & 1] = *_q; }
static void c_params(void *_p, const gli_tex_parameters *_q) { (void)_p; params[paramsSet++ & 1] = *_q; }
static u32 c_create_fb(void *_p) { (void)_p; return 70; }
static void c_target(void *_p, const gli_fb_render_target *_q) { (void)_p; targets[targetsAdded++ & 1] = *_q; }
static bool c_false(const void *_p) { (void)_p; return 0; }
static void c_bind(void *_p, u32 _t, u32 _n) { (void)_p; boundTarget = _t; boundName = _n; say("bind-fb"); }
static void c_delete_fb(void *_p, u32 _n) { (void)_p; check(_n == 70, "its frame buffer deleted"); fbDeleted++; }
static bool c_blit(void *_p, const gli_blit_params *_q) { (void)_p; blit = *_q; blits++; say("blit"); return 1; }
static gli_context_ops ops;

static void start(u32 _setting)
{
	config.frameBufferEmulation.copyToRDRAM = _setting;
	prbsMade = prbsFreed = 0;
	texturesMade = texturesFreed = 0;
	initsMade = paramsSet = targetsAdded = 0;
	gli_d2r_init();
}

static void make_buffers(void)
{
	gli_fb *a = &buffers[0].fb, *b = &buffers[1].fb;
	memset(buffers, 0, sizeof(buffers));
	memset(&depth, 0, sizeof(depth));
	VI.width = 320;
	VI.height = 240;
	/* the colour buffer drawn, its depth buffer at another buffer's address */
	a->m_startAddress = 0x100000;
	a->m_width = 320;
	a->m_height = 240;
	a->m_size = G_IM_SIZ_16b;
	a->m_endAddress = a->m_startAddress + 320 * 240 * 2;
	a->m_FBO = 11;
	a->m_pDepthBuffer = &depth;
	a->m_pTexture = &fbTexture; /* every frame buffer has one */
	a->m_scale = 2.0f;
	b->m_startAddress = 0x200000;
	b->m_width = 320;
	b->m_height = 240;
	b->m_size = G_IM_SIZ_16b;
	b->m_endAddress = b->m_startAddress + 320 * 240 * 2;
	b->m_FBO = 12;
	b->m_resolveFBO = 13;
	b->m_pDepthBuffer = &depth;
	b->m_scale = 2.0f;
	b->m_pTexture = &fbTexture;
	b->m_cleared = 1;
	fbTexture.realWidth = 640;
	depth.m_address = 0x200000;
	depth.m_width = 320;
	depth.m_cleared = 1;
}

static void test_init(void)
{
	u32 s;
	static const u32 slots[6] = { 1, 1, 2, 3, 3, 3 };
	memset(&ops, 0, sizeof(ops));
	ops.createPixelReadBuffer = c_prb;
	ops.init2DTexture = c_init;
	ops.setTextureParameters = c_params;
	ops.createFramebuffer = c_create_fb;
	ops.addFrameBufferRenderTarget = c_target;
	ops.isError = c_false;
	ops.isFramebufferError = c_false;
	ops.bindFramebuffer = c_bind;
	ops.deleteFramebuffer = c_delete_fb;
	ops.blitFramebuffers = c_blit;
	gli_ctx.ops = &ops;
	gli_ctx.drawFramebuffer = GLI_BT_DRAW_FRAMEBUFFER;
	gli_ctx.readFramebuffer = GLI_BT_READ_FRAMEBUFFER;
	gli_ctx.formats.monochromeInternalFormat = 0x1001;
	gli_ctx.formats.monochromeFormat = 0x1002;
	gli_ctx.formats.monochromeType = 0x1003;
	gli_ctx.formats.depthInternalFormat = 0x2001;
	gli_ctx.formats.depthFormat = 0x2002;
	gli_ctx.formats.depthType = 0x2003;
	gli_ctx.formats.depthFormatBytes = 4;

	for (s = 0; s < 6; ++s) {
		start(s);
		check(prbsMade == (int)slots[s], "a read-back buffer for each copy in flight");
		gli_d2r_destroy();
		check(prbsFreed == (int)slots[s] && texturesFreed == 2 && fbDeleted > 0, "dropped with its textures and frame buffer");
	}

	/* the buffers the context gives */
	prbLimit = 1;
	start(3);
	check(prbsMade == 1 && texturesMade == 2, "as many as the context gives");
	gli_d2r_destroy();
	prbLimit = 0;
	start(3);
	check(prbsMade == 0 && texturesMade == 0, "none: nothing else made");
	make_buffers();
	check(!gli_d2r_copy_to_rdram(0x100000) && !gli_d2r_copy_chunk_to_rdram(0x100000) && blits == 0, "and nothing copied");
	fbDeleted = 0;
	gli_d2r_destroy();
	check(fbDeleted == 0, "nor dropped");
	prbLimit = 9;

	start(2);
	check(textures[0].format == G_IM_FMT_I && textures[0].size == 2 && textures[0].clampS == 1 && textures[0].clampT == 1
		&& !textures[0].maskS && !textures[0].maskT && !textures[0].mirrorS && !textures[0].mirrorT
		&& textures[0].frameBufferTexture == GLI_TEX_FB_ONE_SAMPLE && textures[0].width == 640 && textures[0].height == 580
		&& textures[0].textureBytes == 640 * 580, "the colour texture, a byte a texel");
	check(textures[1].format == G_IM_FMT_I && textures[1].textureBytes == 640 * 580 * 4 && textures[1].width == 640
		&& textures[1].frameBufferTexture == GLI_TEX_FB_ONE_SAMPLE, "the depth texture, a float a texel");
	check(initsMade == 2 && inits[0].handle == 50 && inits[0].internalFormat == 0x1001 && inits[0].format == 0x1002
		&& inits[0].dataType == 0x1003 && inits[0].width == 640 && inits[0].height == 580 && inits[0].textureUnitIndex == GLI_TU_TEX0,
		"the colour texture monochrome");
	check(inits[1].handle == 51 && inits[1].internalFormat == 0x2001 && inits[1].format == 0x2002 && inits[1].dataType == 0x2003,
		"the depth texture in the depth format");
	check(paramsSet == 2 && params[0].handle == 50 && params[1].handle == 51 && params[1].minFilter == GLI_TP_FILTER_NEAREST
		&& params[1].magFilter == GLI_TP_FILTER_NEAREST && params[1].target == GLI_TT_TEXTURE_2D, "both nearest");
	check(targetsAdded == 2 && targets[0].bufferHandle == 70 && targets[0].bufferTarget == GLI_BT_DRAW_FRAMEBUFFER
		&& targets[0].attachment == GLI_BA_COLOR_ATTACHMENT0 && targets[0].textureHandle == 50
		&& targets[1].attachment == GLI_BA_DEPTH_ATTACHMENT && targets[1].textureHandle == 51
		&& targets[1].textureTarget == GLI_TT_TEXTURE_2D, "its frame buffer with both");
	check(boundTarget == GLI_BT_DRAW_FRAMEBUFFER && boundName == 0, "the default frame buffer bound after");
}

static void test_copies(void)
{
	/* async: two buffers */
	make_buffers();
	swapCount = 5;
	clear_log();
	blits = 0;
	writes = 0;
	check(gli_d2r_copy_to_rdram(0x100000), "a full copy");
	check(strcmp(log_, "blit draw-buffer bind-fb bind read unbind ") == 0, "blitted, then read, left in flight");
	check(writes == 0 && readyWaits == 0, "not written yet");
	check(blit.readBuffer == 11 && blit.drawBuffer == 70 && blit.srcX0 == 0 && blit.srcY0 == 0 && blit.srcX1 == 640
		&& blit.srcY1 == 480 && blit.dstX0 == 0 && blit.dstY0 == 0 && blit.dstX1 == 320 && blit.dstY1 == 240
		&& blit.mask == GLI_BM_DEPTH_BUFFER && blit.filter == GLI_TP_FILTER_NEAREST, "the depth blitted at its scale");
	check(boundTarget == GLI_BT_READ_FRAMEBUFFER && boundName == 70, "read from its frame buffer");
	check(prbs[0].x == 0 && prbs[0].y == 0 && prbs[0].w == 320 && prbs[0].h == 240 && prbs[0].format == 0x2002
		&& prbs[0].type == 0x2003, "all of it, in the depth format");
	check(!depth.m_cleared && !buffers[1].fb.m_cleared && (gDP.changed & CHANGED_SCISSOR), "not cleared, the scissor to set");

	/* the next copy writes it once landed, into the next buffer */
	make_buffers();
	prbs[0].landed = 1;
	clear_log();
	check(gli_d2r_copy_to_rdram(0x100000), "the next copy");
	check(strcmp(log_, "blit draw-buffer bind map write unmap unbind bind-fb bind read unbind ") == 0,
		"the landed read written before the next");
	check(writes == 1 && lastSrc == prbs[0].pixels && prbs[0].mapRange == 320 * 240 * 4, "converted where it was mapped");
	check(lastWrite.width == 320 && lastWrite.height == 240 && lastWrite.srcStride == 320 && lastWrite.numPixels == 320 * 240
		&& lastWrite.startAddress == 0x200000 && lastWrite.bufferAddress == 0x200000 && lastWrite.size == G_IM_SIZ_16b,
		"written as what it was read for");
	check(prbs[1].h == 240, "into the next buffer");

	/* a read not landed is left, not waited for, while a buffer is free */
	make_buffers();
	writes = 0;
	readyWaits = 0;
	check(gli_d2r_copy_to_rdram(0x100000), "a copy with one in flight");
	check(writes == 0 && readyWaits == 0 && prbs[0].h == 240 && !prbs[0].landed, "the free buffer read, nothing waited for");
	/* the older one landed: written from its own buffer, the other left */
	make_buffers();
	prbs[1].landed = 1;
	check(gli_d2r_copy_to_rdram(0x100000), "a copy with both in flight");
	check(writes == 1 && readyWaits == 0 && lastSrc == prbs[1].pixels, "the landed one written from its buffer");

	/* a chunk: what is in flight, waited for and written, then its own */
	make_buffers();
	clear_log();
	writes = 0;
	readyWaits = 0;
	check(gli_d2r_copy_chunk_to_rdram(0x201234), "a chunk");
	check(writes == 3 && readyWaits == 3, "the reads in flight and its own, waited for");
	check(strstr(log_, "blit") == NULL, "no blit again in the same frame");
	/* blitted already this frame: the start is kept as given, to the 4K's end */
	check(lastWrite.startAddress == 0x201234 && lastWrite.numPixels == (0x2000 - 0x1234) / 2 && prbs[0].y == 0x1234 / 640
		&& prbs[0].h == 1u + 0x2000 / 640 - 0x1234 / 640 && lastWrite.bufferAddress == 0x200000, "to its 4K's end, from its row");
	swapCount = 6;
	make_buffers();
	clear_log();
	check(gli_d2r_copy_chunk_to_rdram(0x201234) && strstr(log_, "blit") != NULL, "a new frame blits again");

	/* the chunk start bounded to the depth image */
	make_buffers();
	depth.m_address = 0x201800;
	buffers[1].fb.m_startAddress = 0x201800;
	buffers[1].fb.m_endAddress = 0x201800 + 320 * 240 * 2;
	swapCount = 7;
	gli_d2r_copy_chunk_to_rdram(0x201900);
	check(lastWrite.startAddress == 0x201800 && lastWrite.numPixels == 0x400, "the chunk from the image's start");
	gli_d2r_destroy();
}

static void test_sync(void)
{
	start(1);
	make_buffers();
	writes = 0;
	readyWaits = 0;
	clear_log();
	check(gli_d2r_copy_to_rdram(0x100000), "synchronous copy");
	check(writes == 1 && readyWaits == 1, "written at once");
	prbs[0].mapFails = 1;
	make_buffers();
	check(!gli_d2r_copy_to_rdram(0x100000), "nothing mapped: not copied");
	check(depth.m_cleared, "nor marked");
	prbs[0].mapFails = 0;

	/* multisampled: from the resolve */
	config.video.multisampling = 4;
	make_buffers();
	resolved = 0;
	gli_d2r_copy_to_rdram(0x100000);
	check(resolved == 1 && blit.readBuffer == 13, "multisampled: from the resolve");
	config.video.multisampling = 0;

	/* what is not copied */
	make_buffers();
	blits = 0;
	VI.width = 0;
	VI.height = 0;
	check(!gli_d2r_copy_to_rdram(0x100000), "the VI's size 0");
	make_buffers();
	VI.height = 0;
	check(gli_d2r_copy_to_rdram(0x100000), "only its height 0: copied");
	blits = 0;
	make_buffers();
	check(!gli_d2r_copy_to_rdram(0x300000), "no buffer there");
	make_buffers();
	buffers[0].fb.m_width = 300;
	check(!gli_d2r_copy_to_rdram(0x100000), "an auxiliary buffer");
	make_buffers();
	buffers[0].fb.m_pDepthBuffer = NULL;
	check(!gli_d2r_copy_to_rdram(0x100000), "no depth buffer");
	make_buffers();
	depth.m_cleared = 0;
	check(!gli_d2r_copy_to_rdram(0x100000), "a depth buffer not cleared");
	make_buffers();
	depth.m_width = 200;
	check(!gli_d2r_copy_to_rdram(0x100000), "a depth buffer of another width");
	make_buffers();
	depth.m_address = RDRAMSize - 100;
	check(!gli_d2r_copy_to_rdram(0x100000), "a depth image past RDRAM");
	make_buffers();
	depth.m_address = RDRAMSize + 1 - 640 * 10;
	check(!gli_d2r_copy_to_rdram(0x100000), "a depth image partly past RDRAM");
	make_buffers();
	depth.m_address = 0x380000;
	buffers[0].fb.m_height = 0;
	check(!gli_d2r_copy_to_rdram(0x100000), "no rows");
	check(blits == 0, "none blitted");

	/* the depth buffer at no buffer: the drawn one copied */
	make_buffers();
	depth.m_address = 0x380000;
	buffers[0].fb.m_cleared = 1;
	check(gli_d2r_copy_to_rdram(0x100000) && blit.readBuffer == 11, "its depth at no buffer: from the drawn one");

	/* software rendered depth: nothing */
	config.frameBufferEmulation.copyDepthToRDRAM = cdSoftwareRender;
	blits = 0;
	make_buffers();
	check(gli_d2r_copy_to_rdram(0x100000) && gli_d2r_copy_chunk_to_rdram(0x100000) && blits == 0, "software depth: nothing to do");
	config.frameBufferEmulation.copyDepthToRDRAM = 0;
	gli_d2r_destroy();
}

int main(void)
{
	test_init();
	test_copies();
	test_sync();
	if (failures) {
		printf("%d failed\n", failures);
		return 1;
	}
	printf("depth to rdram: ok\n");
	return 0;
}
