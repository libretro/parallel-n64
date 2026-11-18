/* test_gliden64_color_to_rdram -- GLideN64's copy of a colour frame buffer
 * to RDRAM (BufferCopy/ColorBufferToRDRAM.c) on stub modules.
 *
 * Started, it has a frame buffer; its texture and reader are made at the
 * first copy, at 320, 480 or 640 wide, whichever narrowest holds the
 * buffer, and as tall as the VI's tallest buffer for that, RGBA, linear,
 * attached to its frame buffer, the reader's slots the queue's; made again
 * when another width needs another texture, else kept. A copy finds the
 * buffer at the address: not with the VI's width 0 or no current buffer,
 * nor an off-screen one, one with no pixels or one past RDRAM. Copies of
 * the same buffer in a frame at another 4K reuse what was set up. A buffer
 * at its own scale is read straight from its frame buffer, or its resolve
 * with multisampling; a scaled one is blitted down first, from the screen's
 * width (cut to its adjusted part) or the texture's for a native factor.
 * Zelda's subscreen hack writes white instead. A reader without slots
 * reads at once; with slots, an asynchronous copy is left in flight and
 * written at a later copy, from its own slot, a synchronous one or a chunk
 * waits for what is in flight and for itself. The rows are cut to RDRAM,
 * the VI's tallest buffer and the texture. Each size is written as such,
 * RDRAM cleared under it first when copied back from RDRAM without FBInfo,
 * and the buffer, still there as it was, marked copied and not cleared.
 *
 *   ./test_gliden64_color_to_rdram
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "BufferCopy/ColorBufferToRDRAM.h"
#include "gli_framebuffer.h"
#include "gli_fb_rdram.h"
#include "gli_drawer.h"
#include "gli_rdram_write.h"
#include "gli_textures.h"
#include "gli_tex.h"
#include "Config.h"
#include "GBI.h"
#include "N64.h"
#include "VI.h"
#include "gDP.h"
#include "Graphics/gli_context.h"
#include "Graphics/ColorBufferReader.h"

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
static u8 rdram[0x800000];
u8 *RDRAM = rdram;
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

/* ---- frame buffers and the window ---- */

struct FrameBuffer { gli_fb fb; };
static struct FrameBuffer buffers[2];
static struct FrameBuffer *current = &buffers[0];
static gli_tex fbTexture;
static u32 swapCount, windowWidth = 640;
static int adjustScreen;
static f32 adjustScale = 1.0f;
static int resolved, rdramCopied, fbInfo;

struct FrameBuffer *gli_fb_find(u32 _address)
{
	u32 i;
	for (i = 0; i < 2; ++i)
		if (_address >= buffers[i].fb.m_startAddress && _address < buffers[i].fb.m_endAddress)
			return &buffers[i];
	return NULL;
}
struct gli_fb *gli_fb_of(struct FrameBuffer *_p) { return &_p->fb; }
struct FrameBuffer *gli_fb_current(void) { return current; }
u32 gli_window_swap_count(void) { return swapCount; }
u32 gli_window_width(void) { return windowWidth; }
bool gli_window_is_adjust_screen(void) { return adjustScreen != 0; }
f32 gli_window_adjust_scale(void) { return adjustScale; }
void gli_fb_set_current_draw_buffer(void) { say("draw-buffer"); }
void gli_fb_copy_rdram(struct FrameBuffer *_p) { (void)_p; rdramCopied++; say("copy-rdram"); }
void gli_fb_resolve(struct FrameBuffer *_p) { check(_p == &buffers[0], "the copied buffer resolved"); resolved++; say("resolve"); }
bool gli_FBInfo_supported(void) { return fbInfo != 0; }
u32 cutHeight(u32 _address, u32 _height, u32 _stride)
{
	if (_address > RDRAMSize)
		return 0;
	if (_address + _stride * _height > (RDRAMSize + 1))
		return (RDRAMSize + 1 - _address) / _stride;
	return _height;
}
u16 gli_VI_GetMaxBufferHeight(u16 _width)
{
	if (_width > 320 || VI.interlaced)
		return VI.PAL ? 580 : 480;
	return VI.PAL ? 290 : 240;
}

/* ---- the drawer ---- */

static gli_blit_params blitRect;
static gli_copy_rect copyRect;
static gli_tex *copyTex;
static u32 copyFilter, copyRead, copyDraw;
static int blits, copies, blitFails;
static u32 boundRead, boundDraw;
void gli_drawer_copy_texrect(const gli_copy_rect *_p, struct gli_tex *_tex, u32 _filter)
{
	copyRect = *_p;
	copyTex = _tex;
	copyFilter = _filter;
	copyRead = boundRead;
	copyDraw = boundDraw;
	copies++;
	say("copy");
}

/* ---- the conversions ---- */

static gli_rdram_write lastWrite;
static const void *lastSrc;
static int writes, whites;
static char lastKind[8];
static u32 whiteAddress, whiteSize;
static void wrote(const char *_kind, const void *_src, const gli_rdram_write *_w)
{
	lastSrc = _src;
	lastWrite = *_w;
	strcpy(lastKind, _kind);
	writes++;
	say("write");
}
void gli_rdram_write_rgba32(const u32 *_src, const gli_rdram_write *_w) { wrote("32", _src, _w); }
void gli_rdram_write_rgba16(const u32 *_src, const gli_rdram_write *_w) { wrote("16", _src, _w); }
void gli_rdram_write_r8(const u8 *_src, const gli_rdram_write *_w) { wrote("8", _src, _w); }
void gli_rdram_write_white(u32 _address, u32 _size) { whiteAddress = _address; whiteSize = _size; whites++; say("white"); }

/* ---- textures ---- */

static gli_tex textures[2];
static int texturesMade, texturesFreed;
gli_tex *gli_textures_add_fb(u32 _target)
{
	gli_tex *t = &textures[texturesMade & 1];
	check(_target == GLI_TT_TEXTURE_2D, "a 2D texture");
	memset(t, 0xFF, sizeof(*t));
	t->name = 50 + (u32)texturesMade++;
	return t;
}
void gli_textures_remove_fb(gli_tex *_t) { (void)_t; texturesFreed++; }

/* ---- the reader ---- */

typedef struct stub_reader
{
	gli_cbr base;
	int landed[3];
	u32 slotHeight[3];
	s32 slotY0[3];
	u32 slotSize[3];
} stub_reader;

static stub_reader reader;
static u32 readerSlots;
static int readersMade, readersFreed, cleanUps, readyWaits, slotReads, mapFails;
static u8 pixels[3][16];
static u8 directPixels[16];
static char slotSeq[16];
static s32 readX0, readY0;
static u32 readWidth, readHeight, readSize, heightBack = 0xFFFFFFFFu, lastSlotWritten;
static int readSync;

static void r_clean_up(gli_cbr *_r) { (void)_r; cleanUps++; say("clean-up"); }
static void r_read_to_slot(gli_cbr *_r, u32 _slot, s32 _x0, s32 _y0, u32 _height, u32 _size)
{
	(void)_r;
	(void)_x0;
	reader.landed[_slot] = 0;
	reader.slotHeight[_slot] = _height;
	reader.slotY0[_slot] = _y0;
	reader.slotSize[_slot] = _size;
	slotReads++;
	say("read-slot");
}
static int r_slot_ready(gli_cbr *_r, u32 _slot, int _wait)
{
	(void)_r;
	if (_wait) {
		readyWaits++;
		reader.landed[_slot] = 1;
	}
	return reader.landed[_slot];
}
static const gli_cbr_ops readerOps = { NULL, NULL, r_clean_up, r_read_to_slot, r_slot_ready, NULL };

void gli_cbr_delete(gli_cbr *_r)
{
	if (_r == NULL)
		return;
	check(_r == &reader.base, "its own reader deleted");
	readersFreed++;
}
const u8 *gli_cbr_read_pixels(gli_cbr *_r, s32 _x0, s32 _y0, u32 _width, u32 *_height, u32 _size, int _sync, u32 *_stride)
{
	(void)_r;
	readX0 = _x0;
	readY0 = _y0;
	readWidth = _width;
	readHeight = *_height;
	readSize = _size;
	readSync = _sync;
	if (heightBack != 0xFFFFFFFFu)
		*_height = heightBack;
	*_stride = 330;
	say("read");
	return mapFails ? NULL : directPixels;
}
const u8 *gli_cbr_slot_pixels(gli_cbr *_r, u32 _slot, u32 _width, u32 *_height, u32 _size, u32 *_stride)
{
	(void)_r;
	(void)_width;
	(void)_size;
	check(reader.landed[_slot], "a slot written once landed");
	check(*_height == reader.slotHeight[_slot], "a slot written as it was read");
	if (heightBack != 0xFFFFFFFFu)
		*_height = heightBack;
	*_stride = 340;
	lastSlotWritten = _slot;
	if (strlen(slotSeq) < sizeof(slotSeq) - 1) {
		slotSeq[strlen(slotSeq) + 1] = '\0';
		slotSeq[strlen(slotSeq)] = (char)('0' + _slot);
	}
	say("map-slot");
	return mapFails ? NULL : pixels[_slot];
}

/* ---- the context ---- */

static gli_init_texture_params inits[2];
static int initsMade;
static gli_tex_parameters params;
static int paramsSet;
static gli_fb_render_target target;
static int targetsAdded, fbDeleted;
static u32 boundTarget, boundName, readerTexture;

static u32 c_create_fb(void *_p) { (void)_p; return 80; }
static void c_delete_fb(void *_p, u32 _n) { (void)_p; check(_n == 80, "its frame buffer deleted"); fbDeleted++; }
static void c_init(void *_p, const gli_init_texture_params *_q) { (void)_p; inits[initsMade++ & 1] = *_q; }
static void c_params(void *_p, const gli_tex_parameters *_q) { (void)_p; params = *_q; paramsSet++; }
static void c_target(void *_p, const gli_fb_render_target *_q) { (void)_p; target = *_q; targetsAdded++; }
static bool c_false(const void *_p) { (void)_p; return 0; }
static void c_bind(void *_p, u32 _t, u32 _n)
{
	(void)_p;
	boundTarget = _t;
	boundName = _n;
	if (_t == GLI_BT_READ_FRAMEBUFFER)
		boundRead = _n;
	else
		boundDraw = _n;
	say("bind-fb");
}
static bool c_blit(void *_p, const gli_blit_params *_q) { (void)_p; blitRect = *_q; blits++; say("blit"); return !blitFails; }
static void *c_reader(void *_p, void *_texture)
{
	(void)_p;
	memset(&reader, 0, sizeof(reader));
	reader.base.ops = &readerOps;
	reader.base.texture = (gli_tex *)_texture;
	reader.base.numSlots = readerSlots;
	readerTexture = ((gli_tex *)_texture)->name;
	readersMade++;
	return &reader.base;
}
static gli_context_ops ops;

static void make_buffers(void)
{
	gli_fb *a = &buffers[0].fb;
	memset(buffers, 0, sizeof(buffers));
	VI.width = 640;
	VI.height = 480;
	a->m_startAddress = 0x100000;
	a->m_width = 320;
	a->m_height = 240;
	a->m_size = G_IM_SIZ_16b;
	a->m_endAddress = a->m_startAddress + 320 * 240 * 2;
	a->m_FBO = 11;
	a->m_resolveFBO = 13;
	a->m_scale = 1.0f;
	a->m_pTexture = &fbTexture;
	a->m_cleared = 1;
	fbTexture.realWidth = 1280;
	fbTexture.realHeight = 960;
	current = &buffers[0];
}

static void reset_counts(void)
{
	writes = whites = blits = copies = resolved = rdramCopied = 0;
	cleanUps = readyWaits = slotReads = 0;
	slotSeq[0] = '\0';
	gDP.changed = 0;
	clear_log();
}

static void setup_context(void)
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
	ops.createColorBufferReader = c_reader;
	gli_ctx.ops = &ops;
	gli_ctx.drawFramebuffer = GLI_BT_DRAW_FRAMEBUFFER;
	gli_ctx.readFramebuffer = GLI_BT_READ_FRAMEBUFFER;
	gli_ctx.formats.colorInternalFormat = 0x3001;
	gli_ctx.formats.colorFormat = 0x3002;
	gli_ctx.formats.colorType = 0x3003;
	gli_ctx.formats.colorFormatBytes = 4;
}

static void restart(u32 _slots)
{
	gli_c2r_destroy();
	readerSlots = _slots;
	texturesMade = texturesFreed = readersMade = readersFreed = 0;
	initsMade = paramsSet = targetsAdded = fbDeleted = 0;
	gli_c2r_init();
	swapCount++;
}

static void test_setup(void)
{
	setup_context();
	readerSlots = 0;
	gli_c2r_init();
	gli_c2r_destroy();
	check(fbDeleted == 1 && texturesFreed == 0 && readersFreed == 0, "its frame buffer made and dropped, nothing else");
	gli_c2r_destroy();
	check(fbDeleted == 1, "dropped once");

	restart(2);
	make_buffers();
	reset_counts();
	gli_c2r_copy_to_rdram(0x100000, 1);
	check(texturesMade == 1 && readersMade == 1, "its texture and reader made at the first copy");
	check(textures[0].format == G_IM_FMT_RGBA && textures[0].size == 2 && textures[0].clampS == 1 && textures[0].clampT == 1
		&& textures[0].frameBufferTexture == GLI_TEX_FB_ONE_SAMPLE && !textures[0].maskS && !textures[0].maskT
		&& !textures[0].mirrorS && !textures[0].mirrorT, "an RGBA frame buffer texture, clamped");
	check(textures[0].realWidth == 320 && textures[0].realHeight == 240 && textures[0].width == 320
		&& textures[0].height == 240 && textures[0].textureBytes == 320 * 240 * 4, "320 wide, the VI's tallest, as wide as it is");
	check(initsMade == 1 && inits[0].handle == 50 && inits[0].width == 320 && inits[0].height == 240
		&& inits[0].internalFormat == 0x3001 && inits[0].format == 0x3002 && inits[0].dataType == 0x3003, "in the colour format");
	check(paramsSet == 1 && params.handle == 50 && params.target == GLI_TT_TEXTURE_2D && params.textureUnitIndex == GLI_TU_TEX0
		&& params.minFilter == GLI_TP_FILTER_LINEAR && params.magFilter == GLI_TP_FILTER_LINEAR, "linear");
	check(targetsAdded == 1 && target.bufferHandle == 80 && target.bufferTarget == GLI_BT_DRAW_FRAMEBUFFER
		&& target.attachment == GLI_BA_COLOR_ATTACHMENT0 && target.textureTarget == GLI_TT_TEXTURE_2D
		&& target.textureHandle == 50, "attached to its frame buffer");
	check(readerTexture == 50, "the reader reads it");
	check(strncmp(log_, "bind-fb bind-fb", 15) == 0, "the default frame buffer bound, then the one read");

	/* kept for another copy that fits it, made again for one that does not */
	make_buffers();
	reset_counts();
	gli_c2r_copy_to_rdram(0x100000, 1);
	check(texturesMade == 1 && readersMade == 1, "kept");
	{
		static const u32 widths[5] = { 321, 480, 481, 640, 1000 };
		static const u32 real[5] = { 480, 480, 640, 640, 640 };
		u32 i;
		for (i = 0; i < 5; ++i) {
			make_buffers();
			buffers[0].fb.m_width = widths[i];
			buffers[0].fb.m_height = 200;
			buffers[0].fb.m_endAddress = 0x100000 + widths[i] * 200 * 2;
			swapCount++;
			gli_c2r_copy_to_rdram(0x100000, 1);
			check(textures[(texturesMade - 1) & 1].realWidth == real[i] && textures[(texturesMade - 1) & 1].realHeight == 480,
				"the narrowest of 320, 480 and 640 that holds it, else 640");
		}
		check(texturesMade == 3 && texturesFreed == 2 && readersFreed == 2, "made again only for another width");
	}
	VI.PAL = 1;
	make_buffers();
	swapCount++;
	gli_c2r_copy_to_rdram(0x100000, 1);
	check(texturesMade == 4 && textures[1].realWidth == 320 && textures[1].realHeight == 290, "and for another VI's tallest");
	VI.PAL = 0;

	gli_c2r_destroy();
	check(texturesFreed == 4 && readersFreed == 4 && fbDeleted == 1, "dropped with its texture and reader");
	restart(2);
}

static void test_not_copied(void)
{
	restart(0);
	make_buffers();
	reset_counts();
	VI.width = 0;
	gli_c2r_copy_to_rdram(0x100000, 1);
	make_buffers();
	current = NULL;
	gli_c2r_copy_to_rdram(0x100000, 1);
	make_buffers();
	gli_c2r_copy_to_rdram(0x300000, 1);
	make_buffers();
	buffers[0].fb.m_isOBScreen = 1;
	gli_c2r_copy_to_rdram(0x100000, 1);
	make_buffers();
	buffers[0].fb.m_height = 0;
	gli_c2r_copy_chunk_to_rdram(0x100000);
	make_buffers();
	buffers[0].fb.m_width = 0;
	gli_c2r_copy_to_rdram(0x100000, 1);
	check(writes == 0 && strstr(log_, "read") == NULL && texturesMade == 0, "nothing read or made");

	/* past RDRAM */
	make_buffers();
	buffers[0].fb.m_startAddress = 0x7FF000 + 1;
	buffers[0].fb.m_endAddress = 0x900000;
	gli_c2r_copy_to_rdram(0x800000, 1);
	check(writes == 0 && texturesMade == 0, "nor past RDRAM");

	/* the VI's width alone is enough */
	make_buffers();
	VI.height = 0;
	gli_c2r_copy_to_rdram(0x100000, 1);
	check(writes == 1, "the VI's height 0: copied");
}

static void test_read_buffer(void)
{
	restart(0);
	make_buffers();
	reset_counts();
	gli_c2r_copy_to_rdram(0x100000, 1);
	check(boundTarget == GLI_BT_READ_FRAMEBUFFER && boundName == 11 && blits == 0, "at its own scale: read from its frame buffer");

	config.video.multisampling = 4;
	make_buffers();
	swapCount++;
	reset_counts();
	gli_c2r_copy_to_rdram(0x100000, 1);
	check(resolved == 1 && boundTarget == GLI_BT_READ_FRAMEBUFFER && boundName == 13, "multisampled: from its resolve");

	/* scaled: blitted down from the screen's width */
	make_buffers();
	buffers[0].fb.m_scale = 2.0f;
	swapCount++;
	reset_counts();
	gli_c2r_copy_to_rdram(0x100000, 1);
	check(blits == 1 && copies == 0 && blitRect.readBuffer == 13 && blitRect.drawBuffer == 80 && blitRect.mask == GLI_BM_COLOR_BUFFER
		&& blitRect.filter == GLI_TP_FILTER_NEAREST, "scaled: blitted into its frame buffer");
	check(blitRect.srcX0 == 0 && blitRect.srcY0 == 0 && blitRect.srcX1 == 640 && blitRect.srcY1 == 480
		&& blitRect.dstX0 == 0 && blitRect.dstY0 == 0 && blitRect.dstX1 == 320 && blitRect.dstY1 == 240,
		"the screen's width and the rows at its scale, to the buffer's");
	check(boundTarget == GLI_BT_READ_FRAMEBUFFER && boundName == 80, "then read from its own frame buffer");

	/* where the context cannot blit: drawn */
	blitFails = 1;
	make_buffers();
	buffers[0].fb.m_scale = 2.0f;
	swapCount++;
	reset_counts();
	gli_c2r_copy_to_rdram(0x100000, 1);
	check(blits == 1 && copies == 1 && copyTex == &fbTexture && copyFilter == GLI_TP_FILTER_NEAREST
		&& copyRead == 13 && copyDraw == 80, "no blit: its texture drawn into the frame buffer");
	check(copyRect.srcX0 == 0 && copyRect.srcY0 == 0 && copyRect.srcX1 == 640 && copyRect.srcY1 == 480
		&& copyRect.srcWidth == 1280 && copyRect.srcHeight == 960 && copyRect.dstX0 == 0 && copyRect.dstY0 == 0
		&& copyRect.dstX1 == 320 && copyRect.dstY1 == 240 && copyRect.dstWidth == 320 && copyRect.dstHeight == 240
		&& !copyRect.invertX && !copyRect.invertY, "the same rect, from the whole texture to its whole one");
	check(boundTarget == GLI_BT_READ_FRAMEBUFFER && boundName == 80, "then read from its own frame buffer");
	blitFails = 0;
	config.video.multisampling = 0;

	adjustScreen = 1;
	adjustScale = 0.75f;
	make_buffers();
	buffers[0].fb.m_scale = 2.0f;
	swapCount++;
	gli_c2r_copy_to_rdram(0x100000, 1);
	check(blitRect.srcX0 == 80 && blitRect.srcX1 == 560 && blitRect.readBuffer == 11, "the screen adjusted: its middle part");
	adjustScreen = 0;
	adjustScale = 1.0f;

	config.frameBufferEmulation.nativeResFactor = 2;
	make_buffers();
	buffers[0].fb.m_scale = 2.0f;
	fbTexture.realWidth = 1000;
	swapCount++;
	gli_c2r_copy_to_rdram(0x100000, 1);
	check(blitRect.srcX0 == 0 && blitRect.srcX1 == 1000, "a native factor: the buffer texture's width");
	config.frameBufferEmulation.nativeResFactor = 0;

	/* Zelda's subscreen: white */
	config.generalEmulation.hacks = hack_subscreen;
	make_buffers();
	buffers[0].fb.m_width = 640;
	buffers[0].fb.m_endAddress = 0x100000 + 640 * 240 * 2;
	swapCount++;
	reset_counts();
	gli_c2r_copy_to_rdram(0x100000, 1);
	check(whites == 1 && whiteAddress == 0x100000 && whiteSize == G_IM_SIZ_16b && writes == 0
		&& buffers[0].fb.m_copiedToRdram && rdramCopied == 1 && !buffers[0].fb.m_cleared, "the subscreen hack: white");
	make_buffers();
	swapCount++;
	reset_counts();
	gli_c2r_copy_to_rdram(0x100000, 1);
	check(whites == 0 && writes == 1, "not as wide as the VI: copied");
	config.generalEmulation.hacks = 0;
	make_buffers();
	buffers[0].fb.m_width = 640;
	buffers[0].fb.m_endAddress = 0x100000 + 640 * 240 * 2;
	swapCount++;
	reset_counts();
	gli_c2r_copy_to_rdram(0x100000, 1);
	check(whites == 0 && writes == 1, "without the hack: copied");

	/* scaled, its rows cut to RDRAM */
	make_buffers();
	buffers[0].fb.m_scale = 2.0f;
	buffers[0].fb.m_startAddress = 0x7FF000;
	buffers[0].fb.m_endAddress = 0x7FF000 + 320 * 240 * 2;
	fbTexture.realWidth = 1280;
	swapCount++;
	gli_c2r_copy_to_rdram(0x7FF000, 1);
	check(blitRect.srcY1 == 2 * (0x1000 / 640) && blitRect.dstY1 == 0x1000 / 640, "scaled: the rows RDRAM holds");
}

static void test_direct(void)
{
	restart(0);
	make_buffers();
	reset_counts();
	gli_c2r_copy_to_rdram(0x100000, 0);
	check(readX0 == 0 && readY0 == 0 && readWidth == 320 && readHeight == 240 && readSize == G_IM_SIZ_16b && !readSync,
		"no slots: all of it read at once");
	check(writes == 1 && lastSrc == directPixels && !strcmp(lastKind, "16") && lastWrite.srcStride == 330
		&& lastWrite.width == 320 && lastWrite.height == 240 && lastWrite.numPixels == 320 * 240
		&& lastWrite.startAddress == 0x100000 && lastWrite.bufferAddress == 0x100000 && lastWrite.size == G_IM_SIZ_16b,
		"written as read, at the reader's stride");
	check(cleanUps == 1 && (gDP.changed & CHANGED_SCISSOR), "cleaned up, the scissor to set");
	check(strstr(log_, "read draw-buffer write") != NULL, "the draw buffer back before the write");
	check(buffers[0].fb.m_copiedToRdram && rdramCopied == 1 && !buffers[0].fb.m_cleared, "the buffer marked copied");

	/* all of it, from anywhere in it */
	make_buffers();
	swapCount++;
	reset_counts();
	gli_c2r_copy_to_rdram(0x101234, 1);
	check(readY0 == 0 && lastWrite.startAddress == 0x100000 && lastWrite.numPixels == 320 * 240, "all of it from anywhere in it");

	/* the rows the reader gives */
	make_buffers();
	swapCount++;
	reset_counts();
	heightBack = 100;
	gli_c2r_copy_to_rdram(0x100000, 1);
	check(readSync && lastWrite.height == 100, "synchronous, as many rows as it gave");
	heightBack = 0;
	make_buffers();
	swapCount++;
	reset_counts();
	gli_c2r_copy_to_rdram(0x100000, 1);
	check(writes == 0 && cleanUps == 1 && !rdramCopied, "no rows: nothing written");
	heightBack = 0xFFFFFFFFu;

	mapFails = 1;
	make_buffers();
	swapCount++;
	reset_counts();
	gli_c2r_copy_to_rdram(0x100000, 1);
	check(writes == 0 && cleanUps == 0 && !(gDP.changed & CHANGED_SCISSOR) && strstr(log_, "draw-buffer") != NULL,
		"nothing read: nothing written, the draw buffer back");
	mapFails = 0;

	/* sizes, and RDRAM cleared under them first */
	{
		static const u32 sizes[3] = { G_IM_SIZ_8b, G_IM_SIZ_16b, G_IM_SIZ_32b };
		static const char *const kinds[3] = { "8", "16", "32" };
		static const u32 bytes[3] = { 1, 2, 4 };
		u32 i;
		config.frameBufferEmulation.copyFromRDRAM = 1;
		for (i = 0; i < 3; ++i) {
			make_buffers();
			buffers[0].fb.m_size = sizes[i];
			buffers[0].fb.m_endAddress = 0x100000 + 320 * 240 * bytes[i];
			memset(rdram + 0x100000 - 4, 0x5a, 320 * 240 * 4 + 8);
			swapCount++;
			reset_counts();
			gli_c2r_copy_to_rdram(0x100000, 1);
			check(writes == 1 && !strcmp(lastKind, kinds[i]) && lastWrite.numPixels == 320 * 240 && lastWrite.size == sizes[i],
				"each size written as such");
			check(rdram[0x100000] == 0 && rdram[0x100000 + 320 * 240 * bytes[i] - 1] == 0
				&& rdram[0x100000 + 320 * 240 * bytes[i]] == 0x5a && rdram[0x100000 - 1] == 0x5a, "cleared under it");
		}
		fbInfo = 1;
		make_buffers();
		memset(rdram + 0x100000, 0x5a, 16);
		swapCount++;
		gli_c2r_copy_to_rdram(0x100000, 1);
		check(rdram[0x100000] == 0x5a, "with FBInfo: not cleared");
		fbInfo = 0;
		config.frameBufferEmulation.copyFromRDRAM = 0;
		make_buffers();
		swapCount++;
		gli_c2r_copy_to_rdram(0x100000, 1);
		check(rdram[0x100000] == 0x5a, "not copied back from RDRAM: not cleared");
	}

	/* the rows: the VI's tallest */
	make_buffers();
	buffers[0].fb.m_height = 300;
	buffers[0].fb.m_endAddress = 0x100000 + 320 * 300 * 2;
	swapCount++;
	gli_c2r_copy_to_rdram(0x100000, 1);
	check(readHeight == 240 && lastWrite.numPixels == 320 * 240, "cut to the VI's tallest");
	/* and RDRAM */
	make_buffers();
	buffers[0].fb.m_startAddress = 0x7FF000;
	buffers[0].fb.m_endAddress = 0x7FF000 + 320 * 240 * 2;
	swapCount++;
	gli_c2r_copy_to_rdram(0x7FF000, 1);
	check(readHeight == 0x1000 / 640 && lastWrite.numPixels == 320 * (0x1000 / 640) && lastWrite.startAddress == 0x7FF000,
		"cut to RDRAM");
}

static void test_chunks(void)
{
	restart(0);
	make_buffers();
	swapCount++;
	reset_counts();
	gli_c2r_copy_chunk_to_rdram(0x101234);
	check(readY0 == 0x1000 / 640 && readHeight == 1 + 0x2000 / 640 - 0x1000 / 640 && readSync, "a chunk: its 4K's rows, waited for");
	check(lastWrite.startAddress == 0x101000 && lastWrite.numPixels == 0x800 && lastWrite.bufferAddress == 0x100000, "its 4K");
	check(strstr(log_, "bind-fb") != NULL, "set up");
	/* another 4K of it in the frame: what was set up reused */
	clear_log();
	gli_c2r_copy_chunk_to_rdram(0x102010);
	check(strstr(log_, "bind-fb") == NULL && lastWrite.startAddress == 0x102000, "another 4K: not set up again");
	/* the same 4K again: set up again */
	clear_log();
	gli_c2r_copy_chunk_to_rdram(0x101000);
	check(strstr(log_, "bind-fb") != NULL, "the same 4K: set up again");
	/* a new frame: set up again */
	swapCount++;
	clear_log();
	gli_c2r_copy_chunk_to_rdram(0x103000);
	check(strstr(log_, "bind-fb") != NULL, "a new frame: set up again");
	/* from the buffer's start */
	make_buffers();
	buffers[0].fb.m_startAddress = 0x100800;
	buffers[0].fb.m_endAddress = 0x100800 + 320 * 240 * 2;
	swapCount++;
	gli_c2r_copy_chunk_to_rdram(0x100900);
	check(lastWrite.startAddress == 0x100800 && lastWrite.numPixels == 0x400 && readY0 == 0, "from the buffer's start");
}

static void test_slots(void)
{
	restart(2);
	make_buffers();
	swapCount++;
	reset_counts();
	gli_c2r_copy_to_rdram(0x100000, 0);
	check(slotReads == 1 && writes == 0 && readyWaits == 0 && reader.slotHeight[0] == 240 && reader.slotY0[0] == 0
		&& reader.slotSize[0] == G_IM_SIZ_16b, "asynchronous: read into a slot, left in flight");
	check(strstr(log_, "read-slot draw-buffer") != NULL && (gDP.changed & CHANGED_SCISSOR), "the draw buffer back, the scissor to set");

	/* the next one, the first not landed: the other slot, nothing waited for */
	make_buffers();
	swapCount++;
	reset_counts();
	gli_c2r_copy_to_rdram(0x100000, 0);
	check(slotReads == 1 && writes == 0 && readyWaits == 0, "a free slot read, nothing waited for");
	/* both in flight, the newer landed: the oldest waited for and written
	 * before its slot is read into again, the newer left to come after it */
	reader.landed[1] = 1;
	make_buffers();
	swapCount++;
	reset_counts();
	gli_c2r_copy_to_rdram(0x100000, 0);
	check(readyWaits == 1 && writes == 1 && !strcmp(slotSeq, "0") && slotReads == 1, "both in flight: the oldest waited for");
	check(lastSrc == pixels[0] && lastWrite.srcStride == 340 && lastWrite.height == 240 && lastWrite.startAddress == 0x100000,
		"written from its own slot, as read");
	check(cleanUps == 1, "cleaned up after");
	/* the landed one next, without a wait */
	make_buffers();
	swapCount++;
	reset_counts();
	gli_c2r_copy_to_rdram(0x100000, 0);
	check(readyWaits == 0 && writes == 1 && !strcmp(slotSeq, "1") && lastSrc == pixels[1], "the landed one written, not waited for");

	/* a synchronous one: all in flight, oldest first, then its own */
	make_buffers();
	swapCount++;
	reset_counts();
	gli_c2r_copy_to_rdram(0x100000, 1);
	check(writes == 3 && readyWaits == 3 && cleanUps == 3 && !strcmp(slotSeq, "010"),
		"synchronous: what is in flight, oldest first, and its own, waited for");
	reset_counts();
	gli_c2r_copy_chunk_to_rdram(0x101000);
	check(writes == 1 && readyWaits == 1 && lastWrite.startAddress == 0x101000 && reader.slotY0[lastSlotWritten] == 0x1000 / 640,
		"a chunk: its own at once, from its row");

	/* a slot's rows cut to the texture */
	make_buffers();
	buffers[0].fb.m_height = 239;
	buffers[0].fb.m_endAddress = 0x100000 + 320 * 239 * 2;
	swapCount++;
	gli_c2r_copy_to_rdram(0x100000, 1);
	check(reader.slotHeight[lastSlotWritten] == 239, "a slot read as tall as the buffer");
	textures[(texturesMade - 1) & 1].realHeight = 100;
	make_buffers();
	swapCount++;
	gli_c2r_copy_to_rdram(0x100000, 1);
	check(texturesMade == 2, "a texture of another height made again");
	textures[(texturesMade - 1) & 1].realHeight = 100;
	make_buffers();
	swapCount++;
	gli_c2r_copy_chunk_to_rdram(0x100000);
	check(texturesMade == 3, "and again");

	/* nothing mapped: nothing written */
	make_buffers();
	swapCount++;
	gli_c2r_copy_to_rdram(0x100000, 0);
	mapFails = 1;
	make_buffers();
	swapCount++;
	reset_counts();
	gli_c2r_copy_to_rdram(0x100000, 1);
	check(writes == 0 && cleanUps == 2, "nothing mapped: nothing written, cleaned up");
	mapFails = 0;

	/* a buffer gone or changed since its read is not marked */
	make_buffers();
	swapCount++;
	gli_c2r_copy_to_rdram(0x100000, 0);
	make_buffers();
	buffers[0].fb.m_width = 300;
	buffers[0].fb.m_endAddress = 0x100000 + 300 * 240 * 2;
	reset_counts();
	gli_c2r_copy_chunk_to_rdram(0x100000);
	check(writes == 2 && rdramCopied == 1, "a buffer changed since: not marked for the older read");
	make_buffers();
	swapCount++;
	gli_c2r_copy_to_rdram(0x100000, 0);
	buffers[0].fb.m_startAddress = 0x0FF000;
	reset_counts();
	gli_c2r_copy_chunk_to_rdram(0x100000);
	check(rdramCopied == 1, "a buffer moved since: not marked");
	make_buffers();
	swapCount++;
	gli_c2r_copy_to_rdram(0x100000, 0);
	buffers[0].fb.m_size = G_IM_SIZ_32b;
	reset_counts();
	gli_c2r_copy_to_rdram(0x100000, 1);
	check(rdramCopied == 1, "of another size since: not marked");
	gli_c2r_destroy();
}

int main(void)
{
	test_setup();
	test_not_copied();
	test_read_buffer();
	test_direct();
	test_chunks();
	test_slots();
	if (failures) {
		printf("%d failed\n", failures);
		return 1;
	}
	printf("color to rdram: ok\n");
	return 0;
}
