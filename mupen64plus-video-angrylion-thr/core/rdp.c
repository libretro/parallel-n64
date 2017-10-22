#include "rdp.h"
#include "vi.h"
#include "common.h"
#include "plugin.h"
#include "rdram.h"
#include "trace_write.h"
#include "msg.h"
#include "irand.h"
#include "file.h"
#include "parallel_c.hpp"

#include <memory.h>
#include <string.h>

#define SIGN16(x)   ((int16_t)(x))
#define SIGN8(x)    ((int8_t)(x))


#define SIGN(x, numb)   (((x) & ((1 << numb) - 1)) | -((x) & (1 << (numb - 1))))
#define SIGNF(x, numb)  ((x) | -((x) & (1 << (numb - 1))))

#define TRELATIVE(x, y)     ((x) - ((y) << 3))




// bit constants for DP_STATUS
#define DP_STATUS_XBUS_DMA      0x001   // DMEM DMA mode is set
#define DP_STATUS_FREEZE        0x002   // Freeze has been set
#define DP_STATUS_FLUSH         0x004   // Flush has been set
#define DP_STATUS_START_GCLK    0x008   // Unknown
#define DP_STATUS_TMEM_BUSY     0x010   // TMEM is in use on the RDP
#define DP_STATUS_PIPE_BUSY     0x020   // Graphics pipe is in use on the RDP
#define DP_STATUS_CMD_BUSY      0x040   // RDP is currently executing a command
#define DP_STATUS_CBUF_BUSY     0x080   // RDRAM RDP command buffer is in use
#define DP_STATUS_DMA_BUSY      0x100   // DMEM RDP command buffer is in use
#define DP_STATUS_END_VALID     0x200   // Unknown
#define DP_STATUS_START_VALID   0x400   // Unknown

static struct core_config* config;
static struct plugin_api* plugin;

static TLS int blshifta = 0, blshiftb = 0, pastblshifta = 0, pastblshiftb = 0;

static TLS struct
{
    int lx, rx;
    int unscrx;
    int validline;
    int32_t r, g, b, a, s, t, w, z;
    int32_t majorx[4];
    int32_t minorx[4];
    int32_t invalyscan[4];
} span[1024];

// span states
static TLS struct
{
    int ds;
    int dt;
    int dw;
    int dr;
    int dg;
    int db;
    int da;
    int dz;
    int dzpix;

    int drdy;
    int dgdy;
    int dbdy;
    int dady;
    int dzdy;
    int cdr;
    int cdg;
    int cdb;
    int cda;
    int cdz;

    int dsdy;
    int dtdy;
    int dwdy;
} spans;



struct color
{
    int32_t r, g, b, a;
};

struct fbcolor
{
    uint8_t r, g, b;
};

struct rectangle
{
    uint16_t xl, yl, xh, yh;
};

struct tex_rectangle
{
    int tilenum;
    uint16_t xl, yl, xh, yh;
    int16_t s, t;
    int16_t dsdx, dtdy;
    uint32_t flip;
};

struct other_modes
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
    struct {
        int stalederivs;
        int dolod;
        int partialreject_1cycle;
        int partialreject_2cycle;
        int rgb_alpha_dither;
        int realblendershiftersneeded;
        int interpixelblendershiftersneeded;
        int getditherlevel;
        int textureuselevel0;
        int textureuselevel1;
    } f;
};



#define PIXEL_SIZE_4BIT         0
#define PIXEL_SIZE_8BIT         1
#define PIXEL_SIZE_16BIT        2
#define PIXEL_SIZE_32BIT        3

#define CYCLE_TYPE_1            0
#define CYCLE_TYPE_2            1
#define CYCLE_TYPE_COPY         2
#define CYCLE_TYPE_FILL         3


#define FORMAT_RGBA             0
#define FORMAT_YUV              1
#define FORMAT_CI               2
#define FORMAT_IA               3
#define FORMAT_I                4


#define TEXEL_RGBA4             0
#define TEXEL_RGBA8             1
#define TEXEL_RGBA16            2
#define TEXEL_RGBA32            3
#define TEXEL_YUV4              4
#define TEXEL_YUV8              5
#define TEXEL_YUV16             6
#define TEXEL_YUV32             7
#define TEXEL_CI4               8
#define TEXEL_CI8               9
#define TEXEL_CI16              0xa
#define TEXEL_CI32              0xb
#define TEXEL_IA4               0xc
#define TEXEL_IA8               0xd
#define TEXEL_IA16              0xe
#define TEXEL_IA32              0xf
#define TEXEL_I4                0x10
#define TEXEL_I8                0x11
#define TEXEL_I16               0x12
#define TEXEL_I32               0x13



static TLS struct other_modes other_modes;

static TLS struct color combined_color;
static TLS struct color texel0_color;
static TLS struct color texel1_color;
static TLS struct color nexttexel_color;
static TLS struct color shade_color;
static TLS int32_t noise = 0;
static TLS int32_t primitive_lod_frac = 0;
static int32_t one_color = 0x100;
static int32_t zero_color = 0x00;

static TLS struct color pixel_color;
static TLS struct color memory_color;
static TLS struct color pre_memory_color;

static TLS struct tile
{
    int format;
    int size;
    int line;
    int tmem;
    int palette;
    int ct, mt, cs, ms;
    int mask_t, shift_t, mask_s, shift_s;

    uint16_t sl, tl, sh, th;

    struct
    {
        int clampdiffs, clampdifft;
        int clampens, clampent;
        int masksclamped, masktclamped;
        int notlutswitch, tlutswitch;
    } f;
} tile[8];

#define PIXELS_TO_BYTES(pix, siz) (((pix) << (siz)) >> 1)

struct spansigs {
    int startspan;
    int endspan;
    int preendspan;
    int nextspan;
    int midspan;
    int longspan;
    int onelessthanmid;
};

static void deduce_derivatives(void);

static TLS int32_t k0_tf = 0, k1_tf = 0, k2_tf = 0, k3_tf = 0;
static TLS int32_t k4 = 0, k5 = 0;
static TLS int32_t lod_frac = 0;

static struct
{
    int copymstrangecrashes, fillmcrashes, fillmbitcrashes, syncfullcrash;
} onetimewarnings;

static TLS uint32_t max_level = 0;
static TLS int32_t min_level = 0;
static int rdp_pipeline_crashed = 0;

static STRICTINLINE int32_t clamp(int32_t value,int32_t min,int32_t max)
{
    if (value < min)
        return min;
    else if (value > max)
        return max;
    else
        return value;
}

#include "rdp/cmd.c"
#include "rdp/dither.c"
#include "rdp/blender.c"
#include "rdp/combiner.c"
#include "rdp/coverage.c"
#include "rdp/zbuffer.c"
#include "rdp/fbuffer.c"
#include "rdp/tex.c"
#include "rdp/rasterizer.c"

int rdp_init(struct core_config* _config)
{
    config = _config;

    uint32_t tmp[2] = {0};
    rdp_set_other_modes(tmp);
    other_modes.f.stalederivs = 1;

    memset(tile, 0, sizeof(tile));

    for (int i = 0; i < 8; i++)
    {
        calculate_tile_derivs(i);
        calculate_clamp_diffs(i);
    }

    memset(&combined_color, 0, sizeof(struct color));
    memset(&prim_color, 0, sizeof(struct color));
    memset(&env_color, 0, sizeof(struct color));
    memset(&key_scale, 0, sizeof(struct color));
    memset(&key_center, 0, sizeof(struct color));

    rdp_pipeline_crashed = 0;
    memset(&onetimewarnings, 0, sizeof(onetimewarnings));

    precalc_cvmask_derivatives();
    z_init();
    dither_init();
    fb_init();
    blender_init();
    combiner_init();
    tex_init();
    rasterizer_init();

    return 0;
}

static void rdp_invalid(const uint32_t* args)
{
}

static void rdp_noop(const uint32_t* args)
{
}

static void rdp_sync_load(const uint32_t* args)
{
}

static void rdp_sync_pipe(const uint32_t* args)
{
}

static void rdp_sync_tile(const uint32_t* args)
{
}

static void rdp_sync_full(const uint32_t* args)
{
    core_dp_sync();
}

static void rdp_set_other_modes(const uint32_t* args)
{
    other_modes.cycle_type          = (args[0] >> 20) & 3;
    other_modes.persp_tex_en        = (args[0] >> 19) & 1;
    other_modes.detail_tex_en       = (args[0] >> 18) & 1;
    other_modes.sharpen_tex_en      = (args[0] >> 17) & 1;
    other_modes.tex_lod_en          = (args[0] >> 16) & 1;
    other_modes.en_tlut             = (args[0] >> 15) & 1;
    other_modes.tlut_type           = (args[0] >> 14) & 1;
    other_modes.sample_type         = (args[0] >> 13) & 1;
    other_modes.mid_texel           = (args[0] >> 12) & 1;
    other_modes.bi_lerp0            = (args[0] >> 11) & 1;
    other_modes.bi_lerp1            = (args[0] >> 10) & 1;
    other_modes.convert_one         = (args[0] >>  9) & 1;
    other_modes.key_en              = (args[0] >>  8) & 1;
    other_modes.rgb_dither_sel      = (args[0] >>  6) & 3;
    other_modes.alpha_dither_sel    = (args[0] >>  4) & 3;
    other_modes.blend_m1a_0         = (args[1] >> 30) & 3;
    other_modes.blend_m1a_1         = (args[1] >> 28) & 3;
    other_modes.blend_m1b_0         = (args[1] >> 26) & 3;
    other_modes.blend_m1b_1         = (args[1] >> 24) & 3;
    other_modes.blend_m2a_0         = (args[1] >> 22) & 3;
    other_modes.blend_m2a_1         = (args[1] >> 20) & 3;
    other_modes.blend_m2b_0         = (args[1] >> 18) & 3;
    other_modes.blend_m2b_1         = (args[1] >> 16) & 3;
    other_modes.force_blend         = (args[1] >> 14) & 1;
    other_modes.alpha_cvg_select    = (args[1] >> 13) & 1;
    other_modes.cvg_times_alpha     = (args[1] >> 12) & 1;
    other_modes.z_mode              = (args[1] >> 10) & 3;
    other_modes.cvg_dest            = (args[1] >>  8) & 3;
    other_modes.color_on_cvg        = (args[1] >>  7) & 1;
    other_modes.image_read_en       = (args[1] >>  6) & 1;
    other_modes.z_update_en         = (args[1] >>  5) & 1;
    other_modes.z_compare_en        = (args[1] >>  4) & 1;
    other_modes.antialias_en        = (args[1] >>  3) & 1;
    other_modes.z_source_sel        = (args[1] >>  2) & 1;
    other_modes.dither_alpha_en     = (args[1] >>  1) & 1;
    other_modes.alpha_compare_en    = (args[1] >>  0) & 1;

    set_blender_input(0, 0, &blender.i1a_r[0], &blender.i1a_g[0], &blender.i1a_b[0], &blender.i1b_a[0],
                      other_modes.blend_m1a_0, other_modes.blend_m1b_0);
    set_blender_input(0, 1, &blender.i2a_r[0], &blender.i2a_g[0], &blender.i2a_b[0], &blender.i2b_a[0],
                      other_modes.blend_m2a_0, other_modes.blend_m2b_0);
    set_blender_input(1, 0, &blender.i1a_r[1], &blender.i1a_g[1], &blender.i1a_b[1], &blender.i1b_a[1],
                      other_modes.blend_m1a_1, other_modes.blend_m1b_1);
    set_blender_input(1, 1, &blender.i2a_r[1], &blender.i2a_g[1], &blender.i2a_b[1], &blender.i2b_a[1],
                      other_modes.blend_m2a_1, other_modes.blend_m2b_1);

    other_modes.f.stalederivs = 1;
}

static void deduce_derivatives()
{
    int special_bsel0, special_bsel1;


    other_modes.f.partialreject_1cycle = (blender.i2b_a[0] == &inv_pixel_color.a && blender.i1b_a[0] == &pixel_color.a);
    other_modes.f.partialreject_2cycle = (blender.i2b_a[1] == &inv_pixel_color.a && blender.i1b_a[1] == &pixel_color.a);


    special_bsel0 = (blender.i2b_a[0] == &memory_color.a);
    special_bsel1 = (blender.i2b_a[1] == &memory_color.a);


    other_modes.f.realblendershiftersneeded = (special_bsel0 && other_modes.cycle_type == CYCLE_TYPE_1) || (special_bsel1 && other_modes.cycle_type == CYCLE_TYPE_2);
    other_modes.f.interpixelblendershiftersneeded = (special_bsel0 && other_modes.cycle_type == CYCLE_TYPE_2);

    other_modes.f.rgb_alpha_dither = (other_modes.rgb_dither_sel << 2) | other_modes.alpha_dither_sel;

    tcdiv_ptr = tcdiv_func[other_modes.persp_tex_en];


    int texel1_used_in_cc1 = 0, texel0_used_in_cc1 = 0, texel0_used_in_cc0 = 0, texel1_used_in_cc0 = 0;
    int texels_in_cc0 = 0, texels_in_cc1 = 0;
    int lod_frac_used_in_cc1 = 0, lod_frac_used_in_cc0 = 0;

    if ((combiner.rgbmul_r[1] == &lod_frac) || (combiner.alphamul[1] == &lod_frac))
        lod_frac_used_in_cc1 = 1;
    if ((combiner.rgbmul_r[0] == &lod_frac) || (combiner.alphamul[0] == &lod_frac))
        lod_frac_used_in_cc0 = 1;

    if (combiner.rgbmul_r[1] == &texel1_color.r || combiner.rgbsub_a_r[1] == &texel1_color.r || combiner.rgbsub_b_r[1] == &texel1_color.r || combiner.rgbadd_r[1] == &texel1_color.r || \
        combiner.alphamul[1] == &texel1_color.a || combiner.alphasub_a[1] == &texel1_color.a || combiner.alphasub_b[1] == &texel1_color.a || combiner.alphaadd[1] == &texel1_color.a || \
        combiner.rgbmul_r[1] == &texel1_color.a)
        texel1_used_in_cc1 = 1;
    if (combiner.rgbmul_r[1] == &texel0_color.r || combiner.rgbsub_a_r[1] == &texel0_color.r || combiner.rgbsub_b_r[1] == &texel0_color.r || combiner.rgbadd_r[1] == &texel0_color.r || \
        combiner.alphamul[1] == &texel0_color.a || combiner.alphasub_a[1] == &texel0_color.a || combiner.alphasub_b[1] == &texel0_color.a || combiner.alphaadd[1] == &texel0_color.a || \
        combiner.rgbmul_r[1] == &texel0_color.a)
        texel0_used_in_cc1 = 1;
    if (combiner.rgbmul_r[0] == &texel1_color.r || combiner.rgbsub_a_r[0] == &texel1_color.r || combiner.rgbsub_b_r[0] == &texel1_color.r || combiner.rgbadd_r[0] == &texel1_color.r || \
        combiner.alphamul[0] == &texel1_color.a || combiner.alphasub_a[0] == &texel1_color.a || combiner.alphasub_b[0] == &texel1_color.a || combiner.alphaadd[0] == &texel1_color.a || \
        combiner.rgbmul_r[0] == &texel1_color.a)
        texel1_used_in_cc0 = 1;
    if (combiner.rgbmul_r[0] == &texel0_color.r || combiner.rgbsub_a_r[0] == &texel0_color.r || combiner.rgbsub_b_r[0] == &texel0_color.r || combiner.rgbadd_r[0] == &texel0_color.r || \
        combiner.alphamul[0] == &texel0_color.a || combiner.alphasub_a[0] == &texel0_color.a || combiner.alphasub_b[0] == &texel0_color.a || combiner.alphaadd[0] == &texel0_color.a || \
        combiner.rgbmul_r[0] == &texel0_color.a)
        texel0_used_in_cc0 = 1;
    texels_in_cc0 = texel0_used_in_cc0 || texel1_used_in_cc0;
    texels_in_cc1 = texel0_used_in_cc1 || texel1_used_in_cc1;


    if (texel1_used_in_cc1)
        other_modes.f.textureuselevel0 = 0;
    else if (texel0_used_in_cc1 || lod_frac_used_in_cc1)
        other_modes.f.textureuselevel0 = 1;
    else
        other_modes.f.textureuselevel0 = 2;

    if (texel1_used_in_cc1)
        other_modes.f.textureuselevel1 = 0;
    else if (texel1_used_in_cc0 || texel0_used_in_cc1)
        other_modes.f.textureuselevel1 = 1;
    else if (texel0_used_in_cc0 || lod_frac_used_in_cc0 || lod_frac_used_in_cc1)
        other_modes.f.textureuselevel1 = 2;
    else
        other_modes.f.textureuselevel1 = 3;


    int lodfracused = 0;

    if ((other_modes.cycle_type == CYCLE_TYPE_2 && (lod_frac_used_in_cc0 || lod_frac_used_in_cc1)) || \
        (other_modes.cycle_type == CYCLE_TYPE_1 && lod_frac_used_in_cc1))
        lodfracused = 1;

    if ((other_modes.cycle_type == CYCLE_TYPE_1 && combiner.rgbsub_a_r[1] == &noise) || \
        (other_modes.cycle_type == CYCLE_TYPE_2 && (combiner.rgbsub_a_r[0] == &noise || combiner.rgbsub_a_r[1] == &noise)) || \
        other_modes.alpha_dither_sel == 2)
        other_modes.f.getditherlevel = 0;
    else if (other_modes.f.rgb_alpha_dither != 0xf)
        other_modes.f.getditherlevel = 1;
    else
        other_modes.f.getditherlevel = 2;

    other_modes.f.dolod = other_modes.tex_lod_en || lodfracused;
}
