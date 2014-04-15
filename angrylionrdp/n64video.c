
#include "main.h"
#include "tctables.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

extern uint32_t *blitter_buf;
extern uint32_t pitchindwords;

#define PRESCALE_WIDTH (screen_width)
#define PRESCALE_HEIGHT (screen_height)


#define SIGN16(x)	((INT16)(x))
#define SIGN8(x)	((INT8)(x))

#define SIGN(x, numb)	(((x) & ((1 << numb) - 1)) | -((x) & (1 << (numb - 1))))
#define SIGNF(x, numb)	((x) | -((x) & (1 << (numb - 1))))




#define GET_LOW(x)	(((x) & 0x3e) << 2)
#define GET_MED(x)	(((x) & 0x7c0) >> 3)
#define GET_HI(x)	(((x) >> 8) & 0xf8)


#define GET_LOW_RGBA16_TMEM(x)	(replicated_rgba[((x) >> 1) & 0x1f])
#define GET_MED_RGBA16_TMEM(x)	(replicated_rgba[((x) >> 6) & 0x1f])
#define GET_HI_RGBA16_TMEM(x)	(replicated_rgba[(x) >> 11])

STRICTINLINE void fatalerror(const char * err, ...)
{
	char VsprintfBuffer[200];
	va_list arg;
	va_start(arg, err);
	vsprintf(VsprintfBuffer, err, arg);
#ifndef WIN32
	printf(VsprintfBuffer);
#endif
	va_end(arg);
}

STRICTINLINE void popmessage(const char* err, ...)
{
	char VsprintfBuffer[200];
	va_list arg;
	va_start(arg, err);
	vsprintf(VsprintfBuffer, err, arg);
#ifdef WIN32
	MessageBoxA(0,VsprintfBuffer,"RDP: warning",MB_OK);
#endif
#ifndef WIN32
	printf(VsprintfBuffer);
#endif
	va_end(arg);
}


#define LOG_RDP_EXECUTION 0
#define	DETAILED_LOGGING 0

//FILE *rdp_exec;

UINT32 rdp_cmd_data[0x10000];
UINT32 rdp_cmd_ptr = 0;
UINT32 rdp_cmd_cur = 0;
UINT32 ptr_onstart = 0;

UINT32 old_vi_origin = 0;
INT32 oldvstart = 1337;
UINT32 oldhstart = 0;
UINT32 oldsomething = 0;
UINT32 prevwasblank = 0;
UINT32 double_stretch = 0;
int blshifta = 0, blshiftb = 0, pastblshifta = 0, pastblshiftb = 0;
UINT32 plim = 0x3fffff;
UINT32 idxlim16 = 0x1fffff;
UINT32 idxlim32 = 0xfffff;
UINT8* rdram_8;
UINT16* rdram_16;
UINT32 brightness = 0;
INT32 iseed = 1;

typedef struct
{
	int lx, rx;
	int unscrx;
	int validline;
	INT32 r, g, b, a, s, t, w, z;
	INT32 majorx[4];
	INT32 minorx[4];
	INT32 invalyscan[4];
} SPAN;

static SPAN span[1024];
UINT32 cvgbuf[1024];


static int spans_ds;
static int spans_dt;
static int spans_dw;
static int spans_dr;
static int spans_dg;
static int spans_db;
static int spans_da;
static int spans_dz;
static int spans_dzpix;

int spans_drdy, spans_dgdy, spans_dbdy, spans_dady, spans_dzdy;
int spans_cdr, spans_cdg, spans_cdb, spans_cda, spans_cdz;

static int spans_dsdy, spans_dtdy, spans_dwdy;



typedef struct
{
	INT32 r, g, b, a;
} COLOR;

typedef struct
{
	UINT8 r, g, b;
} FBCOLOR;

typedef struct
{
	UINT8 r, g, b, cvg;
} CCVG;

typedef struct
{
	UINT16 xl, yl, xh, yh;		
} RECTANGLE;

typedef struct
{
	int tilenum;
	UINT16 xl, yl, xh, yh;		
	INT16 s, t;					
	INT16 dsdx, dtdy;			
	UINT32 flip;	
} TEX_RECTANGLE;

typedef struct
{
	int clampdiffs, clampdifft;
	int clampens, clampent;
	int masksclamped, masktclamped;
	int notlutswitch, tlutswitch;
} FAKETILE;

typedef struct
{
	int format;
	int size;
	int line;
	int tmem;
	int palette;
	int ct, mt, cs, ms;
	int mask_t, shift_t, mask_s, shift_s;
	
	UINT16 sl, tl, sh, th;		
	
	FAKETILE f;
} TILE;

typedef struct
{
	int sub_a_rgb0;
	int sub_b_rgb0;
	int mul_rgb0;
	int add_rgb0;
	int sub_a_a0;
	int sub_b_a0;
	int mul_a0;
	int add_a0;

	int sub_a_rgb1;
	int sub_b_rgb1;
	int mul_rgb1;
	int add_rgb1;
	int sub_a_a1;
	int sub_b_a1;
	int mul_a1;
	int add_a1;
} COMBINE_MODES;

typedef struct 
{
	int stalederivs;
	int dolod;
	int partialreject_1cycle; 
	int partialreject_2cycle;
	int special_bsel0; 
	int special_bsel1;
	int rgb_alpha_dither;
} MODEDERIVS;

typedef struct
{
	int cycle_type;
	int persp_tex_en;
	int detail_tex_en;
	int sharpen_tex_en;
	int tex_lod_en;
	int en_tlut;
	int tlut_type;
	int sample_type;
	int mid_texel;
	int bi_lerp0;
	int bi_lerp1;
	int convert_one;
	int key_en;
	int rgb_dither_sel;
	int alpha_dither_sel;
	int blend_m1a_0;
	int blend_m1a_1;
	int blend_m1b_0;
	int blend_m1b_1;
	int blend_m2a_0;
	int blend_m2a_1;
	int blend_m2b_0;
	int blend_m2b_1;
	int force_blend;
	int alpha_cvg_select;
	int cvg_times_alpha;
	int z_mode;
	int cvg_dest;
	int color_on_cvg;
	int image_read_en;
	int z_update_en;
	int z_compare_en;
	int antialias_en;
	int z_source_sel;
	int dither_alpha_en;
	int alpha_compare_en;
	MODEDERIVS f;
} OTHER_MODES;



#define PIXEL_SIZE_4BIT			0
#define PIXEL_SIZE_8BIT			1
#define PIXEL_SIZE_16BIT		2
#define PIXEL_SIZE_32BIT		3

#define CYCLE_TYPE_1			0
#define CYCLE_TYPE_2			1
#define CYCLE_TYPE_COPY			2
#define CYCLE_TYPE_FILL			3


#define FORMAT_RGBA				0
#define FORMAT_YUV				1
#define FORMAT_CI				2
#define FORMAT_IA				3
#define FORMAT_I				4


#define TEXEL_RGBA4				0
#define TEXEL_RGBA8				1
#define TEXEL_RGBA16			2
#define TEXEL_RGBA32			3
#define TEXEL_YUV4				4
#define TEXEL_YUV8				5
#define TEXEL_YUV16				6
#define TEXEL_YUV32				7
#define TEXEL_CI4				8
#define TEXEL_CI8				9
#define TEXEL_CI16				0xa
#define TEXEL_CI32				0xb
#define TEXEL_IA4				0xc
#define TEXEL_IA8				0xd
#define TEXEL_IA16				0xe
#define TEXEL_IA32				0xf
#define TEXEL_I4				0x10
#define TEXEL_I8				0x11
#define TEXEL_I16				0x12
#define TEXEL_I32				0x13


#define CVG_CLAMP				0
#define CVG_WRAP				1
#define CVG_ZAP					2
#define CVG_SAVE				3


#define ZMODE_OPAQUE			0
#define ZMODE_INTERPENETRATING	1
#define ZMODE_TRANSPARENT		2
#define ZMODE_DECAL				3

COMBINE_MODES combine;
OTHER_MODES other_modes;

COLOR blend_color;
COLOR prim_color;
COLOR env_color;
COLOR fog_color;
COLOR combined_color;
COLOR texel0_color;
COLOR texel1_color;
COLOR nexttexel_color;
COLOR shade_color;
COLOR key_scale;
COLOR key_center;
COLOR key_width;
static INT32 noise = 0;
static INT32 primitive_lod_frac = 0;
static INT32 one_color = 0x100;
static INT32 zero_color = 0x00;

INT32 keyalpha;

static INT32 blenderone	= 0xff;


static INT32 *combiner_rgbsub_a_r[2];
static INT32 *combiner_rgbsub_a_g[2];
static INT32 *combiner_rgbsub_a_b[2];
static INT32 *combiner_rgbsub_b_r[2];
static INT32 *combiner_rgbsub_b_g[2];
static INT32 *combiner_rgbsub_b_b[2];
static INT32 *combiner_rgbmul_r[2];
static INT32 *combiner_rgbmul_g[2];
static INT32 *combiner_rgbmul_b[2];
static INT32 *combiner_rgbadd_r[2];
static INT32 *combiner_rgbadd_g[2];
static INT32 *combiner_rgbadd_b[2];

static INT32 *combiner_alphasub_a[2];
static INT32 *combiner_alphasub_b[2];
static INT32 *combiner_alphamul[2];
static INT32 *combiner_alphaadd[2];


static INT32 *blender1a_r[2];
static INT32 *blender1a_g[2];
static INT32 *blender1a_b[2];
static INT32 *blender1b_a[2];
static INT32 *blender2a_r[2];
static INT32 *blender2a_g[2];
static INT32 *blender2a_b[2];
static INT32 *blender2b_a[2];

COLOR pixel_color;
COLOR inv_pixel_color;
COLOR blended_pixel_color;
COLOR memory_color;
COLOR pre_memory_color;

UINT32 fill_color;		

UINT32 primitive_z;
UINT16 primitive_delta_z;

static int fb_format = FORMAT_RGBA;
static int fb_size = PIXEL_SIZE_4BIT;
static int fb_width = 0;
static UINT32 fb_address = 0;

static int ti_format = FORMAT_RGBA;
static int ti_size = PIXEL_SIZE_4BIT;
static int ti_width = 0;
static UINT32 ti_address = 0;

static UINT32 zb_address = 0;

static TILE tile[8];

static RECTANGLE clip = {0,0,0x2000,0x2000};
static int scfield = 0;
static int sckeepodd = 0;
int oldscyl = 0;

UINT8 TMEM[0x1000]; 

#define tlut ((UINT16*)(&TMEM[0x800]))

#define PIXELS_TO_BYTES(pix, siz) (((pix) << (siz)) >> 1)

typedef struct{
	int startspan;
	int endspan;
	int preendspan;
	int nextspan;
	int midspan;
	int longspan;
}SPANSIGS;


static void rdp_set_other_modes(UINT32 w1, UINT32 w2);
STRICTINLINE void fetch_texel(COLOR *color, int s, int t, UINT32 tilenum);
STRICTINLINE void fetch_texel_entlut(COLOR *color, int s, int t, UINT32 tilenum);
STRICTINLINE void fetch_texel_quadro(COLOR *color0, COLOR *color1, COLOR *color2, COLOR *color3, int s0, int s1, int t0, int t1, UINT32 tilenum);
STRICTINLINE void fetch_texel_entlut_quadro(COLOR *color0, COLOR *color1, COLOR *color2, COLOR *color3, int s0, int s1, int t0, int t1, UINT32 tilenum);
void tile_tlut_common_cs_decoder(UINT32 w1, UINT32 w2);
void loading_pipeline(int start, int end, int tilenum, int coord_quad, int ltlut);
void get_tmem_idx(int s, int t, UINT32 tilenum, UINT32* idx0, UINT32* idx1, UINT32* idx2, UINT32* idx3, UINT32* bit3flipped, UINT32* hibit);
void sort_tmem_idx(UINT32 *idx, UINT32 idxa, UINT32 idxb, UINT32 idxc, UINT32 idxd, UINT32 bankno);
void sort_tmem_shorts_lowhalf(UINT32* bindshort, UINT32 short0, UINT32 short1, UINT32 short2, UINT32 short3, UINT32 bankno);
void compute_color_index(UINT32* cidx, UINT32 readshort, UINT32 nybbleoffset, UINT32 tilenum);
void read_tmem_copy(int s, int s1, int s2, int s3, int t, UINT32 tilenum, UINT32* sortshort, int* hibits, int* lowbits);
void replicate_for_copy(UINT32* outbyte, UINT32 inshort, UINT32 nybbleoffset, UINT32 tilenum, UINT32 tformat, UINT32 tsize);
void fetch_qword_copy(UINT32* hidword, UINT32* lowdword, INT32 ssss, INT32 ssst, UINT32 tilenum);
void render_spans_1cycle_complete(int start, int end, int tilenum, int flip);
void render_spans_1cycle_notexel1(int start, int end, int tilenum, int flip);
void render_spans_1cycle_notex(int start, int end, int tilenum, int flip);
void render_spans_2cycle_complete(int start, int end, int tilenum, int flip);
void render_spans_2cycle_notexelnext(int start, int end, int tilenum, int flip);
void render_spans_2cycle_notexel1(int start, int end, int tilenum, int flip);
void render_spans_2cycle_notex(int start, int end, int tilenum, int flip);
void render_spans_fill(int start, int end, int flip);
void render_spans_copy(int start, int end, int tilenum, int flip);
STRICTINLINE void combiner_1cycle(int adseed, UINT32* curpixel_cvg);
STRICTINLINE void combiner_2cycle(int adseed, UINT32* curpixel_cvg);
STRICTINLINE int blender_1cycle(UINT32* fr, UINT32* fg, UINT32* fb, int dith, UINT32 blend_en, UINT32 prewrap, UINT32 curpixel_cvg, UINT32 curpixel_cvbit);
STRICTINLINE int blender_2cycle(UINT32* fr, UINT32* fg, UINT32* fb, int dith, UINT32 blend_en, UINT32 prewrap, UINT32 curpixel_cvg, UINT32 curpixel_cvbit);
STRICTINLINE void texture_pipeline_cycle(COLOR* TEX, COLOR* prev, INT32 SSS, INT32 SST, UINT32 tilenum, UINT32 cycle);
STRICTINLINE void tc_pipeline_copy(INT32* sss0, INT32* sss1, INT32* sss2, INT32* sss3, INT32* sst, int tilenum);
STRICTINLINE void tc_pipeline_load(INT32* sss, INT32* sst, int tilenum, int coord_quad);
STRICTINLINE void tcclamp_generic(INT32* S, INT32* T, INT32* SFRAC, INT32* TFRAC, INT32 maxs, INT32 maxt, INT32 num);
STRICTINLINE void tcclamp_cycle(INT32* S, INT32* T, INT32* SFRAC, INT32* TFRAC, INT32 maxs, INT32 maxt, INT32 num);
STRICTINLINE void tcclamp_cycle_light(INT32* S, INT32* T, INT32 maxs, INT32 maxt, INT32 num);
STRICTINLINE void tcshift_cycle(INT32* S, INT32* T, INT32* maxs, INT32* maxt, UINT32 num);
STRICTINLINE void tcshift_copy(INT32* S, INT32* T, UINT32 num);
STRICTINLINE void precalculate_everything(void);
STRICTINLINE int alpha_compare(INT32 comb_alpha);
STRICTINLINE INT32 color_combiner_equation(INT32 a, INT32 b, INT32 c, INT32 d);
STRICTINLINE INT32 alpha_combiner_equation(INT32 a, INT32 b, INT32 c, INT32 d);
STRICTINLINE void blender_equation_cycle0(int* r, int* g, int* b);
STRICTINLINE void blender_equation_cycle0_2(int* r, int* g, int* b);
STRICTINLINE void blender_equation_cycle1(int* r, int* g, int* b);
STRICTINLINE UINT32 rightcvghex(UINT32 x, UINT32 fmask); 
STRICTINLINE UINT32 leftcvghex(UINT32 x, UINT32 fmask);
STRICTINLINE void compute_cvg_noflip(INT32 scanline);
STRICTINLINE void compute_cvg_flip(INT32 scanline);
STRICTINLINE void fbwrite_4(UINT32 curpixel, UINT32 r, UINT32 g, UINT32 b, UINT32 blend_en, UINT32 curpixel_cvg, UINT32 curpixel_memcvg);
STRICTINLINE void fbwrite_8(UINT32 curpixel, UINT32 r, UINT32 g, UINT32 b, UINT32 blend_en, UINT32 curpixel_cvg, UINT32 curpixel_memcvg);
STRICTINLINE void fbwrite_16(UINT32 curpixel, UINT32 r, UINT32 g, UINT32 b, UINT32 blend_en, UINT32 curpixel_cvg, UINT32 curpixel_memcvg);
STRICTINLINE void fbwrite_32(UINT32 curpixel, UINT32 r, UINT32 g, UINT32 b, UINT32 blend_en, UINT32 curpixel_cvg, UINT32 curpixel_memcvg);
STRICTINLINE void fbfill_4(UINT32 curpixel);
STRICTINLINE void fbfill_8(UINT32 curpixel);
STRICTINLINE void fbfill_16(UINT32 curpixel);
STRICTINLINE void fbfill_32(UINT32 curpixel);
STRICTINLINE void fbread_4(UINT32 num, UINT32* curpixel_memcvg);
STRICTINLINE void fbread_8(UINT32 num, UINT32* curpixel_memcvg);
STRICTINLINE void fbread_16(UINT32 num, UINT32* curpixel_memcvg);
STRICTINLINE void fbread_32(UINT32 num, UINT32* curpixel_memcvg);
STRICTINLINE void fbread2_4(UINT32 num, UINT32* curpixel_memcvg);
STRICTINLINE void fbread2_8(UINT32 num, UINT32* curpixel_memcvg);
STRICTINLINE void fbread2_16(UINT32 num, UINT32* curpixel_memcvg);
STRICTINLINE void fbread2_32(UINT32 num, UINT32* curpixel_memcvg);
STRICTINLINE UINT32 z_decompress(UINT32 rawz);
STRICTINLINE UINT32 dz_decompress(UINT32 compresseddz);
STRICTINLINE UINT32 dz_compress(UINT32 value);
STRICTINLINE void z_build_com_table(void);
STRICTINLINE void precalc_cvmask_derivatives(void);
STRICTINLINE UINT16 decompress_cvmask_frombyte(UINT8 byte);
STRICTINLINE void lookup_cvmask_derivatives(UINT32 mask, UINT8* offx, UINT8* offy, UINT32* curpixel_cvg, UINT32* curpixel_cvbit);
STRICTINLINE void z_store(UINT32 zcurpixel, UINT32 z, int dzpixenc);
STRICTINLINE UINT32 z_compare(UINT32 zcurpixel, UINT32 sz, UINT16 dzpix, int dzpixenc, UINT32* blend_en, UINT32* prewrap, UINT32* curpixel_cvg, UINT32 curpixel_memcvg);
STRICTINLINE int finalize_spanalpha(UINT32 blend_en, UINT32 curpixel_cvg, UINT32 curpixel_memcvg);
STRICTINLINE INT32 normalize_dzpix(INT32 sum);
STRICTINLINE INT32 CLIP(INT32 value,INT32 min,INT32 max);
STRICTINLINE void video_filter16(int* r, int* g, int* b, UINT32 fboffset, UINT32 num, UINT32 hres, UINT32 centercvg);
STRICTINLINE void video_filter32(int* endr, int* endg, int* endb, UINT32 fboffset, UINT32 num, UINT32 hres, UINT32 centercvg);
STRICTINLINE void divot_filter(CCVG* final, CCVG centercolor, CCVG leftcolor, CCVG rightcolor);
STRICTINLINE void restore_filter16(int* r, int* g, int* b, UINT32 fboffset, UINT32 num, UINT32 hres);
STRICTINLINE void restore_filter32(int* r, int* g, int* b, UINT32 fboffset, UINT32 num, UINT32 hres);
STRICTINLINE void gamma_filters(int* r, int* g, int* b, int gamma_and_dither);
STRICTINLINE void adjust_brightness(int* r, int* g, int* b, int brightcoeff);
STRICTINLINE void clearscreen(UINT32 x0,UINT32 y0, UINT32 x1, UINT32 y1, UINT32 white);
STRICTINLINE void clearfb16(UINT16* fb, UINT32 width,UINT32 height);
STRICTINLINE void tcdiv_persp(INT32 ss, INT32 st, INT32 sw, INT32* sss, INT32* sst);
STRICTINLINE void tcdiv_nopersp(INT32 ss, INT32 st, INT32 sw, INT32* sss, INT32* sst);
STRICTINLINE void tclod_4x17_to_15(INT32 scurr, INT32 snext, INT32 tcurr, INT32 tnext, INT32 previous, INT32* lod);
STRICTINLINE void tclod_tcclamp(INT32* sss, INT32* sst);
STRICTINLINE void lodfrac_lodtile_signals(int lodclamp, INT32 lod, UINT32* l_tile, UINT32* magnify, UINT32* distant);
STRICTINLINE void tclod_1cycle_current(INT32* sss, INT32* sst, INT32 nexts, INT32 nextt, INT32 s, INT32 t, INT32 w, INT32 dsinc, INT32 dtinc, INT32 dwinc, INT32 scanline, INT32 prim_tile, INT32* t1, SPANSIGS* sigs);
STRICTINLINE void tclod_1cycle_current_simple(INT32* sss, INT32* sst, INT32 s, INT32 t, INT32 w, INT32 dsinc, INT32 dtinc, INT32 dwinc, INT32 scanline, INT32 prim_tile, INT32* t1, SPANSIGS* sigs);
STRICTINLINE void tclod_1cycle_next(INT32* sss, INT32* sst, INT32 s, INT32 t, INT32 w, INT32 dsinc, INT32 dtinc, INT32 dwinc, INT32 scanline, INT32 prim_tile, INT32* t1, SPANSIGS* sigs, INT32* prelodfrac);
STRICTINLINE void tclod_2cycle_current(INT32* sss, INT32* sst, INT32 nexts, INT32 nextt, INT32 s, INT32 t, INT32 w, INT32 dsinc, INT32 dtinc, INT32 dwinc, INT32 prim_tile, INT32* t1, INT32* t2);
STRICTINLINE void tclod_2cycle_current_simple(INT32* sss, INT32* sst, INT32 s, INT32 t, INT32 w, INT32 dsinc, INT32 dtinc, INT32 dwinc, INT32 prim_tile, INT32* t1, INT32* t2);
STRICTINLINE void tclod_2cycle_current_notexel1(INT32* sss, INT32* sst, INT32 s, INT32 t, INT32 w, INT32 dsinc, INT32 dtinc, INT32 dwinc, INT32 prim_tile, INT32* t1);
STRICTINLINE void tclod_2cycle_next(INT32* sss, INT32* sst, INT32 s, INT32 t, INT32 w, INT32 dsinc, INT32 dtinc, INT32 dwinc, INT32 prim_tile, INT32* t1, INT32* t2, INT32* prelodfrac);
STRICTINLINE void tclod_copy(INT32* sss, INT32* sst, INT32 s, INT32 t, INT32 w, INT32 dsinc, INT32 dtinc, INT32 dwinc, INT32 prim_tile, INT32* t1);
STRICTINLINE void get_texel1_1cycle(INT32* s1, INT32* t1, INT32 s, INT32 t, INT32 w, INT32 dsinc, INT32 dtinc, INT32 dwinc, INT32 scanline, SPANSIGS* sigs);
STRICTINLINE void get_nexttexel0_2cycle(INT32* s1, INT32* t1, INT32 s, INT32 t, INT32 w, INT32 dsinc, INT32 dtinc, INT32 dwinc);
STRICTINLINE void video_max_optimized(UINT32* Pixels, UINT32* pen);
STRICTINLINE void calculate_clamp_diffs(UINT32 tile);
STRICTINLINE void calculate_tile_derivs(UINT32 tile);
STRICTINLINE void rgb_dither_complete(int* r, int* g, int* b, int dith);
STRICTINLINE void rgb_dither_nothing(int* r, int* g, int* b, int dith);
STRICTINLINE void get_dither_noise_complete(int x, int y, int* cdith, int* adith);
STRICTINLINE void get_dither_only(int x, int y, int* cdith, int* adith);
STRICTINLINE void get_dither_nothing(int x, int y, int* cdith, int* adith);
STRICTINLINE void vi_vl_lerp(CCVG* up, CCVG down, UINT32 frac);
STRICTINLINE void rgbaz_correct_clip(int offx, int offy, int r, int g, int b, int a, int* z, UINT32 curpixel_cvg);
STRICTINLINE void vi_fetch_filter16(CCVG* res, UINT32 fboffset, UINT32 cur_x, UINT32 fsaa, UINT32 dither_filter, UINT32 vres);
STRICTINLINE void vi_fetch_filter32(CCVG* res, UINT32 fboffset, UINT32 cur_x, UINT32 fsaa, UINT32 dither_filter, UINT32 vres);
int IsBadPtrW32(void *ptr, UINT32 bytes);
UINT32 vi_integer_sqrt(UINT32 a);
void deduce_derivatives(void);
STRICTINLINE INT32 irand();

void dump_buffer4kb(char* Name, void* Buff);
void dump_buffer(char* Name, void* Buff,UINT32 Bytes);
void dump_tmem_and_exit(char* Name);
void col_decode16(UINT16* addr, COLOR* col);
void show_combiner_equation(void);
void show_blender_equation(void);
void showtile(UINT32 tilenum, int stop, int clamped);
void show_tri_command(void);
UINT32 compare_tri_command(UINT32 w0, UINT32 w1, UINT32 w2);
void show_color(COLOR* col);
void show_current_cfb(int isviorigin);
int getdebugcolor(void);
void bytefill_tmem(char byte);

static INT32 k0 = 0, k1 = 0, k2 = 0, k3 = 0, k4 = 0, k5 = 0;
static INT32 lod_frac = 0;
UINT32 DebugMode = 0, DebugMode2 = 0;
int debugcolor = 0;
UINT8 hidden_bits[0x400000];
struct {UINT32 shift; UINT32 add;} z_dec_table[8] = {
     6, 0x00000,
     5, 0x20000,
     4, 0x30000,
     3, 0x38000,
     2, 0x3c000,
     1, 0x3e000,
     0, 0x3f000,
     0, 0x3f800,
};


static void (*vi_fetch_filter_func[2])(CCVG*, UINT32, UINT32, UINT32, UINT32, UINT32) = 
{
	vi_fetch_filter16, vi_fetch_filter32
};

static void (*fbread_func[4])(UINT32, UINT32*) = 
{
	fbread_4, fbread_8, fbread_16, fbread_32
};

static void (*fbread2_func[4])(UINT32, UINT32*) =
{
	fbread2_4, fbread2_8, fbread2_16, fbread2_32
};

static void (*fbwrite_func[4])(UINT32, UINT32, UINT32, UINT32, UINT32, UINT32, UINT32) = 
{
	fbwrite_4, fbwrite_8, fbwrite_16, fbwrite_32
};

static void (*fbfill_func[4])(UINT32) =
{
	fbfill_4, fbfill_8, fbfill_16, fbfill_32
};

static void (*get_dither_noise_func[3])(int, int, int*, int*) = 
{
	get_dither_noise_complete, get_dither_only, get_dither_nothing
};

static void (*rgb_dither_func[2])(int*, int*, int*, int) =
{
	rgb_dither_complete, rgb_dither_nothing
};

static void (*tcdiv_func[2])(INT32, INT32, INT32, INT32*, INT32*) =
{
	tcdiv_nopersp, tcdiv_persp
};

static void (*render_spans_1cycle_func[3])(int, int, int, int) =
{
	render_spans_1cycle_notex, render_spans_1cycle_notexel1, render_spans_1cycle_complete
};

static void (*render_spans_2cycle_func[4])(int, int, int, int) =
{
	render_spans_2cycle_notex, render_spans_2cycle_notexel1, render_spans_2cycle_notexelnext, render_spans_2cycle_complete
};

void (*fbread1_ptr)(UINT32, UINT32*);
void (*fbread2_ptr)(UINT32, UINT32*);
void (*fbwrite_ptr)(UINT32, UINT32, UINT32, UINT32, UINT32, UINT32, UINT32);
void (*fbfill_ptr)(UINT32);
void (*get_dither_noise_ptr)(int, int, int*, int*);
void (*rgb_dither_ptr)(int*, int*, int*, int);
void (*tcdiv_ptr)(INT32, INT32, INT32, INT32*, INT32*);
void (*render_spans_1cycle_ptr)(int, int, int, int);
void (*render_spans_2cycle_ptr)(int, int, int, int);

typedef struct{
	UINT8 cvg;
	UINT8 cvbit;
	UINT8 xoff;
	UINT8 yoff;
}CVtcmaskDERIVATIVE;

UINT32 gamma_table[0x100];
UINT32 gamma_dither_table[0x4000];
UINT16 z_com_table[0x40000];
UINT32 z_complete_dec_table[0x4000];
UINT8 replicated_rgba[32];
INT32 vi_restore_table[0x400];
INT32 maskbits_table[16];
UINT32 special_9bit_clamptable[512];
INT32 special_9bit_exttable[512];
INT32 ge_two_table[128];
INT32 log2table[256];
INT32 tcdiv_table[0x8000];
UINT8 bldiv_hwaccurate_table[0x8000];
INT32 clamp_t_diff[8];
INT32 clamp_s_diff[8];
CVtcmaskDERIVATIVE cvarray[0x100];



#define RREADADDR8(in) (((in) <= plim) ? (rdram_8[(in) ^ BYTE_ADDR_XOR]) : 0)
#define RREADIDX16(in) (((in) <= idxlim16) ? (rdram_16[(in) ^ WORD_ADDR_XOR]) : 0)
#define RREADIDX32(in) (((in) <= idxlim32) ? (rdram[(in)]) : 0)

#define RWRITEADDR8(in, val)	{if ((in) <= plim) rdram_8[(in) ^ BYTE_ADDR_XOR] = (val);}
#define RWRITEIDX16(in, val)	{if ((in) <= idxlim16) rdram_16[(in) ^ WORD_ADDR_XOR] = (val);}
#define RWRITEIDX32(in, val)	{if ((in) <= idxlim32) rdram[(in)] = (val);}



#define PAIRREAD16(rdst, hdst, in)		\
{										\
	if ((in) <= idxlim16) {(rdst) = rdram_16[(in) ^ WORD_ADDR_XOR]; (hdst) = hidden_bits[(in)];}	\
	else {(rdst) = (hdst) = 0;}			\
}

#define PAIRWRITE16(in, rval, hval)		\
{										\
	if ((in) <= idxlim16) {rdram_16[(in) ^ WORD_ADDR_XOR] = (rval); hidden_bits[(in)] = (hval);}	\
}

#define PAIRWRITE32(in, rval, hval0, hval1)	\
{											\
	if ((in) <= idxlim32) {rdram[(in)] = (rval); hidden_bits[(in) << 1] = (hval0); hidden_bits[((in) << 1) + 1] = (hval1);}	\
}

#define PAIRWRITE8(in, rval, hval)	\
{									\
	if ((in) <= plim) {rdram_8[(in) ^ BYTE_ADDR_XOR] = (rval); if ((in) & 1) hidden_bits[(in) >> 1] = (hval);}	\
}

struct onetime
{
	int ntscnolerp, copymstrangecrashes, fillmcrashes, fillmbitcrashes, syncfullcrash;
} onetimewarnings;

UINT32 z64gl_command = 0;
UINT32 command_counter = 0;
int SaveLoaded = 0;
UINT32 max_level = 0;
INT32 min_level = 0;
UINT32 tvfadeoutstate[625];
int rdp_pipeline_crashed = 0;


STRICTINLINE void tcmask(INT32* S, INT32* T, INT32 num);
STRICTINLINE void tcmask(INT32* S, INT32* T, INT32 num)
{
	INT32 wrap;
	
	

	if (tile[num].mask_s)
	{
		if (tile[num].ms)
		{
			wrap = *S >> tile[num].f.masksclamped;
			wrap &= 1;
			*S ^= (-wrap);
		}
		*S &= maskbits_table[tile[num].mask_s];
	}

	if (tile[num].mask_t)
	{
		if (tile[num].mt)
		{
			wrap = *T >> tile[num].f.masktclamped;
			wrap &= 1;
			*T ^= (-wrap);
		}
		
		*T &= maskbits_table[tile[num].mask_t];
	}
}


STRICTINLINE void tcmask_coupled(INT32* S, INT32* S1, INT32* T, INT32* T1, INT32 num);
STRICTINLINE void tcmask_coupled(INT32* S, INT32* S1, INT32* T, INT32* T1, INT32 num)
{
	INT32 wrap;
	INT32 maskbits; 
	INT32 wrapthreshold; 


	if (tile[num].mask_s)
	{
		if (tile[num].ms)
		{
			wrapthreshold = tile[num].f.masksclamped;

			wrap = (*S >> wrapthreshold) & 1;
			*S ^= (-wrap);

			wrap = (*S1 >> wrapthreshold) & 1;
			*S1 ^= (-wrap);
		}

		maskbits = maskbits_table[tile[num].mask_s];
		*S &= maskbits;
		*S1 &= maskbits;
	}

	if (tile[num].mask_t)
	{
		if (tile[num].mt)
		{
			wrapthreshold = tile[num].f.masktclamped;

			wrap = (*T >> wrapthreshold) & 1;
			*T ^= (-wrap);

			wrap = (*T1 >> wrapthreshold) & 1;
			*T1 ^= (-wrap);
		}
		maskbits = maskbits_table[tile[num].mask_t];
		*T &= maskbits;
		*T1 &= maskbits;
	}
}


STRICTINLINE void tcmask_copy(INT32* S, INT32* S1, INT32* S2, INT32* S3, INT32* T, INT32 num);
STRICTINLINE void tcmask_copy(INT32* S, INT32* S1, INT32* S2, INT32* S3, INT32* T, INT32 num)
{
	INT32 wrap;
	INT32 maskbits_s; 
	INT32 swrapthreshold; 

	if (tile[num].mask_s)
	{
		if (tile[num].ms)
		{
			swrapthreshold = tile[num].f.masksclamped;

			wrap = (*S >> swrapthreshold) & 1;
			*S ^= (-wrap);

			wrap = (*S1 >> swrapthreshold) & 1;
			*S1 ^= (-wrap);

			wrap = (*S2 >> swrapthreshold) & 1;
			*S2 ^= (-wrap);

			wrap = (*S3 >> swrapthreshold) & 1;
			*S3 ^= (-wrap);
		}

		maskbits_s = maskbits_table[tile[num].mask_s];
		*S &= maskbits_s;
		*S1 &= maskbits_s;
		*S2 &= maskbits_s;
		*S3 &= maskbits_s;
	}

	if (tile[num].mask_t)
	{
		if (tile[num].mt)
		{
			wrap = *T >> tile[num].f.masktclamped; 
			wrap &= 1;
			*T ^= (-wrap);
		}

		*T &= maskbits_table[tile[num].mask_t];
	}
}


STRICTINLINE void tcshift_cycle(INT32* S, INT32* T, INT32* maxs, INT32* maxt, UINT32 num)
{



	INT32 coord = *S;
	INT32 shifter = tile[num].shift_s;

	if (shifter < 11)
	{
		coord = SIGN16(coord);
		coord >>= shifter;
	}
	else
	{
		coord <<= (16 - shifter);
		coord = SIGN16(coord);
	}
	*S = coord; 

	

	
	*maxs = ((coord >> 3) >= tile[num].sh);
	
	

	coord = *T;
	shifter = tile[num].shift_t;

	if (shifter < 11)
	{
		coord = SIGN16(coord);
		coord >>= shifter;
	}
	else
	{
		coord <<= (16 - shifter);
		coord = SIGN16(coord);
	}
	*T = coord; 
	*maxt = ((coord >> 3) >= tile[num].th);
}	


STRICTINLINE void tcshift_copy(INT32* S, INT32* T, UINT32 num)
{
	INT32 coord = *S;
	INT32 shifter = tile[num].shift_s;

	if (shifter < 11)
	{
		coord = SIGN16(coord);
		coord >>= shifter;
	}
	else
	{
		coord <<= (16 - shifter);
		coord = SIGN16(coord);
	}
	*S = coord; 

	coord = *T;
	shifter = tile[num].shift_t;

	if (shifter < 11)
	{
		coord = SIGN16(coord);
		coord >>= shifter;
	}
	else
	{
		coord <<= (16 - shifter);
		coord = SIGN16(coord);
	}
	*T = coord; 
	
}


STRICTINLINE void tcclamp_cycle(INT32* S, INT32* T, INT32* SFRAC, INT32* TFRAC, INT32 maxs, INT32 maxt, INT32 num)
{

	INT32 locs = *S, loct = *T;
	if (tile[num].f.clampens)
	{
		if (!(locs & 0x10000))
		{
			if (!maxs)
				*S = (locs >> 5);
			else
			{
				*S = tile[num].f.clampdiffs;
				*SFRAC = 0;
			}
		}
		else
		{
			*S = 0;
			*SFRAC = 0;
		}
	}
	else
		*S = (locs >> 5);

	if (tile[num].f.clampent)
	{
		if (!(loct & 0x10000))
		{
			if (!maxt)
				*T = (loct >> 5);
			else
			{
				*T = tile[num].f.clampdifft;
				*TFRAC = 0;
			}
		}
		else
		{
			*T = 0;
			*TFRAC = 0;
		}
	}
	else
		*T = (loct >> 5);
}


STRICTINLINE void tcclamp_cycle_light(INT32* S, INT32* T, INT32 maxs, INT32 maxt, INT32 num)
{
	INT32 locs = *S, loct = *T;
	if (tile[num].f.clampens)
	{
		if (!(locs & 0x10000))
		{
			if (!maxs)
				*S = (locs >> 5);
			else
				*S = tile[num].f.clampdiffs;
		}
		else
			*S = 0;
	}
	else
		*S = (locs >> 5);

	if (tile[num].f.clampent)
	{
		if (!(loct & 0x10000))
		{
			if (!maxt)
				*T = (loct >> 5);
			else
				*T = tile[num].f.clampdifft;
		}
		else
			*T = 0;
	}
	else
		*T = (loct >> 5);
}


int rdp_init()
{
   int i;

#if 0
	if (LOG_RDP_EXECUTION)
		rdp_exec = fopen("rdp_execute.txt", "wt");
#endif

   fbread1_ptr = fbread_func[0];
   fbread2_ptr = fbread2_func[0];
   fbwrite_ptr = fbwrite_func[0];
   fbfill_ptr = fbfill_func[0];
   get_dither_noise_ptr = get_dither_noise_func[0];
   rgb_dither_ptr = rgb_dither_func[0];
   tcdiv_ptr = tcdiv_func[0];
   render_spans_1cycle_ptr = render_spans_1cycle_func[2];
   render_spans_2cycle_ptr = render_spans_2cycle_func[1];

	combiner_rgbsub_a_r[0] = combiner_rgbsub_a_r[1] = &one_color;
	combiner_rgbsub_a_g[0] = combiner_rgbsub_a_g[1] = &one_color;
	combiner_rgbsub_a_b[0] = combiner_rgbsub_a_b[1] = &one_color;
	combiner_rgbsub_b_r[0] = combiner_rgbsub_b_r[1] = &one_color;
	combiner_rgbsub_b_g[0] = combiner_rgbsub_b_g[1] = &one_color;
	combiner_rgbsub_b_b[0] = combiner_rgbsub_b_b[1] = &one_color;
	combiner_rgbmul_r[0] = combiner_rgbmul_r[1] = &one_color;
	combiner_rgbmul_g[0] = combiner_rgbmul_g[1] = &one_color;
	combiner_rgbmul_b[0] = combiner_rgbmul_b[1] = &one_color;
	combiner_rgbadd_r[0] = combiner_rgbadd_r[1] = &one_color;
	combiner_rgbadd_g[0] = combiner_rgbadd_g[1] = &one_color;
	combiner_rgbadd_b[0] = combiner_rgbadd_b[1] = &one_color;

	combiner_alphasub_a[0] = combiner_alphasub_a[1] = &one_color;
	combiner_alphasub_b[0] = combiner_alphasub_b[1] = &one_color;
	combiner_alphamul[0] = combiner_alphamul[1] = &one_color;
	combiner_alphaadd[0] = combiner_alphaadd[1] = &one_color;

	rdp_set_other_modes(0, 0);
	other_modes.f.stalederivs = 1;
	
	memset(TMEM, 0, 0x1000);

	memset(hidden_bits, 3, sizeof(hidden_bits));
	
	

	memset(tile, 0, sizeof(tile));
	
	for (i = 0; i < 8; i++)
	{
		calculate_tile_derivs(i);
		calculate_clamp_diffs(i);
	}

	memset(&combined_color, 0, sizeof(COLOR));
	memset(&prim_color, 0, sizeof(COLOR));
	memset(&env_color, 0, sizeof(COLOR));
	memset(&key_scale, 0, sizeof(COLOR));
	memset(&key_center, 0, sizeof(COLOR));

	rdp_pipeline_crashed = 0;
	memset(&onetimewarnings, 0, sizeof(onetimewarnings));

	precalculate_everything();



#ifdef WIN32
	if (IsBadPtrW32(&rdram[0x7f0000 >> 2],16))
	{
		plim = 0x3fffff;
		idxlim16 = 0x1fffff;
		idxlim32 = 0xfffff;
	}
	else
	{
		plim = 0x7fffff;
		idxlim16 = 0x3fffff;
		idxlim32 = 0x1fffff;
	}
#else
	plim = 0x3fffff;
	idxlim16 = 0x1fffff;
	idxlim32 = 0xfffff;
#endif

	
	rdram_8 = (UINT8*)rdram;
	rdram_16 = (UINT16*)rdram;
	return 0;
}

int rdp_update()
{
    int i, j, dither_filter, fsaa, divot, gamma, gamma_dither, lerp_en, extralines,
	vitype, serration_pulses, gamma_and_dither, ispal, slowbright, linecount, lineshifter,
	twolines, lowerfield, vactivelines, cache_marker_init, validh;
	UINT32 final;
	INT32 v_end, v_sync;

	CCVG *viaa_cache, *viaa_cache_next, *divot_cache, *divot_cache_next;
	CCVG viaa_array[2048];
	CCVG divot_array[2048];
	UINT8 cur_cvg;
	UINT32 frame_buffer, pixels, nextpixels, y_start, y_add, pix, prescale_ptr, prevy;
	CCVG color, nextcolor, scancolor, scannextcolor, *tempccvgptr;
	int r, g, b, xfrac, yfrac, vi_width_low, line_x, next_line_x, prev_line_x, far_line_x,
		cache_marker, cache_next_marker, divot_cache_marker, divot_cache_next_marker,
		prev_scan_x, scan_x, next_scan_x, far_scan_x, prev_x, cur_x, next_x, far_x,
		lerping;
	INT32 *d;

	INT32 hres, vres, v_start, h_start, x_start, x_add, h_end, hrightblank;
	void (*vi_fetch_filter_ptr)(CCVG*, UINT32, UINT32, UINT32, UINT32, UINT32) = vi_fetch_filter_func[vitype & 1];

	final = 0;
	hres = (vi_h_start & 0x3ff) - ((vi_h_start >> 16) & 0x3ff);
		
	vres = (vi_v_start & 0x3ff) - ((vi_v_start >> 16) & 0x3ff);
	vres >>= 1;


	dither_filter = (vi_control >> 16) & 1;
	fsaa = !((vi_control >> 9) & 1);
	divot = (vi_control >> 4) & 1;
	gamma = (vi_control >> 3) & 1;
	gamma_dither = (vi_control >> 2) & 1;
	lerp_en = (((vi_control >> 8) & 3) != 3);
	extralines = !((vi_control >> 8) & 1);
	
	vitype = vi_control & 3;
	serration_pulses = (vi_control >> 6) & 1;
	gamma_and_dither = (gamma << 1) | gamma_dither;
	if ((vi_control >> 5) & 1)
		popmessage("rdp_update: vbus_clock_enable bit set in VI_CONTROL_REG register. Never run this code on your N64! It's rumored that turning this bit on\
					will result in permanent damage to the hardware! Emulation will now continue.\n");

	ispal = (vi_v_sync & 0x3ff) > 550;

	if (!ispal && !lerp_en && vitype == 2 && !onetimewarnings.ntscnolerp)
	{
		popmessage("Hardware tests conducted by marshallh and dragonminded show that at least some of 16-bit NTSC modes are incompatible with disabling VI interpolation. \
					Results on hardware can be unpredictable.");
		onetimewarnings.ntscnolerp = 1;
	}
		
	slowbright = 0;
#ifdef WIN32
	if (GetAsyncKeyState(0x91))
		brightness = (brightness + 1) & 15;
#endif
	slowbright = brightness >> 1;

	v_start = (vi_v_start >> 16) & 0x3ff;
	h_start = ((vi_h_start >> 16) & 0x3ff) - (ispal ? 128 : 108);

	x_start = (vi_x_scale >> 16) & 0xfff;
	x_add = vi_x_scale & 0xfff;

	if (h_start < 0)
	{
		x_start += (x_add * (-h_start));
		h_start = 0;
	}

	cache_marker_init = (x_start >> 10) - 2;
	if (cache_marker_init < -1)
		cache_marker_init = -1;

	v_end = vi_v_start & 0x3ff;
	v_sync = vi_v_sync & 0x3ff;

	
	lowerfield = serration_pulses && (ispal ? v_start < oldvstart : v_start > oldvstart);
	
	if (serration_pulses && v_start == oldvstart)
	{
		serration_pulses = lowerfield = 0;
	}

	linecount = serration_pulses ? (pitchindwords << 1) : pitchindwords;
	lineshifter = serration_pulses ? 0 : 1;
	twolines = serration_pulses ? 1 : 0;

	oldvstart = v_start;

	v_start = (v_start - (ispal ? 47 : 37)) >> 1;
	
	
	
	if (v_start < 0)
		v_start = 0;

	if ((hres + h_start) > PRESCALE_WIDTH)
	{
		hres = PRESCALE_WIDTH - h_start;
	}

	
	

	if ((vres + v_start) > PRESCALE_HEIGHT)
	{
		vres = PRESCALE_HEIGHT - v_start;
		popmessage("vres = %d v_start = %d v_video_start = %d", vres, v_start, (vi_v_start >> 16) & 0x3ff);
	}

	h_end = hres + h_start;
	hrightblank = PRESCALE_WIDTH - h_end;

	vactivelines = (vi_v_sync & 0x3ff) - (ispal ? 47 : 37);
	if (vactivelines > PRESCALE_HEIGHT)
		fatalerror("VI_V_SYNC_REG too big");
	if (vactivelines < 0)
		return 0;
	vactivelines >>= lineshifter;

	validh = (hres >=0 && h_start >=0 && h_start < PRESCALE_WIDTH);

	frame_buffer = vi_origin & 0xffffff;
	
	pixels = 0;
	nextpixels = 0;
	r = 0;
	g = 0;
	b = 0;
	xfrac = 0;
	yfrac = 0; 
	y_start = (vi_y_scale >> 16) & 0xfff;
	y_add = vi_y_scale & 0xfff;
	vi_width_low = vi_width & 0xfff;
	line_x = 0;
	next_line_x = 0;
	prev_line_x = 0;
	far_line_x = 0;
	cache_marker = 0;
	cache_next_marker = 0;
	divot_cache_marker = 0;
	divot_cache_next_marker = 0;
	prev_scan_x = 0;
	scan_x = 0;
	next_scan_x = 0;
	far_scan_x = 0;
	prev_x = 0;
	cur_x = 0;
	next_x = 0;
	far_x = 0;

	
	pix = 0;
	cur_cvg = 0;

	lerping = 0;
	prevy = 0;
	viaa_cache = &viaa_array[0];
	viaa_cache_next = &viaa_array[1024];
	divot_cache = &divot_array[0];
	divot_cache_next = &divot_array[1024];

	d = 0;

	prescale_ptr = v_start * linecount + h_start + (lowerfield ? pitchindwords : 0);
	

	if (hres <= 0 || vres <= 0 || (!(vitype & 2) && prevwasblank))
	{
		return 0;
	}
	
	
	
	
	if (!(vitype & 2))
	{
		memset(tvfadeoutstate, 0, PRESCALE_HEIGHT * sizeof(UINT32));
		for (i = 0; i < PRESCALE_HEIGHT; i++)
			memset(&blitter_buf[i * pitchindwords], 0, PRESCALE_WIDTH * sizeof(INT32)); 
		prevwasblank = 1;
	}
	else
	{
		prevwasblank = 0;
		
		
		if (h_start > 0 && h_start < PRESCALE_WIDTH)
		{
			for (i = 0; i < vactivelines; i++)
				memset(&blitter_buf[i * pitchindwords], 0, h_start * sizeof(UINT32));
		}
		if (h_end >= 0 && h_end < PRESCALE_WIDTH)
		{
			for (i = 0; i < vactivelines; i++)
				memset(&blitter_buf[i * pitchindwords + h_end], 0, hrightblank * sizeof(UINT32));
		}
		for (i = 0; i < ((v_start << twolines) + (lowerfield ? 1 : 0)); i++)
		{
			if (tvfadeoutstate[i])
				tvfadeoutstate[i]--;
			if (!tvfadeoutstate[i] && validh)
				memset(&blitter_buf[i * pitchindwords + h_start], 0, hres * sizeof(UINT32));
		}
		if (!serration_pulses)
		{
			for(j = 0; j < vres; j++)
			{
				tvfadeoutstate[i] = 2;
				i++;
			}
		}
		else
		{
			for(j = 0; j < vres; j++)
			{
				tvfadeoutstate[i] = 2;
				if (tvfadeoutstate[i + 1])
					tvfadeoutstate[i + 1]--;
				if (!tvfadeoutstate[i + 1] && validh)
					memset(&blitter_buf[(i + 1) * pitchindwords + h_start], 0, hres * sizeof(UINT32));
				i += 2;
			}
		}
		for (; i < vactivelines; i++)
		{
			if (tvfadeoutstate[i])
				tvfadeoutstate[i]--;
			if (!tvfadeoutstate[i] && validh)
				memset(&blitter_buf[i * pitchindwords + h_start], 0, hres * sizeof(UINT32));
		}
 	}

	switch (vitype)
	{
		case 0:		
		case 1:
		{
			break;
		}

		case 2:		
		case 3:		
		{
			pixels = 0;
#undef RENDER_CVG_BITS16
#undef RENDER_CVG_BITS32
#undef RENDER_MIN_CVG_ONLY
#undef RENDER_MAX_CVG_ONLY

#undef MONITOR_Z
#undef BW_ZBUFFER
#undef ZBUFF_AS_16B_IATEXTURE

#ifdef MONITOR_Z
			frame_buffer = zb_address;
#endif

			if (frame_buffer)
			{
				for (j = 0; j < vres; j++)
				{
					

					x_start = (vi_x_scale >> 16) & 0xfff;

					if ((y_start >> 10) == (prevy + 1) && j)
					{
						cache_marker = cache_next_marker;
						cache_next_marker = cache_marker_init;
						
						tempccvgptr = viaa_cache;
						viaa_cache = viaa_cache_next;
						viaa_cache_next = tempccvgptr;
						if (divot)
						{
							divot_cache_marker = divot_cache_next_marker;
							divot_cache_next_marker = cache_marker_init;
							tempccvgptr = divot_cache;
							divot_cache = divot_cache_next;
							divot_cache_next = tempccvgptr;
						}
					}
					else if ((y_start >> 10) != prevy || !j)
					{
						cache_marker = cache_next_marker = cache_marker_init;
						if (divot)
							divot_cache_marker = divot_cache_next_marker = cache_marker_init; 
					}

					d = (INT32*)&blitter_buf[prescale_ptr];
					prescale_ptr += linecount;

					prevy = y_start >> 10;
					yfrac = (y_start >> 5) & 0x1f;
					pixels = vi_width_low * prevy;
					nextpixels = pixels + vi_width_low;					

					for (i = 0; i < hres; i++)
					{
						line_x = x_start >> 10;
						prev_line_x = line_x - 1;
						next_line_x = line_x + 1;
						far_line_x = line_x + 2;

						cur_x = pixels + line_x;
						prev_x = pixels + prev_line_x;
						next_x = pixels + next_line_x;
						far_x = pixels + far_line_x;

						scan_x = nextpixels + line_x;
						prev_scan_x = nextpixels + prev_line_x;
						next_scan_x = nextpixels + next_line_x;
						far_scan_x = nextpixels + far_line_x;

						xfrac = (x_start >> 5) & 0x1f;

						lerping = lerp_en && (xfrac || yfrac);

						
						if (prev_line_x > cache_marker)
						{
							vi_fetch_filter_ptr(&viaa_cache[prev_line_x], frame_buffer, prev_x, fsaa, dither_filter, vres);
							vi_fetch_filter_ptr(&viaa_cache[line_x], frame_buffer, cur_x, fsaa, dither_filter, vres);
							vi_fetch_filter_ptr(&viaa_cache[next_line_x], frame_buffer, next_x, fsaa, dither_filter, vres);
							cache_marker = next_line_x;
						}
						else if (line_x > cache_marker)
						{
							vi_fetch_filter_ptr(&viaa_cache[line_x], frame_buffer, cur_x, fsaa, dither_filter, vres);
							vi_fetch_filter_ptr(&viaa_cache[next_line_x], frame_buffer, next_x, fsaa, dither_filter, vres);
							cache_marker = next_line_x;
						}
						else if (next_line_x > cache_marker)
						{
							vi_fetch_filter_ptr(&viaa_cache[next_line_x], frame_buffer, next_x, fsaa, dither_filter, vres);
							cache_marker = next_line_x;
						}

						if (prev_line_x > cache_next_marker)
						{
							vi_fetch_filter_ptr(&viaa_cache_next[prev_line_x], frame_buffer, prev_scan_x, fsaa, dither_filter, vres);
							vi_fetch_filter_ptr(&viaa_cache_next[line_x], frame_buffer, scan_x, fsaa, dither_filter, vres);
							vi_fetch_filter_ptr(&viaa_cache_next[next_line_x], frame_buffer, next_scan_x, fsaa, dither_filter, vres);
							cache_next_marker = next_line_x;
						}
						else if (line_x > cache_next_marker)
						{
							vi_fetch_filter_ptr(&viaa_cache_next[line_x], frame_buffer, scan_x, fsaa, dither_filter, vres);
							vi_fetch_filter_ptr(&viaa_cache_next[next_line_x], frame_buffer, next_scan_x, fsaa, dither_filter, vres);
							cache_next_marker = next_line_x;
						}
						else if (next_line_x > cache_next_marker)
						{
							vi_fetch_filter_ptr(&viaa_cache_next[next_line_x], frame_buffer, next_scan_x, fsaa, dither_filter, vres);
							cache_next_marker = next_line_x;
						}
						
						
						if (divot)
						{
							if (far_line_x > cache_marker)
							{
								vi_fetch_filter_ptr(&viaa_cache[far_line_x], frame_buffer, far_x, fsaa, dither_filter, vres);
								cache_marker = far_line_x;
							}

							if (far_line_x > cache_next_marker)
							{
								vi_fetch_filter_ptr(&viaa_cache_next[far_line_x], frame_buffer, far_scan_x, fsaa, dither_filter, vres);
								cache_next_marker = far_line_x;
							}

							if (line_x > divot_cache_marker)
							{
								divot_filter(&divot_cache[line_x], viaa_cache[line_x], viaa_cache[prev_line_x], viaa_cache[next_line_x]);
								divot_filter(&divot_cache[next_line_x], viaa_cache[next_line_x], viaa_cache[line_x], viaa_cache[far_line_x]);
								divot_cache_marker = next_line_x;
							}
							else if (next_line_x > divot_cache_marker)
							{
								divot_filter(&divot_cache[next_line_x], viaa_cache[next_line_x], viaa_cache[line_x], viaa_cache[far_line_x]);
								divot_cache_marker = next_line_x;
							}

							if (line_x > divot_cache_next_marker)
							{
								divot_filter(&divot_cache_next[line_x], viaa_cache_next[line_x], viaa_cache_next[prev_line_x], viaa_cache_next[next_line_x]);
								divot_filter(&divot_cache_next[next_line_x], viaa_cache_next[next_line_x], viaa_cache_next[line_x], viaa_cache_next[far_line_x]);
								divot_cache_next_marker = next_line_x;
							}
							else if (next_line_x > divot_cache_next_marker)
							{
								divot_filter(&divot_cache_next[next_line_x], viaa_cache_next[next_line_x], viaa_cache_next[line_x], viaa_cache_next[far_line_x]);
								divot_cache_next_marker = next_line_x;
							}

							color = divot_cache[line_x];

						}
						else
						{
							color = viaa_cache[line_x];
						}

						if (lerping)
						{
							if (divot)
							{
								nextcolor = divot_cache[next_line_x];
								scancolor = divot_cache_next[line_x];
								scannextcolor = divot_cache_next[next_line_x];
							}
							else
							{
								nextcolor = viaa_cache[next_line_x];
								scancolor = viaa_cache_next[line_x];
								scannextcolor = viaa_cache_next[next_line_x];
							}

							
							
							vi_vl_lerp(&color, scancolor, yfrac);
							vi_vl_lerp(&nextcolor, scannextcolor, yfrac);
							vi_vl_lerp(&color, nextcolor, xfrac);
						}

						r = color.r;
						g = color.g;
						b = color.b;

						gamma_filters(&r, &g, &b, gamma_and_dither);
						
						
#ifdef BW_ZBUFFER
						UINT32 tempz = RREADIDX16((frame_buffer >> 1) + cur_x);
						pix = tempz;
						
						r = g = b = pix >> 8;

#endif
#ifdef ZBUFF_AS_16B_IATEXTURE
						r = g = b = (((pix >> 8) & 0xff) * (pix & 0xff)) >> 8;
#endif
#ifdef RENDER_CVG_BITS16
						
						r = g = b = cur_cvg << 5;
#endif
#ifdef RENDER_CVG_BITS32
						
						r = g = b = cur_cvg << 5;
#endif
#ifdef RENDER_MIN_CVG_ONLY
						if (!cur_cvg)
							r = g = b = 0;
						else
							r = g =  b = 0xff;
#endif
#ifdef RENDER_MAX_CVG_ONLY
						if (cur_cvg != 7)
							r = g = b = 0;
						else
							r = g = b = 0xff;
#endif
						adjust_brightness(&r, &g, &b, slowbright);
						d[i] = (r << 16) | (g << 8) | b;
						x_start += x_add;

					}
					y_start += y_add;
					
					
				}
			}
			break;
		}
        default:    popmessage("Unknown framebuffer format %d\n", vi_control & 0x3);
	}
	
	

	
	
/*
	int visiblelines = (ispal ? 576 : 480) >> lineshifter;

	src.bottom = visiblelines;

	if (dst.left < dst.right && dst.top < dst.bottom)
	{
		res = IDirectDrawSurface_Blt(lpddsprimary, &dst, lpddsback, &src, DDBLT_WAIT, 0);
		if (res == DDERR_SURFACELOST)
		{
			while(1){
			res = IDirectDraw4_RestoreAllSurfaces(lpdd);
			if (res != DD_OK)
				fatalerror("RestoreAllSurfaces failed with DirectDraw error %x", res);
			res = IDirectDrawSurface_Blt(lpddsprimary, &dst, lpddsback, &src, DDBLT_WAIT, 0);		
			if (res != DDERR_SURFACELOST)
				break;
			}
		}
		else if (res != DD_OK && res != DDERR_GENERIC)
			fatalerror("Scaled blit failed with DirectDraw error %x", res);
		
	}
	*/

	
	

	
	return 0;
}


STRICTINLINE void vi_fetch_filter16(CCVG* res, UINT32 fboffset, UINT32 cur_x, UINT32 fsaa, UINT32 dither_filter, UINT32 vres)
{
	int r, g, b;
	UINT32 idx, cur_cvg, pix, hval, fbw;
    idx = (fboffset >> 1) + cur_x;
	PAIRREAD16(pix, hval, idx); 
	if (fsaa)
		cur_cvg = ((pix & 1) << 2) | hval;
	else
		cur_cvg = 7;
	r = GET_HI(pix);
	g = GET_MED(pix);
	b = GET_LOW(pix);

	fbw = vi_width & 0xfff;

	if (cur_cvg == 7)
	{
		if (dither_filter)
			restore_filter16(&r, &g, &b, fboffset, cur_x, fbw);
	}
	else
	{
		video_filter16(&r, &g, &b, fboffset, cur_x, fbw, cur_cvg);
	}


	res->r = r;
	res->g = g;
	res->b = b;
	res->cvg = cur_cvg;
}

STRICTINLINE void vi_fetch_filter32(CCVG* res, UINT32 fboffset, UINT32 cur_x, UINT32 fsaa, UINT32 dither_filter, UINT32 vres)
{
	int r, g, b;
	UINT32 pix, cur_cvg, fbw;
    pix = RREADIDX32((fboffset >> 2) + cur_x);

	if (fsaa)
		cur_cvg = (pix >> 5) & 7;
	else
		cur_cvg = 7;
	r = (pix >> 24) & 0xff;
	g = (pix >> 16) & 0xff;
	b = (pix >> 8) & 0xff;

	fbw = vi_width & 0xfff;
	
	if (cur_cvg == 7)
	{
		if (dither_filter)
			restore_filter32(&r, &g, &b, fboffset, cur_x, fbw);
	}
	else
	{
		video_filter32(&r, &g, &b, fboffset, cur_x, fbw, cur_cvg);
	}

	res->r = r;
	res->g = g;
	res->b = b;
	res->cvg = cur_cvg;
}



STRICTINLINE void SET_SUBA_RGB_INPUT(INT32 **input_r, INT32 **input_g, INT32 **input_b, int code)
{
	switch (code & 0xf)
	{
		case 0:		*input_r = &combined_color.r;	*input_g = &combined_color.g;	*input_b = &combined_color.b;	break;
		case 1:		*input_r = &texel0_color.r;		*input_g = &texel0_color.g;		*input_b = &texel0_color.b;		break;
		case 2:		*input_r = &texel1_color.r;		*input_g = &texel1_color.g;		*input_b = &texel1_color.b;		break;
		case 3:		*input_r = &prim_color.r;		*input_g = &prim_color.g;		*input_b = &prim_color.b;		break;
		case 4:		*input_r = &shade_color.r;		*input_g = &shade_color.g;		*input_b = &shade_color.b;		break;
		case 5:		*input_r = &env_color.r;		*input_g = &env_color.g;		*input_b = &env_color.b;		break;
		case 6:		*input_r = &one_color;			*input_g = &one_color;			*input_b = &one_color;		break;
		case 7:		*input_r = &noise;				*input_g = &noise;				*input_b = &noise;				break;
		case 8: case 9: case 10: case 11: case 12: case 13: case 14: case 15:
		{
			*input_r = &zero_color;		*input_g = &zero_color;		*input_b = &zero_color;		break;
		}
	}
}

STRICTINLINE void SET_SUBB_RGB_INPUT(INT32 **input_r, INT32 **input_g, INT32 **input_b, int code)
{
	switch (code & 0xf)
	{
		case 0:		*input_r = &combined_color.r;	*input_g = &combined_color.g;	*input_b = &combined_color.b;	break;
		case 1:		*input_r = &texel0_color.r;		*input_g = &texel0_color.g;		*input_b = &texel0_color.b;		break;
		case 2:		*input_r = &texel1_color.r;		*input_g = &texel1_color.g;		*input_b = &texel1_color.b;		break;
		case 3:		*input_r = &prim_color.r;		*input_g = &prim_color.g;		*input_b = &prim_color.b;		break;
		case 4:		*input_r = &shade_color.r;		*input_g = &shade_color.g;		*input_b = &shade_color.b;		break;
		case 5:		*input_r = &env_color.r;		*input_g = &env_color.g;		*input_b = &env_color.b;		break;
		case 6:		*input_r = &key_center.r;		*input_g = &key_center.g;		*input_b = &key_center.b;		break;
		case 7:		*input_r = &k4;					*input_g = &k4;					*input_b = &k4;					break;
		case 8: case 9: case 10: case 11: case 12: case 13: case 14: case 15:
		{
			*input_r = &zero_color;		*input_g = &zero_color;		*input_b = &zero_color;		break;
		}
	}
}

STRICTINLINE void SET_MUL_RGB_INPUT(INT32 **input_r, INT32 **input_g, INT32 **input_b, int code)
{
	switch (code & 0x1f)
	{
		case 0:		*input_r = &combined_color.r;	*input_g = &combined_color.g;	*input_b = &combined_color.b;	break;
		case 1:		*input_r = &texel0_color.r;		*input_g = &texel0_color.g;		*input_b = &texel0_color.b;		break;
		case 2:		*input_r = &texel1_color.r;		*input_g = &texel1_color.g;		*input_b = &texel1_color.b;		break;
		case 3:		*input_r = &prim_color.r;		*input_g = &prim_color.g;		*input_b = &prim_color.b;		break;
		case 4:		*input_r = &shade_color.r;		*input_g = &shade_color.g;		*input_b = &shade_color.b;		break;
		case 5:		*input_r = &env_color.r;		*input_g = &env_color.g;		*input_b = &env_color.b;		break;
		case 6:		*input_r = &key_scale.r;		*input_g = &key_scale.g;		*input_b = &key_scale.b;		break;
		case 7:		*input_r = &combined_color.a;	*input_g = &combined_color.a;	*input_b = &combined_color.a;	break;
		case 8:		*input_r = &texel0_color.a;		*input_g = &texel0_color.a;		*input_b = &texel0_color.a;		break;
		case 9:		*input_r = &texel1_color.a;		*input_g = &texel1_color.a;		*input_b = &texel1_color.a;		break;
		case 10:	*input_r = &prim_color.a;		*input_g = &prim_color.a;		*input_b = &prim_color.a;		break;
		case 11:	*input_r = &shade_color.a;		*input_g = &shade_color.a;		*input_b = &shade_color.a;		break;
		case 12:	*input_r = &env_color.a;		*input_g = &env_color.a;		*input_b = &env_color.a;		break;
		case 13:	*input_r = &lod_frac;			*input_g = &lod_frac;			*input_b = &lod_frac;			break;
		case 14:	*input_r = &primitive_lod_frac;	*input_g = &primitive_lod_frac;	*input_b = &primitive_lod_frac; break;
		case 15:	*input_r = &k5;					*input_g = &k5;					*input_b = &k5;					break;
		case 16: case 17: case 18: case 19: case 20: case 21: case 22: case 23:
		case 24: case 25: case 26: case 27: case 28: case 29: case 30: case 31:
		{
			*input_r = &zero_color;		*input_g = &zero_color;		*input_b = &zero_color;		break;
		}
	}
}

STRICTINLINE void SET_ADD_RGB_INPUT(INT32 **input_r, INT32 **input_g, INT32 **input_b, int code)
{
	switch (code & 0x7)
	{
		case 0:		*input_r = &combined_color.r;	*input_g = &combined_color.g;	*input_b = &combined_color.b;	break;
		case 1:		*input_r = &texel0_color.r;		*input_g = &texel0_color.g;		*input_b = &texel0_color.b;		break;
		case 2:		*input_r = &texel1_color.r;		*input_g = &texel1_color.g;		*input_b = &texel1_color.b;		break;
		case 3:		*input_r = &prim_color.r;		*input_g = &prim_color.g;		*input_b = &prim_color.b;		break;
		case 4:		*input_r = &shade_color.r;		*input_g = &shade_color.g;		*input_b = &shade_color.b;		break;
		case 5:		*input_r = &env_color.r;		*input_g = &env_color.g;		*input_b = &env_color.b;		break;
		case 6:		*input_r = &one_color;			*input_g = &one_color;			*input_b = &one_color;			break;
		case 7:		*input_r = &zero_color;			*input_g = &zero_color;			*input_b = &zero_color;			break;
	}
}

STRICTINLINE void SET_SUB_ALPHA_INPUT(INT32 **input, int code)
{
	switch (code & 0x7)
	{
		case 0:		*input = &combined_color.a; break;
		case 1:		*input = &texel0_color.a; break;
		case 2:		*input = &texel1_color.a; break;
		case 3:		*input = &prim_color.a; break;
		case 4:		*input = &shade_color.a; break;
		case 5:		*input = &env_color.a; break;
		case 6:		*input = &one_color; break;
		case 7:		*input = &zero_color; break;
	}
}

STRICTINLINE void SET_MUL_ALPHA_INPUT(INT32 **input, int code)
{
	switch (code & 0x7)
	{
		case 0:		*input = &lod_frac; break;
		case 1:		*input = &texel0_color.a; break;
		case 2:		*input = &texel1_color.a; break;
		case 3:		*input = &prim_color.a; break;
		case 4:		*input = &shade_color.a; break;
		case 5:		*input = &env_color.a; break;
		case 6:		*input = &primitive_lod_frac; break;
		case 7:		*input = &zero_color; break;
	}
}

STRICTINLINE void combiner_1cycle(int adseed, UINT32* curpixel_cvg)
{

	INT32 redkey, greenkey, bluekey, temp;

	

	
	combined_color.r = color_combiner_equation(*combiner_rgbsub_a_r[1],*combiner_rgbsub_b_r[1],*combiner_rgbmul_r[1],*combiner_rgbadd_r[1]);
	combined_color.g = color_combiner_equation(*combiner_rgbsub_a_g[1],*combiner_rgbsub_b_g[1],*combiner_rgbmul_g[1],*combiner_rgbadd_g[1]);
	combined_color.b = color_combiner_equation(*combiner_rgbsub_a_b[1],*combiner_rgbsub_b_b[1],*combiner_rgbmul_b[1],*combiner_rgbadd_b[1]);
	combined_color.a = alpha_combiner_equation(*combiner_alphasub_a[1],*combiner_alphasub_b[1],*combiner_alphamul[1],*combiner_alphaadd[1]);

	pixel_color.a = special_9bit_clamptable[combined_color.a];
	if (pixel_color.a == 0xff)
		pixel_color.a = 0x100;

	if (!other_modes.key_en)
	{
		
		combined_color.r >>= 8;
		combined_color.g >>= 8;
		combined_color.b >>= 8;
		pixel_color.r = special_9bit_clamptable[combined_color.r];
		pixel_color.g = special_9bit_clamptable[combined_color.g];
		pixel_color.b = special_9bit_clamptable[combined_color.b];
	}
	else
	{
		redkey = SIGN(combined_color.r, 17);
		if (redkey >= 0)
			redkey = (key_width.r << 4) - redkey;
		else
			redkey = (key_width.r << 4) + redkey;
		greenkey = SIGN(combined_color.g, 17);
		if (greenkey >= 0)
			greenkey = (key_width.g << 4) - greenkey;
		else
			greenkey = (key_width.g << 4) + greenkey;
		bluekey = SIGN(combined_color.b, 17);
		if (bluekey >= 0)
			bluekey = (key_width.b << 4) - bluekey;
		else
			bluekey = (key_width.b << 4) + bluekey;
		keyalpha = (redkey < greenkey) ? redkey : greenkey;
		keyalpha = (bluekey < keyalpha) ? bluekey : keyalpha;
		keyalpha = CLIP(keyalpha, 0, 0xff);

		
		pixel_color.r = special_9bit_clamptable[*combiner_rgbsub_a_r[1]];
		pixel_color.g = special_9bit_clamptable[*combiner_rgbsub_a_g[1]];
		pixel_color.b = special_9bit_clamptable[*combiner_rgbsub_a_b[1]];

		
		combined_color.r >>= 8;
		combined_color.g >>= 8;
		combined_color.b >>= 8;
	}
	
	
	if (other_modes.cvg_times_alpha)
	{
		temp = (pixel_color.a * (*curpixel_cvg) + 4) >> 3;
		*curpixel_cvg = (temp >> 5) & 0xf;
	}

	if (!other_modes.alpha_cvg_select)
	{	
		if (!other_modes.key_en)
		{
			pixel_color.a += adseed;
			if (pixel_color.a & 0x100)
				pixel_color.a = 0xff;
		}
		else
			pixel_color.a = keyalpha;
	}
	else
	{
		if (other_modes.cvg_times_alpha)
			pixel_color.a = temp;
		else
			pixel_color.a = (*curpixel_cvg) << 5;
		if (pixel_color.a > 0xff)
			pixel_color.a = 0xff;
	}
	

	shade_color.a += adseed;
	if (shade_color.a & 0x100)
		shade_color.a = 0xff;
}

STRICTINLINE void combiner_2cycle(int adseed, UINT32* curpixel_cvg)
{
	INT32 redkey, greenkey, bluekey, temp;

	combined_color.r = color_combiner_equation(*combiner_rgbsub_a_r[0],*combiner_rgbsub_b_r[0],*combiner_rgbmul_r[0],*combiner_rgbadd_r[0]);
	combined_color.g = color_combiner_equation(*combiner_rgbsub_a_g[0],*combiner_rgbsub_b_g[0],*combiner_rgbmul_g[0],*combiner_rgbadd_g[0]);
	combined_color.b = color_combiner_equation(*combiner_rgbsub_a_b[0],*combiner_rgbsub_b_b[0],*combiner_rgbmul_b[0],*combiner_rgbadd_b[0]);
	combined_color.a = alpha_combiner_equation(*combiner_alphasub_a[0],*combiner_alphasub_b[0],*combiner_alphamul[0],*combiner_alphaadd[0]);

	
	

	
	combined_color.r >>= 8;
	combined_color.g >>= 8;
	combined_color.b >>= 8;

	
	texel0_color = texel1_color;
	texel1_color = nexttexel_color;

	
	
	
	
	
	
	

	combined_color.r = color_combiner_equation(*combiner_rgbsub_a_r[1],*combiner_rgbsub_b_r[1],*combiner_rgbmul_r[1],*combiner_rgbadd_r[1]);
	combined_color.g = color_combiner_equation(*combiner_rgbsub_a_g[1],*combiner_rgbsub_b_g[1],*combiner_rgbmul_g[1],*combiner_rgbadd_g[1]);
	combined_color.b = color_combiner_equation(*combiner_rgbsub_a_b[1],*combiner_rgbsub_b_b[1],*combiner_rgbmul_b[1],*combiner_rgbadd_b[1]);
	combined_color.a = alpha_combiner_equation(*combiner_alphasub_a[1],*combiner_alphasub_b[1],*combiner_alphamul[1],*combiner_alphaadd[1]);

	if (!other_modes.key_en)
	{
		
		combined_color.r >>= 8;
		combined_color.g >>= 8;
		combined_color.b >>= 8;

		pixel_color.r = special_9bit_clamptable[combined_color.r];
		pixel_color.g = special_9bit_clamptable[combined_color.g];
		pixel_color.b = special_9bit_clamptable[combined_color.b];
	}
	else
	{
		redkey = SIGN(combined_color.r, 17);
		if (redkey >= 0)
			redkey = (key_width.r << 4) - redkey;
		else
			redkey = (key_width.r << 4) + redkey;
		greenkey = SIGN(combined_color.g, 17);
		if (greenkey >= 0)
			greenkey = (key_width.g << 4) - greenkey;
		else
			greenkey = (key_width.g << 4) + greenkey;
		bluekey = SIGN(combined_color.b, 17);
		if (bluekey >= 0)
			bluekey = (key_width.b << 4) - bluekey;
		else
			bluekey = (key_width.b << 4) + bluekey;
		keyalpha = (redkey < greenkey) ? redkey : greenkey;
		keyalpha = (bluekey < keyalpha) ? bluekey : keyalpha;
		keyalpha = CLIP(keyalpha, 0, 0xff);

		
		pixel_color.r = special_9bit_clamptable[*combiner_rgbsub_a_r[1]];
		pixel_color.g = special_9bit_clamptable[*combiner_rgbsub_a_g[1]];
		pixel_color.b = special_9bit_clamptable[*combiner_rgbsub_a_b[1]];

		
		combined_color.r >>= 8;
		combined_color.g >>= 8;
		combined_color.b >>= 8;
	}
	
	pixel_color.a = special_9bit_clamptable[combined_color.a];
	if (pixel_color.a == 0xff)
		pixel_color.a = 0x100;

	
	if (other_modes.cvg_times_alpha)
	{
		temp = (pixel_color.a * (*curpixel_cvg) + 4) >> 3;
		*curpixel_cvg = (temp >> 5) & 0xf;
	}

	if (!other_modes.alpha_cvg_select)
	{
		if (!other_modes.key_en)
		{
			pixel_color.a += adseed;
			if (pixel_color.a & 0x100)
				pixel_color.a = 0xff;
		}
		else
			pixel_color.a = keyalpha;
	}
	else
	{
		if (other_modes.cvg_times_alpha)
			pixel_color.a = temp;
		else
			pixel_color.a = (*curpixel_cvg) << 5;
		if (pixel_color.a > 0xff)
			pixel_color.a = 0xff;
	}
	

	shade_color.a += adseed;
	if (shade_color.a & 0x100)
		shade_color.a = 0xff;
}

STRICTINLINE void precalculate_everything(void)
{
	UINT32 exponent, mantissa;
	int temppoint, tempslope, normout, wnorm, shift, tlu_rcp, i, k, j,
		d, n, temp, res, invd, nbit, ps[9];
	k = 0;
	j = 0;
	
	for (i = 0; i < 256; i++)
	{
		gamma_table[i] = vi_integer_sqrt(i << 6);
		gamma_table[i] <<= 1;
	}
	for (i = 0; i < 0x4000; i++)
	{
		gamma_dither_table[i] = vi_integer_sqrt(i);
		gamma_dither_table[i] <<= 1;
	}
	
	z_build_com_table();

	for (i = 0; i < 0x4000; i++)
	{
		exponent = (i >> 11) & 7;
		mantissa = i & 0x7ff;
		z_complete_dec_table[i] = ((mantissa << z_dec_table[exponent].shift) + z_dec_table[exponent].add) & 0x3ffff;
	}
	
	precalc_cvmask_derivatives();

	i = 0;
	log2table[0] = log2table[1] = 0;
	for (i = 2; i < 256; i++)
	{
		for (k = 7; k > 0; k--)
		{
			if((i >> k) & 1)
			{
				log2table[i] = k;
				break;
			}
		}
	}
	
	for (i = 0; i < 0x400; i++)
	{
		if (((i >> 5) & 0x1f) > (i & 0x1f))
			vi_restore_table[i] = 1;
		else if (((i >> 5) & 0x1f) < (i & 0x1f))
			vi_restore_table[i] = -1;
		else
			vi_restore_table[i] = 0;
	}
	
	for (i = 0; i < 32; i++)
		replicated_rgba[i] = (i << 3) | ((i >> 2) & 7); 
	
	maskbits_table[0] = 0x3ff;
	for (i = 1; i < 16; i++)
		maskbits_table[i] = ((UINT16)(0xffff) >> (16 - i)) & 0x3ff;
	

	
	
	for(i = 0; i < 0x200; i++)
	{
		switch((i >> 7) & 3)
		{
		case 0:
		case 1:
			special_9bit_clamptable[i] = i & 0xff;
			break;
		case 2:
			special_9bit_clamptable[i] = 0xff;
			break;
		case 3:
			special_9bit_clamptable[i] = 0;
			break;
		}
	}

	
	
	for(i = 0; i < 0x200; i++)
	{
		special_9bit_exttable[i] = ((i & 0x180) == 0x180) ? (i | ~0x1ff) : (i & 0x1ff);
	}

	for (i = 0; i < 0x8000; i++)
	{
		for (k = 1; k <= 14 && !((i << k) & 0x8000); k++) 
			;
		shift = k - 1;
		normout = (i << shift) & 0x3fff;
		wnorm = (normout & 0xff) << 2;
		normout >>= 8;

		
		
		temppoint = norm_point_table[normout];
		tempslope = norm_slope_table[normout];

		tempslope = (tempslope | ~0x3ff) + 1;
		
		tlu_rcp = (((tempslope * wnorm) >> 10) + temppoint) & 0x7fff;
		
		tcdiv_table[i] = shift | (tlu_rcp << 4);
	}

	
	d = 0;
	n = 0;
	temp = 0;
	res = 0;
	invd = 0;
	nbit = 0;

	for (i = 0; i < 0x8000; i++)
	{
		res = 0;
		d = (i >> 11) & 0xf;
		n = i & 0x7ff;
		invd = (~d) & 0xf;
		

		temp = invd + (n >> 8) + 1;
		ps[0] = temp & 7;
		for (k = 0; k < 8; k++)
		{
			nbit = (n >> (7 - k)) & 1;
			if (res & (0x100 >> k))
				temp = invd + (ps[k] << 1) + nbit + 1;
			else
				temp = d + (ps[k] << 1) + nbit;
			ps[k + 1] = temp & 7;
			if (temp & 0x10)
				res |= (1 << (7 - k));
		}
		bldiv_hwaccurate_table[i] = res;
	}
}

STRICTINLINE void SET_BLENDER_INPUT(int cycle, int which, INT32 **input_r, INT32 **input_g, INT32 **input_b, INT32 **input_a, int a, int b)
{

	switch (a & 0x3)
	{
		case 0:
		{
			if (cycle == 0)
			{
				*input_r = &pixel_color.r;
				*input_g = &pixel_color.g;
				*input_b = &pixel_color.b;
			}
			else
			{
				*input_r = &blended_pixel_color.r;
				*input_g = &blended_pixel_color.g;
				*input_b = &blended_pixel_color.b;
			}
			break;
		}

		case 1:
		{
			*input_r = &memory_color.r;
			*input_g = &memory_color.g;
			*input_b = &memory_color.b;
			break;
		}

		case 2:
		{
			*input_r = &blend_color.r;		*input_g = &blend_color.g;		*input_b = &blend_color.b;
			break;
		}

		case 3:
		{
			*input_r = &fog_color.r;		*input_g = &fog_color.g;		*input_b = &fog_color.b;
			break;
		}
	}

	if (which == 0)
	{
		switch (b & 0x3)
		{
			case 0:		*input_a = &pixel_color.a; break;
			case 1:		*input_a = &fog_color.a; break;
			case 2:		*input_a = &shade_color.a; break;
			case 3:		*input_a = &zero_color; break;
		}
	}
	else
	{
		switch (b & 0x3)
		{
			case 0:		*input_a = &inv_pixel_color.a; break;
			case 1:		*input_a = &memory_color.a; break;
			case 2:		*input_a = &blenderone; break;
			case 3:		*input_a = &zero_color; break;
		}
	}
}



static const UINT8 bayer_matrix[16] =
{
	 0,  4,  1, 5,
	 4,  0,  5, 1,
	 3,	 7,  2, 6,
	 7,  3,  6, 2
};


static const UINT8 magic_matrix[16] =
{
	 0,  6,  1, 7,
	 4,  2,  5, 3,
	 3,	 5,  2, 4,
	 7,  1,  6, 0
};

STRICTINLINE int blender_1cycle(UINT32* fr, UINT32* fg, UINT32* fb, int dith, UINT32 blend_en, UINT32 prewrap, UINT32 curpixel_cvg, UINT32 curpixel_cvbit)
{
	int r, g, b, dontblend;
	
	
	if (alpha_compare(pixel_color.a))
	{

		

		
		
		
		if (other_modes.antialias_en ? (curpixel_cvg) : (curpixel_cvbit))
		{

			if (!other_modes.color_on_cvg || prewrap)
			{
				dontblend = (other_modes.f.partialreject_1cycle && pixel_color.a >= 0xff);
				if (!blend_en || dontblend)
				{
					r = *blender1a_r[0];
					g = *blender1a_g[0];
					b = *blender1a_b[0];
				}
				else
				{
					inv_pixel_color.a =  (~(*blender1b_a[0])) & 0xff;
					
					
					
					

					blender_equation_cycle0(&r, &g, &b);
				}
			}
			else
			{
				r = *blender2a_r[0];
				g = *blender2a_g[0];
				b = *blender2a_b[0];
			}

			rgb_dither_ptr(&r, &g, &b, dith);
			*fr = r;
			*fg = g;
			*fb = b;
			return 1;
		}
		else 
			return 0;
		}
	else 
		return 0;
}

STRICTINLINE int blender_2cycle(UINT32* fr, UINT32* fg, UINT32* fb, int dith, UINT32 blend_en, UINT32 prewrap, UINT32 curpixel_cvg, UINT32 curpixel_cvbit)
{
	int r, g, b, dontblend;

	
	if (alpha_compare(pixel_color.a))
	{
		if (other_modes.antialias_en ? (curpixel_cvg) : (curpixel_cvbit))
		{
			
			inv_pixel_color.a =  (~(*blender1b_a[0])) & 0xff;

			blender_equation_cycle0_2(&r, &g, &b);

			
			memory_color = pre_memory_color;

			blended_pixel_color.r = r;
			blended_pixel_color.g = g;
			blended_pixel_color.b = b;
			blended_pixel_color.a = pixel_color.a;

			if (!other_modes.color_on_cvg || prewrap)
			{
				dontblend = (other_modes.f.partialreject_2cycle && pixel_color.a >= 0xff);
				if (!blend_en || dontblend)
				{
					r = *blender1a_r[1];
					g = *blender1a_g[1];
					b = *blender1a_b[1];
				}
				else
				{
					inv_pixel_color.a =  (~(*blender1b_a[1])) & 0xff;
					blender_equation_cycle1(&r, &g, &b);
				}
			}
			else
			{
				r = *blender2a_r[1];
				g = *blender2a_g[1];
				b = *blender2a_b[1];
			}

			
			rgb_dither_ptr(&r, &g, &b, dith);
			*fr = r;
			*fg = g;
			*fb = b;
			return 1;
		}
		else 
			return 0;
	}
	else 
		return 0;
}



STRICTINLINE void fetch_texel(COLOR *color, int s, int t, UINT32 tilenum)
{
	UINT32 tbase = tile[tilenum].line * t + tile[tilenum].tmem;
	

	UINT32 tpal	= tile[tilenum].palette;

	
	
	
	
	
	
	
	UINT16 *tc16 = (UINT16*)TMEM;
	UINT32 taddr = 0;

	

	

	switch (tile[tilenum].f.notlutswitch)
	{
	case TEXEL_RGBA4:
		{
			UINT8 byteval, c;
			taddr = ((tbase << 4) + s) >> 1;
			taddr ^= ((t & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR); 

			byteval = TMEM[taddr & 0xfff];
			c = ((s & 1)) ? (byteval & 0xf) : (byteval >> 4);
			c |= (c << 4);
			color->r = c;
			color->g = c;
			color->b = c;
			color->a = c;
		}
		break;
	case TEXEL_RGBA8:
		{
			UINT8 p;
			taddr = (tbase << 3) + s;
			taddr ^= ((t & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR);

			p = TMEM[taddr & 0xfff];
			color->r = p;
			color->g = p;
			color->b = p;
			color->a = p;
		}
		break;
	case TEXEL_RGBA16:
		{
			UINT16 c;
			taddr = (tbase << 2) + s;
			taddr ^= ((t & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR);
					
			c = tc16[taddr & 0x7ff];
			color->r = GET_HI_RGBA16_TMEM(c);
			color->g = GET_MED_RGBA16_TMEM(c);
			color->b = GET_LOW_RGBA16_TMEM(c);
			color->a = (c & 1) ? 0xff : 0;
		}
		break;
	case TEXEL_RGBA32:
		{
			UINT16 c;
			
			taddr = (tbase << 2) + s;
			taddr ^= ((t & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR);
			
			taddr &= 0x3ff;
			c = tc16[taddr];
			color->r = c >> 8;
			color->g = c & 0xff;
			c = tc16[taddr | 0x400];
			color->b = c >> 8;
			color->a = c & 0xff;
		}
		break;
	case TEXEL_YUV4:
	case TEXEL_YUV8:
		{
			UINT16 c;
			INT32 u, save;
			int taddrlow;

			taddr = (tbase << 3) + s;
			taddrlow = taddr >> 1;

			taddrlow ^= ((t & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR);

			taddrlow &= 0x3ff;
					
			c = tc16[taddrlow];
					
			save = u = c >> 8;
			
			u ^= 0x80;
			if (u & 0x80)
				u |= 0x100;

			color->r = u;
			color->g = u;
			color->b = save;
			color->a = save;
		}
		break;
	case TEXEL_YUV16:
	case TEXEL_YUV32:
		{
			int taddrlow;
			UINT16 c;
			INT32 y, u, v;

			taddr = (tbase << 3) + s;
			taddrlow = taddr >> 1;

			taddr ^= ((t & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR);
			taddrlow ^= ((t & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR);
					
			taddr &= 0x7ff;
			taddrlow &= 0x3ff;
					
			c = tc16[taddrlow];
					
			y = TMEM[taddr | 0x800];
			u = c >> 8;
			v = c & 0xff;

			v ^= 0x80; u ^= 0x80;
			if (v & 0x80)
				v |= 0x100;
			if (u & 0x80)
				u |= 0x100;
			
			

			color->r = u;
			color->g = v;
			color->b = y;
			color->a = y;
		}
		break;
	case TEXEL_CI4:
		{
			UINT8 p;
			taddr = ((tbase << 4) + s) >> 1;
			taddr ^= ((t & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR);
			
			p = TMEM[taddr & 0xfff];
			p = (s & 1) ? (p & 0xf) : (p >> 4);
			p = (tpal << 4) | p;
			color->r = color->g = color->b = color->a = p;
		}
		break;
	case TEXEL_CI8:
		{
			UINT8 p;
			taddr = (tbase << 3) + s;
			taddr ^= ((t & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR);
			
			p = TMEM[taddr & 0xfff];
			color->r = p;
			color->g = p;
			color->b = p;
			color->a = p;
		}
		break;
	case TEXEL_CI16:
		{
			UINT16 c;

			taddr = (tbase << 2) + s;
			taddr ^= ((t & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR);
										
			c = tc16[taddr & 0x7ff];
			color->r = c >> 8;
			color->g = c & 0xff;
			color->b = color->r;
			color->a = (c & 1) ? 0xff : 0;
		}
		break;
	case TEXEL_CI32:
		{
			UINT16 c;
			taddr = (tbase << 2) + s;
			taddr ^= ((t & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR);
					
			c = tc16[taddr & 0x7ff];
			color->r = c >> 8;
			color->g = c & 0xff;
			color->b = color->r;
			color->a = (c & 1) ? 0xff : 0;
			
		}
        break;
	case TEXEL_IA4:
		{
			UINT8 p, i;
			taddr = ((tbase << 4) + s) >> 1;
			taddr ^= ((t & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR); 
			
			p = TMEM[taddr & 0xfff];
			p = (s & 1) ? (p & 0xf) : (p >> 4);
			i = p & 0xe;
			i = (i << 4) | (i << 1) | (i >> 2);
			color->r = i;
			color->g = i;
			color->b = i;
			color->a = (p & 0x1) ? 0xff : 0;
		}
		break;
	case TEXEL_IA8:
		{
			UINT8 p, i;
			taddr = (tbase << 3) + s;
			taddr ^= ((t & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR);
			
			p = TMEM[taddr & 0xfff];
			i = p & 0xf0;
			i |= (i >> 4);
			color->r = i;
			color->g = i;
			color->b = i;
			color->a = ((p & 0xf) << 4) | (p & 0xf);
		}
		break;
	case TEXEL_IA16:
		{
			UINT16 c;
		
			taddr = (tbase << 2) + s;
			taddr ^= ((t & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR); 
										
			c = tc16[taddr & 0x7ff];
			color->r = color->g = color->b = (c >> 8);
			color->a = c & 0xff;
		}
		break;
	case TEXEL_IA32:
		{
			UINT16 c;
			taddr = (tbase << 2) + s;
			taddr ^= ((t & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR);
					
			c = tc16[taddr & 0x7ff];
			color->r = c >> 8;
			color->g = c & 0xff;
			color->b = color->r;
			color->a = (c & 1) ? 0xff : 0;
		}
		break;
	case TEXEL_I4:
		{
			UINT8 byteval, c;
			taddr = ((tbase << 4) + s) >> 1;
			taddr ^= ((t & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR); 
													
			byteval = TMEM[taddr & 0xfff];
			c = (s & 1) ? (byteval & 0xf) : (byteval >> 4);
			c |= (c << 4);
			color->r = c;
			color->g = c;
			color->b = c;
			color->a = c;
		}
		break;
	case TEXEL_I8:
		{
			UINT8 c;
			taddr = (tbase << 3) + s;
			taddr ^= ((t & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR); 

			c = TMEM[taddr & 0xfff];
			color->r = c;
			color->g = c;
			color->b = c;
			color->a = c;
		}
		break;
	case TEXEL_I16:
		{
			UINT16 c;
			taddr = (tbase << 2) + s;
			taddr ^= ((t & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR);
					
			c = tc16[taddr & 0x7ff];
			color->r = c >> 8;
			color->g = c & 0xff;
			color->b = color->r;
			color->a = (c & 1) ? 0xff : 0;
		}
		break;
	case TEXEL_I32:
		{
			UINT16 c;
			taddr = (tbase << 2) + s;
			taddr ^= ((t & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR);
					
			c = tc16[taddr & 0x7ff];
			color->r = c >> 8;
			color->g = c & 0xff;
			color->b = color->r;
			color->a = (c & 1) ? 0xff : 0;
		}
		break;
	default:
		fatalerror("fetch_texel: unknown texture format %d, size %d, tilenum %d\n", tile[tilenum].format, tile[tilenum].size, tilenum);
		break;
	}
}

STRICTINLINE void fetch_texel_entlut(COLOR *color, int s, int t, UINT32 tilenum)
{
	UINT32 tbase = tile[tilenum].line * t + tile[tilenum].tmem;
	UINT32 tpal	= tile[tilenum].palette << 4;
	UINT16 *tc16 = (UINT16*)TMEM;
	UINT32 taddr = 0;
	UINT32 c;

	
	
	switch(tile[tilenum].f.tlutswitch)
	{
	case 0:
	case 1:
	case 2:
		{
			taddr = ((tbase << 4) + s) >> 1;
			taddr ^= ((t & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR);
			c = TMEM[taddr & 0x7ff];
			c = (s & 1) ? (c & 0xf) : (c >> 4);
			c = tlut[((tpal | c) << 2) ^ WORD_ADDR_XOR];
		}
		break;
	case 3:
		{
			taddr = (tbase << 3) + s;
			taddr ^= ((t & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR);
			c = TMEM[taddr & 0x7ff];
			c = (s & 1) ? (c & 0xf) : (c >> 4);
			c = tlut[((tpal | c) << 2) ^ WORD_ADDR_XOR];
		}
		break;
	case 4:
	case 5:
	case 6:
	case 7:
		{
			taddr = (tbase << 3) + s;
			taddr ^= ((t & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR);
			c = TMEM[taddr & 0x7ff];
			c = tlut[(c << 2) ^ WORD_ADDR_XOR];
		}
		break;
	case 8:
	case 9:
	case 10:
		{
			taddr = (tbase << 2) + s;
			taddr ^= ((t & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR);
			c = tc16[taddr & 0x3ff];
			c = tlut[((c >> 6) & ~3) ^ WORD_ADDR_XOR];
			
		}
		break;
	case 11:
		{
			taddr = (tbase << 3) + s;
			taddr ^= ((t & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR);
			c = TMEM[taddr & 0x7ff];
			c = tlut[(c << 2) ^ WORD_ADDR_XOR];
		}
		break;
	case 12:
	case 13:
	case 14:
		{
			taddr = (tbase << 2) + s;
			taddr ^= ((t & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR);
			c = tc16[taddr & 0x3ff];
			c = tlut[((c >> 6) & ~3) ^ WORD_ADDR_XOR];
		}
		break;
	case 15:
		{
			taddr = (tbase << 3) + s;
			taddr ^= ((t & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR);
			c = TMEM[taddr & 0x7ff];
			c = tlut[(c << 2) ^ WORD_ADDR_XOR];
		}
		break;
	default:
		fatalerror("fetch_texel_entlut: unknown texture format %d, size %d, tilenum %d\n", tile[tilenum].format, tile[tilenum].size, tilenum);
		break;
	}

	if (!other_modes.tlut_type)
	{
		color->r = GET_HI_RGBA16_TMEM(c);
		color->g = GET_MED_RGBA16_TMEM(c);
		color->b = GET_LOW_RGBA16_TMEM(c);
		color->a = (c & 1) ? 0xff : 0;
	}
	else
	{
		color->r = color->g = color->b = c >> 8;
		color->a = c & 0xff;
	}

}



STRICTINLINE void fetch_texel_quadro(COLOR *color0, COLOR *color1, COLOR *color2, COLOR *color3, int s0, int s1, int t0, int t1, UINT32 tilenum)
{

	UINT32 tbase0 = tile[tilenum].line * t0 + tile[tilenum].tmem;
	UINT32 tbase2 = tile[tilenum].line * t1 + tile[tilenum].tmem;
	UINT32 tpal	= tile[tilenum].palette;
	UINT32 xort = 0, ands = 0;

	
	

	UINT16 *tc16 = (UINT16*)TMEM;
	UINT32 taddr0 = 0, taddr1 = 0, taddr2 = 0, taddr3 = 0;
	UINT32 taddrlow0 = 0, taddrlow1 = 0, taddrlow2 = 0, taddrlow3 = 0;

	switch (tile[tilenum].f.notlutswitch)
	{
	case TEXEL_RGBA4:
		{
			UINT32 byteval, c;

			taddr0 = ((tbase0 << 4) + s0) >> 1;
			taddr1 = ((tbase0 << 4) + s1) >> 1;
			taddr2 = ((tbase2 << 4) + s0) >> 1;
			taddr3 = ((tbase2 << 4) + s1) >> 1;
			xort = (t0 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
			taddr0 ^= xort;
			taddr1 ^= xort;
			xort = (t1 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
			taddr2 ^= xort;
			taddr3 ^= xort; 
													
			taddr0 &= 0xfff;
			taddr1 &= 0xfff;
			taddr2 &= 0xfff;
			taddr3 &= 0xfff;
			ands = s0 & 1;
			byteval = TMEM[taddr0];
			c = (ands) ? (byteval & 0xf) : (byteval >> 4);
			c |= (c << 4);
			color0->r = c;
			color0->g = c;
			color0->b = c;
			color0->a = c;
			byteval = TMEM[taddr2];
			c = (ands) ? (byteval & 0xf) : (byteval >> 4);
			c |= (c << 4);
			color2->r = c;
			color2->g = c;
			color2->b = c;
			color2->a = c;

			ands = s1 & 1;
			byteval = TMEM[taddr1];
			c = (ands) ? (byteval & 0xf) : (byteval >> 4);
			c |= (c << 4);
			color1->r = c;
			color1->g = c;
			color1->b = c;
			color1->a = c;
			byteval = TMEM[taddr3];
			c = (ands) ? (byteval & 0xf) : (byteval >> 4);
			c |= (c << 4);
			color3->r = c;
			color3->g = c;
			color3->b = c;
			color3->a = c;
		}
		break;
	case TEXEL_RGBA8:
		{
			UINT32 p;
			taddr0 = ((tbase0 << 3) + s0);
			taddr1 = ((tbase0 << 3) + s1);
			taddr2 = ((tbase2 << 3) + s0);
			taddr3 = ((tbase2 << 3) + s1);
			xort = (t0 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
			taddr0 ^= xort;
			taddr1 ^= xort;
			xort = (t1 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
			taddr2 ^= xort;
			taddr3 ^= xort; 
			
			taddr0 &= 0xfff;
			taddr1 &= 0xfff;
			taddr2 &= 0xfff;
			taddr3 &= 0xfff;
			p = TMEM[taddr0];
			color0->r = p;
			color0->g = p;
			color0->b = p;
			color0->a = p;
			p = TMEM[taddr2];
			color2->r = p;
			color2->g = p;
			color2->b = p;
			color2->a = p;
			p = TMEM[taddr1];
			color1->r = p;
			color1->g = p;
			color1->b = p;
			color1->a = p;
			p = TMEM[taddr3];
			color3->r = p;
			color3->g = p;
			color3->b = p;
			color3->a = p;
		}
		break;
	case TEXEL_RGBA16:
		{
			UINT32 c0, c1, c2, c3;
			taddr0 = ((tbase0 << 2) + s0);
			taddr1 = ((tbase0 << 2) + s1);
			taddr2 = ((tbase2 << 2) + s0);
			taddr3 = ((tbase2 << 2) + s1);
			xort = (t0 & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR;
			taddr0 ^= xort;
			taddr1 ^= xort;
			xort = (t1 & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR;
			taddr2 ^= xort;
			taddr3 ^= xort;
					
			taddr0 &= 0x7ff;
			taddr1 &= 0x7ff;
			taddr2 &= 0x7ff;
			taddr3 &= 0x7ff;
			c0 = tc16[taddr0];
			c1 = tc16[taddr1];
			c2 = tc16[taddr2];
			c3 = tc16[taddr3];
			color0->r = GET_HI_RGBA16_TMEM(c0);
			color0->g = GET_MED_RGBA16_TMEM(c0);
			color0->b = GET_LOW_RGBA16_TMEM(c0);
			color0->a = (c0 & 1) ? 0xff : 0;
			color1->r = GET_HI_RGBA16_TMEM(c1);
			color1->g = GET_MED_RGBA16_TMEM(c1);
			color1->b = GET_LOW_RGBA16_TMEM(c1);
			color1->a = (c1 & 1) ? 0xff : 0;
			color2->r = GET_HI_RGBA16_TMEM(c2);
			color2->g = GET_MED_RGBA16_TMEM(c2);
			color2->b = GET_LOW_RGBA16_TMEM(c2);
			color2->a = (c2 & 1) ? 0xff : 0;
			color3->r = GET_HI_RGBA16_TMEM(c3);
			color3->g = GET_MED_RGBA16_TMEM(c3);
			color3->b = GET_LOW_RGBA16_TMEM(c3);
			color3->a = (c3 & 1) ? 0xff : 0;
		}
		break;
	case TEXEL_RGBA32:
		{
			UINT16 c0, c1, c2, c3;
			taddr0 = ((tbase0 << 2) + s0);
			taddr1 = ((tbase0 << 2) + s1);
			taddr2 = ((tbase2 << 2) + s0);
			taddr3 = ((tbase2 << 2) + s1);
			xort = (t0 & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR;
			taddr0 ^= xort;
			taddr1 ^= xort;
			xort = (t1 & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR;
			taddr2 ^= xort;
			taddr3 ^= xort;

			taddr0 &= 0x3ff;
			taddr1 &= 0x3ff;
			taddr2 &= 0x3ff;
			taddr3 &= 0x3ff;
			c0 = tc16[taddr0];
			color0->r = c0 >> 8;
			color0->g = c0 & 0xff;
			c0 = tc16[taddr0 | 0x400];
			color0->b = c0 >>  8;
			color0->a = c0 & 0xff;
			c1 = tc16[taddr1];
			color1->r = c1 >> 8;
			color1->g = c1 & 0xff;
			c1 = tc16[taddr1 | 0x400];
			color1->b = c1 >>  8;
			color1->a = c1 & 0xff;
			c2 = tc16[taddr2];
			color2->r = c2 >> 8;
			color2->g = c2 & 0xff;
			c2 = tc16[taddr2 | 0x400];
			color2->b = c2 >>  8;
			color2->a = c2 & 0xff;
			c3 = tc16[taddr3];
			color3->r = c3 >> 8;
			color3->g = c3 & 0xff;
			c3 = tc16[taddr3 | 0x400];
			color3->b = c3 >>  8;
			color3->a = c3 & 0xff;
		}
		break;
	case TEXEL_YUV4:
	case TEXEL_YUV8:
		{
			UINT16 c0, c1, c2, c3;
			INT32 u0, u1, u2, u3, save0, save1, save2, save3;

			taddr0 = ((tbase0 << 3) + s0);
			taddr1 = ((tbase0 << 3) + s1);
			taddr2 = ((tbase2 << 3) + s0);
			taddr3 = ((tbase2 << 3) + s1);
			taddrlow0 = taddr0 >> 1;
			taddrlow1 = taddr1 >> 1;
			taddrlow2 = taddr2 >> 1;
			taddrlow3 = taddr3 >> 1;

			xort = (t0 & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR;
			taddrlow0 ^= xort;
			taddrlow1 ^= xort;
			xort = (t1 & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR;
			taddrlow2 ^= xort;
			taddrlow3 ^= xort;

			taddrlow0 &= 0x3ff;
			taddrlow1 &= 0x3ff;
			taddrlow2 &= 0x3ff;
			taddrlow3 &= 0x3ff;

			c0 = tc16[taddrlow0];
			c1 = tc16[taddrlow1];
			c2 = tc16[taddrlow2];
			c3 = tc16[taddrlow3];

			save0 = u0 = c0 >> 8;
			u0 ^= 0x80;
			if (u0 & 0x80)
				u0 |= 0x100;
			save1 = u1 = c1 >> 8;
			u1 ^= 0x80;
			if (u1 & 0x80)
				u1 |= 0x100;
			save2 = u2 = c2 >> 8;
			u2 ^= 0x80;
			if (u2 & 0x80)
				u2 |= 0x100;
			save3 = u3 = c3 >> 8;
			u3 ^= 0x80;
			if (u3 & 0x80)
				u3 |= 0x100;

			color0->r = u0;
			color0->g = u0;
			color0->b = save0;
			color0->a = save0;
			color1->r = u1;
			color1->g = u1;
			color1->b = save1;
			color1->a = save1;
			color2->r = u2;
			color2->g = u2;
			color2->b = save2;
			color2->a = save2;
			color3->r = u3;
			color3->g = u3;
			color3->b = save3;
			color3->a = save3;
		}
		break;
	case TEXEL_YUV16:
	case TEXEL_YUV32:
		{
			UINT16 c0, c1, c2, c3;
			INT32 y0, y1, y2, y3, u0, u1, u2, u3, v0, v1, v2, v3;

			taddr0 = ((tbase0 << 3) + s0);
			taddr1 = ((tbase0 << 3) + s1);
			taddr2 = ((tbase2 << 3) + s0);
			taddr3 = ((tbase2 << 3) + s1);
			taddrlow0 = taddr0 >> 1;
			taddrlow1 = taddr1 >> 1;
			taddrlow2 = taddr2 >> 1;
			taddrlow3 = taddr3 >> 1;

			xort = (t0 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
			taddr0 ^= xort;
			taddr1 ^= xort;
			xort = (t1 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
			taddr2 ^= xort;
			taddr3 ^= xort;
			xort = (t0 & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR;
			taddrlow0 ^= xort;
			taddrlow1 ^= xort;
			xort = (t1 & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR;
			taddrlow2 ^= xort;
			taddrlow3 ^= xort;

			taddr0 &= 0x7ff;
			taddr1 &= 0x7ff;
			taddr2 &= 0x7ff;
			taddr3 &= 0x7ff;
			taddrlow0 &= 0x3ff;
			taddrlow1 &= 0x3ff;
			taddrlow2 &= 0x3ff;
			taddrlow3 &= 0x3ff;

			c0 = tc16[taddrlow0];
			c1 = tc16[taddrlow1];
			c2 = tc16[taddrlow2];
			c3 = tc16[taddrlow3];					
			
			y0 = TMEM[taddr0 | 0x800];
			u0 = c0 >> 8;
			v0 = c0 & 0xff;
			y1 = TMEM[taddr1 | 0x800];
			u1 = c1 >> 8;
			v1 = c1 & 0xff;
			y2 = TMEM[taddr2 | 0x800];
			u2 = c2 >> 8;
			v2 = c2 & 0xff;
			y3 = TMEM[taddr3 | 0x800];
			u3 = c3 >> 8;
			v3 = c3 & 0xff;

			v0 ^= 0x80; u0 ^= 0x80;
			if (v0 & 0x80)
				v0 |= 0x100;
			if (u0 & 0x80)
				u0 |= 0x100;
			v1 ^= 0x80; u1 ^= 0x80;
			if (v1 & 0x80)
				v1 |= 0x100;
			if (u1 & 0x80)
				u1 |= 0x100;
			v2 ^= 0x80; u2 ^= 0x80;
			if (v2 & 0x80)
				v2 |= 0x100;
			if (u2 & 0x80)
				u2 |= 0x100;
			v3 ^= 0x80; u3 ^= 0x80;
			if (v3 & 0x80)
				v3 |= 0x100;
			if (u3 & 0x80)
				u3 |= 0x100;

			color0->r = u0;
			color0->g = v0;
			color0->b = y0;
			color0->a = y0;
			color1->r = u1;
			color1->g = v1;
			color1->b = y1;
			color1->a = y1;
			color2->r = u2;
			color2->g = v2;
			color2->b = y2;
			color2->a = y2;
			color3->r = u3;
			color3->g = v3;
			color3->b = y3;
			color3->a = y3;
		}
		break;
	case TEXEL_CI4:
		{
			UINT32 p;
			taddr0 = ((tbase0 << 4) + s0) >> 1;
			taddr1 = ((tbase0 << 4) + s1) >> 1;
			taddr2 = ((tbase2 << 4) + s0) >> 1;
			taddr3 = ((tbase2 << 4) + s1) >> 1;
			xort = (t0 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
			taddr0 ^= xort;
			taddr1 ^= xort;
			xort = (t1 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
			taddr2 ^= xort;
			taddr3 ^= xort;
															
			taddr0 &= 0xfff;
			taddr1 &= 0xfff;
			taddr2 &= 0xfff;
			taddr3 &= 0xfff;
			ands = s0 & 1;
			p = TMEM[taddr0];
			p = (ands) ? (p & 0xf) : (p >> 4);
			p = (tpal << 4) | p;
			color0->r = color0->g = color0->b = color0->a = p;
			p = TMEM[taddr2];
			p = (ands) ? (p & 0xf) : (p >> 4);
			p = (tpal << 4) | p;
			color2->r = color2->g = color2->b = color2->a = p;

			ands = s1 & 1;
			p = TMEM[taddr1];
			p = (ands) ? (p & 0xf) : (p >> 4);
			p = (tpal << 4) | p;
			color1->r = color1->g = color1->b = color1->a = p;
			p = TMEM[taddr3];
			p = (ands) ? (p & 0xf) : (p >> 4);
			p = (tpal << 4) | p;
			color3->r = color3->g = color3->b = color3->a = p;
		}
		break;
	case TEXEL_CI8:
		{
			UINT32 p;
			taddr0 = ((tbase0 << 3) + s0);
			taddr1 = ((tbase0 << 3) + s1);
			taddr2 = ((tbase2 << 3) + s0);
			taddr3 = ((tbase2 << 3) + s1);
			xort = (t0 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
			taddr0 ^= xort;
			taddr1 ^= xort;
			xort = (t1 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
			taddr2 ^= xort;
			taddr3 ^= xort;

			taddr0 &= 0xfff;
			taddr1 &= 0xfff;
			taddr2 &= 0xfff;
			taddr3 &= 0xfff;
			p = TMEM[taddr0];
			color0->r = p;
			color0->g = p;
			color0->b = p;
			color0->a = p;
			p = TMEM[taddr2];
			color2->r = p;
			color2->g = p;
			color2->b = p;
			color2->a = p;
			p = TMEM[taddr1];
			color1->r = p;
			color1->g = p;
			color1->b = p;
			color1->a = p;
			p = TMEM[taddr3];
			color3->r = p;
			color3->g = p;
			color3->b = p;
			color3->a = p;
		}
		break;
	case TEXEL_CI16:
		{
			UINT16 c0, c1, c2, c3;

			taddr0 = ((tbase0 << 2) + s0);
			taddr1 = ((tbase0 << 2) + s1);
			taddr2 = ((tbase2 << 2) + s0);
			taddr3 = ((tbase2 << 2) + s1);
			xort = (t0 & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR;
			taddr0 ^= xort;
			taddr1 ^= xort;
			xort = (t1 & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR;
			taddr2 ^= xort;
			taddr3 ^= xort;
					
			taddr0 &= 0x7ff;
			taddr1 &= 0x7ff;
			taddr2 &= 0x7ff;
			taddr3 &= 0x7ff;
			c0 = tc16[taddr0];
			color0->r = c0 >> 8;
			color0->g = c0 & 0xff;
			color0->b = c0 >> 8;
			color0->a = (c0 & 1) ? 0xff : 0;
			c1 = tc16[taddr1];
			color1->r = c1 >> 8;
			color1->g = c1 & 0xff;
			color1->b = c1 >> 8;
			color1->a = (c1 & 1) ? 0xff : 0;
			c2 = tc16[taddr2];
			color2->r = c2 >> 8;
			color2->g = c2 & 0xff;
			color2->b = c2 >> 8;
			color2->a = (c2 & 1) ? 0xff : 0;
			c3 = tc16[taddr3];
			color3->r = c3 >> 8;
			color3->g = c3 & 0xff;
			color3->b = c3 >> 8;
			color3->a = (c3 & 1) ? 0xff : 0;
			
		}
		break;
	case TEXEL_CI32:
		{
			UINT16 c0, c1, c2, c3;
			taddr0 = ((tbase0 << 2) + s0);
			taddr1 = ((tbase0 << 2) + s1);
			taddr2 = ((tbase2 << 2) + s0);
			taddr3 = ((tbase2 << 2) + s1);
			xort = (t0 & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR;
			taddr0 ^= xort;
			taddr1 ^= xort;
			xort = (t1 & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR;
			taddr2 ^= xort;
			taddr3 ^= xort;
					
			taddr0 &= 0x7ff;
			taddr1 &= 0x7ff;
			taddr2 &= 0x7ff;
			taddr3 &= 0x7ff;
			c0 = tc16[taddr0];
			color0->r = c0 >> 8;
			color0->g = c0 & 0xff;
			color0->b = c0 >> 8;
			color0->a = (c0 & 1) ? 0xff : 0;
			c1 = tc16[taddr1];
			color1->r = c1 >> 8;
			color1->g = c1 & 0xff;
			color1->b = c1 >> 8;
			color1->a = (c1 & 1) ? 0xff : 0;
			c2 = tc16[taddr2];
			color2->r = c2 >> 8;
			color2->g = c2 & 0xff;
			color2->b = c2 >> 8;
			color2->a = (c2 & 1) ? 0xff : 0;
			c3 = tc16[taddr3];
			color3->r = c3 >> 8;
			color3->g = c3 & 0xff;
			color3->b = c3 >> 8;
			color3->a = (c3 & 1) ? 0xff : 0;
			
		}
        break;
	case TEXEL_IA4:
		{
			UINT32 p, i;
			taddr0 = ((tbase0 << 4) + s0) >> 1;
			taddr1 = ((tbase0 << 4) + s1) >> 1;
			taddr2 = ((tbase2 << 4) + s0) >> 1;
			taddr3 = ((tbase2 << 4) + s1) >> 1;
			xort = (t0 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
			taddr0 ^= xort;
			taddr1 ^= xort;
			xort = (t1 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
			taddr2 ^= xort;
			taddr3 ^= xort;
					
			taddr0 &= 0xfff;
			taddr1 &= 0xfff;
			taddr2 &= 0xfff;
			taddr3 &= 0xfff;
			ands = s0 & 1;
			p = TMEM[taddr0];
			p = ands ? (p & 0xf) : (p >> 4);
			i = p & 0xe;
			i = (i << 4) | (i << 1) | (i >> 2);
			color0->r = i;
			color0->g = i;
			color0->b = i;
			color0->a = (p & 0x1) ? 0xff : 0;
			p = TMEM[taddr2];
			p = ands ? (p & 0xf) : (p >> 4);
			i = p & 0xe;
			i = (i << 4) | (i << 1) | (i >> 2);
			color2->r = i;
			color2->g = i;
			color2->b = i;
			color2->a = (p & 0x1) ? 0xff : 0;

			ands = s1 & 1;
			p = TMEM[taddr1];
			p = ands ? (p & 0xf) : (p >> 4);
			i = p & 0xe;
			i = (i << 4) | (i << 1) | (i >> 2);
			color1->r = i;
			color1->g = i;
			color1->b = i;
			color1->a = (p & 0x1) ? 0xff : 0;
			p = TMEM[taddr3];
			p = ands ? (p & 0xf) : (p >> 4);
			i = p & 0xe;
			i = (i << 4) | (i << 1) | (i >> 2);
			color3->r = i;
			color3->g = i;
			color3->b = i;
			color3->a = (p & 0x1) ? 0xff : 0;
			
		}
		break;
	case TEXEL_IA8:
		{
			UINT32 p, i;
			taddr0 = ((tbase0 << 3) + s0);
			taddr1 = ((tbase0 << 3) + s1);
			taddr2 = ((tbase2 << 3) + s0);
			taddr3 = ((tbase2 << 3) + s1);
			xort = (t0 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
			taddr0 ^= xort;
			taddr1 ^= xort;
			xort = (t1 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
			taddr2 ^= xort;
			taddr3 ^= xort;
			
			taddr0 &= 0xfff;
			taddr1 &= 0xfff;
			taddr2 &= 0xfff;
			taddr3 &= 0xfff;
			p = TMEM[taddr0];
			i = p & 0xf0;
			i |= (i >> 4);
			color0->r = i;
			color0->g = i;
			color0->b = i;
			color0->a = ((p & 0xf) << 4) | (p & 0xf);
			p = TMEM[taddr1];
			i = p & 0xf0;
			i |= (i >> 4);
			color1->r = i;
			color1->g = i;
			color1->b = i;
			color1->a = ((p & 0xf) << 4) | (p & 0xf);
			p = TMEM[taddr2];
			i = p & 0xf0;
			i |= (i >> 4);
			color2->r = i;
			color2->g = i;
			color2->b = i;
			color2->a = ((p & 0xf) << 4) | (p & 0xf);
			p = TMEM[taddr3];
			i = p & 0xf0;
			i |= (i >> 4);
			color3->r = i;
			color3->g = i;
			color3->b = i;
			color3->a = ((p & 0xf) << 4) | (p & 0xf);
			
			
		}
		break;
	case TEXEL_IA16:
		{
			UINT16 c0, c1, c2, c3;
			taddr0 = ((tbase0 << 2) + s0);
			taddr1 = ((tbase0 << 2) + s1);
			taddr2 = ((tbase2 << 2) + s0);
			taddr3 = ((tbase2 << 2) + s1);
			xort = (t0 & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR;
			taddr0 ^= xort;
			taddr1 ^= xort;
			xort = (t1 & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR;
			taddr2 ^= xort;
			taddr3 ^= xort;
										
			taddr0 &= 0x7ff;
			taddr1 &= 0x7ff;
			taddr2 &= 0x7ff;
			taddr3 &= 0x7ff;
			c0 = tc16[taddr0];
			color0->r = color0->g = color0->b = c0 >> 8;
			color0->a = c0 & 0xff;
			c1 = tc16[taddr1];
			color1->r = color1->g = color1->b = c1 >> 8;
			color1->a = c1 & 0xff;
			c2 = tc16[taddr2];
			color2->r = color2->g = color2->b = c2 >> 8;
			color2->a = c2 & 0xff;
			c3 = tc16[taddr3];
			color3->r = color3->g = color3->b = c3 >> 8;
			color3->a = c3 & 0xff;
				
		}
		break;
	case TEXEL_IA32:
		{
			UINT16 c0, c1, c2, c3;
			taddr0 = ((tbase0 << 2) + s0);
			taddr1 = ((tbase0 << 2) + s1);
			taddr2 = ((tbase2 << 2) + s0);
			taddr3 = ((tbase2 << 2) + s1);
			xort = (t0 & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR;
			taddr0 ^= xort;
			taddr1 ^= xort;
			xort = (t1 & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR;
			taddr2 ^= xort;
			taddr3 ^= xort;
					
			taddr0 &= 0x7ff;
			taddr1 &= 0x7ff;
			taddr2 &= 0x7ff;
			taddr3 &= 0x7ff;
			c0 = tc16[taddr0];
			color0->r = c0 >> 8;
			color0->g = c0 & 0xff;
			color0->b = c0 >> 8;
			color0->a = (c0 & 1) ? 0xff : 0;
			c1 = tc16[taddr1];
			color1->r = c1 >> 8;
			color1->g = c1 & 0xff;
			color1->b = c1 >> 8;
			color1->a = (c1 & 1) ? 0xff : 0;
			c2 = tc16[taddr2];
			color2->r = c2 >> 8;
			color2->g = c2 & 0xff;
			color2->b = c2 >> 8;
			color2->a = (c2 & 1) ? 0xff : 0;
			c3 = tc16[taddr3];
			color3->r = c3 >> 8;
			color3->g = c3 & 0xff;
			color3->b = c3 >> 8;
			color3->a = (c3 & 1) ? 0xff : 0;
						
		}
		break;
	case TEXEL_I4:
		{
			UINT32 p, c0, c1, c2, c3; 
			
			taddr0 = ((tbase0 << 4) + s0) >> 1;
			taddr1 = ((tbase0 << 4) + s1) >> 1;
			taddr2 = ((tbase2 << 4) + s0) >> 1;
			taddr3 = ((tbase2 << 4) + s1) >> 1;
			xort = (t0 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
			taddr0 ^= xort;
			taddr1 ^= xort;
			xort = (t1 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
			taddr2 ^= xort;
			taddr3 ^= xort;

			taddr0 &= 0xfff;
			taddr1 &= 0xfff;
			taddr2 &= 0xfff;
			taddr3 &= 0xfff;
			ands = s0 & 1;
			p = TMEM[taddr0];
			c0 = ands ? (p & 0xf) : (p >> 4);
			c0 |= (c0 << 4);
			color0->r = color0->g = color0->b = color0->a = c0;
			p = TMEM[taddr2];
			c2 = ands ? (p & 0xf) : (p >> 4);
			c2 |= (c2 << 4);
			color2->r = color2->g = color2->b = color2->a = c2;

			ands = s1 & 1;
			p = TMEM[taddr1];
			c1 = ands ? (p & 0xf) : (p >> 4);
			c1 |= (c1 << 4);
			color1->r = color1->g = color1->b = color1->a = c1;
			p = TMEM[taddr3];
			c3 = ands ? (p & 0xf) : (p >> 4);
			c3 |= (c3 << 4);
			color3->r = color3->g = color3->b = color3->a = c3;
				
		}
		break;
	case TEXEL_I8:
		{
			UINT32 p;

			taddr0 = ((tbase0 << 3) + s0);
			taddr1 = ((tbase0 << 3) + s1);
			taddr2 = ((tbase2 << 3) + s0);
			taddr3 = ((tbase2 << 3) + s1);
			xort = (t0 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
			taddr0 ^= xort;
			taddr1 ^= xort;
			xort = (t1 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
			taddr2 ^= xort;
			taddr3 ^= xort;

			taddr0 &= 0xfff;
			taddr1 &= 0xfff;
			taddr2 &= 0xfff;
			taddr3 &= 0xfff;
			p = TMEM[taddr0];
			color0->r = p;
			color0->g = p;
			color0->b = p;
			color0->a = p;
			p = TMEM[taddr1];
			color1->r = p;
			color1->g = p;
			color1->b = p;
			color1->a = p;
			p = TMEM[taddr2];
			color2->r = p;
			color2->g = p;
			color2->b = p;
			color2->a = p;
			p = TMEM[taddr3];
			color3->r = p;
			color3->g = p;
			color3->b = p;
			color3->a = p;
		}
		break;
	case TEXEL_I16:
		{
			UINT16 c0, c1, c2, c3;
			taddr0 = ((tbase0 << 2) + s0);
			taddr1 = ((tbase0 << 2) + s1);
			taddr2 = ((tbase2 << 2) + s0);
			taddr3 = ((tbase2 << 2) + s1);
			xort = (t0 & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR;
			taddr0 ^= xort;
			taddr1 ^= xort;
			xort = (t1 & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR;
			taddr2 ^= xort;
			taddr3 ^= xort;
					
			taddr0 &= 0x7ff;
			taddr1 &= 0x7ff;
			taddr2 &= 0x7ff;
			taddr3 &= 0x7ff;
			c0 = tc16[taddr0];
			color0->r = c0 >> 8;
			color0->g = c0 & 0xff;
			color0->b = c0 >> 8;
			color0->a = (c0 & 1) ? 0xff : 0;
			c1 = tc16[taddr1];
			color1->r = c1 >> 8;
			color1->g = c1 & 0xff;
			color1->b = c1 >> 8;
			color1->a = (c1 & 1) ? 0xff : 0;
			c2 = tc16[taddr2];
			color2->r = c2 >> 8;
			color2->g = c2 & 0xff;
			color2->b = c2 >> 8;
			color2->a = (c2 & 1) ? 0xff : 0;
			c3 = tc16[taddr3];
			color3->r = c3 >> 8;
			color3->g = c3 & 0xff;
			color3->b = c3 >> 8;
			color3->a = (c3 & 1) ? 0xff : 0;
		}
		break;
	case TEXEL_I32:
		{
			UINT16 c0, c1, c2, c3;
			taddr0 = ((tbase0 << 2) + s0);
			taddr1 = ((tbase0 << 2) + s1);
			taddr2 = ((tbase2 << 2) + s0);
			taddr3 = ((tbase2 << 2) + s1);
			xort = (t0 & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR;
			taddr0 ^= xort;
			taddr1 ^= xort;
			xort = (t1 & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR;
			taddr2 ^= xort;
			taddr3 ^= xort;
					
			taddr0 &= 0x7ff;
			taddr1 &= 0x7ff;
			taddr2 &= 0x7ff;
			taddr3 &= 0x7ff;
			c0 = tc16[taddr0];
			color0->r = c0 >> 8;
			color0->g = c0 & 0xff;
			color0->b = c0 >> 8;
			color0->a = (c0 & 1) ? 0xff : 0;
			c1 = tc16[taddr1];
			color1->r = c1 >> 8;
			color1->g = c1 & 0xff;
			color1->b = c1 >> 8;
			color1->a = (c1 & 1) ? 0xff : 0;
			c2 = tc16[taddr2];
			color2->r = c2 >> 8;
			color2->g = c2 & 0xff;
			color2->b = c2 >> 8;
			color2->a = (c2 & 1) ? 0xff : 0;
			c3 = tc16[taddr3];
			color3->r = c3 >> 8;
			color3->g = c3 & 0xff;
			color3->b = c3 >> 8;
			color3->a = (c3 & 1) ? 0xff : 0;
		}
		break;
	default:
		fatalerror("fetch_texel_quadro: unknown texture format %d, size %d, tilenum %d\n", tile[tilenum].format, tile[tilenum].size, tilenum);
		break;
	}
}

STRICTINLINE void fetch_texel_entlut_quadro(COLOR *color0, COLOR *color1, COLOR *color2, COLOR *color3, int s0, int s1, int t0, int t1, UINT32 tilenum)
{
	UINT32 tbase0 = tile[tilenum].line * t0 + tile[tilenum].tmem;
	UINT32 tbase2 = tile[tilenum].line * t1 + tile[tilenum].tmem;
	UINT32 tpal	= tile[tilenum].palette << 4;
	UINT32 xort = 0, ands = 0;

	UINT16 *tc16 = (UINT16*)TMEM;
	UINT32 taddr0 = 0, taddr1 = 0, taddr2 = 0, taddr3 = 0;
	UINT16 c0, c1, c2, c3;

	
	
	switch(tile[tilenum].f.tlutswitch)
	{
	case 0:
	case 1:
	case 2:
		{
			taddr0 = ((tbase0 << 4) + s0) >> 1;
			taddr1 = ((tbase0 << 4) + s1) >> 1;
			taddr2 = ((tbase2 << 4) + s0) >> 1;
			taddr3 = ((tbase2 << 4) + s1) >> 1;
			xort = (t0 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
			taddr0 ^= xort;
			taddr1 ^= xort;
			xort = (t1 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
			taddr2 ^= xort;
			taddr3 ^= xort;
															
			ands = s0 & 1;
			c0 = TMEM[taddr0 & 0x7ff];
			c0 = (ands) ? (c0 & 0xf) : (c0 >> 4);
			c0 = tlut[((tpal | c0) << 2) ^ WORD_ADDR_XOR];
			c2 = TMEM[taddr2 & 0x7ff];
			c2 = (ands) ? (c2 & 0xf) : (c2 >> 4);
			c2 = tlut[((tpal | c2) << 2) ^ WORD_ADDR_XOR];

			ands = s1 & 1;
			c1 = TMEM[taddr1 & 0x7ff];
			c1 = (ands) ? (c1 & 0xf) : (c1 >> 4);
			c1 = tlut[((tpal | c1) << 2) ^ WORD_ADDR_XOR];
			c3 = TMEM[taddr3 & 0x7ff];
			c3 = (ands) ? (c3 & 0xf) : (c3 >> 4);
			c3 = tlut[((tpal | c3) << 2) ^ WORD_ADDR_XOR];
		}
		break;
	case 3:
		{
			taddr0 = ((tbase0 << 3) + s0);
			taddr1 = ((tbase0 << 3) + s1);
			taddr2 = ((tbase2 << 3) + s0);
			taddr3 = ((tbase2 << 3) + s1);
			xort = (t0 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
			taddr0 ^= xort;
			taddr1 ^= xort;
			xort = (t1 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
			taddr2 ^= xort;
			taddr3 ^= xort;
															
			ands = s0 & 1;
			c0 = TMEM[taddr0 & 0x7ff];
			c0 = (ands) ? (c0 & 0xf) : (c0 >> 4);
			c0 = tlut[((tpal | c0) << 2) ^ WORD_ADDR_XOR];
			c2 = TMEM[taddr2 & 0x7ff];
			c2 = (ands) ? (c2 & 0xf) : (c2 >> 4);
			c2 = tlut[((tpal | c2) << 2) ^ WORD_ADDR_XOR];

			ands = s1 & 1;
			c1 = TMEM[taddr1 & 0x7ff];
			c1 = (ands) ? (c1 & 0xf) : (c1 >> 4);
			c1 = tlut[((tpal | c1) << 2) ^ WORD_ADDR_XOR];
			c3 = TMEM[taddr3 & 0x7ff];
			c3 = (ands) ? (c3 & 0xf) : (c3 >> 4);
			c3 = tlut[((tpal | c3) << 2) ^ WORD_ADDR_XOR];
		}
		break;
	case 4:
	case 5:
	case 6:
	case 7:
		{
			taddr0 = ((tbase0 << 3) + s0);
			taddr1 = ((tbase0 << 3) + s1);
			taddr2 = ((tbase2 << 3) + s0);
			taddr3 = ((tbase2 << 3) + s1);
			xort = (t0 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
			taddr0 ^= xort;
			taddr1 ^= xort;
			xort = (t1 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
			taddr2 ^= xort;
			taddr3 ^= xort;
			
			c0 = TMEM[taddr0 & 0x7ff];
			c0 = tlut[(c0 << 2) ^ WORD_ADDR_XOR];
			c2 = TMEM[taddr2 & 0x7ff];
			c2 = tlut[(c2 << 2) ^ WORD_ADDR_XOR];
			c1 = TMEM[taddr1 & 0x7ff];
			c1 = tlut[(c1 << 2) ^ WORD_ADDR_XOR];
			c3 = TMEM[taddr3 & 0x7ff];
			c3 = tlut[(c3 << 2) ^ WORD_ADDR_XOR];
		}
		break;
	case 8:
	case 9:
	case 10:
		{
			taddr0 = ((tbase0 << 2) + s0);
			taddr1 = ((tbase0 << 2) + s1);
			taddr2 = ((tbase2 << 2) + s0);
			taddr3 = ((tbase2 << 2) + s1);
			xort = (t0 & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR;
			taddr0 ^= xort;
			taddr1 ^= xort;
			xort = (t1 & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR;
			taddr2 ^= xort;
			taddr3 ^= xort;
					
			c0 = tc16[taddr0 & 0x3ff];
			c0 = tlut[((c0 >> 6) & ~3) ^ WORD_ADDR_XOR];
			c1 = tc16[taddr1 & 0x3ff];
			c1 = tlut[((c1 >> 6) & ~3) ^ WORD_ADDR_XOR];
			c2 = tc16[taddr2 & 0x3ff];
			c2 = tlut[((c2 >> 6) & ~3) ^ WORD_ADDR_XOR];
			c3 = tc16[taddr3 & 0x3ff];
			c3 = tlut[((c3 >> 6) & ~3) ^ WORD_ADDR_XOR];
		}
		break;
	case 11:
		{
			taddr0 = ((tbase0 << 3) + s0);
			taddr1 = ((tbase0 << 3) + s1);
			taddr2 = ((tbase2 << 3) + s0);
			taddr3 = ((tbase2 << 3) + s1);
			xort = (t0 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
			taddr0 ^= xort;
			taddr1 ^= xort;
			xort = (t1 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
			taddr2 ^= xort;
			taddr3 ^= xort;
			
			c0 = TMEM[taddr0 & 0x7ff];
			c0 = tlut[(c0 << 2) ^ WORD_ADDR_XOR];
			c2 = TMEM[taddr2 & 0x7ff];
			c2 = tlut[(c2 << 2) ^ WORD_ADDR_XOR];
			c1 = TMEM[taddr1 & 0x7ff];
			c1 = tlut[(c1 << 2) ^ WORD_ADDR_XOR];
			c3 = TMEM[taddr3 & 0x7ff];
			c3 = tlut[(c3 << 2) ^ WORD_ADDR_XOR];
		}
		break;
	case 12:
	case 13:
	case 14:
		{
			taddr0 = ((tbase0 << 2) + s0);
			taddr1 = ((tbase0 << 2) + s1);
			taddr2 = ((tbase2 << 2) + s0);
			taddr3 = ((tbase2 << 2) + s1);
			xort = (t0 & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR;
			taddr0 ^= xort;
			taddr1 ^= xort;
			xort = (t1 & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR;
			taddr2 ^= xort;
			taddr3 ^= xort;
								
			c0 = tc16[taddr0 & 0x3ff];
			c0 = tlut[((c0 >> 6) & ~3) ^ WORD_ADDR_XOR];
			c1 = tc16[taddr1 & 0x3ff];
			c1 = tlut[((c1 >> 6) & ~3) ^ WORD_ADDR_XOR];
			c2 = tc16[taddr2 & 0x3ff];
			c2 = tlut[((c2 >> 6) & ~3) ^ WORD_ADDR_XOR];
			c3 = tc16[taddr3 & 0x3ff];
			c3 = tlut[((c3 >> 6) & ~3) ^ WORD_ADDR_XOR];
		}
		break;
	case 15:
		{
			taddr0 = ((tbase0 << 3) + s0);
			taddr1 = ((tbase0 << 3) + s1);
			taddr2 = ((tbase2 << 3) + s0);
			taddr3 = ((tbase2 << 3) + s1);
			xort = (t0 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
			taddr0 ^= xort;
			taddr1 ^= xort;
			xort = (t1 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
			taddr2 ^= xort;
			taddr3 ^= xort;
			
			c0 = TMEM[taddr0 & 0x7ff];
			c0 = tlut[(c0 << 2) ^ WORD_ADDR_XOR];
			c2 = TMEM[taddr2 & 0x7ff];
			c2 = tlut[(c2 << 2) ^ WORD_ADDR_XOR];
			c1 = TMEM[taddr1 & 0x7ff];
			c1 = tlut[(c1 << 2) ^ WORD_ADDR_XOR];
			c3 = TMEM[taddr3 & 0x7ff];
			c3 = tlut[(c3 << 2) ^ WORD_ADDR_XOR];
		}
		break;
	default:
		fatalerror("fetch_texel_entlut_quadro: unknown texture format %d, size %d, tilenum %d\n", tile[tilenum].format, tile[tilenum].size, tilenum);
		break;
	}

	if (!other_modes.tlut_type)
	{
		color0->r = GET_HI_RGBA16_TMEM(c0);
		color0->g = GET_MED_RGBA16_TMEM(c0);
		color0->b = GET_LOW_RGBA16_TMEM(c0);
		color0->a = (c0 & 1) ? 0xff : 0;
		color1->r = GET_HI_RGBA16_TMEM(c1);
		color1->g = GET_MED_RGBA16_TMEM(c1);
		color1->b = GET_LOW_RGBA16_TMEM(c1);
		color1->a = (c1 & 1) ? 0xff : 0;
		color2->r = GET_HI_RGBA16_TMEM(c2);
		color2->g = GET_MED_RGBA16_TMEM(c2);
		color2->b = GET_LOW_RGBA16_TMEM(c2);
		color2->a = (c2 & 1) ? 0xff : 0;
		color3->r = GET_HI_RGBA16_TMEM(c3);
		color3->g = GET_MED_RGBA16_TMEM(c3);
		color3->b = GET_LOW_RGBA16_TMEM(c3);
		color3->a = (c3 & 1) ? 0xff : 0;
	}
	else
	{
		color0->r = color0->g = color0->b = c0 >> 8;
		color0->a = c0 & 0xff;
		color1->r = color1->g = color1->b = c1 >> 8;
		color1->a = c1 & 0xff;
		color2->r = color2->g = color2->b = c2 >> 8;
		color2->a = c2 & 0xff;
		color3->r = color3->g = color3->b = c3 >> 8;
		color3->a = c3 & 0xff;
	}
}


void get_tmem_idx(int s, int t, UINT32 tilenum, UINT32* idx0, UINT32* idx1, UINT32* idx2, UINT32* idx3, UINT32* bit3flipped, UINT32* hibit)
{
	UINT32 tbase, tsize, tformat, sshorts;
	int tidx_a, tidx_b, tidx_c, tidx_d;

	tbase = (tile[tilenum].line * t) & 0x1ff;
	tbase += tile[tilenum].tmem;
	tsize = tile[tilenum].size;
	tformat = tile[tilenum].format;
	sshorts = 0;

	
	if (tsize == PIXEL_SIZE_8BIT || tformat == FORMAT_YUV)
		sshorts = s >> 1;
	else if (tsize >= PIXEL_SIZE_16BIT)
		sshorts = s;
	else
		sshorts = s >> 2;
	sshorts &= 0x7ff;

	*bit3flipped = ((sshorts & 2) ? 1 : 0) ^ (t & 1);

	tidx_a = ((tbase << 2) + sshorts) & 0x7fd;
	tidx_b = (tidx_a + 1) & 0x7ff;
	tidx_c = (tidx_a + 2) & 0x7ff;
	tidx_d = (tidx_a + 3) & 0x7ff;

	*hibit = (tidx_a & 0x400) ? 1 : 0;

	if (t & 1)
	{
		tidx_a ^= 2;
		tidx_b ^= 2;
		tidx_c ^= 2;
		tidx_d ^= 2;
	}

	
	sort_tmem_idx(idx0, tidx_a, tidx_b, tidx_c, tidx_d, 0);
	sort_tmem_idx(idx1, tidx_a, tidx_b, tidx_c, tidx_d, 1);
	sort_tmem_idx(idx2, tidx_a, tidx_b, tidx_c, tidx_d, 2);
	sort_tmem_idx(idx3, tidx_a, tidx_b, tidx_c, tidx_d, 3);
}







void read_tmem_copy(int s, int s1, int s2, int s3, int t, UINT32 tilenum, UINT32* sortshort, int* hibits, int* lowbits)
{
	UINT32 tbase, tsize, tformat, shbytes, shbytes1, shbytes2, shbytes3, sortidx[8];
	INT32 delta;
	UINT16 *tmem16;
	UINT32 short0, short1, short2, short3;
	int tidx_a, tidx_blow, tidx_bhi, tidx_c, tidx_dlow, tidx_dhi;
	tbase = (tile[tilenum].line * t) & 0x1ff;
	tbase += tile[tilenum].tmem;
	tsize = tile[tilenum].size;
	tformat = tile[tilenum].format;
	shbytes = 0;
	shbytes1 = 0;
	shbytes2 = 0;
	shbytes3 = 0;
	delta = 0;
	
	if (tsize == PIXEL_SIZE_8BIT || tformat == FORMAT_YUV)
	{
		shbytes = s << 1;
		shbytes1 = s1 << 1;
		shbytes2 = s2 << 1;
		shbytes3 = s3 << 1;
	}
	else if (tsize >= PIXEL_SIZE_16BIT)
	{
		shbytes = s << 2;
		shbytes1 = s1 << 2;
		shbytes2 = s2 << 2;
		shbytes3 = s3 << 2;
	}
	else
	{
		shbytes = s;
		shbytes1 = s1;
		shbytes2 = s2;
		shbytes3 = s3;
	}

	shbytes &= 0x1fff;
	shbytes1 &= 0x1fff;
	shbytes2 &= 0x1fff;
	shbytes3 &= 0x1fff;

	tbase <<= 4;
	tidx_a = (tbase + shbytes) & 0x1fff;
	tidx_bhi = (tbase + shbytes1) & 0x1fff;
	tidx_c = (tbase + shbytes2) & 0x1fff;
	tidx_dhi = (tbase + shbytes3) & 0x1fff;

	if (tformat == FORMAT_YUV)
	{
		delta = shbytes1 - shbytes;
		tidx_blow = (tidx_a + (delta << 1)) & 0x1fff;
		tidx_dlow = (tidx_blow + shbytes3 - shbytes) & 0x1fff;
	}
	else
	{
		tidx_blow = tidx_bhi;
		tidx_dlow = tidx_dhi;
	}

	if (t & 1)
	{
		tidx_a ^= 8;
		tidx_blow ^= 8;
		tidx_bhi ^= 8;
		tidx_c ^= 8;
		tidx_dlow ^= 8;
		tidx_dhi ^= 8;
	}

	hibits[0] = (tidx_a & 0x1000) ? 1 : 0;
	hibits[1] = (tidx_blow & 0x1000) ? 1 : 0; 
	hibits[2] =	(tidx_bhi & 0x1000) ? 1 : 0;
	hibits[3] =	(tidx_c & 0x1000) ? 1 : 0;
	hibits[4] =	(tidx_dlow & 0x1000) ? 1 : 0;
	hibits[5] = (tidx_dhi & 0x1000) ? 1 : 0;
	lowbits[0] = tidx_a & 0xf;
	lowbits[1] = tidx_blow & 0xf;
	lowbits[2] = tidx_bhi & 0xf;
	lowbits[3] = tidx_c & 0xf;
	lowbits[4] = tidx_dlow & 0xf;
	lowbits[5] = tidx_dhi & 0xf;

	tmem16 = (UINT16*)TMEM;

	
	tidx_a >>= 2;
	tidx_blow >>= 2;
	tidx_bhi >>= 2;
	tidx_c >>= 2;
	tidx_dlow >>= 2;
	tidx_dhi >>= 2;

	
	sort_tmem_idx(&sortidx[0], tidx_a, tidx_blow, tidx_c, tidx_dlow, 0);
	sort_tmem_idx(&sortidx[1], tidx_a, tidx_blow, tidx_c, tidx_dlow, 1);
	sort_tmem_idx(&sortidx[2], tidx_a, tidx_blow, tidx_c, tidx_dlow, 2);
	sort_tmem_idx(&sortidx[3], tidx_a, tidx_blow, tidx_c, tidx_dlow, 3);

	short0 = tmem16[sortidx[0] ^ WORD_ADDR_XOR];
	short1 = tmem16[sortidx[1] ^ WORD_ADDR_XOR];
	short2 = tmem16[sortidx[2] ^ WORD_ADDR_XOR];
	short3 = tmem16[sortidx[3] ^ WORD_ADDR_XOR];

	
	sort_tmem_shorts_lowhalf(&sortshort[0], short0, short1, short2, short3, lowbits[0] >> 2);
	sort_tmem_shorts_lowhalf(&sortshort[1], short0, short1, short2, short3, lowbits[1] >> 2);
	sort_tmem_shorts_lowhalf(&sortshort[2], short0, short1, short2, short3, lowbits[3] >> 2);
	sort_tmem_shorts_lowhalf(&sortshort[3], short0, short1, short2, short3, lowbits[4] >> 2);

	if (other_modes.en_tlut)
	{
	 	
		compute_color_index(&short0, sortshort[0], lowbits[0] & 3, tilenum);
		compute_color_index(&short1, sortshort[1], lowbits[1] & 3, tilenum);
		compute_color_index(&short2, sortshort[2], lowbits[3] & 3, tilenum);
		compute_color_index(&short3, sortshort[3], lowbits[4] & 3, tilenum);

		
		sortidx[4] = (short0 << 2);
		sortidx[5] = (short1 << 2) | 1;
		sortidx[6] = (short2 << 2) | 2;
		sortidx[7] = (short3 << 2) | 3;
	}
	else
	{
		sort_tmem_idx(&sortidx[4], tidx_a, tidx_bhi, tidx_c, tidx_dhi, 0);
		sort_tmem_idx(&sortidx[5], tidx_a, tidx_bhi, tidx_c, tidx_dhi, 1);
		sort_tmem_idx(&sortidx[6], tidx_a, tidx_bhi, tidx_c, tidx_dhi, 2);
		sort_tmem_idx(&sortidx[7], tidx_a, tidx_bhi, tidx_c, tidx_dhi, 3);
	}

	short0 = tmem16[(sortidx[4] | 0x400) ^ WORD_ADDR_XOR];
	short1 = tmem16[(sortidx[5] | 0x400) ^ WORD_ADDR_XOR];
	short2 = tmem16[(sortidx[6] | 0x400) ^ WORD_ADDR_XOR];
	short3 = tmem16[(sortidx[7] | 0x400) ^ WORD_ADDR_XOR];

	
	
	if (other_modes.en_tlut)
	{
		sort_tmem_shorts_lowhalf(&sortshort[4], short0, short1, short2, short3, 0);
		sort_tmem_shorts_lowhalf(&sortshort[5], short0, short1, short2, short3, 1);
		sort_tmem_shorts_lowhalf(&sortshort[6], short0, short1, short2, short3, 2);
		sort_tmem_shorts_lowhalf(&sortshort[7], short0, short1, short2, short3, 3);
	}
	else
	{
		sort_tmem_shorts_lowhalf(&sortshort[4], short0, short1, short2, short3, lowbits[0] >> 2);
		sort_tmem_shorts_lowhalf(&sortshort[5], short0, short1, short2, short3, lowbits[2] >> 2);
		sort_tmem_shorts_lowhalf(&sortshort[6], short0, short1, short2, short3, lowbits[3] >> 2);
		sort_tmem_shorts_lowhalf(&sortshort[7], short0, short1, short2, short3, lowbits[5] >> 2);
	}
}




void sort_tmem_idx(UINT32 *idx, UINT32 idxa, UINT32 idxb, UINT32 idxc, UINT32 idxd, UINT32 bankno)
{
	if ((idxa & 3) == bankno)
		*idx = idxa & 0x3ff;
	else if ((idxb & 3) == bankno)
		*idx = idxb & 0x3ff;
	else if ((idxc & 3) == bankno)
		*idx = idxc & 0x3ff;
	else if ((idxd & 3) == bankno)
		*idx = idxd & 0x3ff;
	else
		*idx = 0;
}


void sort_tmem_shorts_lowhalf(UINT32* bindshort, UINT32 short0, UINT32 short1, UINT32 short2, UINT32 short3, UINT32 bankno)
{
	switch(bankno)
	{
	case 0:
		*bindshort = short0;
		break;
	case 1:
		*bindshort = short1;
		break;
	case 2:
		*bindshort = short2;
		break;
	case 3:
		*bindshort = short3;
		break;
	}
}



void compute_color_index(UINT32* cidx, UINT32 readshort, UINT32 nybbleoffset, UINT32 tilenum)
{
	UINT32 lownib, hinib;
	if (tile[tilenum].size == PIXEL_SIZE_4BIT)
	{
		lownib = (nybbleoffset ^ 3) << 2;
		hinib = tile[tilenum].palette;
	}
	else
	{
		lownib = ((nybbleoffset & 2) ^ 2) << 2;
		hinib = lownib ? ((readshort >> 12) & 0xf) : ((readshort >> 4) & 0xf);
	}
	lownib = (readshort >> lownib) & 0xf;
	*cidx = (hinib << 4) | lownib;
}


void replicate_for_copy(UINT32* outbyte, UINT32 inshort, UINT32 nybbleoffset, UINT32 tilenum, UINT32 tformat, UINT32 tsize)
{
	UINT32 lownib, hinib;
	switch(tsize)
	{
	case PIXEL_SIZE_4BIT:
		lownib = (nybbleoffset ^ 3) << 2;
		lownib = hinib = (inshort >> lownib) & 0xf;
		if (tformat == FORMAT_CI)
		{
			*outbyte = (tile[tilenum].palette << 4) | lownib;
		}
		else if (tformat == FORMAT_IA)
		{
			lownib = (lownib << 4) | lownib;
			*outbyte = (lownib & 0xe0) | ((lownib & 0xe0) >> 3) | ((lownib & 0xc0) >> 6);
		}
		else
			*outbyte = (lownib << 4) | lownib;
		break;
	case PIXEL_SIZE_8BIT:
		hinib = ((nybbleoffset ^ 3) | 1) << 2;
		if (tformat == FORMAT_IA)
		{
			lownib = (inshort >> hinib) & 0xf;
			*outbyte = (lownib << 4) | lownib;
		}
		else
		{
			lownib = (inshort >> (hinib & ~4)) & 0xf;
			hinib = (inshort >> hinib) & 0xf;
			*outbyte = (hinib << 4) | lownib;
		}
		break;
	default:
		*outbyte = (inshort >> 8) & 0xff;
		break;
	}
}

void fetch_qword_copy(UINT32* hidword, UINT32* lowdword, INT32 ssss, INT32 ssst, UINT32 tilenum)
{
	UINT32 shorta, shortb, shortc, shortd;
	UINT32 sortshort[8];
	int hibits[6];
	int lowbits[6];
	INT32 sss = ssss, sst = ssst, sss1 = 0, sss2 = 0, sss3 = 0;
	int largetex = 0;

	UINT32 tformat, tsize;
	if (other_modes.en_tlut)
	{
		tsize = PIXEL_SIZE_16BIT;
		tformat = other_modes.tlut_type ? FORMAT_IA : FORMAT_RGBA;
	}
	else
	{
		tsize = tile[tilenum].size;
		tformat = tile[tilenum].format;
	}

	tc_pipeline_copy(&sss, &sss1, &sss2, &sss3, &sst, tilenum);
	read_tmem_copy(sss, sss1, sss2, sss3, sst, tilenum, sortshort, hibits, lowbits);
	largetex = (tformat == FORMAT_YUV || (tformat == FORMAT_RGBA && tsize == PIXEL_SIZE_32BIT));

	
	if (other_modes.en_tlut)
	{
		shorta = sortshort[4];
		shortb = sortshort[5];
		shortc = sortshort[6];
		shortd = sortshort[7];
	}
	else if (largetex)
	{
		shorta = sortshort[0];
		shortb = sortshort[1];
		shortc = sortshort[2];
		shortd = sortshort[3];
	}
	else
	{
		shorta = hibits[0] ? sortshort[4] : sortshort[0];
		shortb = hibits[1] ? sortshort[5] : sortshort[1];
		shortc = hibits[3] ? sortshort[6] : sortshort[2];
		shortd = hibits[4] ? sortshort[7] : sortshort[3];
	}

	*lowdword = (shortc << 16) | shortd;

	if (tsize == PIXEL_SIZE_16BIT)
		*hidword = (shorta << 16) | shortb;
	else
	{
		replicate_for_copy(&shorta, shorta, lowbits[0] & 3, tilenum, tformat, tsize);
		replicate_for_copy(&shortb, shortb, lowbits[1] & 3, tilenum, tformat, tsize);
		replicate_for_copy(&shortc, shortc, lowbits[3] & 3, tilenum, tformat, tsize);
		replicate_for_copy(&shortd, shortd, lowbits[4] & 3, tilenum, tformat, tsize);
		*hidword = (shorta << 24) | (shortb << 16) | (shortc << 8) | shortd;
	}
}

STRICTINLINE void texture_pipeline_cycle(COLOR* TEX, COLOR* prev, INT32 SSS, INT32 SST, UINT32 tilenum, UINT32 cycle)											
{
#define TRELATIVE(x, y) 	((x) - ((y) << 3));
#define UPPER ((sfrac + tfrac) & 0x20)




	INT32 maxs, maxt, invt0r, invt0g, invt0b, invt0a;
	INT32 sfrac, tfrac, invsf, invtf;
	int upper = 0;
	int bilerp = cycle ? other_modes.bi_lerp1 : other_modes.bi_lerp0;
	int convert = other_modes.convert_one && cycle;
	COLOR t0, t1, t2, t3;
	int sss1, sst1, sss2, sst2;
	INT32 newk0, newk1, newk2, newk3, invk0, invk1, invk2, invk3;

	sss1 = SSS;
	sst1 = SST;

	tcshift_cycle(&sss1, &sst1, &maxs, &maxt, tilenum);

	sss1 = TRELATIVE(sss1, tile[tilenum].sl);
	sst1 = TRELATIVE(sst1, tile[tilenum].tl);

	if (other_modes.sample_type)
	{	
		sfrac = sss1 & 0x1f;
		tfrac = sst1 & 0x1f;

		tcclamp_cycle(&sss1, &sst1, &sfrac, &tfrac, maxs, maxt, tilenum);
		
	
		if (tile[tilenum].format != FORMAT_YUV)
			sss2 = sss1 + 1;
		else
			sss2 = sss1 + 2;
		
		
		

		sst2 = sst1 + 1;
		

		
		tcmask_coupled(&sss1, &sss2, &sst1, &sst2, tilenum);
		
		

		
		
		
		

		
		if (bilerp)
		{
			
			if (!other_modes.en_tlut)
				fetch_texel_quadro(&t0, &t1, &t2, &t3, sss1, sss2, sst1, sst2, tilenum);
			else
				fetch_texel_entlut_quadro(&t0, &t1, &t2, &t3, sss1, sss2, sst1, sst2, tilenum);

			if (!other_modes.mid_texel || sfrac != 0x10 || tfrac != 0x10)
			{
				if (!convert)
				{
					if (UPPER)
					{
						
						invsf = 0x20 - sfrac;
						invtf = 0x20 - tfrac;
						TEX->r = t3.r + ((((invsf * (t2.r - t3.r)) + (invtf * (t1.r - t3.r))) + 0x10) >> 5);	
						TEX->g = t3.g + ((((invsf * (t2.g - t3.g)) + (invtf * (t1.g - t3.g))) + 0x10) >> 5);																		
						TEX->b = t3.b + ((((invsf * (t2.b - t3.b)) + (invtf * (t1.b - t3.b))) + 0x10) >> 5);																
						TEX->a = t3.a + ((((invsf * (t2.a - t3.a)) + (invtf * (t1.a - t3.a))) + 0x10) >> 5);
					}
					else
					{
						TEX->r = t0.r + ((((sfrac * (t1.r - t0.r)) + (tfrac * (t2.r - t0.r))) + 0x10) >> 5);											
						TEX->g = t0.g + ((((sfrac * (t1.g - t0.g)) + (tfrac * (t2.g - t0.g))) + 0x10) >> 5);											
						TEX->b = t0.b + ((((sfrac * (t1.b - t0.b)) + (tfrac * (t2.b - t0.b))) + 0x10) >> 5);									
						TEX->a = t0.a + ((((sfrac * (t1.a - t0.a)) + (tfrac * (t2.a - t0.a))) + 0x10) >> 5);
					}
				}
				else
				{
					if (UPPER)
					{
						TEX->r = prev->b + ((((prev->r * (t2.r - t3.r)) + (prev->g * (t1.r - t3.r))) + 0x80) >> 8);	
						TEX->g = prev->b + ((((prev->r * (t2.g - t3.g)) + (prev->g * (t1.g - t3.g))) + 0x80) >> 8);																		
						TEX->b = prev->b + ((((prev->r * (t2.b - t3.b)) + (prev->g * (t1.b - t3.b))) + 0x80) >> 8);																
						TEX->a = prev->b + ((((prev->r * (t2.a - t3.a)) + (prev->g * (t1.a - t3.a))) + 0x80) >> 8);
					}
					else
					{
						TEX->r = prev->b + ((((prev->r * (t1.r - t0.r)) + (prev->g * (t2.r - t0.r))) + 0x80) >> 8);											
						TEX->g = prev->b + ((((prev->r * (t1.g - t0.g)) + (prev->g * (t2.g - t0.g))) + 0x80) >> 8);											
						TEX->b = prev->b + ((((prev->r * (t1.b - t0.b)) + (prev->g * (t2.b - t0.b))) + 0x80) >> 8);									
						TEX->a = prev->b + ((((prev->r * (t1.a - t0.a)) + (prev->g * (t2.a - t0.a))) + 0x80) >> 8);
					}	
				}
				
			}
			else
			{
				invt0r  = ~t0.r; invt0g = ~t0.g; invt0b = ~t0.b; invt0a = ~t0.a;
				if (!convert)
				{
					sfrac <<= 2;
					tfrac <<= 2;
					TEX->r = t0.r + ((((sfrac * (t1.r - t0.r)) + (tfrac * (t2.r - t0.r))) + ((invt0r + t3.r) << 6) + 0xc0) >> 8);											
					TEX->g = t0.g + ((((sfrac * (t1.g - t0.g)) + (tfrac * (t2.g - t0.g))) + ((invt0g + t3.g) << 6) + 0xc0) >> 8);											
					TEX->b = t0.b + ((((sfrac * (t1.b - t0.b)) + (tfrac * (t2.b - t0.b))) + ((invt0b + t3.b) << 6) + 0xc0) >> 8);									
					TEX->a = t0.a + ((((sfrac * (t1.a - t0.a)) + (tfrac * (t2.a - t0.a))) + ((invt0a + t3.a) << 6) + 0xc0) >> 8);
				}
				else
				{
					TEX->r = prev->b + ((((prev->r * (t1.r - t0.r)) + (prev->g * (t2.r - t0.r))) + ((invt0r + t3.r) << 6) + 0xc0) >> 8);											
					TEX->g = prev->b + ((((prev->r * (t1.g - t0.g)) + (prev->g * (t2.g - t0.g))) + ((invt0g + t3.g) << 6) + 0xc0) >> 8);											
					TEX->b = prev->b + ((((prev->r * (t1.b - t0.b)) + (prev->g * (t2.b - t0.b))) + ((invt0b + t3.b) << 6) + 0xc0) >> 8);									
					TEX->a = prev->b + ((((prev->r * (t1.a - t0.a)) + (prev->g * (t2.a - t0.a))) + ((invt0a + t3.a) << 6) + 0xc0) >> 8);
				}
			}
			
		}
		else
		{
			newk0 = SIGN(k0, 9); 
			newk1 = SIGN(k1, 9); 
			newk2 = SIGN(k2, 9); 
			newk3 = SIGN(k3, 9);
			invk0 = ~newk0; 
			invk1 = ~newk1; 
			invk2 = ~newk2; 
			invk3 = ~newk3;
			if (!other_modes.en_tlut)
				fetch_texel(&t0, sss1, sst1, tilenum);
			else
				fetch_texel_entlut(&t0, sss1, sst1, tilenum);
			if (convert)
				t0 = *prev;
			t0.r = SIGN(t0.r, 9);
			t0.g = SIGN(t0.g, 9); 
			t0.b = SIGN(t0.b, 9);
			TEX->r = t0.b + ((((newk0 - invk0) * t0.g) + 0x80) >> 8);
			TEX->g = t0.b + ((((newk1 - invk1) * t0.r + (newk2 - invk2) * t0.g) + 0x80) >> 8);
			TEX->b = t0.b + ((((newk3 - invk3) * t0.r) + 0x80) >> 8);
			TEX->a = t0.b;
		}
		
		TEX->r &= 0x1ff;
		TEX->g &= 0x1ff;
		TEX->b &= 0x1ff;
		TEX->a &= 0x1ff;
		
		
	}
	else																								
	{																										
		
		
		

		tcclamp_cycle_light(&sss1, &sst1, maxs, maxt, tilenum);
		
        tcmask(&sss1, &sst1, tilenum);	
																										
			
		if (!other_modes.en_tlut)
			fetch_texel(&t0, sss1, sst1, tilenum);
		else
			fetch_texel_entlut(&t0, sss1, sst1, tilenum);
		
		if (bilerp)
		{
			if (!convert)
				*TEX = t0;
			else
				TEX->r = TEX->g = TEX->b = TEX->a = prev->b;
		}
		else
		{
			newk0 = SIGN(k0, 9); 
			newk1 = SIGN(k1, 9); 
			newk2 = SIGN(k2, 9); 
			newk3 = SIGN(k3, 9);
			invk0 = ~newk0; 
			invk1 = ~newk1; 
			invk2 = ~newk2; 
			invk3 = ~newk3;
			if (convert)
				t0 = *prev;
			t0.r = SIGN(t0.r, 9);
			t0.g = SIGN(t0.g, 9); 
			t0.b = SIGN(t0.b, 9);
			TEX->r = t0.b + ((((newk0 - invk0) * t0.g) + 0x80) >> 8);
			TEX->g = t0.b + ((((newk1 - invk1) * t0.r + (newk2 - invk2) * t0.g) + 0x80) >> 8);
			TEX->b = t0.b + ((((newk3 - invk3) * t0.r) + 0x80) >> 8);
			TEX->a = t0.b;
			TEX->r &= 0x1ff;
			TEX->g &= 0x1ff;
			TEX->b &= 0x1ff;
			TEX->a &= 0x1ff;
		}
	}
																									
}


STRICTINLINE void tc_pipeline_copy(INT32* sss0, INT32* sss1, INT32* sss2, INT32* sss3, INT32* sst, int tilenum)											
{
	int ss0 = *sss0, ss1 = 0, ss2 = 0, ss3 = 0, st = *sst;

	tcshift_copy(&ss0, &st, tilenum);
	
	

	ss0 = TRELATIVE(ss0, tile[tilenum].sl);
	st = TRELATIVE(st, tile[tilenum].tl);
	ss0 = (ss0 >> 5);
	st = (st >> 5);

	ss1 = ss0 + 1;
	ss2 = ss0 + 2;
	ss3 = ss0 + 3;

	tcmask_copy(&ss0, &ss1, &ss2, &ss3, &st, tilenum);	

	*sss0 = ss0;
	*sss1 = ss1;
	*sss2 = ss2;
	*sss3 = ss3;
	*sst = st;
}

STRICTINLINE void tc_pipeline_load(INT32* sss, INT32* sst, int tilenum, int coord_quad)
{
	int sss1 = *sss, sst1 = *sst;
	sss1 = SIGN16(sss1);
	sst1 = SIGN16(sst1);

	
	sss1 = TRELATIVE(sss1, tile[tilenum].sl);
	sst1 = TRELATIVE(sst1, tile[tilenum].tl);
	

	
	if (!coord_quad)
	{
		sss1 = (sss1 >> 5);
		sst1 = (sst1 >> 5);
	}
	else
	{
		sss1 = (sss1 >> 3);
		sst1 = (sst1 >> 3);
	}
	
	*sss = sss1;
	*sst = sst1;
}

void render_spans_1cycle_complete(int start, int end, int tilenum, int flip)
{
	int zb = zb_address >> 1;
	int zbcur;
	UINT8 offx, offy;
	SPANSIGS sigs;
	UINT32 blend_en;
	UINT32 prewrap;
	UINT32 curpixel_cvg, curpixel_cvbit, curpixel_memcvg;

	int prim_tile = tilenum;
	int tile1 = tilenum;
	int newtile = tilenum; 
	int news, newt;

	int i, j;
	
	int drinc, dginc, dbinc, dainc, dzinc, dsinc, dtinc, dwinc;
	int xinc;

	if (flip)
	{
		drinc = spans_dr;
		dginc = spans_dg;
		dbinc = spans_db;
		dainc = spans_da;
		dzinc = spans_dz;
		dsinc = spans_ds;
		dtinc = spans_dt;
		dwinc = spans_dw;
		xinc = 1;
	}
	else
	{
		drinc = -spans_dr;
		dginc = -spans_dg;
		dbinc = -spans_db;
		dainc = -spans_da;
		dzinc = -spans_dz;
		dsinc = -spans_ds;
		dtinc = -spans_dt;
		dwinc = -spans_dw;
		xinc = -1;
	}

	int dzpix;
	if (!other_modes.z_source_sel)
		dzpix = spans_dzpix;
	else
	{
		dzpix = primitive_delta_z;
		dzinc = spans_cdz = spans_dzdy = 0;
	}
	int dzpixenc = dz_compress(dzpix);

	int cdith = 7, adith = 0;
	int r, g, b, a, z, s, t, w;
	int sr, sg, sb, sa, sz, ss, st, sw;
	int xstart, xend, xendsc;
	int sss = 0, sst = 0;
	INT32 prelodfrac;
	int curpixel = 0;
	int x, length, scdiff;
	UINT32 fir, fig, fib;
					
	for (i = start; i <= end; i++)
	{
		if (span[i].validline)
		{

		xstart = span[i].lx;
		xend = span[i].unscrx;
		xendsc = span[i].rx;
		r = span[i].r;
		g = span[i].g;
		b = span[i].b;
		a = span[i].a;
		z = other_modes.z_source_sel ? primitive_z : span[i].z;
		s = span[i].s;
		t = span[i].t;
		w = span[i].w;

		x = xendsc;
		curpixel = fb_width * i + x;
		zbcur = zb + curpixel;

		if (!flip)
		{
			length = xendsc - xstart;
			scdiff = xend - xendsc;
			compute_cvg_noflip(i);
		}
		else
		{
			length = xstart - xendsc;
			scdiff = xendsc - xend;
			compute_cvg_flip(i);
		}
		
		sigs.longspan = (length > 7);
		sigs.midspan = (length == 7);

		
		
		if (scdiff)
		{
			r += (drinc * scdiff);
			g += (dginc * scdiff);
			b += (dbinc * scdiff);
			a += (dainc * scdiff);
			z += (dzinc * scdiff);
			s += (dsinc * scdiff);
			t += (dtinc * scdiff);
			w += (dwinc * scdiff);
		}
		sigs.startspan = 1;

		for (j = 0; j <= length; j++)
		{
			sr = r >> 14;
			sg = g >> 14;
			sb = b >> 14;
			sa = a >> 14;
			ss = s >> 16;
			st = t >> 16;
			sw = w >> 16;
			sz = (z >> 10) & 0x3fffff;
			

			sigs.endspan = (j == length);
			sigs.preendspan = (j == (length - 1));

			lookup_cvmask_derivatives(cvgbuf[x], &offx, &offy, &curpixel_cvg, &curpixel_cvbit);
			

			get_texel1_1cycle(&news, &newt, s, t, w, dsinc, dtinc, dwinc, i, &sigs);

			
			
			if (!sigs.startspan)
			{
				texel0_color = texel1_color;
				lod_frac = prelodfrac;
			}
			else
			{
				tcdiv_ptr(ss, st, sw, &sss, &sst);

				
				tclod_1cycle_current(&sss, &sst, news, newt, s, t, w, dsinc, dtinc, dwinc, i, prim_tile, &tile1, &sigs);
				
				
				
				
				texture_pipeline_cycle(&texel0_color, &texel0_color, sss, sst, tile1, 0);

				
				sigs.startspan = 0;
			}
			
			sigs.nextspan = sigs.endspan;
			sigs.endspan = sigs.preendspan;
			sigs.preendspan = (j == (length - 2));

			s += dsinc;
			t += dtinc;
			w += dwinc;
			
			tclod_1cycle_next(&news, &newt, s, t, w, dsinc, dtinc, dwinc, i, prim_tile, &newtile, &sigs, &prelodfrac);			
			
			texture_pipeline_cycle(&texel1_color, &texel1_color, news, newt, newtile, 0);

			rgbaz_correct_clip(offx, offy, sr, sg, sb, sa, &sz, curpixel_cvg);

			get_dither_noise_ptr(x, i, &cdith, &adith);
			combiner_1cycle(adith, &curpixel_cvg);
				
			fbread1_ptr(curpixel, &curpixel_memcvg);
			if (z_compare(zbcur, sz, dzpix, dzpixenc, &blend_en, &prewrap, &curpixel_cvg, curpixel_memcvg))
			{
				if (blender_1cycle(&fir, &fig, &fib, cdith, blend_en, prewrap, curpixel_cvg, curpixel_cvbit))
				{
					fbwrite_ptr(curpixel, fir, fig, fib, blend_en, curpixel_cvg, curpixel_memcvg);
					if (other_modes.z_update_en)
						z_store(zbcur, sz, dzpixenc);
				}
			}

			
			
			
			r += drinc;
			g += dginc;
			b += dbinc;
			a += dainc;
			z += dzinc;
			
			x += xinc;
			curpixel += xinc;
			zbcur += xinc;
		}
		}
	}
}


void render_spans_1cycle_notexel1(int start, int end, int tilenum, int flip)
{
	int zb = zb_address >> 1;
	int zbcur;
	UINT8 offx, offy;
	SPANSIGS sigs;
	UINT32 blend_en;
	UINT32 prewrap;
	UINT32 curpixel_cvg, curpixel_cvbit, curpixel_memcvg;

	int prim_tile = tilenum;
	int tile1 = tilenum;

	int i, j;

	int drinc, dginc, dbinc, dainc, dzinc, dsinc, dtinc, dwinc;
	int xinc;
	if (flip)
	{
		drinc = spans_dr;
		dginc = spans_dg;
		dbinc = spans_db;
		dainc = spans_da;
		dzinc = spans_dz;
		dsinc = spans_ds;
		dtinc = spans_dt;
		dwinc = spans_dw;
		xinc = 1;
	}
	else
	{
		drinc = -spans_dr;
		dginc = -spans_dg;
		dbinc = -spans_db;
		dainc = -spans_da;
		dzinc = -spans_dz;
		dsinc = -spans_ds;
		dtinc = -spans_dt;
		dwinc = -spans_dw;
		xinc = -1;
	}

	int dzpix;
	if (!other_modes.z_source_sel)
		dzpix = spans_dzpix;
	else
	{
		dzpix = primitive_delta_z;
		dzinc = spans_cdz = spans_dzdy = 0;
	}
	int dzpixenc = dz_compress(dzpix);

	int cdith = 7, adith = 0;
	int r, g, b, a, z, s, t, w;
	int sr, sg, sb, sa, sz, ss, st, sw;
	int xstart, xend, xendsc;
	int sss = 0, sst = 0;
	int curpixel = 0;
	int x, length, scdiff;
	UINT32 fir, fig, fib;
					
	for (i = start; i <= end; i++)
	{
		if (span[i].validline)
		{

		xstart = span[i].lx;
		xend = span[i].unscrx;
		xendsc = span[i].rx;
		r = span[i].r;
		g = span[i].g;
		b = span[i].b;
		a = span[i].a;
		z = other_modes.z_source_sel ? primitive_z : span[i].z;
		s = span[i].s;
		t = span[i].t;
		w = span[i].w;

		x = xendsc;
		curpixel = fb_width * i + x;
		zbcur = zb + curpixel;

		if (!flip)
		{
			length = xendsc - xstart;
			scdiff = xend - xendsc;
			compute_cvg_noflip(i);
		}
		else
		{
			length = xstart - xendsc;
			scdiff = xendsc - xend;
			compute_cvg_flip(i);
		}

		sigs.longspan = (length > 7);
		sigs.midspan = (length == 7);

		if (scdiff)
		{
			r += (drinc * scdiff);
			g += (dginc * scdiff);
			b += (dbinc * scdiff);
			a += (dainc * scdiff);
			z += (dzinc * scdiff);
			s += (dsinc * scdiff);
			t += (dtinc * scdiff);
			w += (dwinc * scdiff);
		}

		for (j = 0; j <= length; j++)
		{
			sr = r >> 14;
			sg = g >> 14;
			sb = b >> 14;
			sa = a >> 14;
			ss = s >> 16;
			st = t >> 16;
			sw = w >> 16;
			sz = (z >> 10) & 0x3fffff;

			sigs.endspan = (j == length);
			sigs.preendspan = (j == (length - 1));

			lookup_cvmask_derivatives(cvgbuf[x], &offx, &offy, &curpixel_cvg, &curpixel_cvbit);

			tcdiv_ptr(ss, st, sw, &sss, &sst);

			tclod_1cycle_current_simple(&sss, &sst, s, t, w, dsinc, dtinc, dwinc, i, prim_tile, &tile1, &sigs);

			texture_pipeline_cycle(&texel0_color, &texel0_color, sss, sst, tile1, 0);

			rgbaz_correct_clip(offx, offy, sr, sg, sb, sa, &sz, curpixel_cvg);

			get_dither_noise_ptr(x, i, &cdith, &adith);
			combiner_1cycle(adith, &curpixel_cvg);
				
			fbread1_ptr(curpixel, &curpixel_memcvg);
			if (z_compare(zbcur, sz, dzpix, dzpixenc, &blend_en, &prewrap, &curpixel_cvg, curpixel_memcvg))
			{
				if (blender_1cycle(&fir, &fig, &fib, cdith, blend_en, prewrap, curpixel_cvg, curpixel_cvbit))
				{
					fbwrite_ptr(curpixel, fir, fig, fib, blend_en, curpixel_cvg, curpixel_memcvg);
					if (other_modes.z_update_en)
						z_store(zbcur, sz, dzpixenc);
				}
			}

			s += dsinc;
			t += dtinc;
			w += dwinc;
			r += drinc;
			g += dginc;
			b += dbinc;
			a += dainc;
			z += dzinc;
			
			x += xinc;
			curpixel += xinc;
			zbcur += xinc;
		}
		}
	}
}


void render_spans_1cycle_notex(int start, int end, int tilenum, int flip)
{
	int zb = zb_address >> 1;
	int zbcur;
	UINT8 offx, offy;
	UINT32 blend_en;
	UINT32 prewrap;
	UINT32 curpixel_cvg, curpixel_cvbit, curpixel_memcvg;

	int i, j;

	int drinc, dginc, dbinc, dainc, dzinc;
	int xinc;

	if (flip)
	{
		drinc = spans_dr;
		dginc = spans_dg;
		dbinc = spans_db;
		dainc = spans_da;
		dzinc = spans_dz;
		xinc = 1;
	}
	else
	{
		drinc = -spans_dr;
		dginc = -spans_dg;
		dbinc = -spans_db;
		dainc = -spans_da;
		dzinc = -spans_dz;
		xinc = -1;
	}
	
	int dzpix;
	if (!other_modes.z_source_sel)
		dzpix = spans_dzpix;
	else
	{
		dzpix = primitive_delta_z;
		dzinc = spans_cdz = spans_dzdy = 0;
	}
	int dzpixenc = dz_compress(dzpix);

	int cdith = 7, adith = 0;
	int r, g, b, a, z;
	int sr, sg, sb, sa, sz;
	int xstart, xend, xendsc;
	int curpixel = 0;
	int x, length, scdiff;
	UINT32 fir, fig, fib;
					
	for (i = start; i <= end; i++)
	{
		if (span[i].validline)
		{

		xstart = span[i].lx;
		xend = span[i].unscrx;
		xendsc = span[i].rx;
		r = span[i].r;
		g = span[i].g;
		b = span[i].b;
		a = span[i].a;
		z = other_modes.z_source_sel ? primitive_z : span[i].z;

		x = xendsc;
		curpixel = fb_width * i + x;
		zbcur = zb + curpixel;

		if (!flip)
		{
			length = xendsc - xstart;
			scdiff = xend - xendsc;
			compute_cvg_noflip(i);
		}
		else
		{
			length = xstart - xendsc;
			scdiff = xendsc - xend;
			compute_cvg_flip(i);
		}

		if (scdiff)
		{
			r += (drinc * scdiff);
			g += (dginc * scdiff);
			b += (dbinc * scdiff);
			a += (dainc * scdiff);
			z += (dzinc * scdiff);
		}

		for (j = 0; j <= length; j++)
		{
			sr = r >> 14;
			sg = g >> 14;
			sb = b >> 14;
			sa = a >> 14;
			sz = (z >> 10) & 0x3fffff;

			lookup_cvmask_derivatives(cvgbuf[x], &offx, &offy, &curpixel_cvg, &curpixel_cvbit);

			rgbaz_correct_clip(offx, offy, sr, sg, sb, sa, &sz, curpixel_cvg);

			get_dither_noise_ptr(x, i, &cdith, &adith);
			combiner_1cycle(adith, &curpixel_cvg);
				
			fbread1_ptr(curpixel, &curpixel_memcvg);
			if (z_compare(zbcur, sz, dzpix, dzpixenc, &blend_en, &prewrap, &curpixel_cvg, curpixel_memcvg))
			{
				if (blender_1cycle(&fir, &fig, &fib, cdith, blend_en, prewrap, curpixel_cvg, curpixel_cvbit))
				{
					fbwrite_ptr(curpixel, fir, fig, fib, blend_en, curpixel_cvg, curpixel_memcvg);
					if (other_modes.z_update_en)
						z_store(zbcur, sz, dzpixenc);
				}
			}
			r += drinc;
			g += dginc;
			b += dbinc;
			a += dainc;
			z += dzinc;
			
			x += xinc;
			curpixel += xinc;
			zbcur += xinc;
		}
		}
	}
}

void render_spans_2cycle_complete(int start, int end, int tilenum, int flip)
{
	int zb = zb_address >> 1;
	int zbcur;
	UINT8 offx, offy;
	SPANSIGS sigs;
	INT32 prelodfrac;
	COLOR nexttexel1_color;
	UINT32 blend_en;
	UINT32 prewrap;
	UINT32 curpixel_cvg, curpixel_cvbit, curpixel_memcvg;

	
	
	int tile2 = (tilenum + 1) & 7;
	int tile1 = tilenum;
	int prim_tile = tilenum;

	int newtile1 = tile1;
	int newtile2 = tile2;
	int news, newt;

	int i, j;

	int drinc, dginc, dbinc, dainc, dzinc, dsinc, dtinc, dwinc;
	int xinc;
	if (flip)
	{
		drinc = spans_dr;
		dginc = spans_dg;
		dbinc = spans_db;
		dainc = spans_da;
		dzinc = spans_dz;
		dsinc = spans_ds;
		dtinc = spans_dt;
		dwinc = spans_dw;
		xinc = 1;
	}
	else
	{
		drinc = -spans_dr;
		dginc = -spans_dg;
		dbinc = -spans_db;
		dainc = -spans_da;
		dzinc = -spans_dz;
		dsinc = -spans_ds;
		dtinc = -spans_dt;
		dwinc = -spans_dw;
		xinc = -1;
	}

	int dzpix;
	if (!other_modes.z_source_sel)
		dzpix = spans_dzpix;
	else
	{
		dzpix = primitive_delta_z;
		dzinc = spans_cdz = spans_dzdy = 0;
	}
	int dzpixenc = dz_compress(dzpix);

	int cdith = 7, adith = 0;
	int r, g, b, a, z, s, t, w;
	int sr, sg, sb, sa, sz, ss, st, sw;
	int xstart, xend, xendsc;
	int sss = 0, sst = 0;
	int curpixel = 0;
	
	int x, length, scdiff;
	UINT32 fir, fig, fib;
				
	for (i = start; i <= end; i++)
	{
		if (span[i].validline)
		{

		xstart = span[i].lx;
		xend = span[i].unscrx;
		xendsc = span[i].rx;
		r = span[i].r;
		g = span[i].g;
		b = span[i].b;
		a = span[i].a;
		z = other_modes.z_source_sel ? primitive_z : span[i].z;
		s = span[i].s;
		t = span[i].t;
		w = span[i].w;

		x = xendsc;
		curpixel = fb_width * i + x;
		zbcur = zb + curpixel;

		if (!flip)
		{
			length = xendsc - xstart;
			scdiff = xend - xendsc;
			compute_cvg_noflip(i);
		}
		else
		{
			length = xstart - xendsc;
			scdiff = xendsc - xend;
			compute_cvg_flip(i);
		}

		
		

		
		

		

		if (scdiff)
		{
			r += (drinc * scdiff);
			g += (dginc * scdiff);
			b += (dbinc * scdiff);
			a += (dainc * scdiff);
			z += (dzinc * scdiff);
			s += (dsinc * scdiff);
			t += (dtinc * scdiff);
			w += (dwinc * scdiff);
		}
		sigs.startspan = 1;

		for (j = 0; j <= length; j++)
		{
			sr = r >> 14;
			sg = g >> 14;
			sb = b >> 14;
			sa = a >> 14;
			ss = s >> 16;
			st = t >> 16;
			sw = w >> 16;
			sz = (z >> 10) & 0x3fffff;
			

			lookup_cvmask_derivatives(cvgbuf[x], &offx, &offy, &curpixel_cvg, &curpixel_cvbit);

			get_nexttexel0_2cycle(&news, &newt, s, t, w, dsinc, dtinc, dwinc);
			
			if (!sigs.startspan)
			{
				lod_frac = prelodfrac;
				texel0_color = nexttexel_color;
				texel1_color = nexttexel1_color;
			}
			else
			{
				tcdiv_ptr(ss, st, sw, &sss, &sst);

				tclod_2cycle_current(&sss, &sst, news, newt, s, t, w, dsinc, dtinc, dwinc, prim_tile, &tile1, &tile2);
				

				
				texture_pipeline_cycle(&texel0_color, &texel0_color, sss, sst, tile1, 0);
				texture_pipeline_cycle(&texel1_color, &texel0_color, sss, sst, tile2, 1);

				sigs.startspan = 0;
			}

			s += dsinc;
			t += dtinc;
			w += dwinc;

			tclod_2cycle_next(&news, &newt, s, t, w, dsinc, dtinc, dwinc, prim_tile, &newtile1, &newtile2, &prelodfrac);

			texture_pipeline_cycle(&nexttexel_color, &nexttexel_color, news, newt, newtile1, 0);
			texture_pipeline_cycle(&nexttexel1_color, &nexttexel_color, news, newt, newtile2, 1);

			rgbaz_correct_clip(offx, offy, sr, sg, sb, sa, &sz, curpixel_cvg);
					
			get_dither_noise_ptr(x, i, &cdith, &adith);
			combiner_2cycle(adith, &curpixel_cvg);
				
			fbread2_ptr(curpixel, &curpixel_memcvg);
			
			
			
			
			
			
			if (z_compare(zbcur, sz, dzpix, dzpixenc, &blend_en, &prewrap, &curpixel_cvg, curpixel_memcvg))
			{
				if (blender_2cycle(&fir, &fig, &fib, cdith, blend_en, prewrap, curpixel_cvg, curpixel_cvbit))
				{
					fbwrite_ptr(curpixel, fir, fig, fib, blend_en, curpixel_cvg, curpixel_memcvg);
					if (other_modes.z_update_en)
						z_store(zbcur, sz, dzpixenc);
					
				}
			}
			
			
			
			
			
			
			
			
			
			

			memory_color = pre_memory_color;
			pastblshifta = blshifta;
			pastblshiftb = blshiftb;
			r += drinc;
			g += dginc;
			b += dbinc;
			a += dainc;
			z += dzinc;
			
			x += xinc;
			curpixel += xinc;
			zbcur += xinc;
		}
		}
	}
}



void render_spans_2cycle_notexelnext(int start, int end, int tilenum, int flip)
{
	int zb = zb_address >> 1;
	int zbcur;
	UINT8 offx, offy;
	UINT32 blend_en;
	UINT32 prewrap;
	UINT32 curpixel_cvg, curpixel_cvbit, curpixel_memcvg;

	int tile2 = (tilenum + 1) & 7;
	int tile1 = tilenum;
	int prim_tile = tilenum;

	int i, j;

	int drinc, dginc, dbinc, dainc, dzinc, dsinc, dtinc, dwinc;
	int xinc;
	if (flip)
	{
		drinc = spans_dr;
		dginc = spans_dg;
		dbinc = spans_db;
		dainc = spans_da;
		dzinc = spans_dz;
		dsinc = spans_ds;
		dtinc = spans_dt;
		dwinc = spans_dw;
		xinc = 1;
	}
	else
	{
		drinc = -spans_dr;
		dginc = -spans_dg;
		dbinc = -spans_db;
		dainc = -spans_da;
		dzinc = -spans_dz;
		dsinc = -spans_ds;
		dtinc = -spans_dt;
		dwinc = -spans_dw;
		xinc = -1;
	}

	int dzpix;
	if (!other_modes.z_source_sel)
		dzpix = spans_dzpix;
	else
	{
		dzpix = primitive_delta_z;
		dzinc = spans_cdz = spans_dzdy = 0;
	}
	int dzpixenc = dz_compress(dzpix);

	int cdith = 7, adith = 0;
	int r, g, b, a, z, s, t, w;
	int sr, sg, sb, sa, sz, ss, st, sw;
	int xstart, xend, xendsc;
	int sss = 0, sst = 0;
	int curpixel = 0;
	
	int x, length, scdiff;
	UINT32 fir, fig, fib;
				
	for (i = start; i <= end; i++)
	{
		if (span[i].validline)
		{

		xstart = span[i].lx;
		xend = span[i].unscrx;
		xendsc = span[i].rx;
		r = span[i].r;
		g = span[i].g;
		b = span[i].b;
		a = span[i].a;
		z = other_modes.z_source_sel ? primitive_z : span[i].z;
		s = span[i].s;
		t = span[i].t;
		w = span[i].w;

		x = xendsc;
		curpixel = fb_width * i + x;
		zbcur = zb + curpixel;

		if (!flip)
		{
			length = xendsc - xstart;
			scdiff = xend - xendsc;
			compute_cvg_noflip(i);
		}
		else
		{
			length = xstart - xendsc;
			scdiff = xendsc - xend;
			compute_cvg_flip(i);
		}

		if (scdiff)
		{
			r += (drinc * scdiff);
			g += (dginc * scdiff);
			b += (dbinc * scdiff);
			a += (dainc * scdiff);
			z += (dzinc * scdiff);
			s += (dsinc * scdiff);
			t += (dtinc * scdiff);
			w += (dwinc * scdiff);
		}

		for (j = 0; j <= length; j++)
		{
			sr = r >> 14;
			sg = g >> 14;
			sb = b >> 14;
			sa = a >> 14;
			ss = s >> 16;
			st = t >> 16;
			sw = w >> 16;
			sz = (z >> 10) & 0x3fffff;

			lookup_cvmask_derivatives(cvgbuf[x], &offx, &offy, &curpixel_cvg, &curpixel_cvbit);
			
			tcdiv_ptr(ss, st, sw, &sss, &sst);

			tclod_2cycle_current_simple(&sss, &sst, s, t, w, dsinc, dtinc, dwinc, prim_tile, &tile1, &tile2);
				
			texture_pipeline_cycle(&texel0_color, &texel0_color, sss, sst, tile1, 0);
			texture_pipeline_cycle(&texel1_color, &texel0_color, sss, sst, tile2, 1);

			rgbaz_correct_clip(offx, offy, sr, sg, sb, sa, &sz, curpixel_cvg);
					
			get_dither_noise_ptr(x, i, &cdith, &adith);
			combiner_2cycle(adith, &curpixel_cvg);
				
			fbread2_ptr(curpixel, &curpixel_memcvg);

			if (z_compare(zbcur, sz, dzpix, dzpixenc, &blend_en, &prewrap, &curpixel_cvg, curpixel_memcvg))
			{
				if (blender_2cycle(&fir, &fig, &fib, cdith, blend_en, prewrap, curpixel_cvg, curpixel_cvbit))
				{
					fbwrite_ptr(curpixel, fir, fig, fib, blend_en, curpixel_cvg, curpixel_memcvg);
					if (other_modes.z_update_en)
						z_store(zbcur, sz, dzpixenc);
				}
			}

			memory_color = pre_memory_color;
			pastblshifta = blshifta;
			pastblshiftb = blshiftb;
			s += dsinc;
			t += dtinc;
			w += dwinc;
			r += drinc;
			g += dginc;
			b += dbinc;
			a += dainc;
			z += dzinc;
			
			x += xinc;
			curpixel += xinc;
			zbcur += xinc;
		}
		}
	}
}


void render_spans_2cycle_notexel1(int start, int end, int tilenum, int flip)
{
	int zb = zb_address >> 1;
	int zbcur;
	UINT8 offx, offy;
	UINT32 blend_en;
	UINT32 prewrap;
	UINT32 curpixel_cvg, curpixel_cvbit, curpixel_memcvg;

	int tile1 = tilenum;
	int prim_tile = tilenum;

	int i, j;

	int drinc, dginc, dbinc, dainc, dzinc, dsinc, dtinc, dwinc;
	int xinc;
	if (flip)
	{
		drinc = spans_dr;
		dginc = spans_dg;
		dbinc = spans_db;
		dainc = spans_da;
		dzinc = spans_dz;
		dsinc = spans_ds;
		dtinc = spans_dt;
		dwinc = spans_dw;
		xinc = 1;
	}
	else
	{
		drinc = -spans_dr;
		dginc = -spans_dg;
		dbinc = -spans_db;
		dainc = -spans_da;
		dzinc = -spans_dz;
		dsinc = -spans_ds;
		dtinc = -spans_dt;
		dwinc = -spans_dw;
		xinc = -1;
	}

	int dzpix;
	if (!other_modes.z_source_sel)
		dzpix = spans_dzpix;
	else
	{
		dzpix = primitive_delta_z;
		dzinc = spans_cdz = spans_dzdy = 0;
	}
	int dzpixenc = dz_compress(dzpix);

	int cdith = 7, adith = 0;
	int r, g, b, a, z, s, t, w;
	int sr, sg, sb, sa, sz, ss, st, sw;
	int xstart, xend, xendsc;
	int sss = 0, sst = 0;
	int curpixel = 0;
	
	int x, length, scdiff;
	UINT32 fir, fig, fib;
				
	for (i = start; i <= end; i++)
	{
		if (span[i].validline)
		{

		xstart = span[i].lx;
		xend = span[i].unscrx;
		xendsc = span[i].rx;
		r = span[i].r;
		g = span[i].g;
		b = span[i].b;
		a = span[i].a;
		z = other_modes.z_source_sel ? primitive_z : span[i].z;
		s = span[i].s;
		t = span[i].t;
		w = span[i].w;

		x = xendsc;
		curpixel = fb_width * i + x;
		zbcur = zb + curpixel;

		if (!flip)
		{
			length = xendsc - xstart;
			scdiff = xend - xendsc;
			compute_cvg_noflip(i);
		}
		else
		{
			length = xstart - xendsc;
			scdiff = xendsc - xend;
			compute_cvg_flip(i);
		}

		if (scdiff)
		{
			r += (drinc * scdiff);
			g += (dginc * scdiff);
			b += (dbinc * scdiff);
			a += (dainc * scdiff);
			z += (dzinc * scdiff);
			s += (dsinc * scdiff);
			t += (dtinc * scdiff);
			w += (dwinc * scdiff);
		}

		for (j = 0; j <= length; j++)
		{
			sr = r >> 14;
			sg = g >> 14;
			sb = b >> 14;
			sa = a >> 14;
			ss = s >> 16;
			st = t >> 16;
			sw = w >> 16;
			sz = (z >> 10) & 0x3fffff;

			lookup_cvmask_derivatives(cvgbuf[x], &offx, &offy, &curpixel_cvg, &curpixel_cvbit);
			
			tcdiv_ptr(ss, st, sw, &sss, &sst);

			tclod_2cycle_current_notexel1(&sss, &sst, s, t, w, dsinc, dtinc, dwinc, prim_tile, &tile1);
			
			
			texture_pipeline_cycle(&texel0_color, &texel0_color, sss, sst, tile1, 0);

			rgbaz_correct_clip(offx, offy, sr, sg, sb, sa, &sz, curpixel_cvg);
					
			get_dither_noise_ptr(x, i, &cdith, &adith);
			combiner_2cycle(adith, &curpixel_cvg);
				
			fbread2_ptr(curpixel, &curpixel_memcvg);

			if (z_compare(zbcur, sz, dzpix, dzpixenc, &blend_en, &prewrap, &curpixel_cvg, curpixel_memcvg))
			{
				if (blender_2cycle(&fir, &fig, &fib, cdith, blend_en, prewrap, curpixel_cvg, curpixel_cvbit))
				{
					fbwrite_ptr(curpixel, fir, fig, fib, blend_en, curpixel_cvg, curpixel_memcvg);
					if (other_modes.z_update_en)
						z_store(zbcur, sz, dzpixenc);
				}
			}

			memory_color = pre_memory_color;
			pastblshifta = blshifta;
			pastblshiftb = blshiftb;
			s += dsinc;
			t += dtinc;
			w += dwinc;
			r += drinc;
			g += dginc;
			b += dbinc;
			a += dainc;
			z += dzinc;
			
			x += xinc;
			curpixel += xinc;
			zbcur += xinc;
		}
		}
	}
}


void render_spans_2cycle_notex(int start, int end, int tilenum, int flip)
{
	int zb = zb_address >> 1;
	int zbcur;
	UINT8 offx, offy;
	int i, j;
	UINT32 blend_en;
	UINT32 prewrap;
	UINT32 curpixel_cvg, curpixel_cvbit, curpixel_memcvg;

	int drinc, dginc, dbinc, dainc, dzinc;
	int xinc;
	if (flip)
	{
		drinc = spans_dr;
		dginc = spans_dg;
		dbinc = spans_db;
		dainc = spans_da;
		dzinc = spans_dz;
		xinc = 1;
	}
	else
	{
		drinc = -spans_dr;
		dginc = -spans_dg;
		dbinc = -spans_db;
		dainc = -spans_da;
		dzinc = -spans_dz;
		xinc = -1;
	}

	int dzpix;
	if (!other_modes.z_source_sel)
		dzpix = spans_dzpix;
	else
	{
		dzpix = primitive_delta_z;
		dzinc = spans_cdz = spans_dzdy = 0;
	}
	int dzpixenc = dz_compress(dzpix);

	int cdith = 7, adith = 0;
	int r, g, b, a, z;
	int sr, sg, sb, sa, sz;
	int xstart, xend, xendsc;
	int curpixel = 0;
	
	int x, length, scdiff;
	UINT32 fir, fig, fib;
				
	for (i = start; i <= end; i++)
	{
		if (span[i].validline)
		{

		xstart = span[i].lx;
		xend = span[i].unscrx;
		xendsc = span[i].rx;
		r = span[i].r;
		g = span[i].g;
		b = span[i].b;
		a = span[i].a;
		z = other_modes.z_source_sel ? primitive_z : span[i].z;

		x = xendsc;
		curpixel = fb_width * i + x;
		zbcur = zb + curpixel;

		if (!flip)
		{
			length = xendsc - xstart;
			scdiff = xend - xendsc;
			compute_cvg_noflip(i);
		}
		else
		{
			length = xstart - xendsc;
			scdiff = xendsc - xend;
			compute_cvg_flip(i);
		}

		if (scdiff)
		{
			r += (drinc * scdiff);
			g += (dginc * scdiff);
			b += (dbinc * scdiff);
			a += (dainc * scdiff);
			z += (dzinc * scdiff);
		}

		for (j = 0; j <= length; j++)
		{
			sr = r >> 14;
			sg = g >> 14;
			sb = b >> 14;
			sa = a >> 14;
			sz = (z >> 10) & 0x3fffff;

			lookup_cvmask_derivatives(cvgbuf[x], &offx, &offy, &curpixel_cvg, &curpixel_cvbit);

			rgbaz_correct_clip(offx, offy, sr, sg, sb, sa, &sz, curpixel_cvg);
					
			get_dither_noise_ptr(x, i, &cdith, &adith);
			combiner_2cycle(adith, &curpixel_cvg);
				
			fbread2_ptr(curpixel, &curpixel_memcvg);

			if (z_compare(zbcur, sz, dzpix, dzpixenc, &blend_en, &prewrap, &curpixel_cvg, curpixel_memcvg))
			{
				if (blender_2cycle(&fir, &fig, &fib, cdith, blend_en, prewrap, curpixel_cvg, curpixel_cvbit))
				{
					fbwrite_ptr(curpixel, fir, fig, fib, blend_en, curpixel_cvg, curpixel_memcvg);
					if (other_modes.z_update_en)
						z_store(zbcur, sz, dzpixenc);
				}
			}

			memory_color = pre_memory_color;
			pastblshifta = blshifta;
			pastblshiftb = blshiftb;
			r += drinc;
			g += dginc;
			b += dbinc;
			a += dainc;
			z += dzinc;
			
			x += xinc;
			curpixel += xinc;
			zbcur += xinc;
		}
		}
	}
}

void render_spans_fill(int start, int end, int flip)
{
	if (fb_size == PIXEL_SIZE_4BIT)
	{
		rdp_pipeline_crashed = 1;
		return;
	}

	int i, j;
	
	int fastkillbits = other_modes.image_read_en || other_modes.z_compare_en;
	int slowkillbits = other_modes.z_update_en && !other_modes.z_source_sel && !fastkillbits;

	int xinc = flip ? 1 : -1;

	int xstart = 0, xendsc;
	int prevxstart;
	int curpixel = 0;
	int x, length;
				
	for (i = start; i <= end; i++)
	{
		prevxstart = xstart;
		xstart = span[i].lx;
		xendsc = span[i].rx;

		x = xendsc;
		curpixel = fb_width * i + x;
		length = flip ? (xstart - xendsc) : (xendsc - xstart);

		if (span[i].validline)
		{
			if (fastkillbits && length >= 0)
			{
				if (!onetimewarnings.fillmbitcrashes)
					popmessage("render_spans_fill: image_read_en %x z_update_en %x z_compare_en %x. RDP crashed",
					other_modes.image_read_en, other_modes.z_update_en, other_modes.z_compare_en);
				onetimewarnings.fillmbitcrashes = 1;
				rdp_pipeline_crashed = 1;
				return;
			}

			
			
			
			
			for (j = 0; j <= length; j++)
			{
				fbfill_ptr(curpixel);
				x += xinc;
				curpixel += xinc;
			}

			if (slowkillbits && length >= 0)
			{
				if (!onetimewarnings.fillmbitcrashes)
					popmessage("render_spans_fill: image_read_en %x z_update_en %x z_compare_en %x z_source_sel %x. RDP crashed",
					other_modes.image_read_en, other_modes.z_update_en, other_modes.z_compare_en, other_modes.z_source_sel);
				onetimewarnings.fillmbitcrashes = 1;
				rdp_pipeline_crashed = 1;
				return;
			}
		}
	}
}

void render_spans_copy(int start, int end, int tilenum, int flip)
{
	int i, j, k;

	if (fb_size == PIXEL_SIZE_32BIT)
	{
		rdp_pipeline_crashed = 1;
		return;
	}
	
	int tile1 = tilenum;
	int prim_tile = tilenum;

	int dsinc, dtinc, dwinc;
	int xinc;
	if (flip)
	{
		dsinc = spans_ds;
		dtinc = spans_dt;
		dwinc = spans_dw;
		xinc = 1;
	}
	else
	{
		dsinc = -spans_ds;
		dtinc = -spans_dt;
		dwinc = -spans_dw;
		xinc = -1;
	}

	int xstart = 0, xendsc;
	int s = 0, t = 0, w = 0, ss = 0, st = 0, sw = 0, sss = 0, sst = 0, ssw = 0;
	int fb_index, length;
	int diff = 0;

	UINT32 hidword = 0, lowdword = 0;
	UINT32 hidword1 = 0, lowdword1 = 0;
	int fbadvance = (fb_size == PIXEL_SIZE_4BIT) ? 8 : 16 >> fb_size;
	UINT32 fbptr = 0;
	int fbptr_advance = flip ? 8 : -8;
	UINT64 copyqword = 0;
	UINT32 tempdword = 0, tempbyte = 0;
	int copywmask = 0, alphamask = 0;
	int bytesperpixel = (fb_size == PIXEL_SIZE_4BIT) ? 1 : (1 << (fb_size - 1));
	UINT32 fbendptr = 0;
	INT32 threshold, currthreshold;

#define PIXELS_TO_BYTES_SPECIAL4(pix, siz) ((siz) ? PIXELS_TO_BYTES(pix, siz) : (pix))
				
	for (i = start; i <= end; i++)
	{
		if (span[i].validline)
		{

		s = span[i].s;
		t = span[i].t;
		w = span[i].w;
		
		xstart = span[i].lx;
		xendsc = span[i].rx;

		fb_index = fb_width * i + xendsc;
		fbptr = fb_address + PIXELS_TO_BYTES_SPECIAL4(fb_index, fb_size);
		fbendptr = fb_address + PIXELS_TO_BYTES_SPECIAL4((fb_width * i + xstart), fb_size);
		length = flip ? (xstart - xendsc) : (xendsc - xstart);

		
		

		for (j = 0; j <= length; j += fbadvance)
		{
			ss = s >> 16;
			st = t >> 16;
			sw = w >> 16;

			tcdiv_ptr(ss, st, sw, &sss, &sst);
			
			tclod_copy(&sss, &sst, s, t, w, dsinc, dtinc, dwinc, prim_tile, &tile1);
			
			
			
			fetch_qword_copy(&hidword, &lowdword, sss, sst, tile1);

			
			
			if (fb_size == PIXEL_SIZE_16BIT || fb_size == PIXEL_SIZE_8BIT)
				copyqword = ((UINT64)hidword << 32) | ((UINT64)lowdword);
			else
				copyqword = 0;
			
			
			if (!other_modes.alpha_compare_en)
				alphamask = 0xff;
			else if (fb_size == PIXEL_SIZE_16BIT)
			{
				alphamask = 0;
				alphamask |= (((copyqword >> 48) & 1) ? 0xC0 : 0);
				alphamask |= (((copyqword >> 32) & 1) ? 0x30 : 0);
				alphamask |= (((copyqword >> 16) & 1) ? 0xC : 0);
				alphamask |= ((copyqword & 1) ? 0x3 : 0);
			}
			else if (fb_size == PIXEL_SIZE_8BIT)
			{
				alphamask = 0;
				threshold = (other_modes.dither_alpha_en) ? (irand() & 0xff) : blend_color.a;
				if (other_modes.dither_alpha_en)
				{
					currthreshold = threshold;
					alphamask |= (((copyqword >> 24) & 0xff) >= currthreshold ? 0xC0 : 0);
					currthreshold = ((threshold & 3) << 6) | (threshold >> 2);
					alphamask |= (((copyqword >> 16) & 0xff) >= currthreshold ? 0x30 : 0);
					currthreshold = ((threshold & 0xf) << 4) | (threshold >> 4);
					alphamask |= (((copyqword >> 8) & 0xff) >= currthreshold ? 0xC : 0);
					currthreshold = ((threshold & 0x3f) << 2) | (threshold >> 6);
					alphamask |= ((copyqword & 0xff) >= currthreshold ? 0x3 : 0);	
				}
				else
				{
					alphamask |= (((copyqword >> 24) & 0xff) >= threshold ? 0xC0 : 0);
					alphamask |= (((copyqword >> 16) & 0xff) >= threshold ? 0x30 : 0);
					alphamask |= (((copyqword >> 8) & 0xff) >= threshold ? 0xC : 0);
					alphamask |= ((copyqword & 0xff) >= threshold ? 0x3 : 0);
				}
			}
			else
				alphamask = 0;

			copywmask = (flip) ? (fbendptr - fbptr + bytesperpixel) : (fbptr - fbendptr + bytesperpixel);
			
			if (copywmask > 8) 
				copywmask = 8;
			tempdword = fbptr;
			k = 7;
			while(copywmask > 0)
			{
				tempbyte = (UINT32)((copyqword >> (k << 3)) & 0xff);
				if (alphamask & (1 << k))
				{
					PAIRWRITE8(tempdword, tempbyte, (tempbyte & 1) ? 3 : 0);
				}
				k--;
				tempdword += xinc;
				copywmask--;
			}
			
			s += dsinc;
			t += dtinc;
			w += dwinc;
			fbptr += fbptr_advance;
		}
		}
	}
}


void loading_pipeline(int start, int end, int tilenum, int coord_quad, int ltlut)
{


	int localdebugmode = 0, cnt = 0;
	int i, j;

	int dsinc, dtinc;
	dsinc = spans_ds;
	dtinc = spans_dt;

	int s, t;
	int ss, st;
	int xstart, xend, xendsc;
	int sss = 0, sst = 0;
	int ti_index, length;

	UINT32 tmemidx0 = 0, tmemidx1 = 0, tmemidx2 = 0, tmemidx3 = 0;
	int dswap = 0;
	UINT16* tmem16 = (UINT16*)TMEM;
	UINT32 readval0, readval1, readval2, readval3;
	UINT32 readidx32;
	UINT64 loadqword;
	UINT16 tempshort;
	int tmem_formatting = 0;
	UINT32 bit3fl = 0, hibit = 0;

	if (end > start && ltlut)
	{
		rdp_pipeline_crashed = 1;
		return;
	}

	if (tile[tilenum].format == FORMAT_YUV)
		tmem_formatting = 0;
	else if (tile[tilenum].format == FORMAT_RGBA && tile[tilenum].size == PIXEL_SIZE_32BIT)
		tmem_formatting = 1;
	else
		tmem_formatting = 2;

	int tiadvance = 0, spanadvance = 0;
	int tiptr = 0;
	switch (ti_size)
	{
	case PIXEL_SIZE_4BIT:
		rdp_pipeline_crashed = 1;
		return;
		break;
	case PIXEL_SIZE_8BIT:
		tiadvance = 8;
		spanadvance = 8;
		break;
	case PIXEL_SIZE_16BIT:
		if (!ltlut)
		{
			tiadvance = 8;
			spanadvance = 4;
		}
		else
		{
			tiadvance = 2;
			spanadvance = 1;
		}
		break;
	case PIXEL_SIZE_32BIT:
		tiadvance = 8;
		spanadvance = 2;
		break;
	}

	for (i = start; i <= end; i++)
	{
		xstart = span[i].lx;
		xend = span[i].unscrx;
		xendsc = span[i].rx;
		s = span[i].s;
		t = span[i].t;

		ti_index = ti_width * i + xend;
		tiptr = ti_address + PIXELS_TO_BYTES(ti_index, ti_size);

		length = (xstart - xend + 1) & 0xfff;

		
		for (j = 0; j < length; j+= spanadvance)
		{
			ss = s >> 16;
			st = t >> 16;

			
			
			
			
			
			
			sss = ss & 0xffff;
			sst = st & 0xffff;

			tc_pipeline_load(&sss, &sst, tilenum, coord_quad);

			dswap = sst & 1;

			
			get_tmem_idx(sss, sst, tilenum, &tmemidx0, &tmemidx1, &tmemidx2, &tmemidx3, &bit3fl, &hibit);

			readidx32 = (tiptr >> 2) & ~1;
			readval0 = RREADIDX32(readidx32);
			readval1 = RREADIDX32(readidx32 + 1);
			readval2 = RREADIDX32(readidx32 + 2);
			readval3 = RREADIDX32(readidx32 + 3);

			
			switch(tiptr & 7)
			{
			case 0:
				if (!ltlut)
					loadqword = ((UINT64)readval0 << 32) | readval1;
				else
				{
					tempshort = readval0 >> 16;
					loadqword = ((UINT64)tempshort << 48) | ((UINT64) tempshort << 32) | ((UINT64) tempshort << 16) | tempshort;
				}
				break;
			case 1:
				loadqword = ((UINT64)readval0 << 40) | ((UINT64)readval1 << 8) | (readval2 >> 24);
				break;
			case 2:
				if (!ltlut)
					loadqword = ((UINT64)readval0 << 48) | ((UINT64)readval1 << 16) | (readval2 >> 16);
				else
				{
					tempshort = readval0 & 0xffff;
					loadqword = ((UINT64)tempshort << 48) | ((UINT64) tempshort << 32) | ((UINT64) tempshort << 16) | tempshort;
				}
				break;
			case 3:
				loadqword = ((UINT64)readval0 << 56) | ((UINT64)readval1 << 24) | (readval2 >> 8);
				break;
			case 4:
				if (!ltlut)
					loadqword = ((UINT64)readval1 << 32) | readval2;
				else
				{
					tempshort = readval1 >> 16;
					loadqword = ((UINT64)tempshort << 48) | ((UINT64) tempshort << 32) | ((UINT64) tempshort << 16) | tempshort;
				}
				break;
			case 5:
				loadqword = ((UINT64)readval1 << 40) | ((UINT64)readval2 << 8) | (readval3 >> 24);
				break;
			case 6:
				if (!ltlut)
					loadqword = ((UINT64)readval1 << 48) | ((UINT64)readval2 << 16) | (readval3 >> 16);
				else
				{
					tempshort = readval1 & 0xffff;
					loadqword = ((UINT64)tempshort << 48) | ((UINT64) tempshort << 32) | ((UINT64) tempshort << 16) | tempshort;
				}
				break;
			case 7:
				loadqword = ((UINT64)readval1 << 56) | ((UINT64)readval2 << 24) | (readval3 >> 8);
				break;
			}

			
			switch(tmem_formatting)
			{
			case 0:
				readval0 = (UINT32)((((loadqword >> 56) & 0xff) << 24) | (((loadqword >> 40) & 0xff) << 16) | (((loadqword >> 24) & 0xff) << 8) | (((loadqword >> 8) & 0xff) << 0));
				readval1 = (UINT32)((((loadqword >> 48) & 0xff) << 24) | (((loadqword >> 32) & 0xff) << 16) | (((loadqword >> 16) & 0xff) << 8) | (((loadqword >> 0) & 0xff) << 0));
				if (bit3fl)
				{
					tmem16[tmemidx2 ^ WORD_ADDR_XOR] = (UINT16)(readval0 >> 16);
					tmem16[tmemidx3 ^ WORD_ADDR_XOR] = (UINT16)(readval0 & 0xffff);
					tmem16[(tmemidx2 | 0x400) ^ WORD_ADDR_XOR] = (UINT16)(readval1 >> 16);
					tmem16[(tmemidx3 | 0x400) ^ WORD_ADDR_XOR] = (UINT16)(readval1 & 0xffff);
				}
				else
				{
					tmem16[tmemidx0 ^ WORD_ADDR_XOR] = (UINT16)(readval0 >> 16);
					tmem16[tmemidx1 ^ WORD_ADDR_XOR] = (UINT16)(readval0 & 0xffff);
					tmem16[(tmemidx0 | 0x400) ^ WORD_ADDR_XOR] = (UINT16)(readval1 >> 16);
					tmem16[(tmemidx1 | 0x400) ^ WORD_ADDR_XOR] = (UINT16)(readval1 & 0xffff);
				}
				break;
			case 1:
				readval0 = (UINT32)(((loadqword >> 48) << 16) | ((loadqword >> 16) & 0xffff));
				readval1 = (UINT32)((((loadqword >> 32) & 0xffff) << 16) | (loadqword & 0xffff));

				if (bit3fl)
				{
					tmem16[tmemidx2 ^ WORD_ADDR_XOR] = (UINT16)(readval0 >> 16);
					tmem16[tmemidx3 ^ WORD_ADDR_XOR] = (UINT16)(readval0 & 0xffff);
					tmem16[(tmemidx2 | 0x400) ^ WORD_ADDR_XOR] = (UINT16)(readval1 >> 16);
					tmem16[(tmemidx3 | 0x400) ^ WORD_ADDR_XOR] = (UINT16)(readval1 & 0xffff);
				}
				else
				{
					tmem16[tmemidx0 ^ WORD_ADDR_XOR] = (UINT16)(readval0 >> 16);
					tmem16[tmemidx1 ^ WORD_ADDR_XOR] = (UINT16)(readval0 & 0xffff);
					tmem16[(tmemidx0 | 0x400) ^ WORD_ADDR_XOR] = (UINT16)(readval1 >> 16);
					tmem16[(tmemidx1 | 0x400) ^ WORD_ADDR_XOR] = (UINT16)(readval1 & 0xffff);
				}
				break;
			case 2:
				if (!dswap)
				{
					if (!hibit)
					{
						tmem16[tmemidx0 ^ WORD_ADDR_XOR] = (UINT16)(loadqword >> 48);
						tmem16[tmemidx1 ^ WORD_ADDR_XOR] = (UINT16)(loadqword >> 32);
						tmem16[tmemidx2 ^ WORD_ADDR_XOR] = (UINT16)(loadqword >> 16);
						tmem16[tmemidx3 ^ WORD_ADDR_XOR] = (UINT16)(loadqword & 0xffff);
					}
					else
					{
						tmem16[(tmemidx0 | 0x400) ^ WORD_ADDR_XOR] = (UINT16)(loadqword >> 48);
						tmem16[(tmemidx1 | 0x400) ^ WORD_ADDR_XOR] = (UINT16)(loadqword >> 32);
						tmem16[(tmemidx2 | 0x400) ^ WORD_ADDR_XOR] = (UINT16)(loadqword >> 16);
						tmem16[(tmemidx3 | 0x400) ^ WORD_ADDR_XOR] = (UINT16)(loadqword & 0xffff);
					}
				}
				else
				{
					if (!hibit)
					{
						tmem16[tmemidx0 ^ WORD_ADDR_XOR] = (UINT16)(loadqword >> 16);
						tmem16[tmemidx1 ^ WORD_ADDR_XOR] = (UINT16)(loadqword & 0xffff);
						tmem16[tmemidx2 ^ WORD_ADDR_XOR] = (UINT16)(loadqword >> 48);
						tmem16[tmemidx3 ^ WORD_ADDR_XOR] = (UINT16)(loadqword >> 32);
					}
					else
					{
						tmem16[(tmemidx0 | 0x400) ^ WORD_ADDR_XOR] = (UINT16)(loadqword >> 16);
						tmem16[(tmemidx1 | 0x400) ^ WORD_ADDR_XOR] = (UINT16)(loadqword & 0xffff);
						tmem16[(tmemidx2 | 0x400) ^ WORD_ADDR_XOR] = (UINT16)(loadqword >> 48);
						tmem16[(tmemidx3 | 0x400) ^ WORD_ADDR_XOR] = (UINT16)(loadqword >> 32);
					}
				}
			break;
			}


			s = (s + dsinc) & ~0x1f;
			t = (t + dtinc) & ~0x1f;
			tiptr += tiadvance;
		}
	}
}

static void edgewalker_for_prims(INT32* ewdata)
{
	int j = 0;
	int xleft = 0, xright = 0, xleft_inc = 0, xright_inc = 0;
	int r = 0, g = 0, b = 0, a = 0, z = 0, s = 0, t = 0, w = 0;
	int dr = 0, dg = 0, db = 0, da = 0;
	int drdx = 0, dgdx = 0, dbdx = 0, dadx = 0, dzdx = 0, dsdx = 0, dtdx = 0, dwdx = 0;
	int drdy = 0, dgdy = 0, dbdy = 0, dady = 0, dzdy = 0, dsdy = 0, dtdy = 0, dwdy = 0;
	int drde = 0, dgde = 0, dbde = 0, dade = 0, dzde = 0, dsde = 0, dtde = 0, dwde = 0;
	int tilenum = 0, flip = 0;
	INT32 yl = 0, ym = 0, yh = 0;
	INT32 xl = 0, xm = 0, xh = 0;
	INT32 dxldy = 0, dxhdy = 0, dxmdy = 0;

	if (other_modes.f.stalederivs)
	{
		deduce_derivatives();
		other_modes.f.stalederivs = 0;
	}

	
	flip = (ewdata[0] & 0x800000) ? 1 : 0;
	max_level = (ewdata[0] >> 19) & 7;
	tilenum = (ewdata[0] >> 16) & 7;

	
	yl = SIGN(ewdata[0], 14); 
	ym = ewdata[1] >> 16;
	ym = SIGN(ym, 14);
	yh = SIGN(ewdata[1], 14); 
	
	xl = SIGN(ewdata[2], 30);
	xh = SIGN(ewdata[4], 30);
	xm = SIGN(ewdata[6], 30);
	
	dxldy = (INT32)ewdata[3];
	dxhdy = (INT32)ewdata[5];
	dxmdy = (INT32)ewdata[7];

	
	r    = (ewdata[8] & 0xffff0000) | ((ewdata[12] >> 16) & 0x0000ffff);
	g    = ((ewdata[8] << 16) & 0xffff0000) | (ewdata[12] & 0x0000ffff);
	b    = (ewdata[9] & 0xffff0000) | ((ewdata[13] >> 16) & 0x0000ffff);
	a    = ((ewdata[9] << 16) & 0xffff0000) | (ewdata[13] & 0x0000ffff);
	drdx = (ewdata[10] & 0xffff0000) | ((ewdata[14] >> 16) & 0x0000ffff);
	dgdx = ((ewdata[10] << 16) & 0xffff0000) | (ewdata[14] & 0x0000ffff);
	dbdx = (ewdata[11] & 0xffff0000) | ((ewdata[15] >> 16) & 0x0000ffff);
	dadx = ((ewdata[11] << 16) & 0xffff0000) | (ewdata[15] & 0x0000ffff);
	drde = (ewdata[16] & 0xffff0000) | ((ewdata[20] >> 16) & 0x0000ffff);
	dgde = ((ewdata[16] << 16) & 0xffff0000) | (ewdata[20] & 0x0000ffff);
	dbde = (ewdata[17] & 0xffff0000) | ((ewdata[21] >> 16) & 0x0000ffff);
	dade = ((ewdata[17] << 16) & 0xffff0000) | (ewdata[21] & 0x0000ffff);
	drdy = (ewdata[18] & 0xffff0000) | ((ewdata[22] >> 16) & 0x0000ffff);
	dgdy = ((ewdata[18] << 16) & 0xffff0000) | (ewdata[22] & 0x0000ffff);
	dbdy = (ewdata[19] & 0xffff0000) | ((ewdata[23] >> 16) & 0x0000ffff);
	dady = ((ewdata[19] << 16) & 0xffff0000) | (ewdata[23] & 0x0000ffff);

	
	s    = (ewdata[24] & 0xffff0000) | ((ewdata[28] >> 16) & 0x0000ffff);
	t    = ((ewdata[24] << 16) & 0xffff0000)	| (ewdata[28] & 0x0000ffff);
	w    = (ewdata[25] & 0xffff0000) | ((ewdata[29] >> 16) & 0x0000ffff);
	dsdx = (ewdata[26] & 0xffff0000) | ((ewdata[30] >> 16) & 0x0000ffff);
	dtdx = ((ewdata[26] << 16) & 0xffff0000)	| (ewdata[30] & 0x0000ffff);
	dwdx = (ewdata[27] & 0xffff0000) | ((ewdata[31] >> 16) & 0x0000ffff);
	dsde = (ewdata[32] & 0xffff0000) | ((ewdata[36] >> 16) & 0x0000ffff);
	dtde = ((ewdata[32] << 16) & 0xffff0000)	| (ewdata[36] & 0x0000ffff);
	dwde = (ewdata[33] & 0xffff0000) | ((ewdata[37] >> 16) & 0x0000ffff);
	dsdy = (ewdata[34] & 0xffff0000) | ((ewdata[38] >> 16) & 0x0000ffff);
	dtdy = ((ewdata[34] << 16) & 0xffff0000)	| (ewdata[38] & 0x0000ffff);
	dwdy = (ewdata[35] & 0xffff0000) | ((ewdata[39] >> 16) & 0x0000ffff);
	
	
	z    = ewdata[40];
	dzdx = ewdata[41];
	dzde = ewdata[42];
	dzdy = ewdata[43];

	
	
	
	
	

	spans_ds = dsdx & ~0x1f;
	spans_dt = dtdx & ~0x1f;
	spans_dw = dwdx & ~0x1f;
	spans_dr = drdx & ~0x1f;
	spans_dg = dgdx & ~0x1f;
	spans_db = dbdx & ~0x1f;
	spans_da = dadx & ~0x1f;
	spans_dz = dzdx;
	
	
	spans_drdy = drdy >> 14;
	spans_dgdy = dgdy >> 14;
	spans_dbdy = dbdy >> 14;
	spans_dady = dady >> 14;
	spans_dzdy = dzdy >> 10;
	spans_drdy = SIGN(spans_drdy, 13);
	spans_dgdy = SIGN(spans_dgdy, 13);
	spans_dbdy = SIGN(spans_dbdy, 13);
	spans_dady = SIGN(spans_dady, 13);
	spans_dzdy = SIGN(spans_dzdy, 22);
	spans_cdr = spans_dr >> 14;
	spans_cdr = SIGN(spans_cdr, 13);
	spans_cdg = spans_dg >> 14;
	spans_cdg = SIGN(spans_cdg, 13);
	spans_cdb = spans_db >> 14;
	spans_cdb = SIGN(spans_cdb, 13);
	spans_cda = spans_da >> 14;
	spans_cda = SIGN(spans_cda, 13);
	spans_cdz = spans_dz >> 10;
	spans_cdz = SIGN(spans_cdz, 22);
	
	spans_dsdy = dsdy & ~0x7fff;
	spans_dtdy = dtdy & ~0x7fff;
	spans_dwdy = dwdy & ~0x7fff;

	
	int dzdy_dz = (dzdy >> 16) & 0xffff;
	int dzdx_dz = (dzdx >> 16) & 0xffff;
	
	spans_dzpix = ((dzdy_dz & 0x8000) ? ((~dzdy_dz) & 0x7fff) : dzdy_dz) + ((dzdx_dz & 0x8000) ? ((~dzdx_dz) & 0x7fff) : dzdx_dz);
	spans_dzpix = normalize_dzpix(spans_dzpix & 0xffff) & 0xffff;
	

	
	xleft_inc = (dxmdy >> 2) & ~0x1;
	xright_inc = (dxhdy >> 2) & ~0x1;
	
	
	
	xright = xh & ~0x1;
	xleft = xm & ~0x1;
		
	int k = 0;

	int dsdiff, dtdiff, dwdiff, drdiff, dgdiff, dbdiff, dadiff, dzdiff;
	int sign_dxhdy = (dxhdy & 0x80000000) ? 1 : 0;
	
	int dsdeh, dtdeh, dwdeh, drdeh, dgdeh, dbdeh, dadeh, dzdeh, dsdyh, dtdyh, dwdyh, drdyh, dgdyh, dbdyh, dadyh, dzdyh; 
	int do_offset = !(sign_dxhdy ^ flip);

	if (do_offset)
	{
		dsdeh = dsde & ~0x1ff;
		dtdeh = dtde & ~0x1ff;	
		dwdeh = dwde & ~0x1ff;	
		drdeh = drde & ~0x1ff;	
		dgdeh = dgde & ~0x1ff;	
		dbdeh = dbde & ~0x1ff;	
		dadeh = dade & ~0x1ff;	
		dzdeh = dzde & ~0x1ff;

		dsdyh = dsdy & ~0x1ff;
		dtdyh = dtdy & ~0x1ff;
		dwdyh = dwdy & ~0x1ff;
		drdyh = drdy & ~0x1ff;
		dgdyh = dgdy & ~0x1ff;
		dbdyh = dbdy & ~0x1ff;
		dadyh = dady & ~0x1ff;
		dzdyh = dzdy & ~0x1ff;


		
		dsdiff = dsdeh - dsdyh;
		dsdiff -= (dsdiff >> 2);
		dtdiff = dtdeh - dtdyh;
		dtdiff -= (dtdiff >> 2);
		dwdiff = dwdeh - dwdyh;
		dwdiff -= (dwdiff >> 2);
		drdiff = drdeh - drdyh;
		drdiff -= (drdiff >> 2);
		dgdiff = dgdeh - dgdyh;
		dgdiff -= (dgdiff >> 2);
		dbdiff = dbdeh - dbdyh;
		dbdiff -= (dbdiff >> 2);
		dadiff = dadeh - dadyh;
		dadiff -= (dadiff >> 2);
		dzdiff = dzdeh - dzdyh;
		dzdiff -= (dzdiff >> 2);
	}
	else
		dsdiff = dtdiff = dwdiff = drdiff = dgdiff = dbdiff = dadiff = dzdiff = 0;

	int xfrac = 0;

	int dsdxh, dtdxh, dwdxh, drdxh, dgdxh, dbdxh, dadxh, dzdxh;
	if (other_modes.cycle_type != CYCLE_TYPE_COPY)
	{
		dsdxh = (dsdx >> 8) & ~1;
		dtdxh = (dtdx >> 8) & ~1;
		dwdxh = (dwdx >> 8) & ~1;
		drdxh = (drdx >> 8) & ~1;
		dgdxh = (dgdx >> 8) & ~1;
		dbdxh = (dbdx >> 8) & ~1;
		dadxh = (dadx >> 8) & ~1;
		dzdxh = (dzdx >> 8) & ~1;
	}
	else
		dsdxh = dtdxh = dwdxh = drdxh = dgdxh = dbdxh = dadxh = dzdxh = 0;





#define ADJUST_ATTR_PRIM()		\
{							\
	span[j].s = ((s & ~0x1ff) + dsdiff - (xfrac * dsdxh)) & ~0x3ff;				\
	span[j].t = ((t & ~0x1ff) + dtdiff - (xfrac * dtdxh)) & ~0x3ff;				\
	span[j].w = ((w & ~0x1ff) + dwdiff - (xfrac * dwdxh)) & ~0x3ff;				\
	span[j].r = ((r & ~0x1ff) + drdiff - (xfrac * drdxh)) & ~0x3ff;				\
	span[j].g = ((g & ~0x1ff) + dgdiff - (xfrac * dgdxh)) & ~0x3ff;				\
	span[j].b = ((b & ~0x1ff) + dbdiff - (xfrac * dbdxh)) & ~0x3ff;				\
	span[j].a = ((a & ~0x1ff) + dadiff - (xfrac * dadxh)) & ~0x3ff;				\
	span[j].z = ((z & ~0x1ff) + dzdiff - (xfrac * dzdxh)) & ~0x3ff;				\
}


#define ADDVALUES_PRIM() {	\
			s += dsde;	\
			t += dtde;	\
			w += dwde; \
			r += drde; \
			g += dgde; \
			b += dbde; \
			a += dade; \
			z += dzde; \
}

	INT32 maxxmx, minxmx, maxxhx, minxhx;

	int spix = 0;
	int ycur =	yh & ~3;
	int ldflag = (sign_dxhdy ^ flip) ? 0 : 3;
	int invaly = 1;
	int length = 0;
	INT32 xrsc = 0, xlsc = 0, stickybit = 0;
	INT32 yllimit = 0, yhlimit = 0;
	if (yl & 0x2000)
		yllimit = 1;
	else if (yl & 0x1000)
		yllimit = 0;
	else
		yllimit = (yl & 0xfff) < clip.yl;
	yllimit = yllimit ? yl : clip.yl;

	int ylfar = yllimit | 3;
	if ((yl >> 2) > (ylfar >> 2))
		ylfar += 4;
	else if ((yllimit >> 2) >= 0 && (yllimit >> 2) < 1023)
		span[(yllimit >> 2) + 1].validline = 0;
	
	
	if (yh & 0x2000)
		yhlimit = 0;
	else if (yh & 0x1000)
		yhlimit = 1;
	else
		yhlimit = (yh >= clip.yh);
	yhlimit = yhlimit ? yh : clip.yh;

	int yhclose = yhlimit & ~3;

	INT32 clipxlshift = clip.xl << 1;
	INT32 clipxhshift = clip.xh << 1;
	int allover = 1, allunder = 1, curover = 0, curunder = 0;
	int allinval = 1;
	INT32 curcross = 0;

	xfrac = ((xright >> 8) & 0xff);

	
	if (flip)
	{
	for (k = ycur; k <= ylfar; k++)
	{
		if (k == ym)
		{
		
			xleft = xl & ~1;
			xleft_inc = (dxldy >> 2) & ~1;
		}
		
		spix = k & 3;
						
		if (k >= yhclose)
		{
			invaly = k < yhlimit || k >= yllimit;
			
			j = k >> 2;

			if (spix == 0)
			{
				maxxmx = 0;
				minxhx = 0xfff;
				allover = allunder = 1;
				allinval = 1;
			}

			stickybit = ((xright >> 1) & 0x1fff) > 0;
			xrsc = ((xright >> 13) & 0x1ffe) | stickybit;
			curunder = ((xright & 0x8000000) || xrsc < clipxhshift); 
			xrsc = curunder ? clipxhshift : (((xright >> 13) & 0x3ffe) | stickybit);
			curover = ((xrsc & 0x2000) || (xrsc & 0x1fff) >= clipxlshift);
			xrsc = curover ? clipxlshift : xrsc;
			span[j].majorx[spix] = xrsc & 0x1fff;
			allover &= curover;
			allunder &= curunder; 

			stickybit = ((xleft >> 1) & 0x1fff) > 0;
			xlsc = ((xleft >> 13) & 0x1ffe) | stickybit;
			curunder = ((xleft & 0x8000000) || xlsc < clipxhshift);
			xlsc = curunder ? clipxhshift : (((xleft >> 13) & 0x3ffe) | stickybit);
			curover = ((xlsc & 0x2000) || (xlsc & 0x1fff) >= clipxlshift);
			xlsc = curover ? clipxlshift : xlsc;
			span[j].minorx[spix] = xlsc & 0x1fff;
			allover &= curover;
			allunder &= curunder; 
			
			
			
			curcross = ((xleft ^ (1 << 27)) & (0x3fff << 14)) < ((xright ^ (1 << 27)) & (0x3fff << 14));
			

			invaly |= curcross;
			span[j].invalyscan[spix] = invaly;
			allinval &= invaly;

			if (!invaly)
			{
				maxxmx = (((xlsc >> 3) & 0xfff) > maxxmx) ? (xlsc >> 3) & 0xfff : maxxmx;
				minxhx = (((xrsc >> 3) & 0xfff) < minxhx) ? (xrsc >> 3) & 0xfff : minxhx;
			}
			
			if (spix == ldflag)
			{
				span[j].unscrx = xright >> 16;
				xfrac = (xright >> 8) & 0xff;
				ADJUST_ATTR_PRIM();
			}

			if (spix == 3)
			{
				span[j].lx = maxxmx;
				span[j].rx = minxhx;
				span[j].validline  = !allinval && !allover && !allunder && (!scfield || (scfield && !(sckeepodd ^ (j & 1))));
				
			}
			
						           
		}

		if (spix == 3)
		{
			ADDVALUES_PRIM();
		}

		xleft += xleft_inc;
		xright += xright_inc;

	}
	}
	else
	{
	for (k = ycur; k <= ylfar; k++)
	{
		if (k == ym)
		{
			xleft = xl & ~1;
			xleft_inc = (dxldy >> 2) & ~1;
		}
		
		spix = k & 3;
						
		if (k >= yhclose)
		{
			invaly = k < yhlimit || k >= yllimit;
			j = k >> 2;

			if (spix == 0)
			{
				maxxhx = 0;
				minxmx = 0xfff;
				allover = allunder = 1;
				allinval = 1;
			}

			stickybit = ((xright >> 1) & 0x1fff) > 0;
			xrsc = ((xright >> 13) & 0x1ffe) | stickybit;
			curunder = ((xright & 0x8000000) || xrsc < clipxhshift); 
			xrsc = curunder ? clipxhshift : (((xright >> 13) & 0x3ffe) | stickybit);
			curover = ((xrsc & 0x2000) || (xrsc & 0x1fff) >= clipxlshift);
			xrsc = curover ? clipxlshift : xrsc;
			span[j].majorx[spix] = xrsc & 0x1fff;
			allover &= curover;
			allunder &= curunder; 

			stickybit = ((xleft >> 1) & 0x1fff) > 0;
			xlsc = ((xleft >> 13) & 0x1ffe) | stickybit;
			curunder = ((xleft & 0x8000000) || xlsc < clipxhshift);
			xlsc = curunder ? clipxhshift : (((xleft >> 13) & 0x3ffe) | stickybit);
			curover = ((xlsc & 0x2000) || (xlsc & 0x1fff) >= clipxlshift);
			xlsc = curover ? clipxlshift : xlsc;
			span[j].minorx[spix] = xlsc & 0x1fff;
			allover &= curover;
			allunder &= curunder; 

			curcross = ((xright ^ (1 << 27)) & (0x3fff << 14)) < ((xleft ^ (1 << 27)) & (0x3fff << 14));
            
			invaly |= curcross;
			span[j].invalyscan[spix] = invaly;
			allinval &= invaly;

			if (!invaly)
			{
				minxmx = (((xlsc >> 3) & 0xfff) < minxmx) ? (xlsc >> 3) & 0xfff : minxmx;
				maxxhx = (((xrsc >> 3) & 0xfff) > maxxhx) ? (xrsc >> 3) & 0xfff : maxxhx;
			}

			if (spix == ldflag)
			{
				span[j].unscrx  = xright >> 16;
				xfrac = (xright >> 8) & 0xff;
				ADJUST_ATTR_PRIM();
			}

			if (spix == 3)
			{
				span[j].lx = minxmx;
				span[j].rx = maxxhx;
				span[j].validline  = !allinval && !allover && !allunder && (!scfield || (scfield && !(sckeepodd ^ (j & 1))));
			}
			
		}

		if (spix == 3)
		{
			ADDVALUES_PRIM();
		}

		xleft += xleft_inc;
		xright += xright_inc;

	}
	}

	
	

	switch(other_modes.cycle_type)
	{
		case CYCLE_TYPE_1: render_spans_1cycle_ptr(yhlimit >> 2, yllimit >> 2, tilenum, flip); break;
		case CYCLE_TYPE_2: render_spans_2cycle_ptr(yhlimit >> 2, yllimit >> 2, tilenum, flip); break;
		case CYCLE_TYPE_COPY: render_spans_copy(yhlimit >> 2, yllimit >> 2, tilenum, flip); break;
		case CYCLE_TYPE_FILL: render_spans_fill(yhlimit >> 2, yllimit >> 2, flip); break;
		default: fatalerror("cycle_type %d", other_modes.cycle_type); break;
	}
	
	
}



static void edgewalker_for_loads(INT32* lewdata)
{
	int j = 0;
	int xleft = 0, xright = 0;
	int xstart = 0, xend = 0;
	int s = 0, t = 0, w = 0;
	int dsdx = 0, dtdx = 0;
	int dsdy = 0, dtdy = 0;
	int dsde = 0, dtde = 0;
	int tilenum = 0, flip = 0;
	INT32 yl = 0, ym = 0, yh = 0;
	INT32 xl = 0, xm = 0, xh = 0;
	INT32 dxldy = 0, dxhdy = 0, dxmdy = 0;

	int commandcode = (lewdata[0] >> 24) & 0x3f;
	int ltlut = (commandcode == 0x30);
	int coord_quad = ltlut || (commandcode == 0x33);
	flip = 1;
	max_level = 0;
	tilenum = (lewdata[0] >> 16) & 7;

	
	yl = SIGN(lewdata[0], 14); 
	ym = lewdata[1] >> 16;
	ym = SIGN(ym, 14);
	yh = SIGN(lewdata[1], 14); 
	
	xl = SIGN(lewdata[2], 30);
	xh = SIGN(lewdata[3], 30);
	xm = SIGN(lewdata[4], 30);
	
	dxldy = 0;
	dxhdy = 0;
	dxmdy = 0;

	
	s    = lewdata[5] & 0xffff0000;
	t    = (lewdata[5] & 0xffff) << 16;
	w    = 0;
	dsdx = (lewdata[7] & 0xffff0000) | ((lewdata[6] >> 16) & 0xffff);
	dtdx = ((lewdata[7] << 16) & 0xffff0000)	| (lewdata[6] & 0xffff);
	dsde = 0;
	dtde = (lewdata[9] & 0xffff) << 16;
	dsdy = 0;
	dtdy = (lewdata[8] & 0xffff) << 16;

	spans_ds = dsdx & ~0x1f;
	spans_dt = dtdx & ~0x1f;
	spans_dw = 0;

	
	

	
	
	xright = xh & ~0x1;
	xleft = xm & ~0x1;
		
	int k = 0;

	int sign_dxhdy = 0;

	int do_offset = 0;

	int xfrac = 0;






#define ADJUST_ATTR_LOAD()										\
{																\
	span[j].s = s & ~0x3ff;										\
	span[j].t = t & ~0x3ff;										\
}


#define ADDVALUES_LOAD() {	\
			t += dtde;		\
}

	INT32 maxxmx, minxhx;

	int spix = 0;
	int ycur =	yh & ~3;
	int ylfar = yl | 3;
	
	int valid_y = 1;
	int length = 0;
	INT32 xrsc = 0, xlsc = 0, stickybit = 0;
	INT32 yllimit = yl;
	INT32 yhlimit = yh;

	xfrac = 0;
	xend = xright >> 16;

	
	for (k = ycur; k <= ylfar; k++)
	{
		if (k == ym)
			xleft = xl & ~1;
		
		spix = k & 3;
						
		if (!(k & ~0xfff))
		{
			j = k >> 2;
			valid_y = !(k < yhlimit || k >= yllimit);

			if (spix == 0)
			{
				maxxmx = 0;
				minxhx = 0xfff;
			}

			xrsc = (xright >> 13) & 0x7ffe;
			
			

			xlsc = (xleft >> 13) & 0x7ffe;

			if (valid_y)
			{
				maxxmx = (((xlsc >> 3) & 0xfff) > maxxmx) ? (xlsc >> 3) & 0xfff : maxxmx;
				minxhx = (((xrsc >> 3) & 0xfff) < minxhx) ? (xrsc >> 3) & 0xfff : minxhx;
			}

			if (spix == 0)
			{
				span[j].unscrx = xend;
				ADJUST_ATTR_LOAD();
			}

			if (spix == 3)
			{
				span[j].lx = maxxmx;
				span[j].rx = minxhx;
				
				
			}
			
						           
		}

		if (spix == 3)
		{
			ADDVALUES_LOAD();
		}

		

	}

	loading_pipeline(yhlimit >> 2, yllimit >> 2, tilenum, coord_quad, ltlut);
}


static const char *const image_format[] = { "RGBA", "YUV", "CI", "IA", "I", "???", "???", "???" };
static const char *const image_size[] = { "4-bit", "8-bit", "16-bit", "32-bit" };

static const UINT32 rdp_command_length[64] =
{
	8,			
	8,			
	8,			
	8,			
	8,			
	8,			
	8,			
	8,			
	32,			
	32+16,		
	32+64,		
	32+64+16,	
	32+64,		
	32+64+16,	
	32+64+64,	
	32+64+64+16,
	8,			
	8,			
	8,			
	8,			
	8,			
	8,			
	8,			
	8,			
	8,			
	8,			
	8,			
	8,			
	8,			
	8,			
	8,			
	8,			
	8,			
	8,			
	8,			
	8,			
	16,			
	16,			
	8,			
	8,			
	8,			
	8,			
	8,			
	8,			
	8,			
	8,			
	8,			
	8,			
	8,			
	8,			
	8,			
	8,			
	8,			
	8,			
	8,			
	8,			
	8,			
	8,			
	8,			
	8,			
	8,			
	8,			
	8,			
	8			
};

static int rdp_dasm(char *buffer)
{
	int tile;
	const char *format, *size;
	char sl[32], tl[32], sh[32], th[32];
	char s[32], t[32];
	char dsdx[32], dtdy[32];
#if DETAILED_LOGGING
	int i;
	char dtdx[32], dwdx[32];
	char dsdy[32], dwdy[32];
	char dsde[32], dtde[32], dwde[32];
	char yl[32], yh[32], ym[32], xl[32], xh[32], xm[32];
	char dxldy[32], dxhdy[32], dxmdy[32];
	char rt[32], gt[32], bt[32], at[32];
	char drdx[32], dgdx[32], dbdx[32], dadx[32];
	char drdy[32], dgdy[32], dbdy[32], dady[32];
	char drde[32], dgde[32], dbde[32], dade[32];
#endif
	UINT32 r,g,b,a;

	UINT32 cmd[64];
	UINT32 length;
	UINT32 command;

	length = rdp_cmd_ptr * 4;
	if (length < 8)
	{
		sprintf(buffer, "ERROR: length = %d\n", length);
		return 0;
	}

	cmd[0] = rdp_cmd_data[rdp_cmd_cur+0];
	cmd[1] = rdp_cmd_data[rdp_cmd_cur+1];

	tile = (cmd[1] >> 24) & 0x7;
	sprintf(sl, "%4.2f", (float)((cmd[0] >> 12) & 0xfff) / 4.0f);
	sprintf(tl, "%4.2f", (float)((cmd[0] >>  0) & 0xfff) / 4.0f);
	sprintf(sh, "%4.2f", (float)((cmd[1] >> 12) & 0xfff) / 4.0f);
	sprintf(th, "%4.2f", (float)((cmd[1] >>  0) & 0xfff) / 4.0f);

	format = image_format[(cmd[0] >> 21) & 0x7];
	size = image_size[(cmd[0] >> 19) & 0x3];

	r = (cmd[1] >> 24) & 0xff;
	g = (cmd[1] >> 16) & 0xff;
	b = (cmd[1] >>  8) & 0xff;
	a = (cmd[1] >>  0) & 0xff;

	command = (cmd[0] >> 24) & 0x3f;
	switch (command)
	{
		case 0x00:	sprintf(buffer, "No Op"); break;
		case 0x08:
			sprintf(buffer, "Tri_NoShade (%08X %08X)", cmd[0], cmd[1]); break;
		case 0x0a:
			sprintf(buffer, "Tri_Tex (%08X %08X)", cmd[0], cmd[1]); break;
		case 0x0c:
			sprintf(buffer, "Tri_Shade (%08X %08X)", cmd[0], cmd[1]); break;
		case 0x0e:
			sprintf(buffer, "Tri_TexShade (%08X %08X)", cmd[0], cmd[1]); break;
		case 0x09:
			sprintf(buffer, "TriZ_NoShade (%08X %08X)", cmd[0], cmd[1]); break;
		case 0x0b:
			sprintf(buffer, "TriZ_Tex (%08X %08X)", cmd[0], cmd[1]); break;
		case 0x0d:
			sprintf(buffer, "TriZ_Shade (%08X %08X)", cmd[0], cmd[1]); break;
		case 0x0f:
			sprintf(buffer, "TriZ_TexShade (%08X %08X)", cmd[0], cmd[1]); break;
#if DETAILED_LOGGING
		case 0x08:		
		{
			int lft = (command >> 23) & 0x1;

			if (length != rdp_command_length[command])
			{
				sprintf(buffer, "ERROR: Tri_NoShade length = %d\n", length);
				return 0;
			}

			cmd[2] = rdp_cmd_data[rdp_cmd_cur+2];
			cmd[3] = rdp_cmd_data[rdp_cmd_cur+3];
			cmd[4] = rdp_cmd_data[rdp_cmd_cur+4];
			cmd[5] = rdp_cmd_data[rdp_cmd_cur+5];
			cmd[6] = rdp_cmd_data[rdp_cmd_cur+6];
			cmd[7] = rdp_cmd_data[rdp_cmd_cur+7];

			sprintf(yl,		"%4.4f", (float)((cmd[0] >>  0) & 0x1fff) / 4.0f);
			sprintf(ym,		"%4.4f", (float)((cmd[1] >> 16) & 0x1fff) / 4.0f);
			sprintf(yh,		"%4.4f", (float)((cmd[1] >>  0) & 0x1fff) / 4.0f);
			sprintf(xl,		"%4.4f", (float)(cmd[2] / 65536.0f));
			sprintf(dxldy,	"%4.4f", (float)(cmd[3] / 65536.0f));
			sprintf(xh,		"%4.4f", (float)(cmd[4] / 65536.0f));
			sprintf(dxhdy,	"%4.4f", (float)(cmd[5] / 65536.0f));
			sprintf(xm,		"%4.4f", (float)(cmd[6] / 65536.0f));
			sprintf(dxmdy,	"%4.4f", (float)(cmd[7] / 65536.0f));

					sprintf(buffer, "Tri_NoShade            %d, XL: %s, XM: %s, XH: %s, YL: %s, YM: %s, YH: %s\n", lft, xl,xm,xh,yl,ym,yh);
			break;
		}
		case 0x0a:		
		{
			int lft = (command >> 23) & 0x1;

			if (length < rdp_command_length[command])
			{
				sprintf(buffer, "ERROR: Tri_Tex length = %d\n", length);
				return 0;
			}

			for (i=2; i < 24; i++)
			{
				cmd[i] = rdp_cmd_data[rdp_cmd_cur+i];
			}

			sprintf(yl,		"%4.4f", (float)((cmd[0] >>  0) & 0x1fff) / 4.0f);
			sprintf(ym,		"%4.4f", (float)((cmd[1] >> 16) & 0x1fff) / 4.0f);
			sprintf(yh,		"%4.4f", (float)((cmd[1] >>  0) & 0x1fff) / 4.0f);
			sprintf(xl,		"%4.4f", (float)((INT32)cmd[2] / 65536.0f));
			sprintf(dxldy,	"%4.4f", (float)((INT32)cmd[3] / 65536.0f));
			sprintf(xh,		"%4.4f", (float)((INT32)cmd[4] / 65536.0f));
			sprintf(dxhdy,	"%4.4f", (float)((INT32)cmd[5] / 65536.0f));
			sprintf(xm,		"%4.4f", (float)((INT32)cmd[6] / 65536.0f));
			sprintf(dxmdy,	"%4.4f", (float)((INT32)cmd[7] / 65536.0f));

			sprintf(s,		"%4.4f", (float)(INT32)((cmd[ 8] & 0xffff0000) | ((cmd[12] >> 16) & 0xffff)) / 65536.0f);
			sprintf(t,		"%4.4f", (float)(INT32)(((cmd[ 8] & 0xffff) << 16) | (cmd[12] & 0xffff)) / 65536.0f);
			sprintf(w,		"%4.4f", (float)(INT32)((cmd[ 9] & 0xffff0000) | ((cmd[13] >> 16) & 0xffff)) / 65536.0f);
			sprintf(dsdx,	"%4.4f", (float)(INT32)((cmd[10] & 0xffff0000) | ((cmd[14] >> 16) & 0xffff)) / 65536.0f);
			sprintf(dtdx,	"%4.4f", (float)(INT32)(((cmd[10] & 0xffff) << 16) | (cmd[14] & 0xffff)) / 65536.0f);
			sprintf(dwdx,	"%4.4f", (float)(INT32)((cmd[11] & 0xffff0000) | ((cmd[15] >> 16) & 0xffff)) / 65536.0f);
			sprintf(dsde,	"%4.4f", (float)(INT32)((cmd[16] & 0xffff0000) | ((cmd[20] >> 16) & 0xffff)) / 65536.0f);
			sprintf(dtde,	"%4.4f", (float)(INT32)(((cmd[16] & 0xffff) << 16) | (cmd[20] & 0xffff)) / 65536.0f);
			sprintf(dwde,	"%4.4f", (float)(INT32)((cmd[17] & 0xffff0000) | ((cmd[21] >> 16) & 0xffff)) / 65536.0f);
			sprintf(dsdy,	"%4.4f", (float)(INT32)((cmd[18] & 0xffff0000) | ((cmd[22] >> 16) & 0xffff)) / 65536.0f);
			sprintf(dtdy,	"%4.4f", (float)(INT32)(((cmd[18] & 0xffff) << 16) | (cmd[22] & 0xffff)) / 65536.0f);
			sprintf(dwdy,	"%4.4f", (float)(INT32)((cmd[19] & 0xffff0000) | ((cmd[23] >> 16) & 0xffff)) / 65536.0f);


			buffer+=sprintf(buffer, "Tri_Tex               %d, XL: %s, XM: %s, XH: %s, YL: %s, YM: %s, YH: %s\n", lft, xl,xm,xh,yl,ym,yh);
			buffer+=sprintf(buffer, "                              ");
			buffer+=sprintf(buffer, "                       S: %s, T: %s, W: %s\n", s, t, w);
			buffer+=sprintf(buffer, "                              ");
			buffer+=sprintf(buffer, "                       DSDX: %s, DTDX: %s, DWDX: %s\n", dsdx, dtdx, dwdx);
			buffer+=sprintf(buffer, "                              ");
			buffer+=sprintf(buffer, "                       DSDE: %s, DTDE: %s, DWDE: %s\n", dsde, dtde, dwde);
			buffer+=sprintf(buffer, "                              ");
			buffer+=sprintf(buffer, "                       DSDY: %s, DTDY: %s, DWDY: %s\n", dsdy, dtdy, dwdy);
			break;
		}
		case 0x0c:		
		{
			int lft = (command >> 23) & 0x1;

			if (length != rdp_command_length[command])
			{
				sprintf(buffer, "ERROR: Tri_Shade length = %d\n", length);
				return 0;
			}

			for (i=2; i < 24; i++)
			{
				cmd[i] = rdp_cmd_data[i];
			}

			sprintf(yl,		"%4.4f", (float)((cmd[0] >>  0) & 0x1fff) / 4.0f);
			sprintf(ym,		"%4.4f", (float)((cmd[1] >> 16) & 0x1fff) / 4.0f);
			sprintf(yh,		"%4.4f", (float)((cmd[1] >>  0) & 0x1fff) / 4.0f);
			sprintf(xl,		"%4.4f", (float)((INT32)cmd[2] / 65536.0f));
			sprintf(dxldy,	"%4.4f", (float)((INT32)cmd[3] / 65536.0f));
			sprintf(xh,		"%4.4f", (float)((INT32)cmd[4] / 65536.0f));
			sprintf(dxhdy,	"%4.4f", (float)((INT32)cmd[5] / 65536.0f));
			sprintf(xm,		"%4.4f", (float)((INT32)cmd[6] / 65536.0f));
			sprintf(dxmdy,	"%4.4f", (float)((INT32)cmd[7] / 65536.0f));
			sprintf(rt,		"%4.4f", (float)(INT32)((cmd[8] & 0xffff0000) | ((cmd[12] >> 16) & 0xffff)) / 65536.0f);
			sprintf(gt,		"%4.4f", (float)(INT32)(((cmd[8] & 0xffff) << 16) | (cmd[12] & 0xffff)) / 65536.0f);
			sprintf(bt,		"%4.4f", (float)(INT32)((cmd[9] & 0xffff0000) | ((cmd[13] >> 16) & 0xffff)) / 65536.0f);
			sprintf(at,		"%4.4f", (float)(INT32)(((cmd[9] & 0xffff) << 16) | (cmd[13] & 0xffff)) / 65536.0f);
			sprintf(drdx,	"%4.4f", (float)(INT32)((cmd[10] & 0xffff0000) | ((cmd[14] >> 16) & 0xffff)) / 65536.0f);
			sprintf(dgdx,	"%4.4f", (float)(INT32)(((cmd[10] & 0xffff) << 16) | (cmd[14] & 0xffff)) / 65536.0f);
			sprintf(dbdx,	"%4.4f", (float)(INT32)((cmd[11] & 0xffff0000) | ((cmd[15] >> 16) & 0xffff)) / 65536.0f);
			sprintf(dadx,	"%4.4f", (float)(INT32)(((cmd[11] & 0xffff) << 16) | (cmd[15] & 0xffff)) / 65536.0f);
			sprintf(drde,	"%4.4f", (float)(INT32)((cmd[16] & 0xffff0000) | ((cmd[20] >> 16) & 0xffff)) / 65536.0f);
			sprintf(dgde,	"%4.4f", (float)(INT32)(((cmd[16] & 0xffff) << 16) | (cmd[20] & 0xffff)) / 65536.0f);
			sprintf(dbde,	"%4.4f", (float)(INT32)((cmd[17] & 0xffff0000) | ((cmd[21] >> 16) & 0xffff)) / 65536.0f);
			sprintf(dade,	"%4.4f", (float)(INT32)(((cmd[17] & 0xffff) << 16) | (cmd[21] & 0xffff)) / 65536.0f);
			sprintf(drdy,	"%4.4f", (float)(INT32)((cmd[18] & 0xffff0000) | ((cmd[22] >> 16) & 0xffff)) / 65536.0f);
			sprintf(dgdy,	"%4.4f", (float)(INT32)(((cmd[18] & 0xffff) << 16) | (cmd[22] & 0xffff)) / 65536.0f);
			sprintf(dbdy,	"%4.4f", (float)(INT32)((cmd[19] & 0xffff0000) | ((cmd[23] >> 16) & 0xffff)) / 65536.0f);
			sprintf(dady,	"%4.4f", (float)(INT32)(((cmd[19] & 0xffff) << 16) | (cmd[23] & 0xffff)) / 65536.0f);

			buffer+=sprintf(buffer, "Tri_Shade              %d, XL: %s, XM: %s, XH: %s, YL: %s, YM: %s, YH: %s\n", lft, xl,xm,xh,yl,ym,yh);
			buffer+=sprintf(buffer, "                              ");
			buffer+=sprintf(buffer, "                       R: %s, G: %s, B: %s, A: %s\n", rt, gt, bt, at);
			buffer+=sprintf(buffer, "                              ");
			buffer+=sprintf(buffer, "                       DRDX: %s, DGDX: %s, DBDX: %s, DADX: %s\n", drdx, dgdx, dbdx, dadx);
			buffer+=sprintf(buffer, "                              ");
			buffer+=sprintf(buffer, "                       DRDE: %s, DGDE: %s, DBDE: %s, DADE: %s\n", drde, dgde, dbde, dade);
			buffer+=sprintf(buffer, "                              ");
			buffer+=sprintf(buffer, "                       DRDY: %s, DGDY: %s, DBDY: %s, DADY: %s\n", drdy, dgdy, dbdy, dady);
			break;
		}
		case 0x0e:		
		{
			int lft = (command >> 23) & 0x1;

			if (length < rdp_command_length[command])
			{
				sprintf(buffer, "ERROR: Tri_TexShade length = %d\n", length);
				return 0;
			}

			for (i=2; i < 40; i++)
			{
				cmd[i] = rdp_cmd_data[rdp_cmd_cur+i];
			}

			sprintf(yl,		"%4.4f", (float)((cmd[0] >>  0) & 0x1fff) / 4.0f);
			sprintf(ym,		"%4.4f", (float)((cmd[1] >> 16) & 0x1fff) / 4.0f);
			sprintf(yh,		"%4.4f", (float)((cmd[1] >>  0) & 0x1fff) / 4.0f);
			sprintf(xl,		"%4.4f", (float)((INT32)cmd[2] / 65536.0f));
			sprintf(dxldy,	"%4.4f", (float)((INT32)cmd[3] / 65536.0f));
			sprintf(xh,		"%4.4f", (float)((INT32)cmd[4] / 65536.0f));
			sprintf(dxhdy,	"%4.4f", (float)((INT32)cmd[5] / 65536.0f));
			sprintf(xm,		"%4.4f", (float)((INT32)cmd[6] / 65536.0f));
			sprintf(dxmdy,	"%4.4f", (float)((INT32)cmd[7] / 65536.0f));
			sprintf(rt,		"%4.4f", (float)(INT32)((cmd[8] & 0xffff0000) | ((cmd[12] >> 16) & 0xffff)) / 65536.0f);
			sprintf(gt,		"%4.4f", (float)(INT32)(((cmd[8] & 0xffff) << 16) | (cmd[12] & 0xffff)) / 65536.0f);
			sprintf(bt,		"%4.4f", (float)(INT32)((cmd[9] & 0xffff0000) | ((cmd[13] >> 16) & 0xffff)) / 65536.0f);
			sprintf(at,		"%4.4f", (float)(INT32)(((cmd[9] & 0xffff) << 16) | (cmd[13] & 0xffff)) / 65536.0f);
			sprintf(drdx,	"%4.4f", (float)(INT32)((cmd[10] & 0xffff0000) | ((cmd[14] >> 16) & 0xffff)) / 65536.0f);
			sprintf(dgdx,	"%4.4f", (float)(INT32)(((cmd[10] & 0xffff) << 16) | (cmd[14] & 0xffff)) / 65536.0f);
			sprintf(dbdx,	"%4.4f", (float)(INT32)((cmd[11] & 0xffff0000) | ((cmd[15] >> 16) & 0xffff)) / 65536.0f);
			sprintf(dadx,	"%4.4f", (float)(INT32)(((cmd[11] & 0xffff) << 16) | (cmd[15] & 0xffff)) / 65536.0f);
			sprintf(drde,	"%4.4f", (float)(INT32)((cmd[16] & 0xffff0000) | ((cmd[20] >> 16) & 0xffff)) / 65536.0f);
			sprintf(dgde,	"%4.4f", (float)(INT32)(((cmd[16] & 0xffff) << 16) | (cmd[20] & 0xffff)) / 65536.0f);
			sprintf(dbde,	"%4.4f", (float)(INT32)((cmd[17] & 0xffff0000) | ((cmd[21] >> 16) & 0xffff)) / 65536.0f);
			sprintf(dade,	"%4.4f", (float)(INT32)(((cmd[17] & 0xffff) << 16) | (cmd[21] & 0xffff)) / 65536.0f);
			sprintf(drdy,	"%4.4f", (float)(INT32)((cmd[18] & 0xffff0000) | ((cmd[22] >> 16) & 0xffff)) / 65536.0f);
			sprintf(dgdy,	"%4.4f", (float)(INT32)(((cmd[18] & 0xffff) << 16) | (cmd[22] & 0xffff)) / 65536.0f);
			sprintf(dbdy,	"%4.4f", (float)(INT32)((cmd[19] & 0xffff0000) | ((cmd[23] >> 16) & 0xffff)) / 65536.0f);
			sprintf(dady,	"%4.4f", (float)(INT32)(((cmd[19] & 0xffff) << 16) | (cmd[23] & 0xffff)) / 65536.0f);

			sprintf(s,		"%4.4f", (float)(INT32)((cmd[24] & 0xffff0000) | ((cmd[28] >> 16) & 0xffff)) / 65536.0f);
			sprintf(t,		"%4.4f", (float)(INT32)(((cmd[24] & 0xffff) << 16) | (cmd[28] & 0xffff)) / 65536.0f);
			sprintf(w,		"%4.4f", (float)(INT32)((cmd[25] & 0xffff0000) | ((cmd[29] >> 16) & 0xffff)) / 65536.0f);
			sprintf(dsdx,	"%4.4f", (float)(INT32)((cmd[26] & 0xffff0000) | ((cmd[30] >> 16) & 0xffff)) / 65536.0f);
			sprintf(dtdx,	"%4.4f", (float)(INT32)(((cmd[26] & 0xffff) << 16) | (cmd[30] & 0xffff)) / 65536.0f);
			sprintf(dwdx,	"%4.4f", (float)(INT32)((cmd[27] & 0xffff0000) | ((cmd[31] >> 16) & 0xffff)) / 65536.0f);
			sprintf(dsde,	"%4.4f", (float)(INT32)((cmd[32] & 0xffff0000) | ((cmd[36] >> 16) & 0xffff)) / 65536.0f);
			sprintf(dtde,	"%4.4f", (float)(INT32)(((cmd[32] & 0xffff) << 16) | (cmd[36] & 0xffff)) / 65536.0f);
			sprintf(dwde,	"%4.4f", (float)(INT32)((cmd[33] & 0xffff0000) | ((cmd[37] >> 16) & 0xffff)) / 65536.0f);
			sprintf(dsdy,	"%4.4f", (float)(INT32)((cmd[34] & 0xffff0000) | ((cmd[38] >> 16) & 0xffff)) / 65536.0f);
			sprintf(dtdy,	"%4.4f", (float)(INT32)(((cmd[34] & 0xffff) << 16) | (cmd[38] & 0xffff)) / 65536.0f);
			sprintf(dwdy,	"%4.4f", (float)(INT32)((cmd[35] & 0xffff0000) | ((cmd[39] >> 16) & 0xffff)) / 65536.0f);


			buffer+=sprintf(buffer, "Tri_TexShade           %d, XL: %s, XM: %s, XH: %s, YL: %s, YM: %s, YH: %s\n", lft, xl,xm,xh,yl,ym,yh);
			buffer+=sprintf(buffer, "                              ");
			buffer+=sprintf(buffer, "                       R: %s, G: %s, B: %s, A: %s\n", rt, gt, bt, at);
			buffer+=sprintf(buffer, "                              ");
			buffer+=sprintf(buffer, "                       DRDX: %s, DGDX: %s, DBDX: %s, DADX: %s\n", drdx, dgdx, dbdx, dadx);
			buffer+=sprintf(buffer, "                              ");
			buffer+=sprintf(buffer, "                       DRDE: %s, DGDE: %s, DBDE: %s, DADE: %s\n", drde, dgde, dbde, dade);
			buffer+=sprintf(buffer, "                              ");
			buffer+=sprintf(buffer, "                       DRDY: %s, DGDY: %s, DBDY: %s, DADY: %s\n", drdy, dgdy, dbdy, dady);

			buffer+=sprintf(buffer, "                              ");
			buffer+=sprintf(buffer, "                       S: %s, T: %s, W: %s\n", s, t, w);
			buffer+=sprintf(buffer, "                              ");
			buffer+=sprintf(buffer, "                       DSDX: %s, DTDX: %s, DWDX: %s\n", dsdx, dtdx, dwdx);
			buffer+=sprintf(buffer, "                              ");
			buffer+=sprintf(buffer, "                       DSDE: %s, DTDE: %s, DWDE: %s\n", dsde, dtde, dwde);
			buffer+=sprintf(buffer, "                              ");
			buffer+=sprintf(buffer, "                       DSDY: %s, DTDY: %s, DWDY: %s\n", dsdy, dtdy, dwdy);
			break;
		}
#endif
		case 0x24: 
		case 0x25:
		{
			if (length < 16)
			{
				sprintf(buffer, "ERROR: Texture_Rectangle length = %d\n", length);
				return 0;
			}
			cmd[2] = rdp_cmd_data[rdp_cmd_cur+2];
			cmd[3] = rdp_cmd_data[rdp_cmd_cur+3];
			sprintf(s,    "%4.4f", (float)(INT16)((cmd[2] >> 16) & 0xffff) / 32.0f);
			sprintf(t,    "%4.4f", (float)(INT16)((cmd[2] >>  0) & 0xffff) / 32.0f);
			sprintf(dsdx, "%4.4f", (float)(INT16)((cmd[3] >> 16) & 0xffff) / 1024.0f);
			sprintf(dtdy, "%4.4f", (float)(INT16)((cmd[3] >> 16) & 0xffff) / 1024.0f);

			if (command == 0x24)
					sprintf(buffer, "Texture_Rectangle      %d, %s, %s, %s, %s,  %s, %s, %s, %s", tile, sh, th, sl, tl, s, t, dsdx, dtdy);
			else
					sprintf(buffer, "Texture_Rectangle_Flip %d, %s, %s, %s, %s,  %s, %s, %s, %s", tile, sh, th, sl, tl, s, t, dsdx, dtdy);

			break;
		}
		case 0x26:	sprintf(buffer, "Sync_Load"); break;
		case 0x27:	sprintf(buffer, "Sync_Pipe"); break;
		case 0x28:	sprintf(buffer, "Sync_Tile"); break;
		case 0x29:	sprintf(buffer, "Sync_Full"); break;
		case 0x2a:  sprintf(buffer, "Set_Key_GB"); break;
		case 0x2b:	sprintf(buffer, "Set_Key_R"); break;
		case 0x2c:	sprintf(buffer, "Set_Convert"); break;
		case 0x2d:	sprintf(buffer, "Set_Scissor            %s, %s, %s, %s", sl, tl, sh, th); break;
		case 0x2e:	sprintf(buffer, "Set_Prim_Depth         %04X, %04X", (cmd[1] >> 16) & 0xffff, cmd[1] & 0xffff); break;
		case 0x2f:	sprintf(buffer, "Set_Other_Modes        %08X %08X", cmd[0], cmd[1]); break;
		case 0x30:	sprintf(buffer, "Load_TLUT              %d, %s, %s, %s, %s", tile, sl, tl, sh, th); break;
		case 0x32:	sprintf(buffer, "Set_Tile_Size          %d, %s, %s, %s, %s", tile, sl, tl, sh, th); break;
		case 0x33:	sprintf(buffer, "Load_Block             %d, %03X, %03X, %03X, %03X", tile, (cmd[0] >> 12) & 0xfff, cmd[0] & 0xfff, (cmd[1] >> 12) & 0xfff, cmd[1] & 0xfff); break;
		case 0x34:	sprintf(buffer, "Load_Tile              %d, %s, %s, %s, %s", tile, sl, tl, sh, th); break;
		case 0x35:	sprintf(buffer, "Set_Tile               %d, %s, %s, %d, %04X", tile, format, size, ((cmd[0] >> 9) & 0x1ff) * 8, (cmd[0] & 0x1ff) * 8); break;
		case 0x36:	sprintf(buffer, "Fill_Rectangle         %s, %s, %s, %s", sh, th, sl, tl); break;
		case 0x37:	sprintf(buffer, "Set_Fill_Color         R: %d, G: %d, B: %d, A: %d", r, g, b, a); break;
		case 0x38:	sprintf(buffer, "Set_Fog_Color          R: %d, G: %d, B: %d, A: %d", r, g, b, a); break;
		case 0x39:	sprintf(buffer, "Set_Blend_Color        R: %d, G: %d, B: %d, A: %d", r, g, b, a); break;
		case 0x3a:	sprintf(buffer, "Set_Prim_Color         %d, %d, R: %d, G: %d, B: %d, A: %d", (cmd[0] >> 8) & 0x1f, cmd[0] & 0xff, r, g, b, a); break;
		case 0x3b:	sprintf(buffer, "Set_Env_Color          R: %d, G: %d, B: %d, A: %d", r, g, b, a); break;
		case 0x3c:	sprintf(buffer, "Set_Combine            %08X %08X", cmd[0], cmd[1]); break;
		case 0x3d:	sprintf(buffer, "Set_Texture_Image      %s, %s, %d, %08X", format, size, (cmd[0] & 0x1ff)+1, cmd[1]); break;
		case 0x3e:	sprintf(buffer, "Set_Mask_Image         %08X", cmd[1]); break;
		case 0x3f:	sprintf(buffer, "Set_Color_Image        %s, %s, %d, %08X", format, size, (cmd[0] & 0x1ff)+1, cmd[1]); break;
		default:	sprintf(buffer, "Unknown command 0x%06X (%08X %08X)", command, cmd[0], cmd[1]); break;
	}

	return rdp_command_length[command];
}







static void rdp_invalid(UINT32 w1, UINT32 w2)
{
}

static void rdp_noop(UINT32 w1, UINT32 w2)
{
}

static void rdp_tri_noshade(UINT32 w1, UINT32 w2)
{
	INT32 ewdata[44];
	memcpy(&ewdata[0], &rdp_cmd_data[rdp_cmd_cur], 8 * sizeof(INT32));
	memset(&ewdata[8], 0, 36 * sizeof(INT32));
	edgewalker_for_prims(ewdata);
}

static void rdp_tri_noshade_z(UINT32 w1, UINT32 w2)
{
	INT32 ewdata[44];
	memcpy(&ewdata[0], &rdp_cmd_data[rdp_cmd_cur], 8 * sizeof(INT32));
	memset(&ewdata[8], 0, 32 * sizeof(INT32));
	memcpy(&ewdata[40], &rdp_cmd_data[rdp_cmd_cur + 8], 4 * sizeof(INT32));
	edgewalker_for_prims(ewdata);
}

static void rdp_tri_tex(UINT32 w1, UINT32 w2)
{
	INT32 ewdata[44];
	memcpy(&ewdata[0], &rdp_cmd_data[rdp_cmd_cur], 8 * sizeof(INT32));
	memset(&ewdata[8], 0, 16 * sizeof(INT32));
	memcpy(&ewdata[24], &rdp_cmd_data[rdp_cmd_cur + 8], 16 * sizeof(INT32));
	memset(&ewdata[40], 0, 4 * sizeof(INT32));
	edgewalker_for_prims(ewdata);
}

static void rdp_tri_tex_z(UINT32 w1, UINT32 w2)
{
	INT32 ewdata[44];
	memcpy(&ewdata[0], &rdp_cmd_data[rdp_cmd_cur], 8 * sizeof(INT32));
	memset(&ewdata[8], 0, 16 * sizeof(INT32));
	memcpy(&ewdata[24], &rdp_cmd_data[rdp_cmd_cur + 8], 16 * sizeof(INT32));
	memcpy(&ewdata[40], &rdp_cmd_data[rdp_cmd_cur + 24], 4 * sizeof(INT32));
	edgewalker_for_prims(ewdata);
}

static void rdp_tri_shade(UINT32 w1, UINT32 w2)
{
	INT32 ewdata[44];
	memcpy(&ewdata[0], &rdp_cmd_data[rdp_cmd_cur], 24 * sizeof(INT32));
	memset(&ewdata[24], 0, 20 * sizeof(INT32));
	edgewalker_for_prims(ewdata);
}

static void rdp_tri_shade_z(UINT32 w1, UINT32 w2)
{
	INT32 ewdata[44];
	memcpy(&ewdata[0], &rdp_cmd_data[rdp_cmd_cur], 24 * sizeof(INT32));
	memset(&ewdata[24], 0, 16 * sizeof(INT32));
	memcpy(&ewdata[40], &rdp_cmd_data[rdp_cmd_cur + 24], 4 * sizeof(INT32));
	edgewalker_for_prims(ewdata);
}

static void rdp_tri_texshade(UINT32 w1, UINT32 w2)
{
	INT32 ewdata[44];
	memcpy(&ewdata[0], &rdp_cmd_data[rdp_cmd_cur], 40 * sizeof(INT32));
	memset(&ewdata[40], 0, 4 * sizeof(INT32));
	edgewalker_for_prims(ewdata);
}

static void rdp_tri_texshade_z(UINT32 w1, UINT32 w2)
{
	INT32 ewdata[44];
	memcpy(&ewdata[0], &rdp_cmd_data[rdp_cmd_cur], 44 * sizeof(INT32));
	edgewalker_for_prims(ewdata);
}

static void rdp_tex_rect(UINT32 w1, UINT32 w2)
{
	UINT32 w3 = rdp_cmd_data[rdp_cmd_cur + 2];
	UINT32 w4 = rdp_cmd_data[rdp_cmd_cur + 3];

	
	UINT32 tilenum	= (w2 >> 24) & 0x7;
	UINT32 xl = (w1 >> 12) & 0xfff;
	UINT32 yl	= (w1 >>  0) & 0xfff;
	UINT32 xh	= (w2 >> 12) & 0xfff;
	UINT32 yh	= (w2 >>  0) & 0xfff;
	
	INT32 s = (w3 >> 16) & 0xffff;
	INT32 t = (w3 >>  0) & 0xffff;
	INT32 dsdx = (w4 >> 16) & 0xffff;
	INT32 dtdy = (w4 >>  0) & 0xffff;
	
	dsdx = SIGN16(dsdx);
	dtdy = SIGN16(dtdy);
	
	if (other_modes.cycle_type == CYCLE_TYPE_FILL || other_modes.cycle_type == CYCLE_TYPE_COPY)
		yl |= 3;

	UINT32 xlint = (xl >> 2) & 0x3ff;
	UINT32 xhint = (xh >> 2) & 0x3ff;

	INT32 ewdata[44];
	ewdata[0] = (0x24 << 24) | ((0x80 | tilenum) << 16) | yl;
	ewdata[1] = (yl << 16) | yh;
	ewdata[2] = (xlint << 16) | ((xl & 3) << 14);
	ewdata[3] = 0;
	ewdata[4] = (xhint << 16) | ((xh & 3) << 14);
	ewdata[5] = 0;
	ewdata[6] = (xlint << 16) | ((xl & 3) << 14);
	ewdata[7] = 0;
	memset(&ewdata[8], 0, 16 * sizeof(UINT32));
	ewdata[24] = (s << 16) | t;
	ewdata[25] = 0;
	ewdata[26] = ((dsdx >> 5) << 16);
	ewdata[27] = 0;
	ewdata[28] = 0;
	ewdata[29] = 0;
	ewdata[30] = ((dsdx & 0x1f) << 11) << 16;
	ewdata[31] = 0;
	ewdata[32] = (dtdy >> 5) & 0xffff;
	ewdata[33] = 0;
	ewdata[34] = (dtdy >> 5) & 0xffff;
	ewdata[35] = 0;
	ewdata[36] = (dtdy & 0x1f) << 11;
	ewdata[37] = 0;
	ewdata[38] = (dtdy & 0x1f) << 11;
	ewdata[39] = 0;
	memset(&ewdata[40], 0, 4 * sizeof(INT32));

	

	edgewalker_for_prims(ewdata);

}

static void rdp_tex_rect_flip(UINT32 w1, UINT32 w2)
{
	UINT32 w3 = rdp_cmd_data[rdp_cmd_cur+2];
	UINT32 w4 = rdp_cmd_data[rdp_cmd_cur+3];
	
	
	UINT32 tilenum	= (w2 >> 24) & 0x7;
	UINT32 xl = (w1 >> 12) & 0xfff;
	UINT32 yl	= (w1 >>  0) & 0xfff;
	UINT32 xh	= (w2 >> 12) & 0xfff;
	UINT32 yh	= (w2 >>  0) & 0xfff;
	
	INT32 s = (w3 >> 16) & 0xffff;
	INT32 t = (w3 >>  0) & 0xffff;
	INT32 dsdx = (w4 >> 16) & 0xffff;
	INT32 dtdy = (w4 >>  0) & 0xffff;
	
	dsdx = SIGN16(dsdx);
	dtdy = SIGN16(dtdy);

	if (other_modes.cycle_type == CYCLE_TYPE_FILL || other_modes.cycle_type == CYCLE_TYPE_COPY)
		yl |= 3;

	UINT32 xlint = (xl >> 2) & 0x3ff;
	UINT32 xhint = (xh >> 2) & 0x3ff;

	INT32 ewdata[44];
	ewdata[0] = (0x25 << 24) | ((0x80 | tilenum) << 16) | yl;
	ewdata[1] = (yl << 16) | yh;
	ewdata[2] = (xlint << 16) | ((xl & 3) << 14);
	ewdata[3] = 0;
	ewdata[4] = (xhint << 16) | ((xh & 3) << 14);
	ewdata[5] = 0;
	ewdata[6] = (xlint << 16) | ((xl & 3) << 14);
	ewdata[7] = 0;
	memset(&ewdata[8], 0, 16 * sizeof(INT32));
	ewdata[24] = (s << 16) | t;
	ewdata[25] = 0;
	
	ewdata[26] = (dtdy >> 5) & 0xffff;
	ewdata[27] = 0;
	ewdata[28] = 0;
	ewdata[29] = 0;
	ewdata[30] = ((dtdy & 0x1f) << 11);
	ewdata[31] = 0;
	ewdata[32] = (dsdx >> 5) << 16;
	ewdata[33] = 0;
	ewdata[34] = (dsdx >> 5) << 16;
	ewdata[35] = 0;
	ewdata[36] = (dsdx & 0x1f) << 27;
	ewdata[37] = 0;
	ewdata[38] = (dsdx & 0x1f) << 27;
	ewdata[39] = 0;
	memset(&ewdata[40], 0, 4 * sizeof(INT32));

	edgewalker_for_prims(ewdata);
}

static void rdp_sync_load(UINT32 w1, UINT32 w2)
{
	
}

static void rdp_sync_pipe(UINT32 w1, UINT32 w2)
{
	
	
}

static void rdp_sync_tile(UINT32 w1, UINT32 w2)
{
	
}

static void rdp_sync_full(UINT32 w1, UINT32 w2)
{
	
	

	
	
	
	
	
	
	
	
	
	

	
	
	
	
	
	
	
	
	

	z64gl_command = 0;

	*gfx_al.MI_INTR_REG |= DP_INTERRUPT;
	gfx_al.CheckInterrupts();
}

static void rdp_set_key_gb(UINT32 w1, UINT32 w2)
{
	key_width.g = (w1 >> 12) & 0xfff;
	key_width.b = w1 & 0xfff;
	key_center.g = (w2 >> 24) & 0xff;
	key_scale.g = (w2 >> 16) & 0xff;
	key_center.b = (w2 >> 8) & 0xff;
	key_scale.b = w2 & 0xff;
}

static void rdp_set_key_r(UINT32 w1, UINT32 w2)
{
	key_width.r = (w2 >> 16) & 0xfff;
	key_center.r = (w2 >> 8) & 0xff;
	key_scale.r = w2 & 0xff;
}

static void rdp_set_convert(UINT32 w1, UINT32 w2)
{
	k0 = (w1 >> 13) & 0x1ff;
	k1 = (w1 >> 4) & 0x1ff;
	k2 = ((w1 & 0xf) << 5) | ((w2 >> 27) & 0x1f);
	k3 = (w2 >> 18) & 0x1ff;
	k4 = (w2 >> 9) & 0x1ff;
	k5 = w2 & 0x1ff;
}

static void rdp_set_scissor(UINT32 w1, UINT32 w2)
{
	clip.xh = (w1 >> 12) & 0xfff;
	clip.yh = (w1 >>  0) & 0xfff;
	clip.xl = (w2 >> 12) & 0xfff;
	clip.yl = (w2 >>  0) & 0xfff;
	
	scfield = (w2 >> 25) & 1;
	sckeepodd = (w2 >> 24) & 1;
}

static void rdp_set_prim_depth(UINT32 w1, UINT32 w2)
{
	primitive_z = w2 & (0x7fff << 16);
	

	primitive_delta_z = (UINT16)(w2);
}

static void rdp_set_other_modes(UINT32 w1, UINT32 w2)
{
	other_modes.cycle_type			= (w1 >> 20) & 0x3;
	other_modes.persp_tex_en 		= (w1 & 0x80000) ? 1 : 0;
	other_modes.detail_tex_en		= (w1 & 0x40000) ? 1 : 0;
	other_modes.sharpen_tex_en		= (w1 & 0x20000) ? 1 : 0;
	other_modes.tex_lod_en			= (w1 & 0x10000) ? 1 : 0;
	other_modes.en_tlut				= (w1 & 0x08000) ? 1 : 0;
	other_modes.tlut_type			= (w1 & 0x04000) ? 1 : 0;
	other_modes.sample_type			= (w1 & 0x02000) ? 1 : 0;
	other_modes.mid_texel			= (w1 & 0x01000) ? 1 : 0;
	other_modes.bi_lerp0			= (w1 & 0x00800) ? 1 : 0;
	other_modes.bi_lerp1			= (w1 & 0x00400) ? 1 : 0;
	other_modes.convert_one			= (w1 & 0x00200) ? 1 : 0;
	other_modes.key_en				= (w1 & 0x00100) ? 1 : 0;
	other_modes.rgb_dither_sel		= (w1 >> 6) & 0x3;
	other_modes.alpha_dither_sel	= (w1 >> 4) & 0x3;
	other_modes.blend_m1a_0			= (w2 >> 30) & 0x3;
	other_modes.blend_m1a_1			= (w2 >> 28) & 0x3;
	other_modes.blend_m1b_0			= (w2 >> 26) & 0x3;
	other_modes.blend_m1b_1			= (w2 >> 24) & 0x3;
	other_modes.blend_m2a_0			= (w2 >> 22) & 0x3;
	other_modes.blend_m2a_1			= (w2 >> 20) & 0x3;
	other_modes.blend_m2b_0			= (w2 >> 18) & 0x3;
	other_modes.blend_m2b_1			= (w2 >> 16) & 0x3;
	other_modes.force_blend			= (w2 >> 14) & 1;
	other_modes.alpha_cvg_select	= (w2 >> 13) & 1;
	other_modes.cvg_times_alpha		= (w2 >> 12) & 1;
	other_modes.z_mode				= (w2 >> 10) & 0x3;
	other_modes.cvg_dest			= (w2 >> 8) & 0x3;
	other_modes.color_on_cvg		= (w2 >> 7) & 1;
	other_modes.image_read_en		= (w2 >> 6) & 1;
	other_modes.z_update_en			= (w2 >> 5) & 1;
	other_modes.z_compare_en		= (w2 >> 4) & 1;
	other_modes.antialias_en		= (w2 >> 3) & 1;
	other_modes.z_source_sel		= (w2 >> 2) & 1;
	other_modes.dither_alpha_en		= (w2 >> 1) & 1;
	other_modes.alpha_compare_en	= (w2) & 1;

	SET_BLENDER_INPUT(0, 0, &blender1a_r[0], &blender1a_g[0], &blender1a_b[0], &blender1b_a[0],
					  other_modes.blend_m1a_0, other_modes.blend_m1b_0);
	SET_BLENDER_INPUT(0, 1, &blender2a_r[0], &blender2a_g[0], &blender2a_b[0], &blender2b_a[0],
					  other_modes.blend_m2a_0, other_modes.blend_m2b_0);
	SET_BLENDER_INPUT(1, 0, &blender1a_r[1], &blender1a_g[1], &blender1a_b[1], &blender1b_a[1],
					  other_modes.blend_m1a_1, other_modes.blend_m1b_1);
	SET_BLENDER_INPUT(1, 1, &blender2a_r[1], &blender2a_g[1], &blender2a_b[1], &blender2b_a[1],
					  other_modes.blend_m2a_1, other_modes.blend_m2b_1);

	other_modes.f.stalederivs = 1;
}

void deduce_derivatives()
{
	
	other_modes.f.partialreject_1cycle = (blender2b_a[0] == &inv_pixel_color.a && blender1b_a[0] == &pixel_color.a);
	other_modes.f.partialreject_2cycle = (blender2b_a[1] == &inv_pixel_color.a && blender1b_a[1] == &pixel_color.a);

	other_modes.f.special_bsel0 = (blender2b_a[0] == &memory_color.a);
	other_modes.f.special_bsel1 = (blender2b_a[1] == &memory_color.a);

	other_modes.f.rgb_alpha_dither = (other_modes.rgb_dither_sel << 2) | other_modes.alpha_dither_sel;

	if (other_modes.rgb_dither_sel == 3)
		rgb_dither_ptr = rgb_dither_func[1];
	else
		rgb_dither_ptr = rgb_dither_func[0];

	tcdiv_ptr = tcdiv_func[other_modes.persp_tex_en];

	
	int texel1_used_in_cc1 = 0, texel0_used_in_cc1 = 0, texel0_used_in_cc0 = 0, texel1_used_in_cc0 = 0;
	int texels_in_cc0 = 0, texels_in_cc1 = 0;
	int lod_frac_used_in_cc1 = 0, lod_frac_used_in_cc0 = 0;

	if ((combiner_rgbmul_r[1] == &lod_frac) || (combiner_alphamul[1] == &lod_frac))
		lod_frac_used_in_cc1 = 1;
	if ((combiner_rgbmul_r[0] == &lod_frac) || (combiner_alphamul[0] == &lod_frac))
		lod_frac_used_in_cc0 = 1;

	if (combiner_rgbmul_r[1] == &texel1_color.r || combiner_rgbsub_a_r[1] == &texel1_color.r || combiner_rgbsub_b_r[1] == &texel1_color.r || combiner_rgbadd_r[1] == &texel1_color.r || \
		combiner_alphamul[1] == &texel1_color.a || combiner_alphasub_a[1] == &texel1_color.a || combiner_alphasub_b[1] == &texel1_color.a || combiner_alphaadd[1] == &texel1_color.a || \
		combiner_rgbmul_r[1] == &texel1_color.a)
		texel1_used_in_cc1 = 1;
	if (combiner_rgbmul_r[1] == &texel0_color.r || combiner_rgbsub_a_r[1] == &texel0_color.r || combiner_rgbsub_b_r[1] == &texel0_color.r || combiner_rgbadd_r[1] == &texel0_color.r || \
		combiner_alphamul[1] == &texel0_color.a || combiner_alphasub_a[1] == &texel0_color.a || combiner_alphasub_b[1] == &texel0_color.a || combiner_alphaadd[1] == &texel0_color.a || \
		combiner_rgbmul_r[1] == &texel0_color.a)
		texel0_used_in_cc1 = 1;
	if (combiner_rgbmul_r[0] == &texel1_color.r || combiner_rgbsub_a_r[0] == &texel1_color.r || combiner_rgbsub_b_r[0] == &texel1_color.r || combiner_rgbadd_r[0] == &texel1_color.r || \
		combiner_alphamul[0] == &texel1_color.a || combiner_alphasub_a[0] == &texel1_color.a || combiner_alphasub_b[0] == &texel1_color.a || combiner_alphaadd[0] == &texel1_color.a || \
		combiner_rgbmul_r[0] == &texel1_color.a)
		texel1_used_in_cc0 = 1;
	if (combiner_rgbmul_r[0] == &texel0_color.r || combiner_rgbsub_a_r[0] == &texel0_color.r || combiner_rgbsub_b_r[0] == &texel0_color.r || combiner_rgbadd_r[0] == &texel0_color.r || \
		combiner_alphamul[0] == &texel0_color.a || combiner_alphasub_a[0] == &texel0_color.a || combiner_alphasub_b[0] == &texel0_color.a || combiner_alphaadd[0] == &texel0_color.a || \
		combiner_rgbmul_r[0] == &texel0_color.a)
		texel0_used_in_cc0 = 1;
	texels_in_cc0 = texel0_used_in_cc0 || texel1_used_in_cc0;
	texels_in_cc1 = texel0_used_in_cc1 || texel1_used_in_cc1;	

	
	if (texel1_used_in_cc1)
		render_spans_1cycle_ptr = render_spans_1cycle_func[2];
	else if (texel0_used_in_cc1 || lod_frac_used_in_cc1)
		render_spans_1cycle_ptr = render_spans_1cycle_func[1];
	else
		render_spans_1cycle_ptr = render_spans_1cycle_func[0];

	if (texel1_used_in_cc1)
		render_spans_2cycle_ptr = render_spans_2cycle_func[3];
	else if (texel1_used_in_cc0 || texel0_used_in_cc1)
		render_spans_2cycle_ptr = render_spans_2cycle_func[2];
	else if (texel0_used_in_cc0 || lod_frac_used_in_cc0 || lod_frac_used_in_cc1)
		render_spans_2cycle_ptr = render_spans_2cycle_func[1];
	else
		render_spans_2cycle_ptr = render_spans_2cycle_func[0];

	
	int lodfracused = 0;

	if ((other_modes.cycle_type == CYCLE_TYPE_2 && (lod_frac_used_in_cc0 || lod_frac_used_in_cc1)) || \
		(other_modes.cycle_type == CYCLE_TYPE_1 && lod_frac_used_in_cc1))
		lodfracused = 1;

	if ((other_modes.cycle_type == CYCLE_TYPE_1 && combiner_rgbsub_a_r[1] == &noise) || \
		(other_modes.cycle_type == CYCLE_TYPE_2 && (combiner_rgbsub_a_r[0] == &noise || combiner_rgbsub_a_r[1] == &noise)) || \
		other_modes.alpha_dither_sel == 2)
		get_dither_noise_ptr = get_dither_noise_func[0];
	else if (other_modes.f.rgb_alpha_dither != 0xf)
		get_dither_noise_ptr = get_dither_noise_func[1];
	else
		get_dither_noise_ptr = get_dither_noise_func[2];

	other_modes.f.dolod = other_modes.tex_lod_en || lodfracused;
}

STRICTINLINE INT32 irand()
{
	iseed *= 0x343fd;
	iseed += 0x269ec3;
	return ((iseed >> 16) & 0x7fff);
}

static void rdp_set_tile_size(UINT32 w1, UINT32 w2)
{
	int tilenum = (w2 >> 24) & 0x7;
	tile[tilenum].sl = (w1 >> 12) & 0xfff;
	tile[tilenum].tl = (w1 >>  0) & 0xfff;
	tile[tilenum].sh = (w2 >> 12) & 0xfff;
	tile[tilenum].th = (w2 >>  0) & 0xfff;

	calculate_clamp_diffs(tilenum);
}
	
static void rdp_load_block(UINT32 w1, UINT32 w2)
{
	int tilenum = (w2 >> 24) & 0x7;
	int sl, sh, tl, dxt;
						
	
	tile[tilenum].sl = sl = ((w1 >> 12) & 0xfff);
	tile[tilenum].tl = tl = ((w1 >>  0) & 0xfff);
	tile[tilenum].sh = sh = ((w2 >> 12) & 0xfff);
	tile[tilenum].th = dxt	= ((w2 >>  0) & 0xfff);

	calculate_clamp_diffs(tilenum);

	int tlclamped = tl & 0x3ff;

	INT32 lewdata[10];
	
	lewdata[0] = (w1 & 0xff000000) | (0x10 << 19) | (tilenum << 16) | ((tlclamped << 2) | 3);
	lewdata[1] = (((tlclamped << 2) | 3) << 16) | (tlclamped << 2);
	lewdata[2] = sh << 16;
	lewdata[3] = sl << 16;
	lewdata[4] = sh << 16;
	lewdata[5] = ((sl << 3) << 16) | (tl << 3);
	lewdata[6] = (dxt & 0xff) << 8;
	lewdata[7] = ((0x80 >> ti_size) << 16) | (dxt >> 8);
	lewdata[8] = 0x20;
	lewdata[9] = 0x20;

	edgewalker_for_loads(lewdata);

}

static void rdp_load_tlut(UINT32 w1, UINT32 w2)
{
	

	tile_tlut_common_cs_decoder(w1, w2);
}

static void rdp_load_tile(UINT32 w1, UINT32 w2)
{
	tile_tlut_common_cs_decoder(w1, w2);
}

void tile_tlut_common_cs_decoder(UINT32 w1, UINT32 w2)
{
	int tilenum = (w2 >> 24) & 0x7;
	int sl, tl, sh, th;

	
	tile[tilenum].sl = sl = ((w1 >> 12) & 0xfff);
	tile[tilenum].tl = tl = ((w1 >>  0) & 0xfff);
	tile[tilenum].sh = sh = ((w2 >> 12) & 0xfff);
	tile[tilenum].th = th = ((w2 >>  0) & 0xfff);

	calculate_clamp_diffs(tilenum);

	
	INT32 lewdata[10];

	lewdata[0] = (w1 & 0xff000000) | (0x10 << 19) | (tilenum << 16) | (th | 3);
	lewdata[1] = ((th | 3) << 16) | (tl);
	lewdata[2] = ((sh >> 2) << 16) | ((sh & 3) << 14);
	lewdata[3] = ((sl >> 2) << 16) | ((sl & 3) << 14);
	lewdata[4] = ((sh >> 2) << 16) | ((sh & 3) << 14);
	lewdata[5] = ((sl << 3) << 16) | (tl << 3);
	lewdata[6] = 0;
	lewdata[7] = (0x200 >> ti_size) << 16;
	lewdata[8] = 0x20;
	lewdata[9] = 0x20;

	edgewalker_for_loads(lewdata);
}

static void rdp_set_tile(UINT32 w1, UINT32 w2)
{
	int tilenum = (w2 >> 24) & 0x7;
	
	tile[tilenum].format	= (w1 >> 21) & 0x7;
	tile[tilenum].size		= (w1 >> 19) & 0x3;
	tile[tilenum].line		= (w1 >>  9) & 0x1ff;
	tile[tilenum].tmem		= (w1 >>  0) & 0x1ff;
	tile[tilenum].palette	= (w2 >> 20) & 0xf;
	tile[tilenum].ct		= (w2 >> 19) & 0x1;
	tile[tilenum].mt		= (w2 >> 18) & 0x1;
	tile[tilenum].mask_t	= (w2 >> 14) & 0xf;
	tile[tilenum].shift_t	= (w2 >> 10) & 0xf;
	tile[tilenum].cs		= (w2 >>  9) & 0x1;
	tile[tilenum].ms		= (w2 >>  8) & 0x1;
	tile[tilenum].mask_s	= (w2 >>  4) & 0xf;
	tile[tilenum].shift_s	= (w2 >>  0) & 0xf;

	calculate_tile_derivs(tilenum);
}

static void rdp_fill_rect(UINT32 w1, UINT32 w2)
{
	UINT32 xl = (w1 >> 12) & 0xfff;
	UINT32 yl = (w1 >>  0) & 0xfff;
	UINT32 xh = (w2 >> 12) & 0xfff;
	UINT32 yh = (w2 >>  0) & 0xfff;

	if (other_modes.cycle_type == CYCLE_TYPE_FILL || other_modes.cycle_type == CYCLE_TYPE_COPY)
		yl |= 3;

	UINT32 xlint = (xl >> 2) & 0x3ff;
	UINT32 xhint = (xh >> 2) & 0x3ff;

	INT32 ewdata[44];
	ewdata[0] = (0x3680 << 16) | yl;
	ewdata[1] = (yl << 16) | yh;
	ewdata[2] = (xlint << 16) | ((xl & 3) << 14);
	ewdata[3] = 0;
	ewdata[4] = (xhint << 16) | ((xh & 3) << 14);
	ewdata[5] = 0;
	ewdata[6] = (xlint << 16) | ((xl & 3) << 14);
	ewdata[7] = 0;
	memset(&ewdata[8], 0, 36 * sizeof(INT32));

	edgewalker_for_prims(ewdata);
}

static void rdp_set_fill_color(UINT32 w1, UINT32 w2)
{
	fill_color = w2;
}

static void rdp_set_fog_color(UINT32 w1, UINT32 w2)
{
	fog_color.r = (w2 >> 24) & 0xff;
	fog_color.g = (w2 >> 16) & 0xff;
	fog_color.b = (w2 >>  8) & 0xff;
	fog_color.a = (w2 >>  0) & 0xff;
}

static void rdp_set_blend_color(UINT32 w1, UINT32 w2)
{
	blend_color.r = (w2 >> 24) & 0xff;
	blend_color.g = (w2 >> 16) & 0xff;
	blend_color.b = (w2 >>  8) & 0xff;
	blend_color.a = (w2 >>  0) & 0xff;
}

static void rdp_set_prim_color(UINT32 w1, UINT32 w2)
{
	min_level = (w1 >> 8) & 0x1f;
	primitive_lod_frac = w1 & 0xff;
	prim_color.r = (w2 >> 24) & 0xff;
	prim_color.g = (w2 >> 16) & 0xff;
	prim_color.b = (w2 >>  8) & 0xff;
	prim_color.a = (w2 >>  0) & 0xff;
}

static void rdp_set_env_color(UINT32 w1, UINT32 w2)
{
	env_color.r = (w2 >> 24) & 0xff;
	env_color.g = (w2 >> 16) & 0xff;
	env_color.b = (w2 >>  8) & 0xff;
	env_color.a = (w2 >>  0) & 0xff;
}

static void rdp_set_combine(UINT32 w1, UINT32 w2)
{
	combine.sub_a_rgb0	= (w1 >> 20) & 0xf;
	combine.mul_rgb0	= (w1 >> 15) & 0x1f;
	combine.sub_a_a0	= (w1 >> 12) & 0x7;
	combine.mul_a0		= (w1 >>  9) & 0x7;
	combine.sub_a_rgb1	= (w1 >>  5) & 0xf;
	combine.mul_rgb1	= (w1 >>  0) & 0x1f;

	combine.sub_b_rgb0	= (w2 >> 28) & 0xf;
	combine.sub_b_rgb1	= (w2 >> 24) & 0xf;
	combine.sub_a_a1	= (w2 >> 21) & 0x7;
	combine.mul_a1		= (w2 >> 18) & 0x7;
	combine.add_rgb0	= (w2 >> 15) & 0x7;
	combine.sub_b_a0	= (w2 >> 12) & 0x7;
	combine.add_a0		= (w2 >>  9) & 0x7;
	combine.add_rgb1	= (w2 >>  6) & 0x7;
	combine.sub_b_a1	= (w2 >>  3) & 0x7;
	combine.add_a1		= (w2 >>  0) & 0x7;

	
	SET_SUBA_RGB_INPUT(&combiner_rgbsub_a_r[0], &combiner_rgbsub_a_g[0], &combiner_rgbsub_a_b[0], combine.sub_a_rgb0);
	SET_SUBB_RGB_INPUT(&combiner_rgbsub_b_r[0], &combiner_rgbsub_b_g[0], &combiner_rgbsub_b_b[0], combine.sub_b_rgb0);
	SET_MUL_RGB_INPUT(&combiner_rgbmul_r[0], &combiner_rgbmul_g[0], &combiner_rgbmul_b[0], combine.mul_rgb0);
	SET_ADD_RGB_INPUT(&combiner_rgbadd_r[0], &combiner_rgbadd_g[0], &combiner_rgbadd_b[0], combine.add_rgb0);
	SET_SUB_ALPHA_INPUT(&combiner_alphasub_a[0], combine.sub_a_a0);
	SET_SUB_ALPHA_INPUT(&combiner_alphasub_b[0], combine.sub_b_a0);
	SET_MUL_ALPHA_INPUT(&combiner_alphamul[0], combine.mul_a0);
	SET_SUB_ALPHA_INPUT(&combiner_alphaadd[0], combine.add_a0);

	SET_SUBA_RGB_INPUT(&combiner_rgbsub_a_r[1], &combiner_rgbsub_a_g[1], &combiner_rgbsub_a_b[1], combine.sub_a_rgb1);
	SET_SUBB_RGB_INPUT(&combiner_rgbsub_b_r[1], &combiner_rgbsub_b_g[1], &combiner_rgbsub_b_b[1], combine.sub_b_rgb1);
	SET_MUL_RGB_INPUT(&combiner_rgbmul_r[1], &combiner_rgbmul_g[1], &combiner_rgbmul_b[1], combine.mul_rgb1);
	SET_ADD_RGB_INPUT(&combiner_rgbadd_r[1], &combiner_rgbadd_g[1], &combiner_rgbadd_b[1], combine.add_rgb1);
	SET_SUB_ALPHA_INPUT(&combiner_alphasub_a[1], combine.sub_a_a1);
	SET_SUB_ALPHA_INPUT(&combiner_alphasub_b[1], combine.sub_b_a1);
	SET_MUL_ALPHA_INPUT(&combiner_alphamul[1], combine.mul_a1);
	SET_SUB_ALPHA_INPUT(&combiner_alphaadd[1], combine.add_a1);

	other_modes.f.stalederivs = 1;
}

static void rdp_set_texture_image(UINT32 w1, UINT32 w2)
{
	ti_format	= (w1 >> 21) & 0x7;
	ti_size		= (w1 >> 19) & 0x3;
	ti_width	= (w1 & 0x3ff) + 1;
	ti_address	= w2 & 0x0ffffff;
	
	
	
}

static void rdp_set_mask_image(UINT32 w1, UINT32 w2)
{
	zb_address	= w2 & 0x0ffffff;
}

static void rdp_set_color_image(UINT32 w1, UINT32 w2)
{
	fb_format 	= (w1 >> 21) & 0x7;
	fb_size		= (w1 >> 19) & 0x3;
	fb_width	= (w1 & 0x3ff) + 1;
	fb_address	= w2 & 0x0ffffff;

	
	fbread1_ptr = fbread_func[fb_size];
	fbread2_ptr = fbread2_func[fb_size];
	fbwrite_ptr = fbwrite_func[fb_size];
	fbfill_ptr = fbfill_func[fb_size];
}



static void (*const rdp_command_table[64])(UINT32 w1, UINT32 w2) =
{
	
	rdp_noop,			rdp_invalid,			rdp_invalid,			rdp_invalid,
	rdp_invalid,		rdp_invalid,			rdp_invalid,			rdp_invalid,
	rdp_tri_noshade,	rdp_tri_noshade_z,		rdp_tri_tex,			rdp_tri_tex_z,
	rdp_tri_shade,		rdp_tri_shade_z,		rdp_tri_texshade,		rdp_tri_texshade_z,
	
	rdp_invalid,		rdp_invalid,			rdp_invalid,			rdp_invalid,
	rdp_invalid,		rdp_invalid,			rdp_invalid,			rdp_invalid,
	rdp_invalid,		rdp_invalid,			rdp_invalid,			rdp_invalid,
	rdp_invalid,		rdp_invalid,			rdp_invalid,			rdp_invalid,
	
	rdp_invalid,		rdp_invalid,			rdp_invalid,			rdp_invalid,
	rdp_tex_rect,		rdp_tex_rect_flip,		rdp_sync_load,			rdp_sync_pipe,
	rdp_sync_tile,		rdp_sync_full,			rdp_set_key_gb,			rdp_set_key_r,
	rdp_set_convert,	rdp_set_scissor,		rdp_set_prim_depth,		rdp_set_other_modes,
	
	rdp_load_tlut,		rdp_invalid,			rdp_set_tile_size,		rdp_load_block,
	rdp_load_tile,		rdp_set_tile,			rdp_fill_rect,			rdp_set_fill_color,
	rdp_set_fog_color,	rdp_set_blend_color,	rdp_set_prim_color,		rdp_set_env_color,
	rdp_set_combine,	rdp_set_texture_image,	rdp_set_mask_image,		rdp_set_color_image
};

void rdp_process_list(void)
{
	int i, length;
	UINT32 cmd, cmd_length;
	UINT32 dp_current_al = dp_current & ~7, dp_end_al = dp_end & ~7; 

	dp_status &= ~DP_STATUS_FREEZE;
	
	
	
	

	

	if (dp_end_al <= dp_current_al)
	{
		
		
		
		
		
		
		return;
	}

	length = (dp_end_al - dp_current_al) >> 2;

	ptr_onstart = rdp_cmd_ptr;

	

	
	
	
	
	

	
	if ((rdp_cmd_ptr + length) & ~0xffff)
		fatalerror("rdp_process_list: rdp_cmd_ptr overflow: length 0x%x ptr_onstart 0x%x", length, ptr_onstart);

	
	dp_current_al >>= 2;

	if (dp_status & DP_STATUS_XBUS_DMA)
	{
		for (i = 0; i < length; i ++)
		{
			rdp_cmd_data[rdp_cmd_ptr] = rsp_dmem[dp_current_al & 0x3ff];
			rdp_cmd_ptr++;
			dp_current_al++;
		}
	}
	else
	{
		for (i = 0; i < length; i ++)
		{
			rdp_cmd_data[rdp_cmd_ptr] = RREADIDX32(dp_current_al & 0x3fffff);
			rdp_cmd_ptr++;
			dp_current_al++;
		}
	}
	

	while (rdp_cmd_cur < rdp_cmd_ptr && !rdp_pipeline_crashed)
	{
		cmd = (rdp_cmd_data[rdp_cmd_cur] >> 24) & 0x3f;
		cmd_length = rdp_command_length[cmd] >> 2;

		
		
		if ((rdp_cmd_ptr - rdp_cmd_cur) < cmd_length)
		{
			
			dp_start = dp_current = dp_end;
			return;
		}
		
#if 0
		if (LOG_RDP_EXECUTION)
		{
			char string[4000];
			if (1)
			{
			z64gl_command += cmd_length;
			
			
			rdp_dasm(string);
			fprintf(rdp_exec, "%08X: %08X %08X   %s\n", command_counter, rdp_cmd_data[rdp_cmd_cur+0], rdp_cmd_data[rdp_cmd_cur+1], string);
			}
			command_counter++;
		}
#endif
		
		rdp_command_table[cmd](rdp_cmd_data[rdp_cmd_cur+0], rdp_cmd_data[rdp_cmd_cur + 1]);
		
		rdp_cmd_cur += cmd_length;
	};
	rdp_cmd_ptr = 0;
	rdp_cmd_cur = 0;
	dp_start = dp_current = dp_end;
	
	
	
}

STRICTINLINE int alpha_compare(INT32 comb_alpha)
{
	INT32 threshold;
	if (!other_modes.alpha_compare_en)
		return 1;
	else
	{
		if (!other_modes.dither_alpha_en)
			threshold = blend_color.a;
		else
			threshold = irand() & 0xff;
		if (comb_alpha >= threshold)
			return 1;
		else
			return 0;
	}
}

STRICTINLINE INT32 color_combiner_equation(INT32 a, INT32 b, INT32 c, INT32 d)
{





	a = special_9bit_exttable[a];
	b = special_9bit_exttable[b];
	c = SIGNF(c, 9);
	d = special_9bit_exttable[d];
	a = ((a - b) * c) + (d << 8) + 0x80;
	return (a & 0x1ffff);
}

STRICTINLINE INT32 alpha_combiner_equation(INT32 a, INT32 b, INT32 c, INT32 d)
{
	a = special_9bit_exttable[a];
	b = special_9bit_exttable[b];
	c = SIGNF(c, 9);
	d = special_9bit_exttable[d];
	a = (((a - b) * c) + (d << 8) + 0x80) >> 8;
	return (a & 0x1ff);
}


STRICTINLINE void blender_equation_cycle0(int* r, int* g, int* b)
{
	int blend1a, blend2a;
	int blr, blg, blb, sum;
	blend1a = *blender1b_a[0] >> 3;
	blend2a = *blender2b_a[0] >> 3;

	int mulb;
    
	
	
	if (other_modes.f.special_bsel0)
	{
		blend1a = (blend1a >> blshifta) & 0x3C;
		blend2a = (blend2a >> blshiftb) | 3;
	}
	
	mulb = blend2a + 1;

	
	blr = (*blender1a_r[0]) * blend1a + (*blender2a_r[0]) * mulb;
	blg = (*blender1a_g[0]) * blend1a + (*blender2a_g[0]) * mulb;
	blb = (*blender1a_b[0]) * blend1a + (*blender2a_b[0]) * mulb;
	
	

	if (!other_modes.force_blend)
	{
		
		
		
		
		
		sum = ((blend1a & ~3) + (blend2a & ~3) + 4) << 9;
		*r = bldiv_hwaccurate_table[sum | ((blr >> 2) & 0x7ff)];
		*g = bldiv_hwaccurate_table[sum | ((blg >> 2) & 0x7ff)];
		*b = bldiv_hwaccurate_table[sum | ((blb >> 2) & 0x7ff)];
	}
	else
	{
		*r = (blr >> 5) & 0xff;	
		*g = (blg >> 5) & 0xff; 
		*b = (blb >> 5) & 0xff;
	}	
}

STRICTINLINE void blender_equation_cycle0_2(int* r, int* g, int* b)
{
	int blend1a, blend2a;
	blend1a = *blender1b_a[0] >> 3;
	blend2a = *blender2b_a[0] >> 3;

	if (other_modes.f.special_bsel0)
	{
		blend1a = (blend1a >> pastblshifta) & 0x3C;
		blend2a = (blend2a >> pastblshiftb) | 3;
	}
	
	blend2a += 1;
	*r = (((*blender1a_r[0]) * blend1a + (*blender2a_r[0]) * blend2a) >> 5) & 0xff;
	*g = (((*blender1a_g[0]) * blend1a + (*blender2a_g[0]) * blend2a) >> 5) & 0xff;
	*b = (((*blender1a_b[0]) * blend1a + (*blender2a_b[0]) * blend2a) >> 5) & 0xff;
}

STRICTINLINE void blender_equation_cycle1(int* r, int* g, int* b)
{
	int blend1a, blend2a;
	int blr, blg, blb, sum;
	blend1a = *blender1b_a[1] >> 3;
	blend2a = *blender2b_a[1] >> 3;

	int mulb;
	if (other_modes.f.special_bsel1)
	{
		blend1a = (blend1a >> blshifta) & 0x3C;
		blend2a = (blend2a >> blshiftb) | 3;
	}
	
	mulb = blend2a + 1;
	blr = (*blender1a_r[1]) * blend1a + (*blender2a_r[1]) * mulb;
	blg = (*blender1a_g[1]) * blend1a + (*blender2a_g[1]) * mulb;
	blb = (*blender1a_b[1]) * blend1a + (*blender2a_b[1]) * mulb;

	if (!other_modes.force_blend)
	{
		sum = ((blend1a & ~3) + (blend2a & ~3) + 4) << 9;
		*r = bldiv_hwaccurate_table[sum | ((blr >> 2) & 0x7ff)];
		*g = bldiv_hwaccurate_table[sum | ((blg >> 2) & 0x7ff)];
		*b = bldiv_hwaccurate_table[sum | ((blb >> 2) & 0x7ff)];
	}
	else
	{
		*r = (blr >> 5) & 0xff;	
		*g = (blg >> 5) & 0xff; 
		*b = (blb >> 5) & 0xff;
	}
}




STRICTINLINE UINT32 rightcvghex(UINT32 x, UINT32 fmask)
{
	UINT32 covered = ((x & 7) + 1) >> 1;
	
	covered = 0xf0 >> covered;
	return (covered & fmask);
}

STRICTINLINE UINT32 leftcvghex(UINT32 x, UINT32 fmask) 
{
	UINT32 covered = ((x & 7) + 1) >> 1;
	covered = 0xf >> covered;
	return (covered & fmask);
}

STRICTINLINE void compute_cvg_flip(INT32 scanline)
{
	INT32 purgestart, purgeend;
	int i, length, fmask, maskshift, fmaskshifted;
	INT32 fleft, minorcur, majorcur, minorcurint, majorcurint, samecvg;
	
	purgestart = span[scanline].rx;
	purgeend = span[scanline].lx;
	length = purgeend - purgestart;
	if (length >= 0)
	{
		memset(&cvgbuf[purgestart], 0, (length + 1) << 2);
		for(i = 0; i < 4; i++)
		{
			if (!span[scanline].invalyscan[i])
			{
				minorcur = span[scanline].minorx[i];
				majorcur = span[scanline].majorx[i];
				minorcurint = minorcur >> 3;
				majorcurint = majorcur >> 3;
				fmask = 0xa >> (i & 1);
				
				
				
				
				maskshift = (i - 2) & 4;

				fmaskshifted = fmask << maskshift;
				fleft = majorcurint + 1;

				if (minorcurint != majorcurint)
				{
					cvgbuf[minorcurint] |= (rightcvghex(minorcur, fmask) << maskshift);
					cvgbuf[majorcurint] |= (leftcvghex(majorcur, fmask) << maskshift);
				}
				else
				{
					samecvg = rightcvghex(minorcur, fmask) & leftcvghex(majorcur, fmask);
					cvgbuf[majorcurint] |= (samecvg << maskshift);
				}
				for (; fleft < minorcurint; fleft++)
					cvgbuf[fleft] |= fmaskshifted;
			}
		}
	}
}

STRICTINLINE void compute_cvg_noflip(INT32 scanline)
{
	INT32 purgestart, purgeend;
	int i, length, fmask, maskshift, fmaskshifted;
	INT32 fleft, minorcur, majorcur, minorcurint, majorcurint, samecvg;
	
	purgestart = span[scanline].lx;
	purgeend = span[scanline].rx;
	length = purgeend - purgestart;

	if (length >= 0)
	{
		memset(&cvgbuf[purgestart], 0, (length + 1) << 2);

		for(i = 0; i < 4; i++)
		{
			if (!span[scanline].invalyscan[i])
			{
				minorcur = span[scanline].minorx[i];
				majorcur = span[scanline].majorx[i];
				minorcurint = minorcur >> 3;
				majorcurint = majorcur >> 3;
				fmask = 0xa >> (i & 1);
				maskshift = (i - 2) & 4;
				fmaskshifted = fmask << maskshift;
				fleft = minorcurint + 1;

				if (minorcurint != majorcurint)
				{
					cvgbuf[minorcurint] |= (leftcvghex(minorcur, fmask) << maskshift);
					cvgbuf[majorcurint] |= (rightcvghex(majorcur, fmask) << maskshift);
				}
				else
				{
					samecvg = leftcvghex(minorcur, fmask) & rightcvghex(majorcur, fmask);
					cvgbuf[majorcurint] |= (samecvg << maskshift);
				}
				for (; fleft < majorcurint; fleft++)
					cvgbuf[fleft] |= fmaskshifted;
			}
		}
	}
}

int rdp_close()
{
	return 0;
}

STRICTINLINE void fbwrite_4(UINT32 curpixel, UINT32 r, UINT32 g, UINT32 b, UINT32 blend_en, UINT32 curpixel_cvg, UINT32 curpixel_memcvg)
{
	UINT32 fb = fb_address + curpixel;
	RWRITEADDR8(fb, 0);
}

STRICTINLINE void fbwrite_8(UINT32 curpixel, UINT32 r, UINT32 g, UINT32 b, UINT32 blend_en, UINT32 curpixel_cvg, UINT32 curpixel_memcvg)
{
	UINT32 fb = fb_address + curpixel;
	PAIRWRITE8(fb, r & 0xff, (r & 1) ? 3 : 0);
}

STRICTINLINE void fbwrite_16(UINT32 curpixel, UINT32 r, UINT32 g, UINT32 b, UINT32 blend_en, UINT32 curpixel_cvg, UINT32 curpixel_memcvg)
{
#undef CVG_DRAW
#ifdef CVG_DRAW
	int covdraw = (curpixel_cvg - 1) << 5;
	r=covdraw; g=covdraw; b=covdraw;
#endif

	UINT32 fb;
	UINT16 rval;
	UINT8 hval;
	fb = (fb_address >> 1) + curpixel;	

	INT32 finalcvg = finalize_spanalpha(blend_en, curpixel_cvg, curpixel_memcvg);
	INT16 finalcolor; 

	if (fb_format == FORMAT_RGBA)
	{
		finalcolor = ((r & ~7) << 8) | ((g & ~7) << 3) | ((b & ~7) >> 2);
	}
	else
	{
		finalcolor = (r << 8) | (finalcvg << 5);
		finalcvg = 0;
	}

	
	rval = finalcolor|(finalcvg >> 2);
	hval = finalcvg & 3;
	PAIRWRITE16(fb, rval, hval);
}

STRICTINLINE void fbwrite_32(UINT32 curpixel, UINT32 r, UINT32 g, UINT32 b, UINT32 blend_en, UINT32 curpixel_cvg, UINT32 curpixel_memcvg)
{
	UINT32 fb = (fb_address >> 2) + curpixel;

	INT32 finalcolor;
	INT32 finalcvg = finalize_spanalpha(blend_en, curpixel_cvg, curpixel_memcvg);
		
	finalcolor = (r << 24) | (g << 16) | (b << 8);
	finalcolor |= (finalcvg << 5);

	PAIRWRITE32(fb, finalcolor, (g & 1) ? 3 : 0, 0);
}

STRICTINLINE void fbfill_4(UINT32 curpixel)
{
	rdp_pipeline_crashed = 1;
}

STRICTINLINE void fbfill_8(UINT32 curpixel)
{
	UINT32 fb = fb_address + curpixel;
	UINT32 val = (fill_color >> (((fb & 3) ^ 3) << 3)) & 0xff;
	UINT8 hval = ((val & 1) << 1) | (val & 1);
	PAIRWRITE8(fb, val, hval);
}

STRICTINLINE void fbfill_16(UINT32 curpixel)
{
	UINT16 val;
	UINT8 hval;
	UINT32 fb = (fb_address >> 1) + curpixel;
	if (fb & 1)
		val = fill_color & 0xffff;
	else
		val = (fill_color >> 16) & 0xffff;
	hval = ((val & 1) << 1) | (val & 1);
	PAIRWRITE16(fb, val, hval);
}

STRICTINLINE void fbfill_32(UINT32 curpixel)
{
	UINT32 fb = (fb_address >> 2) + curpixel;
	PAIRWRITE32(fb, fill_color, (fill_color & 0x10000) ? 3 : 0, (fill_color & 0x1) ? 3 : 0);
}

STRICTINLINE void fbread_4(UINT32 curpixel, UINT32* curpixel_memcvg)
{
	memory_color.r = memory_color.g = memory_color.b = 0;
	
	*curpixel_memcvg = 7;
	memory_color.a = 0xe0;
}

STRICTINLINE void fbread2_4(UINT32 curpixel, UINT32* curpixel_memcvg)
{
	pre_memory_color.r = pre_memory_color.g = pre_memory_color.b = 0;
	pre_memory_color.a = 0xe0;
	*curpixel_memcvg = 7;
}

STRICTINLINE void fbread_8(UINT32 curpixel, UINT32* curpixel_memcvg)
{
	UINT8 mem = RREADADDR8(fb_address + curpixel);
	memory_color.r = memory_color.g = memory_color.b = mem;
	*curpixel_memcvg = 7;
	memory_color.a = 0xe0;
}

STRICTINLINE void fbread2_8(UINT32 curpixel, UINT32* curpixel_memcvg)
{
	UINT8 mem = RREADADDR8(fb_address + curpixel);
	pre_memory_color.r = pre_memory_color.g = pre_memory_color.b = mem;
	pre_memory_color.a = 0xe0;
	*curpixel_memcvg = 7;
}

STRICTINLINE void fbread_16(UINT32 curpixel, UINT32* curpixel_memcvg)
{
	UINT16 fword;
	UINT8 hbyte;
	UINT32 addr = (fb_address >> 1) + curpixel;
	PAIRREAD16(fword, hbyte, addr);
	UINT8 lowbits;

	if (fb_format == FORMAT_RGBA)
	{
		memory_color.r = GET_HI(fword);
		memory_color.g = GET_MED(fword);
		memory_color.b = GET_LOW(fword);
		lowbits = ((fword & 1) << 2) | hbyte;
	}
	else
	{
		memory_color.r = memory_color.g = memory_color.b = fword >> 8;
		lowbits = (fword >> 5) & 7;
	}

	if (other_modes.image_read_en)
	{
		*curpixel_memcvg = lowbits;
		memory_color.a = lowbits << 5;
	}
	else
	{
		*curpixel_memcvg = 7;
		memory_color.a = 0xe0;
	}
}

STRICTINLINE void fbread2_16(UINT32 curpixel, UINT32* curpixel_memcvg)
{
	UINT16 fword;
	UINT8 hbyte;
	UINT32 addr = (fb_address >> 1) + curpixel;
	PAIRREAD16(fword, hbyte, addr);
	UINT8 lowbits;

	if (fb_format == FORMAT_RGBA)
	{
		pre_memory_color.r = GET_HI(fword);
		pre_memory_color.g = GET_MED(fword);
		pre_memory_color.b = GET_LOW(fword);
		lowbits = ((fword & 1) << 2) | hbyte;
	}
	else
	{
		pre_memory_color.r = pre_memory_color.g = pre_memory_color.b = fword >> 8;
		lowbits = (fword >> 5) & 7;
	}

	if (other_modes.image_read_en)
	{
		*curpixel_memcvg = lowbits;
		pre_memory_color.a = lowbits << 5;
	}
	else
	{
		*curpixel_memcvg = 7;
		pre_memory_color.a = 0xe0;
	}
	
}

STRICTINLINE void fbread_32(UINT32 curpixel, UINT32* curpixel_memcvg)
{
	UINT32 mem = RREADIDX32((fb_address >> 2) + curpixel);
	memory_color.r = (mem >> 24) & 0xff;
	memory_color.g = (mem >> 16) & 0xff;
	memory_color.b = (mem >> 8) & 0xff;
	if (other_modes.image_read_en)
	{
		*curpixel_memcvg = (mem >> 5) & 7;
		memory_color.a = (mem) & 0xe0;
	}
	else
	{
		*curpixel_memcvg = 7;
		memory_color.a = 0xe0;
	}
}

STRICTINLINE void fbread2_32(UINT32 curpixel, UINT32* curpixel_memcvg)
{
	UINT32 mem = RREADIDX32((fb_address >> 2) + curpixel);
	pre_memory_color.r = (mem >> 24) & 0xff;
	pre_memory_color.g = (mem >> 16) & 0xff;
	pre_memory_color.b = (mem >> 8) & 0xff;
	if (other_modes.image_read_en)
	{
		*curpixel_memcvg = (mem >> 5) & 7;
		pre_memory_color.a = (mem) & 0xe0;
	}
	else
	{
		*curpixel_memcvg = 7;
		pre_memory_color.a = 0xe0;
	}
}

STRICTINLINE UINT32 z_decompress(UINT32 zb)
{
	return z_complete_dec_table[(zb >> 2) & 0x3fff];
}

STRICTINLINE void z_build_com_table(void)
{
   int z;
	UINT16 altmem = 0;
	for(z = 0; z < 0x40000; z++)
   {
      switch((z >> 11) & 0x7f)
      {
         case 0x00:
         case 0x01:
         case 0x02:
         case 0x03:
         case 0x04:
         case 0x05:
         case 0x06:
         case 0x07:
         case 0x08:
         case 0x09:
         case 0x0a:
         case 0x0b:
         case 0x0c:
         case 0x0d:
         case 0x0e:
         case 0x0f:
         case 0x10:
         case 0x11:
         case 0x12:
         case 0x13:
         case 0x14:
         case 0x15:
         case 0x16:
         case 0x17:
         case 0x18:
         case 0x19:
         case 0x1a:
         case 0x1b:
         case 0x1c:
         case 0x1d:
         case 0x1e:
         case 0x1f:
         case 0x20:
         case 0x21:
         case 0x22:
         case 0x23:
         case 0x24:
         case 0x25:
         case 0x26:
         case 0x27:
         case 0x28:
         case 0x29:
         case 0x2a:
         case 0x2b:
         case 0x2c:
         case 0x2d:
         case 0x2e:
         case 0x2f:
         case 0x30:
         case 0x31:
         case 0x32:
         case 0x33:
         case 0x34:
         case 0x35:
         case 0x36:
         case 0x37:
         case 0x38:
         case 0x39:
         case 0x3a:
         case 0x3b:
         case 0x3c:
         case 0x3d:
         case 0x3e:
         case 0x3f:
            altmem = (z >> 4) & 0x1ffc;
            break;
         case 0x40:
         case 0x41:
         case 0x42:
         case 0x43:
         case 0x44:
         case 0x45:
         case 0x46:
         case 0x47:
         case 0x48:
         case 0x49:
         case 0x4a:
         case 0x4b:
         case 0x4c:
         case 0x4d:
         case 0x4e:
         case 0x4f:
         case 0x50:
         case 0x51:
         case 0x52:
         case 0x53:
         case 0x54:
         case 0x55:
         case 0x56:
         case 0x57:
         case 0x58:
         case 0x59:
         case 0x5a:
         case 0x5b:
         case 0x5c:
         case 0x5d:
         case 0x5e:
         case 0x5f:
            altmem = ((z >> 3) & 0x1ffc) | 0x2000;
            break;
         case 0x60:
         case 0x61:
         case 0x62:
         case 0x63:
         case 0x64:
         case 0x65:
         case 0x66:
         case 0x67:
         case 0x68:
         case 0x69:
         case 0x6a:
         case 0x6b:
         case 0x6c:
         case 0x6d:
         case 0x6e:
         case 0x6f:
            altmem = ((z >> 2) & 0x1ffc) | 0x4000;
            break;
         case 0x70:
         case 0x71:
         case 0x72:
         case 0x73:
         case 0x74:
         case 0x75:
         case 0x76:
         case 0x77:
            altmem = ((z >> 1) & 0x1ffc) | 0x6000;
            break;
         case 0x78:
         case 0x79:
         case 0x7a:
         case 0x7b:
            altmem = (z & 0x1ffc) | 0x8000;
            break;
         case 0x7c:
         case 0x7d:
            altmem = ((z << 1) & 0x1ffc) | 0xa000;
            break;
         case 0x7e:
            altmem = ((z << 2) & 0x1ffc) | 0xc000;
            break;
         case 0x7f:
            altmem = ((z << 2) & 0x1ffc) | 0xe000;
            break;
         default:
            fatalerror("z_build_com_table failed");
            break;
      }

      z_com_table[z] = altmem;

   }
}

STRICTINLINE void precalc_cvmask_derivatives(void)
{
	int i = 0, k = 0;
	UINT16 mask = 0, maskx = 0, masky = 0;
	UINT8 offx = 0, offy = 0;
	const UINT8 yarray[16] = {0, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0};
	const UINT8 xarray[16] = {0, 3, 2, 2, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0};

	
	for (; i < 0x100; i++)
	{
		mask = decompress_cvmask_frombyte(i);
		cvarray[i].cvg = cvarray[i].cvbit = 0;
		cvarray[i].cvbit = (i >> 7) & 1;
		for (k = 0; k < 8; k++)
			cvarray[i].cvg += ((i >> k) & 1);

		
		masky = maskx = offx = offy = 0;
		for (k = 0; k < 4; k++)
			masky |= ((mask & (0xf000 >> (k << 2))) > 0) << k;

		offy = yarray[masky];
		
		maskx = (mask & (0xf000 >> (offy << 2))) >> ((offy ^ 3) << 2);
		
		
		offx = xarray[maskx];
		
		cvarray[i].xoff = offx;
		cvarray[i].yoff = offy;
	}
}

STRICTINLINE UINT16 decompress_cvmask_frombyte(UINT8 x)
{
	UINT16 y = (x & 1) | ((x & 2) << 4) | (x & 4) | ((x & 8) << 4) |
		((x & 0x10) << 4) | ((x & 0x20) << 8) | ((x & 0x40) << 4) | ((x & 0x80) << 8);
	return y;
}

STRICTINLINE void lookup_cvmask_derivatives(UINT32 mask, UINT8* offx, UINT8* offy, UINT32* curpixel_cvg, UINT32* curpixel_cvbit)
{
	CVtcmaskDERIVATIVE temp = cvarray[mask];
	*curpixel_cvg = temp.cvg;
	*curpixel_cvbit = temp.cvbit;
	*offx = temp.xoff;
	*offy = temp.yoff;
}

STRICTINLINE void z_store(UINT32 zcurpixel, UINT32 z, int dzpixenc)
{
	UINT16 zval = z_com_table[z & 0x3ffff]|(dzpixenc >> 2);
	UINT8 hval = dzpixenc & 3;
	PAIRWRITE16(zcurpixel, zval, hval);
}

STRICTINLINE UINT32 dz_decompress(UINT32 dz_compressed)
{
	return (1 << dz_compressed);
}

STRICTINLINE UINT32 dz_compress(UINT32 value)
{
	int j = 0;
	if (value & 0xff00)
		j |= 8;
	if (value & 0xf0f0)
		j |= 4;
	if (value & 0xcccc)
		j |= 2;
	if (value & 0xaaaa)
		j |= 1;
	return j;
}

STRICTINLINE UINT32 z_compare(UINT32 zcurpixel, UINT32 sz, UINT16 dzpix, int dzpixenc, UINT32* blend_en, UINT32* prewrap, UINT32* curpixel_cvg, UINT32 curpixel_memcvg)
{


	int force_coplanar = 0;
	sz &= 0x3ffff;

	UINT32 oz, dzmem, zval, hval;
	INT32 rawdzmem;

	if (other_modes.z_compare_en)
	{
		PAIRREAD16(zval, hval, zcurpixel);
		oz = z_decompress(zval);		
		rawdzmem = ((zval & 3) << 2) | hval;
		dzmem = dz_decompress(rawdzmem);

		
		blshifta = CLIP(dzpixenc - rawdzmem, 0, 4);
		blshiftb = CLIP(rawdzmem - dzpixenc, 0, 4);

		int precision_factor = (zval >> 13) & 0xf;

		
		UINT32 dzmemmodifier; 
		if (precision_factor < 3)
		{
			if (dzmem != 0x8000)
			{
				dzmemmodifier = 16 >> precision_factor;
				dzmem <<= 1;
				if (dzmem <= dzmemmodifier)
					dzmem = dzmemmodifier;
			}
			else
			{
				force_coplanar = 1;
				dzmem <<= 1;
			}
			
		}
		if (dzmem > 0x8000)
			dzmem = 0xffff;

		UINT32 dznew = (dzmem > dzpix) ? dzmem : (UINT32)dzpix;
		UINT32 dznotshift = dznew;
		dznew <<= 3;
		

		UINT32 farther = force_coplanar || ((sz + dznew) >= oz);
		
		int overflow = (curpixel_memcvg + *curpixel_cvg) & 8;
		*blend_en = other_modes.force_blend || (!overflow && other_modes.antialias_en && farther);
		
		*prewrap = overflow;

		
		
		int cvgcoeff = 0;
		UINT32 dzenc = 0;
	
		INT32 diff;
		UINT32 nearer, max, infront;

		switch(other_modes.z_mode)
		{
		case ZMODE_OPAQUE: 
			infront = sz < oz;
			diff = (INT32)sz - (INT32)dznew;
			nearer = force_coplanar || (diff <= (INT32)oz);
			max = (oz == 0x3ffff);
			return (max || (overflow ? infront : nearer));
			break;
		case ZMODE_INTERPENETRATING: 
			infront = sz < oz;
			if (!infront || !farther || !overflow)
			{
				diff = (INT32)sz - (INT32)dznew;
				nearer = force_coplanar || (diff <= (INT32)oz);
				max = (oz == 0x3ffff);
				return (max || (overflow ? infront : nearer)); 
			}
			else
			{
				dzenc = dz_compress(dznotshift & 0xffff);
				cvgcoeff = ((oz >> dzenc) - (sz >> dzenc)) & 0xf;
				*curpixel_cvg = ((cvgcoeff * (*curpixel_cvg)) >> 3) & 0xf;
				return 1;
			}
			break;
		case ZMODE_TRANSPARENT: 
			infront = sz < oz;
			max = (oz == 0x3ffff);
			return (infront || max); 
			break;
		case ZMODE_DECAL: 
			diff = (INT32)sz - (INT32)dznew;
			nearer = force_coplanar || (diff <= (INT32)oz);
			max = (oz == 0x3ffff);
			return (farther && nearer && !max); 
			break;
		}
		return 0;
	}
	else
	{
		

		blshifta = CLIP(dzpixenc - 0xf, 0, 4);
		blshiftb = CLIP(0xf - dzpixenc, 0, 4);

		int overflow = (curpixel_memcvg + *curpixel_cvg) & 8;
		*blend_en = other_modes.force_blend || (!overflow && other_modes.antialias_en);
		*prewrap = overflow;

		return 1;
	}
}

STRICTINLINE int finalize_spanalpha(UINT32 blend_en, UINT32 curpixel_cvg, UINT32 curpixel_memcvg)
{
	int finalcvg;

	
	
	switch(other_modes.cvg_dest)
	{
	case CVG_CLAMP: 
		if (!blend_en)
		{
			finalcvg = curpixel_cvg - 1;
			
			
		}
		else
		{
			finalcvg = curpixel_cvg + curpixel_memcvg;
		}
		if (!(finalcvg & 8))
			finalcvg &= 7;
		else
			finalcvg = 7;
		break;
	case CVG_WRAP:
		finalcvg = (curpixel_cvg + curpixel_memcvg) & 7;
		break;
	case CVG_ZAP: 
		finalcvg = 7;
		break;
	case CVG_SAVE: 
		finalcvg = curpixel_memcvg;
		break;
	}

	return finalcvg;
}

STRICTINLINE INT32 normalize_dzpix(INT32 sum)
{
   int count;
	if (sum & 0xc000)
		return 0x8000;
	if (!(sum & 0xffff))
		return 1;
	if (sum == 1)
		return 3;
	for(count = 0x2000; count > 0; count >>= 1)
    {
      if (sum & count)
        return(count << 1);
    }
	fatalerror("normalize_dzpix: invalid codepath taken");
	return 0;
}

STRICTINLINE INT32 CLIP(INT32 value,INT32 min,INT32 max)
{
	if (value < min)
		return min;
	else if (value > max)
		return max;
	else
		return value;
}


STRICTINLINE void video_filter16(int* endr, int* endg, int* endb, UINT32 fboffset, UINT32 num, UINT32 hres, UINT32 centercvg)
{





	UINT32 penumaxr, penumaxg, penumaxb, penuminr, penuming, penuminb;
	UINT16 pix;
	UINT32 numoffull = 1;
	UINT32 hidval;
	UINT32 r, g, b; 
	UINT32 backr[7], backg[7], backb[7];
	UINT32 invr[7], invg[7], invb[7];

	r = *endr;
	g = *endg;
	b = *endb;

	backr[0] = r;
	backg[0] = g;
	backb[0] = b;
	invr[0] = (~r) & 0xff;
	invg[0] = (~g) & 0xff;
	invb[0] = (~b) & 0xff;

	
	

	
	UINT32 idx = (fboffset >> 1) + num;
	UINT32 leftup = idx - hres - 1;
	UINT32 rightup = idx - hres + 1;
	UINT32 toleft = idx - 2;
	UINT32 toright = idx + 2;
	UINT32 leftdown = idx + hres - 1;
	UINT32 rightdown = idx + hres + 1;


#define VI_ANDER(x) {													\
			PAIRREAD16(pix, hidval, x);									\
			if (hidval == 3 && (pix & 1))								\
			{															\
				backr[numoffull] = GET_HI(pix);							\
				backg[numoffull] = GET_MED(pix);						\
				backb[numoffull] = GET_LOW(pix);						\
				invr[numoffull] = (~backr[numoffull]) & 0xff;			\
				invg[numoffull] = (~backg[numoffull]) & 0xff;			\
				invb[numoffull] = (~backb[numoffull]) & 0xff;			\
			}															\
			else														\
			{															\
                backr[numoffull] = invr[numoffull] = 0;	\
				backg[numoffull] = invg[numoffull] = 0;					\
				backb[numoffull] = invb[numoffull] = 0;					\
			}															\
			numoffull++;												\
}
	
	VI_ANDER(leftup);
	VI_ANDER(rightup);
	VI_ANDER(toleft);
	VI_ANDER(toright);
	VI_ANDER(leftdown);
	VI_ANDER(rightdown);

	UINT32 colr, colg, colb;

	video_max_optimized(&backr[0], &penumaxr);
	video_max_optimized(&backg[0], &penumaxg);
	video_max_optimized(&backb[0], &penumaxb);
	video_max_optimized(&invr[0], &penuminr);
	video_max_optimized(&invg[0], &penuming);
	video_max_optimized(&invb[0], &penuminb);

	penuminr = (~penuminr) & 0xff;
	penuming = (~penuming) & 0xff;
	penuminb = (~penuminb) & 0xff;

	
	
	UINT32 coeff = 7 - centercvg;
	colr = penuminr + penumaxr - (r << 1);
	colg = penuming + penumaxg - (g << 1);
	colb = penuminb + penumaxb - (b << 1);

	colr = (((colr * coeff) + 4) >> 3) + r;
	colg = (((colg * coeff) + 4) >> 3) + g;
	colb = (((colb * coeff) + 4) >> 3) + b;

	*endr = colr & 0xff;
	*endg = colg & 0xff;
	*endb = colb & 0xff;

	
	
}

STRICTINLINE void video_filter32(int* endr, int* endg, int* endb, UINT32 fboffset, UINT32 num, UINT32 hres, UINT32 centercvg)
{

	UINT32 penumaxr, penumaxg, penumaxb, penuminr, penuming, penuminb;
	UINT32 numoffull = 1;
	UINT32 pix = 0, pixcvg = 0;
	UINT32 r, g, b; 
	UINT32 backr[7], backg[7], backb[7];
	UINT32 invr[7], invg[7], invb[7];

	r = *endr;
	g = *endg;
	b = *endb;

	backr[0] = r;
	backg[0] = g;
	backb[0] = b;
	invr[0] = (~r) & 0xff;
	invg[0] = (~g) & 0xff;
	invb[0] = (~b) & 0xff;

	UINT32 idx = (fboffset >> 2) + num;
	UINT32 leftup = idx - hres - 1;
	UINT32 rightup = idx - hres + 1;
	UINT32 toleft = idx - 2;
	UINT32 toright = idx + 2;
	UINT32 leftdown = idx + hres - 1;
	UINT32 rightdown = idx + hres + 1;

#define VI_ANDER32(x) {													\
			pix = RREADIDX32(x);										\
			pixcvg = (pix >> 5) & 7;									\
			if (pixcvg == 7)											\
			{															\
				backr[numoffull] = (pix >> 24) & 0xff;					\
				backg[numoffull] = (pix >> 16) & 0xff;					\
				backb[numoffull] = (pix >> 8) & 0xff;					\
				invr[numoffull] = (~backr[numoffull]) & 0xff;			\
				invg[numoffull] = (~backg[numoffull]) & 0xff;			\
				invb[numoffull] = (~backb[numoffull]) & 0xff;			\
			}															\
			else														\
			{															\
                backr[numoffull] = invr[numoffull] = 0;					\
				backg[numoffull] = invg[numoffull] = 0;					\
				backb[numoffull] = invb[numoffull] = 0;					\
			}															\
			numoffull++;												\
}
	
	VI_ANDER32(leftup);
	VI_ANDER32(rightup);
	VI_ANDER32(toleft);
	VI_ANDER32(toright);
	VI_ANDER32(leftdown);
	VI_ANDER32(rightdown);

	UINT32 colr, colg, colb;

	video_max_optimized(&backr[0], &penumaxr);
	video_max_optimized(&backg[0], &penumaxg);
	video_max_optimized(&backb[0], &penumaxb);
	video_max_optimized(&invr[0], &penuminr);
	video_max_optimized(&invg[0], &penuming);
	video_max_optimized(&invb[0], &penuminb);

	penuminr = (~penuminr) & 0xff;
	penuming = (~penuming) & 0xff;
	penuminb = (~penuminb) & 0xff;

	UINT32 coeff = 7 - centercvg;
	colr = penuminr + penumaxr - (r << 1);
	colg = penuming + penumaxg - (g << 1);
	colb = penuminb + penumaxb - (b << 1);

	colr = (((colr * coeff) + 4) >> 3) + r;
	colg = (((colg * coeff) + 4) >> 3) + g;
	colb = (((colb * coeff) + 4) >> 3) + b;

	*endr = colr & 0xff;
	*endg = colg & 0xff;
	*endb = colb & 0xff;
}

STRICTINLINE void divot_filter(CCVG* final, CCVG centercolor, CCVG leftcolor, CCVG rightcolor)
{





	UINT32 leftr, leftg, leftb, rightr, rightg, rightb, centerr, centerg, centerb;

	*final = centercolor;
	
	if ((centercolor.cvg & leftcolor.cvg & rightcolor.cvg) == 7)
	
	
	
	{
		return;
	}

	leftr = leftcolor.r;	
	leftg = leftcolor.g;	
	leftb = leftcolor.b;
	rightr = rightcolor.r;	
	rightg = rightcolor.g;	
	rightb = rightcolor.b;
	centerr = centercolor.r;
	centerg = centercolor.g;
	centerb = centercolor.b;


	if ((leftr >= centerr && rightr >= leftr) || (leftr >= rightr && centerr >= leftr))
		final->r = leftr;
	else if ((rightr >= centerr && leftr >= rightr) || (rightr >= leftr && centerr >= rightr))
		final->r = rightr;

	if ((leftg >= centerg && rightg >= leftg) || (leftg >= rightg && centerg >= leftg))
		final->g = leftg;
	else if ((rightg >= centerg && leftg >= rightg) || (rightg >= leftg && centerg >= rightg))
		final->g = rightg;

	if ((leftb >= centerb && rightb >= leftb) || (leftb >= rightb && centerb >= leftb))
		final->b = leftb;
	else if ((rightb >= centerb && leftb >= rightb) || (rightb >= leftb && centerb >= rightb))
		final->b = rightb;
}

STRICTINLINE void restore_filter16(int* r, int* g, int* b, UINT32 fboffset, UINT32 num, UINT32 hres)
{


	UINT32 idx = (fboffset >> 1) + num;
	UINT32 leftuppix = idx - hres - 1;
	UINT32 leftdownpix = idx + hres - 1;
	UINT32 toleftpix = idx - 1;

	UINT32 rend = *r;
	UINT32 gend = *g;
	UINT32 bend = *b;
	UINT32 rcomp = (rend >> 3) & 0x1f;
	UINT32 gcomp = (gend >> 3) & 0x1f;
	UINT32 bcomp = (bend >> 3) & 0x1f;

	UINT32 tempr, tempg, tempb;
	UINT16 pix;


#define VI_COMPARE(x)												\
{																	\
	pix = RREADIDX16((x));											\
	tempr = (pix >> 6) & 0x3e0;										\
	tempg = (pix >> 1) & 0x3e0;										\
	tempb = (pix << 4) & 0x3e0;										\
	rend += vi_restore_table[tempr | rcomp];						\
	gend += vi_restore_table[tempg | gcomp];						\
	bend += vi_restore_table[tempb | bcomp];						\
}

	VI_COMPARE(leftuppix);
	VI_COMPARE(leftuppix + 1);
	VI_COMPARE(leftuppix + 2);
	VI_COMPARE(leftdownpix);
	VI_COMPARE(leftdownpix + 1);
	VI_COMPARE(leftdownpix + 2);
	VI_COMPARE(toleftpix);
	VI_COMPARE(toleftpix + 2);

	
	*r = rend;
	*g = gend;
	*b = bend;
}

STRICTINLINE void restore_filter32(int* r, int* g, int* b, UINT32 fboffset, UINT32 num, UINT32 hres)
{
	UINT32 idx = (fboffset >> 2) + num;
	UINT32 leftuppix = idx - hres - 1;
	UINT32 leftdownpix = idx + hres - 1;
	UINT32 toleftpix = idx - 1;

	UINT32 rend = *r;
	UINT32 gend = *g;
	UINT32 bend = *b;
	UINT32 rcomp = (rend >> 3) & 0x1f;
	UINT32 gcomp = (gend >> 3) & 0x1f;
	UINT32 bcomp = (bend >> 3) & 0x1f;

	UINT32 tempr, tempg, tempb;
	UINT32 pix;

#define VI_COMPARE32(x)													\
{																		\
	pix = RREADIDX32(x);												\
	tempr = (pix >> 19) & 0x3e0;										\
	tempg = (pix >> 11) & 0x3e0;										\
	tempb = (pix >> 3) & 0x3e0;											\
	rend += vi_restore_table[tempr | rcomp];							\
	gend += vi_restore_table[tempg | gcomp];							\
	bend += vi_restore_table[tempb | bcomp];							\
}

	VI_COMPARE32(leftuppix);
	VI_COMPARE32(leftuppix + 1);
	VI_COMPARE32(leftuppix + 2);
	VI_COMPARE32(leftdownpix);
	VI_COMPARE32(leftdownpix + 1);
	VI_COMPARE32(leftdownpix + 2);
	VI_COMPARE32(toleftpix);
	VI_COMPARE32(toleftpix + 2);

	*r = rend;
	*g = gend;
	*b = bend;
}

STRICTINLINE void gamma_filters(int* r, int* g, int* b, int gamma_and_dither)
{
	int cdith, dith;
	
	

	switch(gamma_and_dither)
	{
	case 0:
		return;
		break;
	case 1:
		cdith = irand();
		dith = cdith & 1;
		if (*r < 255)
			*r += dith;
		dith = (cdith >> 1) & 1;
		if (*g < 255)
			*g += dith;
		dith = (cdith >> 2) & 1;
		if (*b < 255)
			*b += dith;
		break;
	case 2:
		*r = gamma_table[*r];
		*g = gamma_table[*g];
		*b = gamma_table[*b];
		break;
	case 3:
		cdith = irand();
		dith = cdith & 0x3f;
		*r = gamma_dither_table[((*r) << 6)|dith];
		dith = (cdith >> 6) & 0x3f;
		*g = gamma_dither_table[((*g) << 6)|dith];
		dith = ((cdith >> 9) & 0x38) | (cdith & 7);
		*b = gamma_dither_table[((*b) << 6)|dith];
		break;
	}
}

STRICTINLINE void adjust_brightness(int* r, int* g, int* b, int brightcoeff)
{
	brightcoeff &= 7;
	switch(brightcoeff)
	{
	case 0:	
		break;
	case 1: 
	case 2:
	case 3:
		*r += (*r >> (4 - brightcoeff));
		*g += (*g >> (4 - brightcoeff));
		*b += (*b >> (4 - brightcoeff));
		if (*r > 0xff)
			*r = 0xff;
		if (*g > 0xff)
			*g = 0xff;
		if (*b > 0xff)
			*b = 0xff;
		break;
	case 4:
	case 5:
	case 6:
	case 7:
		*r = (*r + 1) << (brightcoeff - 3);
		*g = (*g + 1) << (brightcoeff - 3);
		*b = (*b + 1) << (brightcoeff - 3);
		if (*r > 0xff)
			*r = 0xff;
		if (*g > 0xff)
			*g = 0xff;
		if (*b > 0xff)
			*b = 0xff;
		break;
	}
}


STRICTINLINE void video_max_optimized(UINT32* Pixels, UINT32* pen)
{
	int i;
	int pos = 0;
	UINT32 curpen = Pixels[0];
	UINT32 max;
	for (i = 1; i < 7; i++)
	{
	    if (Pixels[i] > Pixels[pos])
		{
			curpen = Pixels[pos];
			pos = i;			
		}
	}
	max = Pixels[pos];
	if (curpen != max)
	{
		for (i = pos + 1; i < 7; i++)
		{
			if (Pixels[i] > curpen)
			{
				curpen = Pixels[i];
			}
		}
	}
	*pen = curpen;

	
}


STRICTINLINE void calculate_clamp_diffs(UINT32 i)
{
	tile[i].f.clampdiffs = ((tile[i].sh >> 2) - (tile[i].sl >> 2)) & 0x3ff;
	tile[i].f.clampdifft = ((tile[i].th >> 2) - (tile[i].tl >> 2)) & 0x3ff;
}


STRICTINLINE void calculate_tile_derivs(UINT32 i)
{
	tile[i].f.clampens = tile[i].cs || !tile[i].mask_s;
	tile[i].f.clampent = tile[i].ct || !tile[i].mask_t;
	tile[i].f.masksclamped = tile[i].mask_s <= 10 ? tile[i].mask_s : 10;
	tile[i].f.masktclamped = tile[i].mask_t <= 10 ? tile[i].mask_t : 10;
	tile[i].f.notlutswitch = (tile[i].format << 2) | tile[i].size;
	tile[i].f.tlutswitch = (tile[i].size << 2) | ((tile[i].format + 2) & 3);
}

STRICTINLINE void rgb_dither_complete(int* r, int* g, int* b, int dith)
{
	if ((*r & 7) > dith)
	{
		if (*r > 247)
			*r = 255;
		else
			*r = (*r & 0xf8) + 8;
	}
	if (other_modes.rgb_dither_sel != 2)
	{
		if ((*g & 7) > dith)
		{
			if (*g > 247)
				*g = 255;
			else
				*g = (*g & 0xf8) + 8;
		}
		if ((*b & 7) > dith)
		{
			if (*b > 247)
				*b = 255;
			else
				*b = (*b & 0xf8) + 8;
		}
	}
	else
	{
		if ((*g & 7) > ((dith + 3) & 7))
		{
			if (*g > 247)
				*g = 255;
			else
				*g = (*g & 0xf8) + 8;
		}
		if ((*b & 7) > ((dith + 5) & 7))
		{
			if (*b > 247)
				*b = 255;
			else
				*b = (*b & 0xf8) + 8;
		}
	}
}

STRICTINLINE void rgb_dither_nothing(int* r, int* g, int* b, int dith)
{
}


STRICTINLINE void get_dither_noise_complete(int x, int y, int* cdith, int* adith)
{

	
	noise = ((irand() & 7) << 6) | 0x20;
	
	
	int dithindex; 
	switch(other_modes.f.rgb_alpha_dither)
	{
	case 0:
		dithindex = ((y & 3) << 2) | (x & 3);
		*adith = *cdith = magic_matrix[dithindex];
		break;
	case 1:
		dithindex = ((y & 3) << 2) | (x & 3);
		*cdith = magic_matrix[dithindex];
		*adith = (~(*cdith)) & 7;
		break;
	case 2:
		dithindex = ((y & 3) << 2) | (x & 3);
		*cdith = magic_matrix[dithindex];
		*adith = (noise >> 6) & 7;
		break;
	case 3:
		dithindex = ((y & 3) << 2) | (x & 3);
		*cdith = magic_matrix[dithindex];
		*adith = 0;
		break;
	case 4:
		dithindex = ((y & 3) << 2) | (x & 3);
		*adith = *cdith = bayer_matrix[dithindex];
		break;
	case 5:
		dithindex = ((y & 3) << 2) | (x & 3);
		*cdith = bayer_matrix[dithindex];
		*adith = (~(*cdith)) & 7;
		break;
	case 6:
		dithindex = ((y & 3) << 2) | (x & 3);
		*cdith = bayer_matrix[dithindex];
		*adith = (noise >> 6) & 7;
		break;
	case 7:
		dithindex = ((y & 3) << 2) | (x & 3);
		*cdith = bayer_matrix[dithindex];
		*adith = 0;
		break;
	case 8:
		dithindex = ((y & 3) << 2) | (x & 3);
		*cdith = irand() & 7;
		*adith = magic_matrix[dithindex];
		break;
	case 9:
		dithindex = ((y & 3) << 2) | (x & 3);
		*cdith = irand() & 7;
		*adith = (~magic_matrix[dithindex]) & 7;
		break;
	case 10:
		*cdith = irand() & 7;
		*adith = (noise >> 6) & 7;
		break;
	case 11:
		*cdith = irand() & 7;
		*adith = 0;
		break;
	case 12:
		dithindex = ((y & 3) << 2) | (x & 3);
		*cdith = 7;
		*adith = bayer_matrix[dithindex];
		break;
	case 13:
		dithindex = ((y & 3) << 2) | (x & 3);
		*cdith = 7;
		*adith = (~bayer_matrix[dithindex]) & 7;
		break;
	case 14:
		*cdith = 7;
		*adith = (noise >> 6) & 7;
		break;
	case 15:
		*cdith = 7;
		*adith = 0;
		break;
	}
}


STRICTINLINE void get_dither_only(int x, int y, int* cdith, int* adith)
{
	int dithindex; 
	switch(other_modes.f.rgb_alpha_dither)
	{
	case 0:
		dithindex = ((y & 3) << 2) | (x & 3);
		*adith = *cdith = magic_matrix[dithindex];
		break;
	case 1:
		dithindex = ((y & 3) << 2) | (x & 3);
		*cdith = magic_matrix[dithindex];
		*adith = (~(*cdith)) & 7;
		break;
	case 2:
		dithindex = ((y & 3) << 2) | (x & 3);
		*cdith = magic_matrix[dithindex];
		*adith = (noise >> 6) & 7;
		break;
	case 3:
		dithindex = ((y & 3) << 2) | (x & 3);
		*cdith = magic_matrix[dithindex];
		*adith = 0;
		break;
	case 4:
		dithindex = ((y & 3) << 2) | (x & 3);
		*adith = *cdith = bayer_matrix[dithindex];
		break;
	case 5:
		dithindex = ((y & 3) << 2) | (x & 3);
		*cdith = bayer_matrix[dithindex];
		*adith = (~(*cdith)) & 7;
		break;
	case 6:
		dithindex = ((y & 3) << 2) | (x & 3);
		*cdith = bayer_matrix[dithindex];
		*adith = (noise >> 6) & 7;
		break;
	case 7:
		dithindex = ((y & 3) << 2) | (x & 3);
		*cdith = bayer_matrix[dithindex];
		*adith = 0;
		break;
	case 8:
		dithindex = ((y & 3) << 2) | (x & 3);
		*cdith = irand() & 7;
		*adith = magic_matrix[dithindex];
		break;
	case 9:
		dithindex = ((y & 3) << 2) | (x & 3);
		*cdith = irand() & 7;
		*adith = (~magic_matrix[dithindex]) & 7;
		break;
	case 10:
		*cdith = irand() & 7;
		*adith = (noise >> 6) & 7;
		break;
	case 11:
		*cdith = irand() & 7;
		*adith = 0;
		break;
	case 12:
		dithindex = ((y & 3) << 2) | (x & 3);
		*cdith = 7;
		*adith = bayer_matrix[dithindex];
		break;
	case 13:
		dithindex = ((y & 3) << 2) | (x & 3);
		*cdith = 7;
		*adith = (~bayer_matrix[dithindex]) & 7;
		break;
	case 14:
		*cdith = 7;
		*adith = (noise >> 6) & 7;
		break;
	case 15:
		*cdith = 7;
		*adith = 0;
		break;
	}
}

STRICTINLINE void get_dither_nothing(int x, int y, int* cdith, int* adith)
{
}

STRICTINLINE void vi_vl_lerp(CCVG* up, CCVG down, UINT32 frac)
{
	UINT32 r0, g0, b0;
	if (!frac)
		return;

	r0 = up->r;
	g0 = up->g;
	b0 = up->b;
	
	up->r = ((((down.r - r0) * frac + 16) >> 5) + r0) & 0xff;
	up->g = ((((down.g - g0) * frac + 16) >> 5) + g0) & 0xff;
	up->b = ((((down.b - b0) * frac + 16) >> 5) + b0) & 0xff;

}

STRICTINLINE void rgbaz_correct_clip(int offx, int offy, int r, int g, int b, int a, int* z, UINT32 curpixel_cvg)
{
	int summand_r, summand_b, summand_g, summand_a;
	int summand_z;
	int sz = *z;
	int zanded;




	if (curpixel_cvg == 8)
	{
		r >>= 2;
		g >>= 2;
		b >>= 2;
		a >>= 2;
		sz = sz >> 3;
	}
	else
	{
		summand_r = offx * spans_cdr + offy * spans_drdy;
		summand_g = offx * spans_cdg + offy * spans_dgdy;
		summand_b = offx * spans_cdb + offy * spans_dbdy;
		summand_a = offx * spans_cda + offy * spans_dady;
		summand_z = offx * spans_cdz + offy * spans_dzdy;

		r = ((r << 2) + summand_r) >> 4;
		g = ((g << 2) + summand_g) >> 4;
		b = ((b << 2) + summand_b) >> 4;
		a = ((a << 2) + summand_a) >> 4;
		sz = ((sz << 2) + summand_z) >> 5;
	}

	
	shade_color.r = special_9bit_clamptable[r & 0x1ff];
	shade_color.g = special_9bit_clamptable[g & 0x1ff];
	shade_color.b = special_9bit_clamptable[b & 0x1ff];
	shade_color.a = special_9bit_clamptable[a & 0x1ff];
	
	
	
	zanded = (sz & 0x60000) >> 17;

	
	switch(zanded)
	{
		case 0: *z = sz & 0x3ffff;						break;
		case 1:	*z = sz & 0x3ffff;						break;
		case 2: *z = 0x3ffff;							break;
		case 3: *z = 0;									break;
	}
}



int IsBadPtrW32(void *ptr, UINT32 bytes)
{
#ifdef WIN32
	SIZE_T dwSize;
    MEMORY_BASIC_INFORMATION meminfo;
    if (!ptr)
        return 1;
    memset(&meminfo, 0x00, sizeof(meminfo));
    dwSize = VirtualQuery(ptr, &meminfo, sizeof(meminfo));
    if (!dwSize)
        return 1;
    if (MEM_COMMIT != meminfo.State)
        return 1;
    if (!(meminfo.Protect & (PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)))
        return 1;
    if (bytes > meminfo.RegionSize)
        return 1;
    if ((UINT64)((char*)ptr - (char*)meminfo.BaseAddress) > (UINT64)(meminfo.RegionSize - bytes))
        return 1;
#endif
    return 0;
}

UINT32 vi_integer_sqrt(UINT32 a)
{
	unsigned long op = a, res = 0, one = 1 << 30;

    while (one > op) 
		one >>= 2;

    while (one != 0) 
	{
        if (op >= res + one) 
		{
            op -= res + one;
            res += one << 1;
        }
        res >>= 1;
        one >>= 2;
    }
    return res;
}

STRICTINLINE void clearscreen(UINT32 x0, UINT32 y0, UINT32 x1, UINT32 y1, UINT32 white)
{

}

STRICTINLINE void clearfb16(UINT16* fb, UINT32 width,UINT32 height)
{
	UINT16* d;
	UINT32 j;
	int i = width << 1;
	for (j = 0; j < height; j++)
	{
		d = &fb[j*width];
		memset(d,0,i);
	}
}


STRICTINLINE void tcdiv_nopersp(INT32 ss, INT32 st, INT32 sw, INT32* sss, INT32* sst)
{



	*sss = (SIGN16(ss)) & 0x1ffff;
	*sst = (SIGN16(st)) & 0x1ffff;
}

STRICTINLINE void tcdiv_persp(INT32 ss, INT32 st, INT32 sw, INT32* sss, INT32* sst)
{


	int w_carry = 0;
	int shift; 
	int tlu_rcp;
    int sprod, tprod;
	int outofbounds_s, outofbounds_t;
	int tempmask;
	int shift_value;
	INT32 temps, tempt;

	
	
	int overunder_s = 0, overunder_t = 0;
	
	
	if (SIGN16(sw) <= 0)
		w_carry = 1;

	sw &= 0x7fff;

	
	
	shift = tcdiv_table[sw];
	tlu_rcp = shift >> 4;
	shift &= 0xf;

	sprod = SIGN16(ss) * tlu_rcp;
	tprod = SIGN16(st) * tlu_rcp;

	
	
	
	tempmask = ((1 << 30) - 1) & -((1 << 29) >> shift);
	
	outofbounds_s = sprod & tempmask;
	outofbounds_t = tprod & tempmask;
	
	if (shift != 0xe)
	{
		shift_value = 13 - shift;
		temps = sprod = (sprod >> shift_value);
		tempt = tprod = (tprod >> shift_value);
	}
	else
	{
		temps = sprod << 1;
		tempt = tprod << 1;
	}
	
	if (outofbounds_s != tempmask && outofbounds_s != 0)
	{
		if (!(sprod & (1 << 29)))
			overunder_s = 2 << 17;
		else
			overunder_s = 1 << 17;
	}

	if (outofbounds_t != tempmask && outofbounds_t != 0)
	{
		if (!(tprod & (1 << 29)))
			overunder_t = 2 << 17;
		else
			overunder_t = 1 << 17;
	}

	if (w_carry)
	{
		overunder_s |= (2 << 17);
		overunder_t |= (2 << 17);
	}

	*sss = (temps & 0x1ffff) | overunder_s;
	*sst = (tempt & 0x1ffff) | overunder_t;
}

STRICTINLINE void tclod_2cycle_current(INT32* sss, INT32* sst, INT32 nexts, INT32 nextt, INT32 s, INT32 t, INT32 w, INT32 dsinc, INT32 dtinc, INT32 dwinc, INT32 prim_tile, INT32* t1, INT32* t2)
{








	int nextys, nextyt, nextysw;
	int lodclamp = 0;
	INT32 lod = 0;
	UINT32 l_tile;
	UINT32 magnify = 0;
	UINT32 distant = 0;
	int inits = *sss, initt = *sst;

	tclod_tcclamp(sss, sst);

	if (other_modes.f.dolod)
	{
		
		
		
		
		
		
		nextys = (s + spans_dsdy) >> 16;
		nextyt = (t + spans_dtdy) >> 16;
		nextysw = (w + spans_dwdy) >> 16;

		tcdiv_ptr(nextys, nextyt, nextysw, &nextys, &nextyt);

		lodclamp = (initt & 0x60000) || (nextt & 0x60000) || (inits & 0x60000) || (nexts & 0x60000) || (nextys & 0x60000) || (nextyt & 0x60000);
		
		

		
		tclod_4x17_to_15(inits, nexts, initt, nextt, 0, &lod);
		tclod_4x17_to_15(inits, nextys, initt, nextyt, lod, &lod);

		lodfrac_lodtile_signals(lodclamp, lod, &l_tile, &magnify, &distant);

		
		if (other_modes.tex_lod_en)
		{
			if (distant)
				l_tile = max_level;
			if (!other_modes.detail_tex_en)
			{
				*t1 = (prim_tile + l_tile) & 7;
				if (!(distant || (!other_modes.sharpen_tex_en && magnify)))
					*t2 = (*t1 + 1) & 7;
				else
					*t2 = *t1;
			}
			else 
			{
				if (!magnify)
					*t1 = (prim_tile + l_tile + 1);
				else
					*t1 = (prim_tile + l_tile);
				*t1 &= 7;
				if (!distant && !magnify)
					*t2 = (prim_tile + l_tile + 2) & 7;
				else
					*t2 = (prim_tile + l_tile + 1) & 7;
			}
		}
	}
}


STRICTINLINE void tclod_2cycle_current_simple(INT32* sss, INT32* sst, INT32 s, INT32 t, INT32 w, INT32 dsinc, INT32 dtinc, INT32 dwinc, INT32 prim_tile, INT32* t1, INT32* t2)
{
	int nextys, nextyt, nextysw, nexts, nextt, nextsw;
	int lodclamp = 0;
	INT32 lod = 0;
	UINT32 l_tile;
	UINT32 magnify = 0;
	UINT32 distant = 0;
	int inits = *sss, initt = *sst;

	tclod_tcclamp(sss, sst);

	if (other_modes.f.dolod)
	{
		nextsw = (w + dwinc) >> 16;
		nexts = (s + dsinc) >> 16;
		nextt = (t + dtinc) >> 16;
		nextys = (s + spans_dsdy) >> 16;
		nextyt = (t + spans_dtdy) >> 16;
		nextysw = (w + spans_dwdy) >> 16;

		tcdiv_ptr(nexts, nextt, nextsw, &nexts, &nextt);
		tcdiv_ptr(nextys, nextyt, nextysw, &nextys, &nextyt);

		lodclamp = (initt & 0x60000) || (nextt & 0x60000) || (inits & 0x60000) || (nexts & 0x60000) || (nextys & 0x60000) || (nextyt & 0x60000);

		tclod_4x17_to_15(inits, nexts, initt, nextt, 0, &lod);
		tclod_4x17_to_15(inits, nextys, initt, nextyt, lod, &lod);

		lodfrac_lodtile_signals(lodclamp, lod, &l_tile, &magnify, &distant);
	
		if (other_modes.tex_lod_en)
		{
			if (distant)
				l_tile = max_level;
			if (!other_modes.detail_tex_en)
			{
				*t1 = (prim_tile + l_tile) & 7;
				if (!(distant || (!other_modes.sharpen_tex_en && magnify)))
					*t2 = (*t1 + 1) & 7;
				else
					*t2 = *t1;
			}
			else 
			{
				if (!magnify)
					*t1 = (prim_tile + l_tile + 1);
				else
					*t1 = (prim_tile + l_tile);
				*t1 &= 7;
				if (!distant && !magnify)
					*t2 = (prim_tile + l_tile + 2) & 7;
				else
					*t2 = (prim_tile + l_tile + 1) & 7;
			}
		}
	}
}


STRICTINLINE void tclod_2cycle_current_notexel1(INT32* sss, INT32* sst, INT32 s, INT32 t, INT32 w, INT32 dsinc, INT32 dtinc, INT32 dwinc, INT32 prim_tile, INT32* t1)
{
	int nextys, nextyt, nextysw, nexts, nextt, nextsw;
	int lodclamp = 0;
	INT32 lod = 0;
	UINT32 l_tile;
	UINT32 magnify = 0;
	UINT32 distant = 0;
	int inits = *sss, initt = *sst;

	tclod_tcclamp(sss, sst);

	if (other_modes.f.dolod)
	{
		nextsw = (w + dwinc) >> 16;
		nexts = (s + dsinc) >> 16;
		nextt = (t + dtinc) >> 16;
		nextys = (s + spans_dsdy) >> 16;
		nextyt = (t + spans_dtdy) >> 16;
		nextysw = (w + spans_dwdy) >> 16;

		tcdiv_ptr(nexts, nextt, nextsw, &nexts, &nextt);
		tcdiv_ptr(nextys, nextyt, nextysw, &nextys, &nextyt);

		lodclamp = (initt & 0x60000) || (nextt & 0x60000) || (inits & 0x60000) || (nexts & 0x60000) || (nextys & 0x60000) || (nextyt & 0x60000);

		tclod_4x17_to_15(inits, nexts, initt, nextt, 0, &lod);
		tclod_4x17_to_15(inits, nextys, initt, nextyt, lod, &lod);

		lodfrac_lodtile_signals(lodclamp, lod, &l_tile, &magnify, &distant);
	
		if (other_modes.tex_lod_en)
		{
			if (distant)
				l_tile = max_level;
			if (!other_modes.detail_tex_en || magnify)
				*t1 = (prim_tile + l_tile) & 7;
			else
				*t1 = (prim_tile + l_tile + 1) & 7;
		}
		
	}
}

STRICTINLINE void tclod_2cycle_next(INT32* sss, INT32* sst, INT32 s, INT32 t, INT32 w, INT32 dsinc, INT32 dtinc, INT32 dwinc, INT32 prim_tile, INT32* t1, INT32* t2, INT32* prelodfrac)
{
	int nexts, nextt, nextsw, nextys, nextyt, nextysw;
	int lodclamp = 0;
	INT32 lod = 0;
	UINT32 l_tile;
	UINT32 magnify = 0;
	UINT32 distant = 0;
	int inits = *sss, initt = *sst;

	tclod_tcclamp(sss, sst);

	if (other_modes.f.dolod)
	{
		nextsw = (w + dwinc) >> 16;
		nexts = (s + dsinc) >> 16;
		nextt = (t + dtinc) >> 16;
		nextys = (s + spans_dsdy) >> 16;
		nextyt = (t + spans_dtdy) >> 16;
		nextysw = (w + spans_dwdy) >> 16;

		tcdiv_ptr(nexts, nextt, nextsw, &nexts, &nextt);
		tcdiv_ptr(nextys, nextyt, nextysw, &nextys, &nextyt);
	
		lodclamp = (initt & 0x60000) || (nextt & 0x60000) || (inits & 0x60000) || (nexts & 0x60000) || (nextys & 0x60000) || (nextyt & 0x60000);

		tclod_4x17_to_15(inits, nexts, initt, nextt, 0, &lod);
		tclod_4x17_to_15(inits, nextys, initt, nextyt, lod, &lod);

		
		if ((lod & 0x4000) || lodclamp)
			lod = 0x7fff;
		else if (lod < min_level)
			lod = min_level;
						
		magnify = (lod < 32) ? 1: 0;
		l_tile =  log2table[(lod >> 5) & 0xff];
		distant = ((lod & 0x6000) || (l_tile >= max_level)) ? 1 : 0;

		*prelodfrac = ((lod << 3) >> l_tile) & 0xff;

		
		if(!other_modes.sharpen_tex_en && !other_modes.detail_tex_en)
		{
			if (distant)
				*prelodfrac = 0xff;
			else if (magnify)
				*prelodfrac = 0;
		}

		
		

		if(other_modes.sharpen_tex_en && magnify)
			*prelodfrac |= 0x100;

		if (other_modes.tex_lod_en)
		{
			if (distant)
				l_tile = max_level;
			if (!other_modes.detail_tex_en)
			{
				*t1 = (prim_tile + l_tile) & 7;
				if (!(distant || (!other_modes.sharpen_tex_en && magnify)))
					*t2 = (*t1 + 1) & 7;
				else
					*t2 = *t1;
			}
			else 
			{
				if (!magnify)
					*t1 = (prim_tile + l_tile + 1);
				else
					*t1 = (prim_tile + l_tile);
				*t1 &= 7;
				if (!distant && !magnify)
					*t2 = (prim_tile + l_tile + 2) & 7;
				else
					*t2 = (prim_tile + l_tile + 1) & 7;
			}
		}
	}
}

STRICTINLINE void tclod_1cycle_current(INT32* sss, INT32* sst, INT32 nexts, INT32 nextt, INT32 s, INT32 t, INT32 w, INT32 dsinc, INT32 dtinc, INT32 dwinc, INT32 scanline, INT32 prim_tile, INT32* t1, SPANSIGS* sigs)
{









	int fars, fart, farsw;
	int lodclamp = 0;
	INT32 lod = 0;
	UINT32 l_tile = 0, magnify = 0, distant = 0;
	
	tclod_tcclamp(sss, sst);

	if (other_modes.f.dolod)
	{
		int nextscan = scanline + 1;

		
		if (span[nextscan].validline)
		{
			if (!sigs->endspan || !sigs->longspan)
			{
				if (!(sigs->preendspan && sigs->longspan) && !(sigs->endspan && sigs->midspan))
				{
					farsw = (w + (dwinc << 1)) >> 16;
					fars = (s + (dsinc << 1)) >> 16;
					fart = (t + (dtinc << 1)) >> 16;
				}
				else
				{
					farsw = (w - dwinc) >> 16;
					fars = (s - dsinc) >> 16;
					fart = (t - dtinc) >> 16;
				}
			}
			else
			{
				fart = (span[nextscan].t + dtinc) >> 16; 
				fars = (span[nextscan].s + dsinc) >> 16; 
				farsw = (span[nextscan].w + dwinc) >> 16;
			}
		}
		else
		{
			farsw = (w + (dwinc << 1)) >> 16;
			fars = (s + (dsinc << 1)) >> 16;
			fart = (t + (dtinc << 1)) >> 16;
		}

		tcdiv_ptr(fars, fart, farsw, &fars, &fart);

		lodclamp = (fart & 0x60000) || (nextt & 0x60000) || (fars & 0x60000) || (nexts & 0x60000);
		
		

		
		tclod_4x17_to_15(nexts, fars, nextt, fart, 0, &lod);

		lodfrac_lodtile_signals(lodclamp, lod, &l_tile, &magnify, &distant);
	
		if (other_modes.tex_lod_en)
		{
			if (distant)
				l_tile = max_level;

			
			
			if (!other_modes.detail_tex_en || magnify)
				*t1 = (prim_tile + l_tile) & 7;
			else
				*t1 = (prim_tile + l_tile + 1) & 7;
		}
	}
}



STRICTINLINE void tclod_1cycle_current_simple(INT32* sss, INT32* sst, INT32 s, INT32 t, INT32 w, INT32 dsinc, INT32 dtinc, INT32 dwinc, INT32 scanline, INT32 prim_tile, INT32* t1, SPANSIGS* sigs)
{
	int fars, fart, farsw, nexts, nextt, nextsw;
	int lodclamp = 0;
	INT32 lod = 0;
	UINT32 l_tile = 0, magnify = 0, distant = 0;
	
	tclod_tcclamp(sss, sst);

	if (other_modes.f.dolod)
	{

		int nextscan = scanline + 1;
		if (span[nextscan].validline)
		{
			if (!sigs->endspan || !sigs->longspan)
			{
				nextsw = (w + dwinc) >> 16;
				nexts = (s + dsinc) >> 16;
				nextt = (t + dtinc) >> 16;
				
				if (!(sigs->preendspan && sigs->longspan) && !(sigs->endspan && sigs->midspan))
				{
					farsw = (w + (dwinc << 1)) >> 16;
					fars = (s + (dsinc << 1)) >> 16;
					fart = (t + (dtinc << 1)) >> 16;
				}
				else
				{
					farsw = (w - dwinc) >> 16;
					fars = (s - dsinc) >> 16;
					fart = (t - dtinc) >> 16;
				}
			}
			else
			{
				nextt = span[nextscan].t >> 16;
				nexts = span[nextscan].s >> 16;
				nextsw = span[nextscan].w >> 16;
				fart = (span[nextscan].t + dtinc) >> 16; 
				fars = (span[nextscan].s + dsinc) >> 16; 
				farsw = (span[nextscan].w + dwinc) >> 16;
			}
		}
		else
		{
			nextsw = (w + dwinc) >> 16;
			nexts = (s + dsinc) >> 16;
			nextt = (t + dtinc) >> 16;
			farsw = (w + (dwinc << 1)) >> 16;
			fars = (s + (dsinc << 1)) >> 16;
			fart = (t + (dtinc << 1)) >> 16;
		}

		tcdiv_ptr(nexts, nextt, nextsw, &nexts, &nextt);
		tcdiv_ptr(fars, fart, farsw, &fars, &fart);

		lodclamp = (fart & 0x60000) || (nextt & 0x60000) || (fars & 0x60000) || (nexts & 0x60000);

		tclod_4x17_to_15(nexts, fars, nextt, fart, 0, &lod);

		lodfrac_lodtile_signals(lodclamp, lod, &l_tile, &magnify, &distant);
	
		if (other_modes.tex_lod_en)
		{
			if (distant)
				l_tile = max_level;
			if (!other_modes.detail_tex_en || magnify)
				*t1 = (prim_tile + l_tile) & 7;
			else
				*t1 = (prim_tile + l_tile + 1) & 7;
		}
	}
}

STRICTINLINE void tclod_1cycle_next(INT32* sss, INT32* sst, INT32 s, INT32 t, INT32 w, INT32 dsinc, INT32 dtinc, INT32 dwinc, INT32 scanline, INT32 prim_tile, INT32* t1, SPANSIGS* sigs, INT32* prelodfrac)
{
	int nexts, nextt, nextsw, fars, fart, farsw;
	int lodclamp = 0;
	INT32 lod = 0;
	UINT32 l_tile = 0, magnify = 0, distant = 0;
	
	tclod_tcclamp(sss, sst);

	if (other_modes.f.dolod)
	{
		
		int nextscan = scanline + 1;
		
		if (span[nextscan].validline)
		{
			if (!sigs->nextspan)
			{
				if (!sigs->endspan || !sigs->longspan)
				{
					nextsw = (w + dwinc) >> 16;
					nexts = (s + dsinc) >> 16;
					nextt = (t + dtinc) >> 16;
					
					if (!(sigs->preendspan && sigs->longspan) && !(sigs->endspan && sigs->midspan))
					{
						farsw = (w + (dwinc << 1)) >> 16;
						fars = (s + (dsinc << 1)) >> 16;
						fart = (t + (dtinc << 1)) >> 16;
					}
					else
					{
						farsw = (w - dwinc) >> 16;
						fars = (s - dsinc) >> 16;
						fart = (t - dtinc) >> 16;
					}
				}
				else
				{
					nextt = span[nextscan].t;
					nexts = span[nextscan].s;
					nextsw = span[nextscan].w;
					fart = (nextt + dtinc) >> 16; 
					fars = (nexts + dsinc) >> 16; 
					farsw = (nextsw + dwinc) >> 16;
					nextt >>= 16;
					nexts >>= 16;
					nextsw >>= 16;
				}
			}
			else
			{
				if (sigs->longspan || sigs->midspan)
				{
					nextt = span[nextscan].t + dtinc;
					nexts = span[nextscan].s + dsinc;
					nextsw = span[nextscan].w + dwinc;
					fart = (nextt + dtinc) >> 16; 
					fars = (nexts + dsinc) >> 16; 
					farsw = (nextsw + dwinc) >> 16;
					nextt >>= 16;
					nexts >>= 16;
					nextsw >>= 16;
				}
				else
				{
					nextsw = (w + dwinc) >> 16;
					nexts = (s + dsinc) >> 16;
					nextt = (t + dtinc) >> 16;
					farsw = (w - dwinc) >> 16;
					fars = (s - dsinc) >> 16;
					fart = (t - dtinc) >> 16;
				}
			}
		}
		else
		{
			nextsw = (w + dwinc) >> 16;
			nexts = (s + dsinc) >> 16;
			nextt = (t + dtinc) >> 16;
			farsw = (w + (dwinc << 1)) >> 16;
			fars = (s + (dsinc << 1)) >> 16;
			fart = (t + (dtinc << 1)) >> 16;
		}

		tcdiv_ptr(nexts, nextt, nextsw, &nexts, &nextt);
		tcdiv_ptr(fars, fart, farsw, &fars, &fart);

		lodclamp = (fart & 0x60000) || (nextt & 0x60000) || (fars & 0x60000) || (nexts & 0x60000);
		
		
		tclod_4x17_to_15(nexts, fars, nextt, fart, 0, &lod);

		
		if ((lod & 0x4000) || lodclamp)
			lod = 0x7fff;
		else if (lod < min_level)
			lod = min_level;
					
		magnify = (lod < 32) ? 1: 0;
		l_tile =  log2table[(lod >> 5) & 0xff];
		distant = ((lod & 0x6000) || (l_tile >= max_level)) ? 1 : 0;

		*prelodfrac = ((lod << 3) >> l_tile) & 0xff;

		
		if(!other_modes.sharpen_tex_en && !other_modes.detail_tex_en)
		{
			if (distant)
				*prelodfrac = 0xff;
			else if (magnify)
				*prelodfrac = 0;
		}

		if(other_modes.sharpen_tex_en && magnify)
			*prelodfrac |= 0x100;

		if (other_modes.tex_lod_en)
		{
			if (distant)
				l_tile = max_level;
			if (!other_modes.detail_tex_en || magnify)
				*t1 = (prim_tile + l_tile) & 7;
			else
				*t1 = (prim_tile + l_tile + 1) & 7;
		}
	}
}

STRICTINLINE void tclod_copy(INT32* sss, INT32* sst, INT32 s, INT32 t, INT32 w, INT32 dsinc, INT32 dtinc, INT32 dwinc, INT32 prim_tile, INT32* t1)
{




	int nexts, nextt, nextsw, fars, fart, farsw;
	int lodclamp = 0;
	INT32 lod = 0;
	UINT32 l_tile = 0, magnify = 0, distant = 0;

	tclod_tcclamp(sss, sst);

	if (other_modes.tex_lod_en)
	{
		
		
		
		nextsw = (w + dwinc) >> 16;
		nexts = (s + dsinc) >> 16;
		nextt = (t + dtinc) >> 16;
		farsw = (w + (dwinc << 1)) >> 16;
		fars = (s + (dsinc << 1)) >> 16;
		fart = (t + (dtinc << 1)) >> 16;
	
		tcdiv_ptr(nexts, nextt, nextsw, &nexts, &nextt);
		tcdiv_ptr(fars, fart, farsw, &fars, &fart);

		lodclamp = (fart & 0x60000) || (nextt & 0x60000) || (fars & 0x60000) || (nexts & 0x60000);

		tclod_4x17_to_15(nexts, fars, nextt, fart, 0, &lod);

		if ((lod & 0x4000) || lodclamp)
			lod = 0x7fff;
		else if (lod < min_level)
			lod = min_level;
						
		magnify = (lod < 32) ? 1: 0;
		l_tile =  log2table[(lod >> 5) & 0xff];
		distant = ((lod & 0x6000) || (l_tile >= max_level)) ? 1 : 0;

		if (distant)
			l_tile = max_level;
	
		if (!other_modes.detail_tex_en || magnify)
			*t1 = (prim_tile + l_tile) & 7;
		else
			*t1 = (prim_tile + l_tile + 1) & 7;
	}

}

STRICTINLINE void get_texel1_1cycle(INT32* s1, INT32* t1, INT32 s, INT32 t, INT32 w, INT32 dsinc, INT32 dtinc, INT32 dwinc, INT32 scanline, SPANSIGS* sigs)
{
	INT32 nexts, nextt, nextsw;
	
	if (!sigs->endspan || !sigs->longspan || !span[scanline + 1].validline)
	{
	
	
		nextsw = (w + dwinc) >> 16;
		nexts = (s + dsinc) >> 16;
		nextt = (t + dtinc) >> 16;
	}
	else
	{
		INT32 nextscan = scanline + 1;
		nextt = span[nextscan].t >> 16;
		nexts = span[nextscan].s >> 16;
		nextsw = span[nextscan].w >> 16;
	}

	tcdiv_ptr(nexts, nextt, nextsw, s1, t1);
}

STRICTINLINE void get_nexttexel0_2cycle(INT32* s1, INT32* t1, INT32 s, INT32 t, INT32 w, INT32 dsinc, INT32 dtinc, INT32 dwinc)
{
	INT32 nexts, nextt, nextsw;
	nextsw = (w + dwinc) >> 16;
	nexts = (s + dsinc) >> 16;
	nextt = (t + dtinc) >> 16;

	tcdiv_ptr(nexts, nextt, nextsw, s1, t1);
}



STRICTINLINE void tclod_4x17_to_15(INT32 scurr, INT32 snext, INT32 tcurr, INT32 tnext, INT32 previous, INT32* lod)
{



	int dels = SIGN(snext, 17) - SIGN(scurr, 17);
	if (dels & 0x20000)
		dels = ~dels & 0x1ffff;
	int delt = SIGN(tnext, 17) - SIGN(tcurr, 17);
	if(delt & 0x20000)
		delt = ~delt & 0x1ffff;
	

	dels = (dels > delt) ? dels : delt;
	dels = (previous > dels) ? previous : dels;
	*lod = dels & 0x7fff;
	if (dels & 0x1c000)
		*lod |= 0x4000;
}

STRICTINLINE void tclod_tcclamp(INT32* sss, INT32* sst)
{
	INT32 tempanded, temps = *sss, tempt = *sst;

	
	
	
	
	if (!(temps & 0x40000))
	{
		if (!(temps & 0x20000))
		{
			tempanded = temps & 0x18000;
			if (tempanded != 0x8000)
			{
				if (tempanded != 0x10000)
					*sss &= 0xffff;
				else
					*sss = 0x8000;
			}
			else
				*sss = 0x7fff;
		}
		else
			*sss = 0x8000;
	}
	else
		*sss = 0x7fff;

	if (!(tempt & 0x40000))
	{
		if (!(tempt & 0x20000))
		{
			tempanded = tempt & 0x18000;
			if (tempanded != 0x8000)
			{
				if (tempanded != 0x10000)
					*sst &= 0xffff;
				else
					*sst = 0x8000;
			}
			else
				*sst = 0x7fff;
		}
		else
			*sst = 0x8000;
	}
	else
		*sst = 0x7fff;

}


STRICTINLINE void lodfrac_lodtile_signals(int lodclamp, INT32 lod, UINT32* l_tile, UINT32* magnify, UINT32* distant)
{
	UINT32 ltil, dis, mag;
	INT32 lf;

	
	if ((lod & 0x4000) || lodclamp)
		lod = 0x7fff;
	else if (lod < min_level)
		lod = min_level;
						
	mag = (lod < 32) ? 1: 0;
	ltil=  log2table[(lod >> 5) & 0xff];
	dis = ((lod & 0x6000) || (ltil >= max_level)) ? 1 : 0;
						
	lf = ((lod << 3) >> ltil) & 0xff;

	
	if(!other_modes.sharpen_tex_en && !other_modes.detail_tex_en)
	{
		if (dis)
			lf = 0xff;
		else if (mag)
			lf = 0;
	}

	
	

	if(other_modes.sharpen_tex_en && mag)
		lf |= 0x100;

	*distant = dis;
	*l_tile = ltil;
	*magnify = mag;
	lod_frac = lf;
}



void dump_buffer4kb(char* Name, void* Buff)
{
#if 0
	FILE* Cur = fopen(Name,"wb");
	fwrite(Buff,1,4096,Cur);
	fclose(Cur);
#endif
}

void dump_buffer(char* Name, void* Buff,UINT32 Bytes)
{
#if 0
	FILE* Cur = fopen(Name,"wb");
	fwrite(Buff,1,Bytes,Cur);
	fclose(Cur);
#endif
}

void dump_tmem_and_exit(char* Name)
{
	dump_buffer4kb(Name,TMEM);
}

void col_decode16(UINT16* addr, COLOR* col)
{
	col->r = GET_HI(*addr);
	col->g = GET_MED(*addr);
	col->b = GET_LOW(*addr);
	col->a = (*addr & 1) ? 0xff : 0;
}

void show_combiner_equation(void)
{
const char *saRGBText[] =
{
  "PREV",			"TEXEL0",			"TEXEL1",			"PRIM", 
  "SHADE",			"ENV",		"1",			"NOISE",
  "0",				"0",				"0",				"0",
  "0",				"0",				"0",				"0"
};

const char *sbRGBText[] =
{
  "PREV",			"TEXEL0",			"TEXEL1",			"PRIM", 
  "SHADE",			"ENV",		"CENTER",			"K4",
  "0",				"0",				"0",				"0",
  "0",				"0",				"0",				"0"
};
  
const char *mRGBText[] =
{
  "PREV",			"TEXEL0",			"TEXEL1",			"PRIM", 
  "SHADE",			"ENV",		"SCALE",			"PREV_ALPHA",
  "TEXEL0_ALPHA",		"TEXEL1_ALPHA",		"PRIM_ALPHA",	"SHADE_ALPHA",
  "ENV_ALPHA",		"LOD_FRACTION",		"PRIM_LOD_FRAC",	"K5",
  "0",				"0",				"0",				"0",
  "0",				"0",				"0",				"0",
  "0",				"0",				"0",				"0",
  "0",				"0",				"0",				"0"
  };

const char *aRGBText[] =
{
  "PREV",			"TEXEL0",			"TEXEL1",			"PRIM", 
  "SHADE",			"ENV",		"1",				"0",
};

const char *saAText[] =
{
  "PREV",			"TEXEL0",			"TEXEL1",			"PRIM", 
  "SHADE",			"ENV",		"1",				"0",
};

const char *sbAText[] =
{
  "PREV",			"TEXEL0",			"TEXEL1",			"PRIM", 
  "SHADE",			"ENV",		"1",				"0",
};
  
const char *mAText[] =
{
  "LOD_FRACTION",		"TEXEL0",			"TEXEL1",			"PRIM", 
  "SHADE",			"ENV",		"PRIM_LOD_FRAC",	"0",
};

const char *aAText[] =
{
  "PREV",			"TEXEL0",			"TEXEL1",			"PRIM", 
  "SHADE",			"ENV",		"1",				"0",
};

if (other_modes.cycle_type!=CYCLE_TYPE_1 && other_modes.cycle_type!=CYCLE_TYPE_2)
{
	popmessage("show_combiner_equation not implemented for cycle type %d",other_modes.cycle_type);
	return;
}

if (other_modes.cycle_type == CYCLE_TYPE_1)
	popmessage("Combiner equation is (%s - %s) * %s + %s | (%s - %s) * %s + %s",saRGBText[combine.sub_a_rgb0],
	sbRGBText[combine.sub_b_rgb0],mRGBText[combine.mul_rgb0],aRGBText[combine.add_rgb0],
	saAText[combine.sub_a_a0],sbAText[combine.sub_b_a0],mAText[combine.mul_a0],aAText[combine.add_a0]);
if (other_modes.cycle_type == CYCLE_TYPE_2)
	popmessage("Combiner equation is (%s - %s) * %s + %s | (%s - %s) * %s + %s \n (%s - %s) * %s + %s | (%s - %s) * %s + %s",
	saRGBText[combine.sub_a_rgb0],sbRGBText[combine.sub_b_rgb0],mRGBText[combine.mul_rgb0],
	aRGBText[combine.add_rgb0],saAText[combine.sub_a_a0],sbAText[combine.sub_b_a0],
	mAText[combine.mul_a0],aAText[combine.add_a0],
	saRGBText[combine.sub_a_rgb1],sbRGBText[combine.sub_b_rgb1],mRGBText[combine.mul_rgb1],
	aRGBText[combine.add_rgb1],saAText[combine.sub_a_a1],sbAText[combine.sub_b_a1],
	mAText[combine.mul_a1],aAText[combine.add_a1]);
UINT32 LocalDebugMode=0;
if (LocalDebugMode)
	popmessage("%d %d %d %d %d %d %d %d",combine.sub_a_rgb0,combine.sub_b_rgb0,combine.mul_rgb0,combine.add_rgb0,
	combine.sub_a_a0,combine.sub_b_a0,combine.mul_a0,combine.add_a0);
}

void show_blender_equation(void)
{
const char * bRGBText[] = { "PREV", "MEMRGB", "BLEND", "FOG" };
const char * bAText[2][4] = { {"PREVA", "FOGA", "SHADEA", "0"},
                                     {"INVALPHA", "MEMA", "1", "0"}};
if (other_modes.cycle_type!=CYCLE_TYPE_1 && other_modes.cycle_type!=CYCLE_TYPE_2)
{
	popmessage("show_blender_equation not implemented for cycle type %d",other_modes.cycle_type);
	return;
}
if (other_modes.cycle_type == CYCLE_TYPE_1)
	popmessage("Blender equation is %s * %s + %s * %s",bRGBText[other_modes.blend_m1a_0],
	bAText[0][other_modes.blend_m1b_0],bRGBText[other_modes.blend_m2a_0],bAText[0][other_modes.blend_m2b_0]);
if (other_modes.cycle_type == CYCLE_TYPE_2)
	popmessage("Blender equation is %s * %s + %s * %s\n%s * %s + %s * %s",
	bRGBText[other_modes.blend_m1a_0],bAText[0][other_modes.blend_m1b_0],
	bRGBText[other_modes.blend_m2a_0],bAText[1][other_modes.blend_m2b_0],
	bRGBText[other_modes.blend_m1a_1],bAText[0][other_modes.blend_m1b_1],
	bRGBText[other_modes.blend_m2a_1],bAText[1][other_modes.blend_m2b_1]);
}


void show_tri_command(void)
{
	popmessage("w0: 0x%08x, w1: 0x%08x, w2: 0x%08x",rdp_cmd_data[rdp_cmd_cur], rdp_cmd_data[rdp_cmd_cur + 1], rdp_cmd_data[rdp_cmd_cur + 2]);
}

UINT32 compare_tri_command(UINT32 w0, UINT32 w1, UINT32 w2)
{
	if (w0 == rdp_cmd_data[rdp_cmd_cur] && w1 == rdp_cmd_data[rdp_cmd_cur + 1] && w2 == rdp_cmd_data[rdp_cmd_cur + 2])
		return 1;
	else
		return 0;
}

void show_color(COLOR* col)
{
	popmessage("R: 0x%x, G: 0x%x, B: 0x%x, A: 0x%x", col->r, col->g, col->b, col->a);
}
int getdebugcolor(void)
{
	return (irand() & 0x7f) + (irand() & 0x3f) + (irand() & 0x1f);
}

void bytefill_tmem(char byte)
{
	memset(TMEM, byte, 4096);
}
