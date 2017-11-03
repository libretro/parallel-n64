#pragma once

#include <stdint.h>
#include <stdbool.h>

enum dp_register
{
    DP_START,
    DP_END,
    DP_CURRENT,
    DP_STATUS,
    DP_CLOCK,
    DP_BUFBUSY,
    DP_PIPEBUSY,
    DP_TMEM,
    DP_NUM_REG
};

enum vi_register
{
    VI_STATUS,  // aka VI_CONTROL
    VI_ORIGIN,  // aka VI_DRAM_ADDR
    VI_WIDTH,
    VI_INTR,
    VI_V_CURRENT_LINE,
    VI_TIMING,
    VI_V_SYNC,
    VI_H_SYNC,
    VI_LEAP,    // aka VI_H_SYNC_LEAP
    VI_H_START, // aka VI_H_VIDEO
    VI_V_START, // aka VI_V_VIDEO
    VI_V_BURST,
    VI_X_SCALE,
    VI_Y_SCALE,
    VI_NUM_REG
};

enum vi_mode
{
    VI_MODE_NORMAL,     // color buffer with VI filter
    VI_MODE_COLOR,      // direct color buffer, unfiltered
    VI_MODE_DEPTH,      // depth buffer as grayscale
    VI_MODE_COVERAGE,   // coverage as grayscale
    VI_MODE_NUM
};

struct core_config
{
    struct {
        enum vi_mode mode;
        bool widescreen;
        bool overscan;
    } vi;
    bool parallel;
    uint32_t num_workers;
};

struct color
{
    int32_t r, g, b, a;
};

struct rectangle
{
    uint16_t xl, yl, xh, yh;
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

typedef struct rdp_globals
{
   uint8_t cvgbuf[1024];
   uint8_t tmem[0x1000];

   uint16_t primitive_delta_z;

   int blshifta;
   int blshiftb;
   int pastblshifta;
   int pastblshiftb;
   int scfield;
   int sckeepodd;
   int ti_format;
   int ti_size;
   int ti_width;
   int fb_format;
   int fb_size;
   int fb_width;

   int32_t iseed;
   int32_t k0_tf;
   int32_t k1_tf;
   int32_t k2_tf;
   int32_t k3_tf;
   int32_t k4;
   int32_t k5;
   int32_t min_level;
   int32_t primitive_lod_frac;
   int32_t lod_frac;
   int32_t noise;
   int32_t pastrawdzmem;

   uint32_t max_level;
   uint32_t primitive_z;
   uint32_t zb_address;
   uint32_t ti_address;
   uint32_t fb_address;
   uint32_t fill_color;

   struct color texel0_color;
   struct color texel1_color;
   struct color nexttexel_color;
   struct color shade_color;
   struct color combined_color;
   struct color pixel_color;
   struct color inv_pixel_color;
   struct color blended_pixel_color;

   struct color prim_color;
   struct color blend_color;
   struct color env_color;
   struct color fog_color;

   struct color memory_color;
   struct color pre_memory_color;

   struct color key_scale;
   struct color key_center;
   struct color key_width;

   struct rectangle clip;
   struct other_modes other_modes;

   void (*fbread1_ptr)(uint32_t, uint32_t*);
   void (*fbread2_ptr)(uint32_t, uint32_t*);
   void (*fbwrite_ptr)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
   void (*tcdiv_ptr)(int32_t, int32_t, int32_t, int32_t*, int32_t*);

   struct
   {
      int lx, rx;
      int unscrx;
      int validline;
      int32_t r, g, b, a, s, t, w, z;
      int32_t majorx[4];
      int32_t minorx[4];
      int32_t invalyscan[4];
   } span[1024];

   /* span states */
   struct
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

   struct tile
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

   struct
   {
      int32_t *i1a_r[2];
      int32_t *i1a_g[2];
      int32_t *i1a_b[2];
      int32_t *i1b_a[2];
      int32_t *i2a_r[2];
      int32_t *i2a_g[2];
      int32_t *i2a_b[2];
      int32_t *i2b_a[2];
   } blender;

   struct
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
   } combine;

   struct
   {
      int32_t *rgbsub_a_r[2];
      int32_t *rgbsub_a_g[2];
      int32_t *rgbsub_a_b[2];
      int32_t *rgbsub_b_r[2];
      int32_t *rgbsub_b_g[2];
      int32_t *rgbsub_b_b[2];
      int32_t *rgbmul_r[2];
      int32_t *rgbmul_g[2];
      int32_t *rgbmul_b[2];
      int32_t *rgbadd_r[2];
      int32_t *rgbadd_g[2];
      int32_t *rgbadd_b[2];

      int32_t *alphasub_a[2];
      int32_t *alphasub_b[2];
      int32_t *alphamul[2];
      int32_t *alphaadd[2];
   } combiner;
};

extern uint8_t rdram_hidden_bits[0x400000];

#ifdef __cplusplus
extern "C" {
#endif

void core_init(struct core_config* config);
void core_close(void);
void core_config_update(struct core_config* config);
void core_config_defaults(struct core_config* config);
void core_dp_sync(void);
void screen_swap(void);

#ifdef __cplusplus
}
#endif
