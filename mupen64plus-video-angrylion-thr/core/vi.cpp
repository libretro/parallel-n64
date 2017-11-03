#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <memory.h>
#include <assert.h>

#include "vi.h"
#include "rdp.h"
#include "common.h"
#include "rdram.h"
#include "parallel_c.hpp"

#ifdef __cplusplus
extern "C" {
#endif

#include "msg.h"

#ifdef __cplusplus
}
#endif

/* START OF DEFINES */

// anamorphic NTSC resolution
#define H_RES_NTSC 640
#define V_RES_NTSC 480

// anamorphic PAL resolution
#define H_RES_PAL 768
#define V_RES_PAL 576

// typical VI_V_SYNC values for NTSC and PAL
#define V_SYNC_NTSC 525
#define V_SYNC_PAL 625

// maximum possible size of the prescale area
#define PRESCALE_WIDTH H_RES_NTSC
#define PRESCALE_HEIGHT V_SYNC_PAL

/* END OF DEFINES */

/* START OF MACROS */

#define VI_COMPARE(x)                                               \
{                                                                   \
    addr = (x);                                                     \
    RREADIDX16(pix, addr);                                          \
    tempr = (pix >> 11) & 0x1f;                                     \
    tempg = (pix >> 6) & 0x1f;                                      \
    tempb = (pix >> 1) & 0x1f;                                      \
    rend += redptr[tempr];                                          \
    gend += greenptr[tempg];                                        \
    bend += blueptr[tempb];                                         \
}

#define VI_COMPARE_OPT(x)                                           \
{                                                                   \
   RREADIDX16FAST(pix, (x));                                        \
    tempr = (pix >> 11) & 0x1f;                                     \
    tempg = (pix >> 6) & 0x1f;                                      \
    tempb = (pix >> 1) & 0x1f;                                      \
    rend += redptr[tempr];                                          \
    gend += greenptr[tempg];                                        \
    bend += blueptr[tempb];                                         \
}

#define VI_COMPARE32(x)                                                 \
{                                                                       \
    addr = (x);                                                         \
    RREADIDX32(pix, addr);                                              \
    tempr = (pix >> 27) & 0x1f;                                         \
    tempg = (pix >> 19) & 0x1f;                                         \
    tempb = (pix >> 11) & 0x1f;                                         \
    rend += redptr[tempr];                                              \
    gend += greenptr[tempg];                                            \
    bend += blueptr[tempb];                                             \
}

#define VI_COMPARE32_OPT(x)                                             \
{                                                                       \
    pix   = rdram32[(x)];                                               \
    tempr = (pix >> 27) & 0x1f;                                         \
    tempg = (pix >> 19) & 0x1f;                                         \
    tempb = (pix >> 11) & 0x1f;                                         \
    rend += redptr[tempr];                                              \
    gend += greenptr[tempg];                                            \
    bend += blueptr[tempb];                                             \
}

#define VI_ANDER(x) {                                                   \
            PAIRREAD16(&pix, &hidval, (x));                                   \
            if (hidval == 3 && (pix & 1))                               \
            {                                                           \
                backr[numoffull] = GET_HI(pix);                         \
                backg[numoffull] = GET_MED(pix);                        \
                backb[numoffull] = GET_LOW(pix);                        \
                numoffull++;                                            \
            }                                                           \
}

#define VI_ANDER32(x) {                                                 \
            RREADIDX32(pix, (x));                                       \
            pixcvg = (pix >> 5) & 7;                                    \
            if (pixcvg == 7)                                            \
            {                                                           \
                backr[numoffull] = (pix >> 24) & 0xff;                  \
                backg[numoffull] = (pix >> 16) & 0xff;                  \
                backb[numoffull] = (pix >> 8) & 0xff;                   \
                numoffull++;                                            \
            }                                                           \
}

/* END OF MACROS */

enum vi_type
{
    VI_TYPE_BLANK,      // no data, no sync
    VI_TYPE_RESERVED,   // unused, should never be set
    VI_TYPE_RGBA5551,   // 16 bit color (internally 18 bit RGBA5553)
    VI_TYPE_RGBA8888    // 32 bit color
};

enum vi_aa
{
    VI_AA_RESAMP_EXTRA_ALWAYS,  // resample and AA (always fetch extra lines)
    VI_AA_RESAMP_EXTRA,         // resample and AA (fetch extra lines if needed)
    VI_AA_RESAMP_ONLY,          // only resample (treat as all fully covered)
    VI_AA_REPLICATE             // replicate pixels, no interpolation
};

union vi_reg_ctrl
{
    struct {
        uint32_t type : 2;
        uint32_t gamma_dither_enable : 1;
        uint32_t gamma_enable : 1;
        uint32_t divot_enable : 1;
        uint32_t vbus_clock_enable : 1;
        uint32_t serrate : 1;
        uint32_t test_mode : 1;
        uint32_t aa_mode : 2;
        uint32_t reserved : 1;
        uint32_t kill_we : 1;
        uint32_t pixel_advance : 4;
        uint32_t dither_filter_enable : 1;
    };
    uint32_t raw;
};

struct ccvg
{
    uint8_t r, g, b, cvg;
};

/* START OF VARIABLES */

static uint32_t gamma_table[0x100];
static uint32_t gamma_dither_table[0x4000];

static int vi_restore_table[0x400];

/* END OF VARIABLES */

static uint32_t vi_integer_sqrt(uint32_t a)
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

static STRICTINLINE void gamma_filters(int* r, int* g, int* b, union vi_reg_ctrl ctrl)
{
   int cdith, dith;

   switch((ctrl.gamma_enable << 1) | ctrl.gamma_dither_enable)
   {
      case 0: // no gamma, no dithering
         return;
      case 1: // no gamma, dithering enabled
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
      case 2: // gamma enabled, no dithering
         *r = gamma_table[*r];
         *g = gamma_table[*g];
         *b = gamma_table[*b];
         break;
      case 3: // gamma and dithering enabled
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

void vi_gamma_init(void)
{
   int i;
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
}

static STRICTINLINE void vi_vl_lerp(struct ccvg* up, struct ccvg down, uint32_t frac)
{
    uint32_t r0, g0, b0;
    if (!frac)
        return;

    r0 = up->r;
    g0 = up->g;
    b0 = up->b;

    up->r = ((((down.r - r0) * frac + 16) >> 5) + r0) & 0xff;
    up->g = ((((down.g - g0) * frac + 16) >> 5) + g0) & 0xff;
    up->b = ((((down.b - b0) * frac + 16) >> 5) + b0) & 0xff;
}

static STRICTINLINE void divot_filter(struct ccvg* final, struct ccvg centercolor, struct ccvg leftcolor, struct ccvg rightcolor)
{
    uint32_t leftr, leftg, leftb, rightr, rightg, rightb, centerr, centerg, centerb;

    *final = centercolor;

    if ((centercolor.cvg & leftcolor.cvg & rightcolor.cvg) == 7)
        return;

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

static STRICTINLINE void video_max_optimized(uint32_t* pixels, uint32_t* penumin, uint32_t* penumax, int numofels)
{
    int i;
    int posmax = 0, posmin = 0;
    uint32_t curpenmax = pixels[0], curpenmin = pixels[0];
    uint32_t max, min;

    for (i = 1; i < numofels; i++)
    {
        if (pixels[i] > pixels[posmax])
        {
            curpenmax = pixels[posmax];
            posmax = i;
        }
        else if (pixels[i] < pixels[posmin])
        {
            curpenmin = pixels[posmin];
            posmin = i;
        }
    }
    max = pixels[posmax];
    min = pixels[posmin];
    if (curpenmax != max)
    {
        for (i = posmax + 1; i < numofels; i++)
        {
            if (pixels[i] > curpenmax)
                curpenmax = pixels[i];
        }
    }
    if (curpenmin != min)
    {
        for (i = posmin + 1; i < numofels; i++)
        {
            if (pixels[i] < curpenmin)
                curpenmin = pixels[i];
        }
    }
    *penumax = curpenmax;
    *penumin = curpenmin;
}


static STRICTINLINE void video_filter16(int* endr, int* endg, int* endb, uint32_t fboffset, uint32_t num, uint32_t hres, uint32_t centercvg, uint32_t fetchbugstate)
{
    uint32_t penumaxr, penumaxg, penumaxb, penuminr, penuming, penuminb;
    uint16_t pix;
    uint32_t numoffull = 1;
    uint8_t hidval;
    uint32_t backr[7], backg[7], backb[7];
    uint32_t r = *endr;
    uint32_t g = *endg;
    uint32_t b = *endb;

    backr[0] = r;
    backg[0] = g;
    backb[0] = b;

    uint32_t idx = (fboffset >> 1) + num;

    uint32_t toleft = idx - 2;
    uint32_t toright = idx + 2;

    uint32_t leftup, rightup, leftdown, rightdown;

    leftup = idx - hres - 1;
    rightup = idx - hres + 1;

    if (fetchbugstate != 1)
    {
        leftdown = idx + hres - 1;
        rightdown = idx + hres + 1;
    }
    else
    {
        leftdown = toleft;
        rightdown = toright;
    }

    VI_ANDER(leftup);
    VI_ANDER(rightup);
    VI_ANDER(toleft);
    VI_ANDER(toright);
    VI_ANDER(leftdown);
    VI_ANDER(rightdown);

    uint32_t colr, colg, colb;

    video_max_optimized(backr, &penuminr, &penumaxr, numoffull);
    video_max_optimized(backg, &penuming, &penumaxg, numoffull);
    video_max_optimized(backb, &penuminb, &penumaxb, numoffull);

    uint32_t coeff = 7 - centercvg;
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

static STRICTINLINE void video_filter32(int* endr, int* endg, int* endb, uint32_t fboffset, uint32_t num, uint32_t hres, uint32_t centercvg, uint32_t fetchbugstate)
{
    uint32_t penumaxr, penumaxg, penumaxb, penuminr, penuming, penuminb;
    uint32_t numoffull = 1;
    uint32_t pix = 0, pixcvg = 0;
    uint32_t backr[7], backg[7], backb[7];
    uint32_t r = *endr;
    uint32_t g = *endg;
    uint32_t b = *endb;

    backr[0] = r;
    backg[0] = g;
    backb[0] = b;

    uint32_t idx = (fboffset >> 2) + num;

    uint32_t toleft = idx - 2;
    uint32_t toright = idx + 2;

    uint32_t leftup, rightup, leftdown, rightdown;

    leftup = idx - hres - 1;
    rightup = idx - hres + 1;

    if (fetchbugstate != 1)
    {
        leftdown = idx + hres - 1;
        rightdown = idx + hres + 1;
    }
    else
    {
        leftdown = toleft;
        rightdown = toright;
    }

    VI_ANDER32(leftup);
    VI_ANDER32(rightup);
    VI_ANDER32(toleft);
    VI_ANDER32(toright);
    VI_ANDER32(leftdown);
    VI_ANDER32(rightdown);

    uint32_t colr, colg, colb;

    video_max_optimized(backr, &penuminr, &penumaxr, numoffull);
    video_max_optimized(backg, &penuming, &penumaxg, numoffull);
    video_max_optimized(backb, &penuminb, &penumaxb, numoffull);

    uint32_t coeff = 7 - centercvg;
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

static STRICTINLINE void restore_filter16(int* r, int* g, int* b, uint32_t fboffset, uint32_t num, uint32_t hres, uint32_t fetchbugstate)
{
   uint32_t idx = (fboffset >> 1) + num;

   uint32_t toleftpix = idx - 1;

   uint32_t leftdownpix, maxpix;

   uint32_t leftuppix = idx - hres - 1;

   if (fetchbugstate != 1)
   {
      leftdownpix = idx + hres - 1;
      maxpix = idx + hres + 1;
   }
   else
   {
      leftdownpix = toleftpix;
      maxpix = toleftpix + 2;
   }

   int rend = *r;
   int gend = *g;
   int bend = *b;
   const int* redptr = &vi_restore_table[(rend << 2) & 0x3e0];
   const int* greenptr = &vi_restore_table[(gend << 2) & 0x3e0];
   const int* blueptr = &vi_restore_table[(bend << 2) & 0x3e0];

   uint32_t tempr, tempg, tempb;
   uint16_t pix;
   uint32_t addr;

   if (  (maxpix    <= idxlim16) &&
         (leftuppix <= idxlim16))
   {
      VI_COMPARE_OPT(leftuppix);
      VI_COMPARE_OPT(leftuppix + 1);
      VI_COMPARE_OPT(leftuppix + 2);
      VI_COMPARE_OPT(leftdownpix);
      VI_COMPARE_OPT(leftdownpix + 1);
      VI_COMPARE_OPT(maxpix);
      VI_COMPARE_OPT(toleftpix);
      VI_COMPARE_OPT(toleftpix + 2);
   }
   else
   {
      VI_COMPARE(leftuppix);
      VI_COMPARE(leftuppix + 1);
      VI_COMPARE(leftuppix + 2);
      VI_COMPARE(leftdownpix);
      VI_COMPARE(leftdownpix + 1);
      VI_COMPARE(maxpix);
      VI_COMPARE(toleftpix);
      VI_COMPARE(toleftpix + 2);
   }


   *r = rend;
   *g = gend;
   *b = bend;
}

static STRICTINLINE void restore_filter32(int* r, int* g, int* b, uint32_t fboffset, uint32_t num, uint32_t hres, uint32_t fetchbugstate)
{
   uint32_t idx = (fboffset >> 2) + num;

   uint32_t toleftpix = idx - 1;

   uint32_t leftdownpix, maxpix;

   uint32_t leftuppix = idx - hres - 1;

   if (fetchbugstate != 1)
   {
      leftdownpix = idx + hres - 1;
      maxpix = idx +hres + 1;
   }
   else
   {
      leftdownpix = toleftpix;
      maxpix = toleftpix + 2;
   }

   int rend = *r;
   int gend = *g;
   int bend = *b;
   const int* redptr = &vi_restore_table[(rend << 2) & 0x3e0];
   const int* greenptr = &vi_restore_table[(gend << 2) & 0x3e0];
   const int* blueptr = &vi_restore_table[(bend << 2) & 0x3e0];

   uint32_t tempr, tempg, tempb;
   uint32_t pix, addr;

   if (
         (maxpix <= idxlim32)    &&
         (leftuppix <= idxlim32)
      )
   {
      VI_COMPARE32_OPT(leftuppix);
      VI_COMPARE32_OPT(leftuppix + 1);
      VI_COMPARE32_OPT(leftuppix + 2);
      VI_COMPARE32_OPT(leftdownpix);
      VI_COMPARE32_OPT(leftdownpix + 1);
      VI_COMPARE32_OPT(maxpix);
      VI_COMPARE32_OPT(toleftpix);
      VI_COMPARE32_OPT(toleftpix + 2);
   }
   else
   {
      VI_COMPARE32(leftuppix);
      VI_COMPARE32(leftuppix + 1);
      VI_COMPARE32(leftuppix + 2);
      VI_COMPARE32(leftdownpix);
      VI_COMPARE32(leftdownpix + 1);
      VI_COMPARE32(maxpix);
      VI_COMPARE32(toleftpix);
      VI_COMPARE32(toleftpix + 2);
   }

   *r = rend;
   *g = gend;
   *b = bend;
}

void vi_restore_init(void)
{
   int i;
   for (i = 0; i < 0x400; i++)
   {
      if (((i >> 5) & 0x1f) < (i & 0x1f))
         vi_restore_table[i] = 1;
      else if (((i >> 5) & 0x1f) > (i & 0x1f))
         vi_restore_table[i] = -1;
      else
         vi_restore_table[i] = 0;
   }
}

static void vi_fetch_filter16(struct ccvg* res, uint32_t fboffset, uint32_t cur_x, union vi_reg_ctrl ctrl, uint32_t vres, uint32_t fetchstate);
static void vi_fetch_filter32(struct ccvg* res, uint32_t fboffset, uint32_t cur_x, union vi_reg_ctrl ctrl, uint32_t vres, uint32_t fetchstate);

static void (*vi_fetch_filter_func[2])(struct ccvg*, uint32_t, uint32_t, union vi_reg_ctrl, uint32_t, uint32_t) =
{
    vi_fetch_filter16, vi_fetch_filter32
};

void (*vi_fetch_filter_ptr)(struct ccvg*, uint32_t, uint32_t, union vi_reg_ctrl, uint32_t, uint32_t);

static void vi_fetch_filter16(struct ccvg* res, uint32_t fboffset, uint32_t cur_x, union vi_reg_ctrl ctrl, uint32_t hres, uint32_t fetchstate)
{
    int r, g, b;
    uint32_t idx     = (fboffset >> 1) + cur_x;
    uint8_t hval     = 0;
    uint16_t pix     = 0;
    uint32_t cur_cvg = 0;
    if (ctrl.aa_mode <= VI_AA_RESAMP_EXTRA)
    {
        PAIRREAD16(&pix, &hval, idx);
        cur_cvg = ((pix & 1) << 2) | hval;
    }
    else
    {
        RREADIDX16(pix, idx);
        cur_cvg = 7;
    }
    r = GET_HI(pix);
    g = GET_MED(pix);
    b = GET_LOW(pix);

    if (cur_cvg == 7)
    {
        if (ctrl.dither_filter_enable)
            restore_filter16(&r, &g, &b, fboffset, cur_x, hres, fetchstate);
    }
    else
        video_filter16(&r, &g, &b, fboffset, cur_x, hres, cur_cvg, fetchstate);

    res->r = r;
    res->g = g;
    res->b = b;
    res->cvg = cur_cvg;
}

static void vi_fetch_filter32(struct ccvg* res, uint32_t fboffset, uint32_t cur_x, union vi_reg_ctrl ctrl, uint32_t hres, uint32_t fetchstate)
{
    int r, g, b;
    uint32_t cur_cvg;
    uint32_t pix, addr = (fboffset >> 2) + cur_x;

    RREADIDX32(pix, addr);
    if (ctrl.aa_mode <= VI_AA_RESAMP_EXTRA)
        cur_cvg = (pix >> 5) & 7;
    else
        cur_cvg = 7;
    r = (pix >> 24) & 0xff;
    g = (pix >> 16) & 0xff;
    b = (pix >> 8) & 0xff;

    if (cur_cvg == 7)
    {
        if (ctrl.dither_filter_enable)
            restore_filter32(&r, &g, &b, fboffset, cur_x, hres, fetchstate);
    }
    else
        video_filter32(&r, &g, &b, fboffset, cur_x, hres, cur_cvg, fetchstate);

    res->r = r;
    res->g = g;
    res->b = b;
    res->cvg = cur_cvg;
}

// config
static struct core_config* config;

// states
static uint32_t prevvicurrent;
static int32_t emucontrolsvicurrent;
static bool prevserrate;
static bool lowerfield;
static int32_t oldvstart;
static bool prevwasblank;
static int32_t vactivelines;
static bool ispal;
static int32_t minhpass;
static int32_t maxhpass;
static uint32_t x_add;
static uint32_t x_start;
static uint32_t y_add;
static uint32_t y_start;
static int32_t v_sync;
static int32_t vi_width_low;
static uint32_t frame_buffer;
static uint32_t tvfadeoutstate[PRESCALE_HEIGHT];

static enum vi_mode vi_mode;

// prescale buffer
static int32_t prescale[PRESCALE_WIDTH * PRESCALE_HEIGHT];
static uint32_t prescale_ptr;
static int32_t linecount;

// parsed VI registers
static uint32_t** vi_reg_ptr;
static union vi_reg_ctrl ctrl;
static int32_t hres, vres;
static int32_t hres_raw, vres_raw;
static int32_t v_start;
static int32_t h_start;
static int32_t v_current_line;

#ifdef __cplusplus
extern "C" {
#endif

unsigned angrylion_get_vi(void)
{
   if (!config)
      return 0 ;

   return (unsigned)config->vi.mode;
}

void angrylion_set_vi(unsigned value)
{
   if (!config)
      return;

   if (value == 1)
      config->vi.mode = VI_MODE_NORMAL;
   else if (value == 0)
      config->vi.mode = VI_MODE_COLOR;
}

#ifdef __cplusplus
}
#endif

void vi_init(struct core_config* _config)
{
    config               = _config;

    vi_gamma_init();
    vi_restore_init();

    memset(prescale, 0, sizeof(prescale));
    vi_mode              = VI_MODE_NORMAL;

    prevvicurrent        = 0;
    emucontrolsvicurrent = -1;
    prevserrate          = false;
    oldvstart            = 1337;
    prevwasblank         = false;
}

static bool vi_process_start(void)
{
    int32_t i;
    uint32_t final       = 0;

    vi_fetch_filter_ptr  = vi_fetch_filter_func[ctrl.type & 1];

    ispal                = v_sync > (V_SYNC_NTSC + 25);
    h_start             -= (ispal ? 128 : 108);

    bool h_start_clamped = false;

    if (h_start < 0)
    {
       x_start         += (x_add * (-h_start));
       hres            += h_start;

       h_start          = 0;
       h_start_clamped  = true;
    }

    bool isblank        = (ctrl.type & 2) == 0;
    bool validinterlace = !isblank && ctrl.serrate;

    if (validinterlace)
    {
       if (prevserrate && emucontrolsvicurrent < 0)
          emucontrolsvicurrent = v_current_line != prevvicurrent;

       if (emucontrolsvicurrent == 1)
          lowerfield = v_current_line ^ 1;
       else if (!emucontrolsvicurrent)
       {
          if (v_start == oldvstart)
             lowerfield ^= true;
          else
             lowerfield = v_start < oldvstart;
       }

       prevvicurrent = v_current_line;
       oldvstart = v_start;
    }

    prevserrate = validinterlace;

    uint32_t lineshifter = !ctrl.serrate;

    int32_t vstartoffset = ispal ? 44 : 34;
    v_start = (v_start - vstartoffset) / 2;

    if (v_start < 0)
    {
        y_start += (y_add * (uint32_t)(-v_start));
        v_start  = 0;
    }

    bool hres_clamped = false;

    if ((hres + h_start) > PRESCALE_WIDTH)
    {
        hres = PRESCALE_WIDTH - h_start;
        hres_clamped = true;
    }

    if ((vres + v_start) > PRESCALE_HEIGHT)
    {
        vres = PRESCALE_HEIGHT - v_start;
        msg_warning("vres = %d v_start = %d v_video_start = %d", vres, v_start, (*vi_reg_ptr[VI_V_START] >> 16) & 0x3ff);
    }

    int32_t h_end = hres + h_start; // note: the result appears to be different to VI_H_END
    int32_t hrightblank = PRESCALE_WIDTH - h_end;

    vactivelines = v_sync - vstartoffset;
    if (vactivelines > PRESCALE_HEIGHT)
        msg_error("VI_V_SYNC_REG too big");
    if (vactivelines < 0)
        return false;

    vactivelines >>= lineshifter;

    bool validh = hres > 0 && h_start < PRESCALE_WIDTH;

    uint32_t pix = 0;
    uint8_t cur_cvg = 0;

    int32_t *d = 0;

    minhpass = h_start_clamped ? 0 : 8;
    maxhpass =  hres_clamped ? hres : (hres - 7);

    if (isblank && prevwasblank)
        return false;

    prevwasblank = isblank;

    linecount = ctrl.serrate ? (PRESCALE_WIDTH << 1) : PRESCALE_WIDTH;
    prescale_ptr = v_start * linecount + h_start + (lowerfield ? PRESCALE_WIDTH : 0);

    if (isblank)
    {
        // blank signal, clear entire screen buffer
        memset(tvfadeoutstate, 0, PRESCALE_HEIGHT * sizeof(uint32_t));
        memset(prescale, 0, sizeof(prescale));
    }
    else
    {
        // clear left border
        int32_t j;
        if (h_start > 0 && h_start < PRESCALE_WIDTH)
        {
            for (i = 0; i < vactivelines; i++)
                memset(&prescale[i * PRESCALE_WIDTH], 0, h_start * sizeof(uint32_t));
        }

        // clear right border
        if (h_end >= 0 && h_end < PRESCALE_WIDTH)
        {
            for (i = 0; i < vactivelines; i++)
                memset(&prescale[i * PRESCALE_WIDTH + h_end], 0, hrightblank * sizeof(uint32_t));
        }

        // clear top border
        for (i = 0; i < ((v_start << ctrl.serrate) + lowerfield); i++)
        {
            if (tvfadeoutstate[i])
            {
                tvfadeoutstate[i]--;
                if (!tvfadeoutstate[i])
                {
                    if (validh)
                        memset(&prescale[i * PRESCALE_WIDTH + h_start], 0, hres * sizeof(uint32_t));
                    else
                        memset(&prescale[i * PRESCALE_WIDTH], 0, PRESCALE_WIDTH * sizeof(uint32_t));
                }
            }
        }

        if (!ctrl.serrate)
        {
           for(j = 0; j < vres; j++)
           {
              if (validh)
                 tvfadeoutstate[i] = 2;
              else if (tvfadeoutstate[i])
              {
                 tvfadeoutstate[i]--;
                 if (!tvfadeoutstate[i])
                    memset(&prescale[i * PRESCALE_WIDTH], 0, PRESCALE_WIDTH * sizeof(uint32_t));
              }

              i++;
           }
        } else {
            for(j = 0; j < vres; j++)
            {
                if (validh)
                    tvfadeoutstate[i] = 2;
                else if (tvfadeoutstate[i])
                {
                    tvfadeoutstate[i]--;
                    if (!tvfadeoutstate[i])
                        memset(&prescale[i * PRESCALE_WIDTH], 0, PRESCALE_WIDTH * sizeof(uint32_t));
                }

                if (tvfadeoutstate[i + 1])
                {
                   tvfadeoutstate[i + 1]--;
                   if (!tvfadeoutstate[i + 1])
                      if (validh)
                         memset(&prescale[(i + 1) * PRESCALE_WIDTH + h_start], 0, hres * sizeof(uint32_t));
                      else
                         memset(&prescale[(i + 1) * PRESCALE_WIDTH], 0, PRESCALE_WIDTH * sizeof(uint32_t));
                }

                i += 2;
            }
        }

        // clear bottom border
        for (; i < vactivelines; i++)
        {
           if (tvfadeoutstate[i])
              tvfadeoutstate[i]--;
           if (!tvfadeoutstate[i])
              if (validh)
                 memset(&prescale[i * PRESCALE_WIDTH + h_start], 0, hres * sizeof(uint32_t));
              else
                 memset(&prescale[i * PRESCALE_WIDTH], 0, PRESCALE_WIDTH * sizeof(uint32_t));
        }
    }

    return validh;
}

static void vi_process(void)
{
    struct ccvg viaa_array[0xa10 << 1];
    struct ccvg divot_array[0xa10 << 1];

    int32_t cache_marker = 0, cache_next_marker = 0, divot_cache_marker = 0, divot_cache_next_marker = 0;
    int32_t cache_marker_init = (x_start >> 10) - 1;

    struct ccvg *viaa_cache = &viaa_array[0];
    struct ccvg *viaa_cache_next = &viaa_array[0xa10];
    struct ccvg *divot_cache = &divot_array[0];
    struct ccvg *divot_cache_next = &divot_array[0xa10];

    struct ccvg color, nextcolor, scancolor, scannextcolor;

    uint32_t pixels = 0, nextpixels = 0, fetchbugstate = 0;

    int32_t r = 0, g = 0, b = 0;
    int32_t xfrac = 0, yfrac = 0;
    int32_t line_x = 0, next_line_x = 0, prev_line_x = 0, far_line_x = 0;
    int32_t prev_scan_x = 0, scan_x = 0, next_scan_x = 0, far_scan_x = 0;
    int32_t prev_x = 0, cur_x = 0, next_x = 0, far_x = 0;

    bool cache_init = false;

    pixels = 0;

    int32_t y_begin = 0;
    int32_t y_end = vres;
    int32_t y_inc = 1;

    if (config->parallel)
    {
        y_begin = parallel_worker->m_worker_id;
        y_inc   = parallel_worker_num();
    }

    for (int32_t y = y_begin; y < y_end; y += y_inc)
    {
        uint32_t x_offs = x_start;
        uint32_t curry = y_start + y * y_add;
        uint32_t nexty = y_start + (y + 1) * y_add;
        uint32_t prevy = curry >> 10;

        cache_marker = cache_next_marker = cache_marker_init;
        if (ctrl.divot_enable)
            divot_cache_marker = divot_cache_next_marker = cache_marker_init;

        int* d = prescale + prescale_ptr + linecount * y;

        yfrac = (curry >> 5) & 0x1f;
        pixels = vi_width_low * prevy;
        nextpixels = vi_width_low + pixels;

        if (prevy == (nexty >> 10))
            fetchbugstate = 2;
        else
            fetchbugstate >>= 1;

        for (int32_t x = 0; x < hres; x++, x_offs += x_add)
        {
            line_x = x_offs >> 10;
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

            line_x++;
            prev_line_x++;
            next_line_x++;
            far_line_x++;

            xfrac = (x_offs >> 5) & 0x1f;

            int32_t lerping = ctrl.aa_mode != VI_AA_REPLICATE && (xfrac || yfrac);

            if (prev_line_x > cache_marker) {
                vi_fetch_filter_ptr(&viaa_cache[prev_line_x], frame_buffer, prev_x, ctrl, vi_width_low, 0);
                vi_fetch_filter_ptr(&viaa_cache[line_x], frame_buffer, cur_x, ctrl, vi_width_low, 0);
                vi_fetch_filter_ptr(&viaa_cache[next_line_x], frame_buffer, next_x, ctrl, vi_width_low, 0);
                cache_marker = next_line_x;
            } else if (line_x > cache_marker) {
                vi_fetch_filter_ptr(&viaa_cache[line_x], frame_buffer, cur_x, ctrl, vi_width_low, 0);
                vi_fetch_filter_ptr(&viaa_cache[next_line_x], frame_buffer, next_x, ctrl, vi_width_low, 0);
                cache_marker = next_line_x;
            } else if (next_line_x > cache_marker) {
                vi_fetch_filter_ptr(&viaa_cache[next_line_x], frame_buffer, next_x, ctrl, vi_width_low, 0);
                cache_marker = next_line_x;
            }

            if (prev_line_x > cache_next_marker) {
                vi_fetch_filter_ptr(&viaa_cache_next[prev_line_x], frame_buffer, prev_scan_x, ctrl, vi_width_low, fetchbugstate);
                vi_fetch_filter_ptr(&viaa_cache_next[line_x], frame_buffer, scan_x, ctrl, vi_width_low, fetchbugstate);
                vi_fetch_filter_ptr(&viaa_cache_next[next_line_x], frame_buffer, next_scan_x, ctrl, vi_width_low, fetchbugstate);
                cache_next_marker = next_line_x;
            } else if (line_x > cache_next_marker) {
                vi_fetch_filter_ptr(&viaa_cache_next[line_x], frame_buffer, scan_x, ctrl, vi_width_low, fetchbugstate);
                vi_fetch_filter_ptr(&viaa_cache_next[next_line_x], frame_buffer, next_scan_x, ctrl, vi_width_low, fetchbugstate);
                cache_next_marker = next_line_x;
            } else if (next_line_x > cache_next_marker) {
                vi_fetch_filter_ptr(&viaa_cache_next[next_line_x], frame_buffer, next_scan_x, ctrl, vi_width_low, fetchbugstate);
                cache_next_marker = next_line_x;
            }

            if (ctrl.divot_enable)
            {
                if (far_line_x > cache_marker)
                {
                    vi_fetch_filter_ptr(&viaa_cache[far_line_x], frame_buffer, far_x, ctrl, vi_width_low, 0);
                    cache_marker = far_line_x;
                }

                if (far_line_x > cache_next_marker)
                {
                    vi_fetch_filter_ptr(&viaa_cache_next[far_line_x], frame_buffer, far_scan_x, ctrl, vi_width_low, fetchbugstate);
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
                color = viaa_cache[line_x];

            if (lerping)
            {
                if (ctrl.divot_enable)
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

            gamma_filters(&r, &g, &b, ctrl);

            if (x >= minhpass && x < maxhpass)
                d[x] = (r << 16) | (g << 8) | b;
            else
                d[x] = 0;
        }

        if (!cache_init && y_add == 0x400)
        {
           cache_marker = cache_next_marker;
           cache_next_marker = cache_marker_init;

           struct ccvg* tempccvgptr = viaa_cache;
           viaa_cache = viaa_cache_next;
           viaa_cache_next = tempccvgptr;

           if (ctrl.divot_enable)
           {
              divot_cache_marker = divot_cache_next_marker;
              divot_cache_next_marker = cache_marker_init;
              tempccvgptr = divot_cache;
              divot_cache = divot_cache_next;
              divot_cache_next = tempccvgptr;
           }

           cache_init = true;
        }
    }
}

extern "C" uint32_t *blitter_buf_lock;
extern "C" unsigned int screen_width, screen_height;
extern "C" uint32_t screen_pitch;


static INLINE void screen_upload(int32_t* buffer,
      int32_t width, int32_t height, int32_t pitch, int32_t output_height)
{
   int i, cur_line;
	uint32_t * buf = (uint32_t*)buffer;
	for (i = 0; i <height; i++)
		memcpy(&blitter_buf_lock[i * width], &buf[i * width], width * 4);


  screen_width=width;
  screen_height=height;
  screen_pitch=width*4;


}


static void vi_process_end(void)
{
    int32_t width;
    int32_t height;
    int32_t output_height;
    int32_t pitch   = PRESCALE_WIDTH;
    int32_t* buffer = prescale;

    if (config->vi.overscan)
    {
        // use entire prescale buffer
        width = PRESCALE_WIDTH;
        height = (ispal ? V_RES_PAL : V_RES_NTSC) >> !ctrl.serrate;
        output_height = V_RES_NTSC;
    }
    else
    {
        // crop away overscan area from prescale
        width = maxhpass - minhpass;
        height = vres << ctrl.serrate;
        output_height = (vres << 1) * V_SYNC_NTSC / v_sync;
        int32_t x = h_start + minhpass;
        int32_t y = (v_start + (emucontrolsvicurrent ? lowerfield : 0)) << ctrl.serrate;
        buffer += x + y * pitch;
    }

    if (config->vi.widescreen)
        output_height = output_height * 9 / 16;

    screen_upload(buffer, width, height, pitch, output_height);
}

static bool vi_process_start_fast(void)
{
    // note: this is probably a very, very crude method to get the frame size,
    // but should hopefully work most of the time
    hres_raw = x_add * hres / 1024;
    vres_raw = y_add * vres / 1024;

    // skip invalid frame sizes
    if (hres_raw <= 0 || vres_raw <= 0)
        return false;

    // drop every other interlaced frame to avoid "wobbly" output due to the
    // vertical offset
    // TODO: completely skip rendering these frames in unfiltered to improve
    // performance?
    if (v_current_line)
        return false;

    // skip blank/invalid modes
    if (!(ctrl.type & 2))
        return false;

    return true;
}

static void vi_process_fast(void)
{
   int32_t y_begin = 0;
   int32_t y_end = vres_raw;
   int32_t y_inc = 1;

   if (config->parallel)
   {
      y_begin = parallel_worker->m_worker_id;
      y_inc = parallel_worker_num();
   }

   for (int32_t y = y_begin; y < y_end; y += y_inc)
   {
      int32_t line = y * vi_width_low;
      uint32_t* dst = (uint32_t*)(prescale + y * hres_raw);

      for (int32_t x = 0; x < hres_raw; x++)
      {
         uint32_t r, g, b;

         switch (config->vi.mode)
         {
            case VI_MODE_COLOR:
               switch (ctrl.type)
               {
                  case VI_TYPE_RGBA5551:
                     {
                        uint16_t pix;
                        uint32_t in = (frame_buffer >> 1) + line + x;
                        RREADIDX16(pix, in);
                        r = ((pix >> 11) & 0x1f) << 3;
                        g = ((pix >>  6) & 0x1f) << 3;
                        b = ((pix >>  1) & 0x1f) << 3;
                     }
                     break;
                  case VI_TYPE_RGBA8888:
                     {
                        uint32_t pix;
                        uint32_t in  = (frame_buffer >> 2) + line + x;
                        RREADIDX32(pix, in);
                        r = (pix >> 24) & 0xff;
                        g = (pix >> 16) & 0xff;
                        b = (pix >>  8) & 0xff;
                     }
                     break;

                  default:
                     assert(false);
               }
               break;

            case VI_MODE_DEPTH:
               {
                  uint16_t pix;
                  uint32_t in = ((rdp_get_zb_address() >> 1) + line + x) >> 8;
                  RREADIDX16(pix,in);
                  r = g = b = pix;
               }
               break;

            case VI_MODE_COVERAGE:
               {
                  // TODO: incorrect for RGBA8888?
                  uint8_t hval = 0;
                  uint16_t pix = 0;
                  uint32_t in  = (frame_buffer >> 1) + line + x;
                  PAIRREAD16(&pix, &hval, in);
                  r = g = b = (((pix & 1) << 2) | hval) << 5;
               }
               break;
            default:
               assert(false);
         }

         gamma_filters((int32_t*)&r, (int32_t*)&g, (int32_t*)&b, ctrl);

         dst[x] = (r << 16) | (g << 8) | b;
      }
   }
}

static void vi_process_end_fast(void)
{
   int32_t filtered_height = (vres << 1) * V_SYNC_NTSC / v_sync;
   int32_t output_height = hres_raw * filtered_height / hres;

   if (config->vi.widescreen)
      output_height = output_height * 9 / 16;

   screen_upload(prescale, hres_raw, vres_raw, hres_raw, output_height);
}

void vi_update(void)
{
    // clear buffer after switching VI modes to make sure that black borders are
    // actually black and don't contain garbage
    if (config->vi.mode != vi_mode)
    {
        memset(prescale, 0, sizeof(prescale));
        vi_mode = config->vi.mode;
    }

    // check for configuration errors
    if (config->vi.mode >= VI_MODE_NUM)
        msg_error("Invalid VI mode: %d", config->vi.mode);

    /* parse and check some common registers */
    vi_reg_ptr = (uint32_t**)&gfx_info.VI_STATUS_REG;

    v_start = (*vi_reg_ptr[VI_V_START] >> 16) & 0x3ff;
    h_start = (*vi_reg_ptr[VI_H_START] >> 16) & 0x3ff;

    int32_t v_end = *vi_reg_ptr[VI_V_START] & 0x3ff;
    int32_t h_end = *vi_reg_ptr[VI_H_START] & 0x3ff;

    hres =  h_end - h_start;
    vres = (v_end - v_start) >> 1; // vertical is measured in half-lines

    if (hres <= 0 || vres <= 0)
       return;

    x_add = *vi_reg_ptr[VI_X_SCALE] & 0xfff;
    x_start = (*vi_reg_ptr[VI_X_SCALE] >> 16) & 0xfff;

    y_add = *vi_reg_ptr[VI_Y_SCALE] & 0xfff;
    y_start = (*vi_reg_ptr[VI_Y_SCALE] >> 16) & 0xfff;

    v_sync = *vi_reg_ptr[VI_V_SYNC] & 0x3ff;
    v_current_line = *vi_reg_ptr[VI_V_CURRENT_LINE] & 1;

    vi_width_low = *vi_reg_ptr[VI_WIDTH] & 0xfff;
    frame_buffer = *vi_reg_ptr[VI_ORIGIN] & 0xffffff;

    // cancel if the frame buffer contains no valid address
    if (!frame_buffer)
        return;

    ctrl.raw = *vi_reg_ptr[VI_STATUS];

    // check for unexpected VI type bits set
    if (ctrl.type & ~3)
        msg_error("Unknown framebuffer format %d", ctrl.type);

    // warn about AA glitches in certain cases
    static bool nolerp;
    if (ctrl.aa_mode == VI_AA_REPLICATE && ctrl.type == VI_TYPE_RGBA5551 &&
        h_start < 0x80 && x_add <= 0x200 && !nolerp) {
        msg_warning("vi_update: Disabling VI interpolation in 16-bit color "
                    "modes causes glitches on hardware if h_start is less than "
                    "128 pixels and x_scale is less or equal to 0x200.");
        nolerp = true;
    }

    // check for the dangerous vbus_clock_enable flag. it was introduced to
    // configure Ultra 64 prototypes and enabling it on final hardware will
    // enable two output drivers on the same bus at the same time
    static bool vbusclock;
    if (ctrl.vbus_clock_enable && !vbusclock)
    {
        msg_warning("vi_update: vbus_clock_enable bit set in VI_CONTROL_REG "
                    "register. Never run this code on your N64! It's rumored "
                    "that turning this bit on will result in permanent damage "
                    "to the hardware! Emulation will now continue.");
        vbusclock = true;
    }

    // select filter functions based on config
    bool (*vi_process_start_ptr)(void);
    void (*vi_process_ptr)(void);
    void (*vi_process_end_ptr)(void);

    if (config->vi.mode == VI_MODE_NORMAL)
    {
        vi_process_start_ptr = vi_process_start;
        vi_process_ptr = vi_process;
        vi_process_end_ptr = vi_process_end;
    }
    else
    {
        vi_process_start_ptr = vi_process_start_fast;
        vi_process_ptr = vi_process_fast;
        vi_process_end_ptr = vi_process_end_fast;
    }

    // try to init VI frame, abort if there's nothing to display
    if (!vi_process_start_ptr())
        return;

    // run filter update in parallel if enabled
    if (config->parallel)
        parallel_run(vi_process_ptr);
    else
        vi_process_ptr();

    // finish and send buffer to screen
    vi_process_end_ptr();

    /* render frame to screen */
    screen_swap();
}

void vi_close(void)
{
}
