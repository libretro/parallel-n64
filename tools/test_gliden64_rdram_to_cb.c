/* test_gliden64_rdram_to_cb -- GLideN64's copy of what the CPU wrote to a
 * frame buffer in RDRAM onto that buffer's texture
 * (BufferCopy/RDRAMtoColorBuffer.c), on stub modules.
 *
 * Started, its upload texture is a one-sample frame buffer texture of
 * 640x580 in the colour format, linear, and its buffer holds four bytes a
 * pixel or the format's own if more. A write the CPU made is marked when
 * whole pixels of the buffer it finds; a copy goes to the buffer at the
 * address, or the one marks were made in, a colour frame buffer always and
 * others only with copyFromRDRAM on and no FBInfo; not to 8-bit buffers,
 * nor one whose start the RDP has drawn since; Resident Evil 2's hack
 * leaves RDP buffers alone and bounds the height. The pixels are taken
 * whole or by the marks, put in a float or 565 texture's format, and, when
 * any is not zero, drawn as a copy-mode textured rect into the buffer with
 * blending, no depth test and no scissor, through tile 0 of nothing, then
 * the drawer's buffer is made current; RDRAM is cleared after it with no
 * FBInfo, no copy to RDRAM, and the buffer changed. However it ends, its
 * buffer and marks are forgotten and the current buffer put back. Past the
 * upload texture nothing is copied.
 *
 *   ./test_gliden64_rdram_to_cb
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "BufferCopy/RDRAMtoColorBuffer.h"
#include "gli_framebuffer.h"
#include "gli_fb_rdram.h"
#include "gli_rdram_to_color.h"
#include "gli_textures.h"
#include "gli_tex.h"
#include "gli_combiner.h"
#include "gli_drawer.h"
#include "Config.h"
#include "GBI.h"
#include "N64.h"
#include "RSP.h"
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

/* ---- the globals ---- */

Config gliden64Config;
gDPInfo gDP;
gSPInfo gSP;
VIInfo VI;
RSPInfo RSP;
static u8 rdram[0x100000];
u8 *RDRAM = rdram;
u32 RDRAMSize = sizeof(rdram) - 1;
gli_context gli_ctx;

static char log_[2048];
static void say(const char *_s)
{
	if (strlen(log_) + strlen(_s) + 2 < sizeof(log_)) {
		strcat(log_, _s);
		strcat(log_, " ");
	}
}

/* ---- frame buffers ---- */

struct FrameBuffer { gli_fb fb; };
static struct FrameBuffer buffers[2];
static struct FrameBuffer *current, *drawn;
static int fbInfo, journaled;

struct FrameBuffer *gli_fb_find(u32 _address)
{
	u32 i;
	for (i = 0; i < 2; ++i)
		if (buffers[i].fb.m_width != 0 && _address >= buffers[i].fb.m_startAddress && _address < buffers[i].fb.m_endAddress)
			return &buffers[i];
	return NULL;
}
struct FrameBuffer *gli_fb_current(void) { return current; }
void gli_fb_set_current(struct FrameBuffer *_p) { current = _p; say("set-current"); }
struct gli_fb *gli_fb_of(struct FrameBuffer *_p) { return &_p->fb; }
bool gli_FBInfo_supported(void) { return fbInfo != 0; }
void gli_fb_set_current_draw_buffer(void) { say("draw-buffer"); }
u32 cutHeight(u32 _address, u32 _height, u32 _stride)
{
	if (_address > RDRAMSize)
		return 0;
	if (_address + _stride * _height > (RDRAMSize + 1))
		return (RDRAMSize + 1 - _address) / _stride;
	return _height;
}
static u16 maxBufferHeight = 240;
u16 gli_VI_GetMaxBufferHeight(u16 _width) { (void)_width; return maxBufferHeight; }
void gli_RSP_StereoJournal(u32 _address, u32 _size) { (void)_address; (void)_size; journaled++; }

/* ---- textures ---- */

static gli_tex tex;
static int texRemoved, activated;
gli_tex *gli_textures_add_fb(u32 _target) { check(_target == GLI_TT_TEXTURE_2D, "a 2D texture"); memset(&tex, 0xFF, sizeof(tex)); tex.name = 31; return &tex; }
void gli_textures_remove_fb(gli_tex *_t) { check(_t == &tex, "its texture dropped"); texRemoved++; }
void gli_textures_activate(u32 _t, gli_tex *_tex) { check(_t == 0 && _tex == &tex, "the upload texture on tile 0"); activated++; say("activate"); }

/* ---- the pixels ---- */

static int copyImage, copyMarked, toFloat, to565, nonZero = 1;
static u32 lastAddress, lastSize, lastW, lastH, lastCount;
static int lastFull;
static void *lastDst;
static gli_r2c_marks *seenMarks;
void gli_r2c_marks_clear(gli_r2c_marks *_m) { _m->count = 0; say("marks-clear"); }
void gli_r2c_mark(gli_r2c_marks *_m, u32 _address) { seenMarks = _m; _m->count++; lastAddress = _address; }
int gli_r2c_copy_image(u32 _a, u32 _s, u32 *_d, u32 _w, u32 _h, int _f)
{
	/* ABGR8888 into the upload buffer, as the real one writes it */
	memset(_d, 0x11, (size_t)_w * _h * 4);
	copyImage++; lastAddress = _a; lastSize = _s; lastDst = _d; lastW = _w; lastH = _h; lastFull = _f;
	return nonZero;
}
int gli_r2c_copy_marked(const gli_r2c_marks *_m, u32 _a, u32 _s, u32 *_d, u32 _w, u32 _h, int _f)
{
	(void)_m;
	copyMarked++; lastAddress = _a; lastSize = _s; lastDst = _d; lastW = _w; lastH = _h; lastFull = _f;
	return nonZero;
}
void gli_r2c_to_float(void *_b, u32 _c) { (void)_b; lastCount = _c; toFloat++; }
void gli_r2c_to_565(void *_b, u32 _c) { (void)_b; lastCount = _c; to565++; }

/* ---- the combiner and drawer ---- */

static int rectMode = -1, cycleAtDraw = -1;
void gli_combiner_set_rect_mode(bool _rect) { rectMode = _rect; }
void gli_combiner_update(void) { say("combiner"); }
void gli_combiner_update_parameters(void) { say("parameters"); }
static gli_texrect_params rect;
static const struct FrameBuffer *rectBuffer;
static gDPTile *tileAtDraw;
static gDPTile tileSeen;
static int blendAtDraw, depthAtDraw, scissorAtDraw;
void gli_drawer_draw_textured_rect(const gli_texrect_params *_p, const struct FrameBuffer *_b)
{
	rect = *_p;
	rectBuffer = _b;
	tileAtDraw = gSP.textureTile[0];
	if (tileAtDraw != NULL)
		tileSeen = *tileAtDraw;
	cycleAtDraw = (int)gDP.otherMode.cycleType;
	say("rect");
}

/* ---- the context ---- */

static gli_init_texture_params initParams;
static gli_tex_parameters texParams;
static gli_update_texture_params updateParams;
static u32 enabled[4];
static u32 blendS, blendD, boundTarget, boundName;
static void c_init(void *_p, const gli_init_texture_params *_q) { (void)_p; initParams = *_q; }
static void c_params(void *_p, const gli_tex_parameters *_q) { (void)_p; texParams = *_q; }
static void c_update(void *_p, const gli_update_texture_params *_q) { (void)_p; updateParams = *_q; say("upload"); }
static void c_enable(void *_p, u32 _c, bool _e)
{
	(void)_p;
	if (_c == GLI_EN_BLEND) { enabled[0] = _e; blendAtDraw = _e; }
	else if (_c == GLI_EN_DEPTH_TEST) { enabled[1] = _e; depthAtDraw = _e; }
	else if (_c == GLI_EN_SCISSOR_TEST) { enabled[2] = _e; say(_e ? "scissor-on" : "scissor-off"); }
	else check(0, "an enable it has");
}
static void c_blend(void *_p, u32 _s, u32 _d) { (void)_p; blendS = _s; blendD = _d; }
static void c_bind(void *_p, u32 _t, u32 _n) { (void)_p; boundTarget = _t; boundName = _n; say("bind"); }
static gli_context_ops ops;

static void reset(void)
{
	log_[0] = '\0';
	copyImage = copyMarked = toFloat = to565 = activated = journaled = 0;
	rectBuffer = NULL;
}

static void make_buffer(int _i, u32 _address, u32 _size, u32 _width, u32 _height)
{
	gli_fb *b = &buffers[_i].fb;
	memset(b, 0, sizeof(*b));
	b->m_startAddress = _address;
	b->m_size = _size;
	b->m_width = _width;
	b->m_height = _height;
	b->m_endAddress = _address + ((_width * _height) << _size >> 1);
	b->m_FBO = 40 + _i;
	b->m_cfb = 1;
}

/* the stack the copy's tile sits in, dirtied first */
static void dirty_stack(void)
{
	volatile u8 junk[4096];
	u32 i;
	for (i = 0; i < sizeof(junk); ++i)
		junk[i] = 0xA5;
}

/* a copy that finds nothing: a buffer kept with no marks let go */
static void let_go(void)
{
	gli_r2cb_copy_from_rdram(0, 0);
}

static void test_init(void)
{
	memset(&ops, 0, sizeof(ops));
	ops.init2DTexture = c_init;
	ops.setTextureParameters = c_params;
	ops.update2DTexture = c_update;
	ops.enable = c_enable;
	ops.setBlending = c_blend;
	ops.bindFramebuffer = c_bind;
	gli_ctx.ops = &ops;
	gli_ctx.drawFramebuffer = GLI_BT_DRAW_FRAMEBUFFER;
	gli_ctx.formats.colorFormatBytes = 4;
	gli_ctx.formats.colorInternalFormat = GLI_ICF_RGBA8;
	gli_ctx.formats.colorFormat = GLI_CF_RGBA;
	gli_ctx.formats.colorType = GLI_DT_UNSIGNED_BYTE;

	gli_r2cb_init();
	check(tex.format == G_IM_FMT_RGBA && tex.size == 2 && tex.clampS && tex.clampT && !tex.maskS && !tex.maskT
		&& !tex.mirrorS && !tex.mirrorT && tex.frameBufferTexture == GLI_TEX_FB_ONE_SAMPLE, "an RGBA one-sample frame buffer texture");
	check(tex.realWidth == 640 && tex.realHeight == 580 && tex.textureBytes == 640 * 580 * 4, "640x580 of the colour format");
	check(initParams.handle == 31 && initParams.width == 640 && initParams.height == 580 && initParams.internalFormat == GLI_ICF_RGBA8
		&& initParams.format == GLI_CF_RGBA && initParams.dataType == GLI_DT_UNSIGNED_BYTE && initParams.target == GLI_TT_TEXTURE_2D
		&& initParams.mipMapLevels == 1 && initParams.data == NULL, "its storage");
	check(texParams.handle == 31 && texParams.target == GLI_TT_TEXTURE_2D && texParams.textureUnitIndex == GLI_TU_TEX0
		&& texParams.minFilter == GLI_TP_FILTER_LINEAR && texParams.magFilter == GLI_TP_FILTER_LINEAR
		&& texParams.wrapS == GLI_PARAM_INVALID && texParams.maxMipmapLevel == GLI_PARAM_INVALID, "linear, the rest left");
}

static void test_marks(void)
{
	make_buffer(0, 0x1000, G_IM_SIZ_16b, 64, 32);
	buffers[1].fb.m_width = 0;
	gDP.colorImage.changed = 0;
	seenMarks = NULL;
	gli_r2cb_add_address(0x9000, 2);
	check(seenMarks == NULL, "no buffer there: nothing marked");
	gli_r2cb_add_address(0x1001, 1);
	check(seenMarks == NULL && gDP.colorImage.changed == 0, "a byte inside a pixel: not marked");
	gli_r2cb_add_address(0x1002, 2);
	check(seenMarks != NULL && lastAddress == 0x1002 && gDP.colorImage.changed == TRUE, "a whole pixel marked, the image changed");
	gli_r2cb_add_address(0x1004, 4);
	check(lastAddress == 0x1004, "a word on a pixel");
	gli_r2cb_add_address(0x1007, 2);
	check(lastAddress == 0x1007, "a pixel's size written anywhere: marked");
	gli_r2cb_add_address(0x1009, 4);
	check(lastAddress == 0x1007, "another size off a pixel: not marked");
	gli_r2cb_add_address(0x1006, 1);
	check(lastAddress == 0x1006, "a byte on a pixel's start");
	check(seenMarks->count == 4, "four marks");
}

static void test_copy(void)
{
	/* the marked buffer, copied by its marks */
	current = &buffers[1];
	maxBufferHeight = 32;
	gDP.colorImage.changed = 0;
	gDP.otherMode.cycleType = G_CYC_1CYCLE;
	config.frameBufferEmulation.copyToRDRAM = 0;
	VI.real_height = 20;
	reset();
	dirty_stack();
	gli_r2cb_copy_from_rdram(0x1100, 0);
	check(copyMarked == 1 && copyImage == 0 && lastAddress == 0x1000 && lastSize == G_IM_SIZ_16b && lastW == 64,
		"the marks of the buffer they were made in");
	check(lastH == 32 && !lastFull, "the most the VI shows past the start, with alpha");
	check(strcmp(log_, "combiner upload activate parameters bind scissor-off rect scissor-on draw-buffer marks-clear set-current ") == 0,
		"uploaded and drawn in order, then forgotten and put back");
	check(rectMode == 1 && cycleAtDraw == G_CYC_COPY && gDP.otherMode.cycleType == G_CYC_1CYCLE, "a copy-mode rect, the cycle put back");
	check(updateParams.handle == 31 && updateParams.textureUnitIndex == GLI_TU_TEX0 && updateParams.width == 64
		&& updateParams.height == 32 && updateParams.format == GLI_CF_RGBA && updateParams.dataType == GLI_DT_UNSIGNED_BYTE
		&& updateParams.data == lastDst && updateParams.x == 0 && updateParams.y == 0, "the pixels uploaded where they were converted");
	check(tex.width == 64 && tex.height == 32 && tex.scaleS == 1.0f / 640 && tex.scaleT == 1.0f / 580 && tex.shiftScaleS == 1.0f
		&& tex.shiftScaleT == 1.0f && tex.offsetS == 0.0f && tex.offsetT == 0.0f, "the texture's scale");
	check(blendAtDraw == 1 && blendS == GLI_BLEND_SRC_ALPHA && blendD == GLI_BLEND_ONE_MINUS_SRC_ALPHA && depthAtDraw == 0,
		"blended, no depth test");
	check(boundTarget == GLI_BT_DRAW_FRAMEBUFFER && boundName == 40, "into the buffer's frame buffer");
	check(rectBuffer == &buffers[0] && rect.ulx == 0 && rect.uly == 0 && rect.lrx == 64 && rect.lry == 32 && rect.dsdx == 1.0f
		&& rect.dtdy == 1.0f && rect.s == 0 && rect.t == 0 && !rect.flip && rect.forceAjustScale && !rect.texrectCmd,
		"a rect over the whole buffer, scaled to it");
	{
		static const gDPTile zero;
		check(tileAtDraw != NULL && memcmp(&tileSeen, &zero, sizeof(zero)) == 0, "tile 0 of nothing");
	}
	check(gSP.textureTile[0] == NULL, "the tile put back");
	check(current == &buffers[1], "the current buffer put back");
	check((gDP.changed & (CHANGED_RENDERMODE | CHANGED_COMBINE)) == (CHANGED_RENDERMODE | CHANGED_COMBINE), "render mode and combine to set again");

	/* no marks and no buffer kept: only a colour frame buffer, or with
	 * copyFromRDRAM and no FBInfo */
	reset();
	gli_r2cb_copy_from_rdram(0x1100, 0);
	check(copyImage == 0 && copyMarked == 0, "nothing marked, no colour frame buffer: no copy");
	config.frameBufferEmulation.copyFromRDRAM = 1;
	gli_r2cb_copy_from_rdram(0x1100, 0);
	check(copyImage == 1 && !lastFull, "copyFromRDRAM: the whole image");
	fbInfo = 1;
	gli_r2cb_copy_from_rdram(0x1100, 0);
	check(copyImage == 1, "not with FBInfo");
	gli_r2cb_copy_from_rdram(0x1100, 1);
	check(copyImage == 2 && lastFull, "a colour frame buffer always, opaque");
	fbInfo = 0;
	config.frameBufferEmulation.copyFromRDRAM = 0;

	/* the start the RDP drew to: not copied */
	gDP.colorImage.changed = 1;
	reset();
	gli_r2cb_copy_from_rdram(0x1000, 1);
	check(copyImage == 0, "the image's start, changed since: no copy");
	gDP.colorImage.changed = 0;
	gli_r2cb_copy_from_rdram(0x1000, 1);
	check(copyImage == 0, "the buffer that skip kept, with no marks, let go on the next");
	gli_r2cb_copy_from_rdram(0x1000, 1);
	check(copyImage == 1 && lastH == 20, "at its start, the VI's height");

	/* a buffer kept with no marks is forgotten */
	gli_r2cb_add_address(0x1002, 2);
	gDP.colorImage.changed = 0;
	seenMarks->count = 0;
	gli_r2cb_copy_from_rdram(0x1000, 1);
	check(copyImage == 1, "a buffer kept with no marks: forgotten, no copy");
	gli_r2cb_copy_from_rdram(0x1000, 1);
	check(copyImage == 2, "and found again");

	/* 8-bit buffers are not copied */
	make_buffer(1, 0x8000, G_IM_SIZ_8b, 64, 32);
	gli_r2cb_copy_from_rdram(0x8000, 1);
	check(copyImage == 2, "8-bit: no copy");
	let_go();

	/* nothing but zero: not drawn, but forgotten all the same */
	nonZero = 0;
	current = &buffers[1];
	reset();
	gli_r2cb_copy_from_rdram(0x1000, 1);
	check(copyImage == 1 && strcmp(log_, "marks-clear set-current ") == 0, "all zero: not drawn, still forgotten");
	nonZero = 1;

	/* past the upload texture: nothing */
	make_buffer(0, 0x1000, G_IM_SIZ_16b, 700, 10);
	reset();
	gli_r2cb_copy_from_rdram(0x1000, 1);
	check(copyImage == 0 && strcmp(log_, "marks-clear set-current ") == 0, "wider than the upload texture: nothing, forgotten");
	make_buffer(0, 0x1000, G_IM_SIZ_16b, 64, 32);

	/* RDRAM cleared under it */
	memset(rdram + 0x1000, 0xAA, 64 * 32 * 2);
	buffers[0].fb.m_changed = 1;
	reset();
	gli_r2cb_copy_from_rdram(0x1100, 1);
	check(rdram[0x1000] == 0xAA, "opaque: RDRAM kept");
	gli_r2cb_add_address(0x1002, 2);
	gli_r2cb_copy_from_rdram(0x1100, 0);
	check(rdram[0x1000] == 0 && rdram[0x1000 + 64 * 32 * 2 - 1] == 0 && rdram[0x1000 + 64 * 32 * 2] != 0xAA - 1, "with alpha, changed: RDRAM cleared");
	memset(rdram + 0x1000, 0xAA, 64 * 32 * 2);
	RSP.stereoPass = RSP_STEREO_LEFT;
	config.frameBufferEmulation.copyToRDRAM = 1;
	gli_r2cb_add_address(0x1002, 2);
	gli_r2cb_copy_from_rdram(0x1100, 0);
	check(rdram[0x1000] == 0xAA && journaled == 0, "kept when copied to RDRAM");
	config.frameBufferEmulation.copyToRDRAM = 0;
	fbInfo = 1;
	gli_r2cb_add_address(0x1002, 2);
	gli_r2cb_copy_from_rdram(0x1100, 0);
	check(rdram[0x1000] == 0xAA, "kept with FBInfo");
	fbInfo = 0;
	buffers[0].fb.m_changed = 0;
	gli_r2cb_add_address(0x1002, 2);
	gli_r2cb_copy_from_rdram(0x1100, 0);
	check(rdram[0x1000] == 0xAA, "kept when the buffer has not changed");
	buffers[0].fb.m_changed = 1;
	gli_r2cb_add_address(0x1002, 2);
	gli_r2cb_copy_from_rdram(0x1100, 0);
	check(rdram[0x1000] == 0 && journaled == 1, "cleared, journaled for the right eye");
	RSP.stereoPass = 0;

	/* the end of RDRAM bounds the clear */
	make_buffer(0, RDRAMSize + 1 - 64 * 2 * 4, G_IM_SIZ_16b, 64, 8);
	buffers[0].fb.m_changed = 1;
	memset(rdram + RDRAMSize + 1 - 64 * 2 * 4 - 2, 0x55, 64 * 2 * 4 + 2);
	gli_r2cb_add_address(buffers[0].fb.m_startAddress + 2, 2);
	gli_r2cb_copy_from_rdram(buffers[0].fb.m_startAddress + 2, 0);
	check(lastH == 4 && rdram[RDRAMSize] == 0 && rdram[RDRAMSize + 1 - 64 * 2 * 4 - 1] == 0x55, "the rows that fit, cleared to RDRAM's end");
	make_buffer(0, 0x1000, G_IM_SIZ_16b, 64, 32);

	/* float and 565 textures */
	gDP.colorImage.changed = 0;
	gli_ctx.formats.colorType = GLI_DT_FLOAT;
	gli_r2cb_copy_from_rdram(0x1000, 1);
	check(toFloat == 1 && to565 == 0 && lastCount == 64 * 20, "float: widened in place");
	gli_ctx.formats.colorType = GLI_DT_UNSIGNED_SHORT_5_6_5;
	gli_r2cb_copy_from_rdram(0x1000, 1);
	check(to565 == 1 && toFloat == 1 && lastCount == 64 * 20, "565: narrowed in place");
	gli_ctx.formats.colorType = GLI_DT_UNSIGNED_BYTE;
}

static void test_re2(void)
{
	config.generalEmulation.hacks = hack_RE2;
	gDP.colorImage.changed = 0;
	make_buffer(0, 0x1000, G_IM_SIZ_16b, 64, 100);
	buffers[0].fb.m_cfb = 0;
	maxBufferHeight = 90;
	VI.real_height = 60;
	reset();
	gli_r2cb_copy_from_rdram(0x1100, 0);
	config.frameBufferEmulation.copyFromRDRAM = 1;
	gli_r2cb_copy_from_rdram(0x1100, 0);
	check(copyImage == 0, "RE2: not into a buffer the RDP draws");
	let_go();
	buffers[0].fb.m_cfb = 1;
	gli_r2cb_copy_from_rdram(0x1100, 0);
	check(copyImage == 1 && lastH == 64, "RE2: past the start, the VI's height and four rows");
	buffers[0].fb.m_height = 50;
	gli_r2cb_copy_from_rdram(0x1100, 0);
	check(lastH == 50, "RE2: no more than the buffer's height");
	gli_r2cb_copy_from_rdram(0x1000, 0);
	check(lastH == 64, "RE2: at the start, the VI's and four");
	maxBufferHeight = 62;
	gli_r2cb_copy_from_rdram(0x1000, 0);
	check(lastH == 62, "RE2: no more than the VI could show");
	buffers[0].fb.m_height = 0;
	gli_r2cb_copy_from_rdram(0x1100, 0);
	check(copyImage == 5 && lastH == 62, "RE2: a buffer of no height bounded by the VI");
	buffers[0].fb.m_cfb = 0;
	gli_r2cb_copy_from_rdram(0x1100, 1);
	check(copyImage == 6, "RE2: a colour frame buffer copied into an RDP one");
	buffers[0].fb.m_cfb = 1;
	config.frameBufferEmulation.copyFromRDRAM = 0;

	/* the buffer form */
	maxBufferHeight = 90;
	reset();
	gli_r2cb_copy_buffer_from_rdram(&buffers[0]);
	check(copyImage == 1 && lastH == 64 && lastFull, "RE2: a buffer, to the VI's height and four, opaque");
	VI.real_height = 0;
	gli_r2cb_copy_buffer_from_rdram(&buffers[0]);
	check(lastH == 90, "RE2: no VI height, the most it could show");
	config.generalEmulation.hacks = 0;
	VI.real_height = 60;
	gli_r2cb_copy_buffer_from_rdram(&buffers[0]);
	check(lastH == 90, "no hack: the most the VI could show");
	gli_r2cb_copy_buffer_from_rdram(NULL);
	check(copyImage == 3, "no buffer: nothing");
	gli_r2cb_copy_from_rdram(0x1100, 0);
	check(copyImage == 3, "the buffer form keeps nothing");
	gli_r2cb_copy_from_rdram(0x1100, 1);
	check(lastH == 90, "no hack past the start: the most the VI could show");
}

/* the upload buffer holds four bytes a pixel for a narrower format */
static void test_upload_buffer(void)
{
	gli_r2cb_destroy();
	gli_ctx.formats.colorFormatBytes = 2;
	gli_ctx.formats.colorType = GLI_DT_UNSIGNED_SHORT_5_6_5;
	gli_r2cb_init();
	check(tex.textureBytes == 640 * 580 * 2, "the texture in its format's bytes");
	make_buffer(0, 0x1000, G_IM_SIZ_16b, 640, 580);
	maxBufferHeight = 580;
	VI.real_height = 580;
	gDP.colorImage.changed = 0;
	reset();
	gli_r2cb_copy_from_rdram(0x1000, 1);
	check(copyImage == 1 && lastH == 580 && to565 == 1, "a whole upload converted in it");
	gli_ctx.formats.colorFormatBytes = 4;
	gli_ctx.formats.colorType = GLI_DT_UNSIGNED_BYTE;

	/* past RDRAM: no rows, no copy */
	make_buffer(0, RDRAMSize + 0x100, G_IM_SIZ_16b, 64, 8);
	reset();
	gli_r2cb_copy_from_rdram(RDRAMSize + 0x100, 1);
	check(copyImage == 0, "past RDRAM: no copy");
	let_go();
}

int main(void)
{
	dirty_stack();
	test_init();
	test_marks();
	test_copy();
	test_re2();
	test_upload_buffer();
	gli_r2cb_destroy();
	check(texRemoved == 2, "destroyed: its texture dropped");
	gli_r2cb_destroy();
	check(texRemoved == 2, "once");
	if (failures) {
		printf("%d failed\n", failures);
		return 1;
	}
	printf("rdram to color buffer: ok\n");
	return 0;
}
