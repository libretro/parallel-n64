/* Rice's blender (mupen64plus-video-rice/src/Blender.c): what the RDP's
 * blender asks, as the renderer's blend functions, through a renderer of
 * its own here recording what it is asked:
 *
 *   * fill: blending off; copy: the source as it is;
 *   * 2 cycles: with force_bl and z_cmp, source alpha; otherwise by both
 *     cycles' blender modes (the pass-through ones with alpha_cvg_sel,
 *     and the alpha test from gDP's alpha compare), the rest by whether
 *     the second cycle passes the first through;
 *   * 1 cycle: with force_bl and z_cmp (not fog by shade alpha) or for
 *     Command & Conquer with force_bl, source alpha; otherwise by the
 *     first cycle's mode (Mario Tennis's own for the opaque one), the rest
 *     source alpha with the alpha test on;
 *   * normal: source alpha; disabled: the source as it is. */
#include <stdio.h>
#include <string.h>

#include "Blender.h"
#include "Config.h"
#include "RenderBase.h"

static int failures;
#define CHECK(c) do { if (!(c)) { printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

/* ---- what Rice gives it ---- */
ALIGN(16, RDP_Options gRDP);
struct gDPInfo gDP;
GlobalOptions options;

/* ---- a renderer of its own ---- */
static uint32_t src, dst;
static int funcs, on, alphatest;   /* on: -1 not asked, 0 off, 1 on; alphatest the same */
static void rec_func(CBlender *b, uint32_t s, uint32_t d) { (void)b; src = s; dst = d; funcs++; }
static void rec_enable(CBlender *b) { (void)b; on = 1; }
static void rec_disable(CBlender *b) { (void)b; on = 0; }
static const CBlenderOps recording = { rec_func, rec_enable, rec_disable };
static CBlender blender = { &recording };

void RiceRender_SetAlphaTestEnable(int enable) { alphatest = enable != 0; }

static void reset(void)
{
   src = dst = 0;
   funcs = 0;
   on = alphatest = -1;
}

static void mode(uint32_t cycle, uint32_t bl, int force_bl, int z_cmp, int cvg_sel, int cvg_x_alpha)
{
   memset(&gRDP.otherMode, 0, sizeof(gRDP.otherMode));
   gRDP.otherMode.cycle_type = cycle;
   gRDP.otherMode.blender = bl;
   gRDP.otherMode.force_bl = force_bl;
   gRDP.otherMode.z_cmp = z_cmp;
   gRDP.otherMode.alpha_cvg_sel = cvg_sel;
   gRDP.otherMode.cvg_x_alpha = cvg_x_alpha;
   reset();
   Blender_InitBlenderMode(&blender);
}

#define ASKED(s, d, o) (funcs == 1 && src == (s) && dst == (d) && on == (o))
#define ASK2(pair, o) ASKED(pair, o)   /* the pair a macro of two */
#define SRCA BLEND_SRCALPHA, BLEND_INVSRCALPHA
#define AS_IS BLEND_ONE, BLEND_ZERO

static void fill_copy(void)
{
   mode(G_CYC_FILL, 0x0c08, 1, 1, 1, 1);
   CHECK(funcs == 0 && on == 0 && alphatest == -1);
   mode(G_CYC_COPY, 0x0040, 1, 1, 0, 0);
   CHECK(ASK2(AS_IS, 1) && alphatest == -1);
}

static void two_cycles(void)
{
   /* force_bl and z_cmp: source alpha, whatever the modes */
   mode(G_CYC_2CYCLE, 0x0c08 + 0x0302, 1, 1, 0, 0);
   CHECK(ASK2(SRCA, 1) && alphatest == -1);
   mode(G_CYC_2CYCLE, 0x0c08 + 0x0302, 1, 0, 0, 0);    /* force_bl alone: not */
   CHECK(ASK2(AS_IS, 0));
   mode(G_CYC_2CYCLE, 0x0c08 + 0x0302, 0, 1, 0, 0);    /* z_cmp alone: not */
   CHECK(ASK2(AS_IS, 0));

   /* passed through: blending with alpha_cvg_sel, the alpha test by gDP's alpha compare */
   gDP.otherMode.l = 1 << G_MDSFT_ALPHACOMPARE;
   mode(G_CYC_2CYCLE, 0x0c08 + 0x0302, 0, 0, 1, 0);
   CHECK(ASK2(AS_IS, 1) && alphatest == 1);
   gDP.otherMode.l = 3 << G_MDSFT_ALPHACOMPARE;
   mode(G_CYC_2CYCLE, 0xc400 + 0x0302, 0, 0, 0, 0);
   CHECK(ASK2(AS_IS, 0) && alphatest == 0);
   gDP.otherMode.l = 2 << G_MDSFT_ALPHACOMPARE;
   mode(G_CYC_2CYCLE, 0x0c08 + 0x0302, 0, 0, 0, 0);
   CHECK(ASK2(AS_IS, 0) && alphatest == 0);
   gDP.otherMode.l = 0;

   /* pass then opaque: source alpha with coverage times alpha and alpha_cvg_sel */
   mode(G_CYC_2CYCLE, 0x0c08 + 0x0011, 0, 0, 1, 1);
   CHECK(ASK2(SRCA, 1));
   mode(G_CYC_2CYCLE, 0x0c08 + 0x0011, 0, 0, 1, 0);
   CHECK(ASK2(AS_IS, 1));
   mode(G_CYC_2CYCLE, 0x0c08 + 0x0011, 0, 0, 0, 1);
   CHECK(ASK2(AS_IS, 1));

   /* translucent second cycles, and fog over memory passed through */
   mode(G_CYC_2CYCLE, 0x0c08 + 0x0010, 0, 0, 0, 0);
   CHECK(ASK2(SRCA, 1) && alphatest == -1);
   mode(G_CYC_2CYCLE, 0xc800 + 0x0010, 0, 0, 0, 0);
   CHECK(ASK2(SRCA, 1));
   mode(G_CYC_2CYCLE, 0xc400 + 0x0010, 0, 0, 0, 0);
   CHECK(ASK2(SRCA, 1));
   mode(G_CYC_2CYCLE, 0x04c0 + 0x0302, 0, 0, 0, 0);
   CHECK(ASK2(SRCA, 1));
   mode(G_CYC_2CYCLE, 0x0040 + 0x0010, 0, 0, 0, 0);
   CHECK(ASK2(SRCA, 1));
   mode(G_CYC_2CYCLE, 0x8400 + 0x0010, 0, 0, 0, 0);
   CHECK(ASK2(SRCA, 1));
   mode(G_CYC_2CYCLE, 0x0040 + 0x0110, 0, 0, 0, 0);
   CHECK(ASK2(SRCA, 1));
   mode(G_CYC_2CYCLE, 0x0c08 + 0x0110, 0, 0, 0, 0);
   CHECK(ASK2(SRCA, 1));

   /* opaque ends: the source as it is */
   mode(G_CYC_2CYCLE, 0x04c0 + 0x0011, 0, 0, 0, 0);
   CHECK(ASK2(AS_IS, 1));
   mode(G_CYC_2CYCLE, 0xc400 + 0x0011, 0, 0, 0, 0);
   CHECK(ASK2(AS_IS, 1));
   mode(G_CYC_2CYCLE, 0xc800 + 0x0011, 0, 0, 0, 0);
   CHECK(ASK2(AS_IS, 1));
   mode(G_CYC_2CYCLE, 0x8400 + 0x0011, 0, 0, 0, 0);
   CHECK(ASK2(AS_IS, 1));
   mode(G_CYC_2CYCLE, 0xc800 + 0x0000, 0, 0, 0, 0);
   CHECK(ASK2(AS_IS, 1));
   mode(G_CYC_2CYCLE, 0x0000 + 0x0011, 0, 0, 0, 0);
   CHECK(ASK2(AS_IS, 1));
   mode(G_CYC_2CYCLE, 0xcc08 + 0x0000, 0, 0, 0, 0);
   CHECK(ASK2(AS_IS, 1));
   mode(G_CYC_2CYCLE, 0xc800 + 0x0302, 0, 0, 0, 0);
   CHECK(ASK2(AS_IS, 1));
   mode(G_CYC_2CYCLE, 0xc000 + 0x0302, 0, 0, 0, 0);
   CHECK(ASK2(AS_IS, 1));

   /* the two of their own */
   mode(G_CYC_2CYCLE, 0xc800 + 0x0301, 0, 0, 0, 0);
   CHECK(ASKED(BLEND_SRCALPHA, BLEND_ZERO, 1));
   mode(G_CYC_2CYCLE, 0x0c08 + 0x1111, 0, 0, 0, 0);
   CHECK(ASKED(BLEND_ZERO, BLEND_DESTALPHA, 1));

   /* the rest: as it is when the second cycle passes the first, source alpha otherwise */
   mode(G_CYC_2CYCLE, 0x4c40 + 0x0302, 0, 0, 0, 0);
   CHECK(ASK2(AS_IS, 1) && alphatest == -1);
   mode(G_CYC_2CYCLE, 0x4c40 + 0x1310, 0, 0, 0, 0);
   CHECK(ASK2(SRCA, 1) && alphatest == -1);
}

static void one_cycle(void)
{
   /* force_bl and z_cmp: source alpha, but not for fog by shade alpha */
   mode(G_CYC_1CYCLE, 0x0c08, 1, 1, 0, 0);
   CHECK(ASK2(SRCA, 1));
   mode(G_CYC_1CYCLE, 0xc800, 1, 1, 0, 0);
   CHECK(ASK2(AS_IS, 1));
   mode(G_CYC_1CYCLE, 0x0c08, 0, 1, 0, 0);
   CHECK(ASK2(AS_IS, 0));
   /* Command & Conquer: with force_bl */
   options.enableHackForGames = HACK_FOR_COMMANDCONQUER;
   mode(G_CYC_1CYCLE, 0x0c08, 1, 0, 0, 0);
   CHECK(ASK2(SRCA, 1));
   mode(G_CYC_1CYCLE, 0x0c08, 0, 0, 0, 0);
   CHECK(ASK2(AS_IS, 0));
   options.enableHackForGames = NO_HACK_FOR_GAME;
   mode(G_CYC_1CYCLE, 0x0c08, 1, 0, 0, 0);
   CHECK(ASK2(AS_IS, 0));

   /* by the first cycle's mode, the second's not looked at */
   mode(G_CYC_1CYCLE, 0x0040 | 0x3333, 0, 0, 0, 0);
   CHECK(ASK2(SRCA, 1) && alphatest == -1);
   mode(G_CYC_1CYCLE, 0x8040, 0, 0, 0, 0);
   CHECK(ASK2(SRCA, 1));
   mode(G_CYC_1CYCLE, 0xc440, 0, 0, 0, 0);
   CHECK(ASK2(SRCA, 1));
   mode(G_CYC_1CYCLE, 0x0440, 0, 0, 0, 0);
   CHECK(ASK2(SRCA, 1));
   mode(G_CYC_1CYCLE, 0x8c88, 0, 0, 0, 0);
   CHECK(ASK2(SRCA, 1));
   mode(G_CYC_1CYCLE, 0x00c0, 0, 0, 0, 0);
   CHECK(ASK2(SRCA, 1));
   mode(G_CYC_1CYCLE, 0x4044, 0, 0, 0, 0);
   CHECK(ASKED(BLEND_ZERO, BLEND_DESTALPHA, 1));
   mode(G_CYC_1CYCLE, 0x0c08, 0, 0, 1, 0);
   CHECK(ASK2(AS_IS, 1) && alphatest == -1);
   mode(G_CYC_1CYCLE, 0x0c08, 0, 0, 0, 0);
   CHECK(ASK2(AS_IS, 0));
   mode(G_CYC_1CYCLE, 0x0044, 0, 0, 0, 0);
   CHECK(ASK2(AS_IS, 1));
   options.enableHackForGames = HACK_FOR_MARIO_TENNIS;
   mode(G_CYC_1CYCLE, 0x0044, 0, 0, 0, 0);
   CHECK(ASK2(SRCA, 1));
   options.enableHackForGames = NO_HACK_FOR_GAME;
   mode(G_CYC_1CYCLE, 0x0000, 0, 0, 0, 0);
   CHECK(ASK2(AS_IS, 1));
   mode(G_CYC_1CYCLE, 0xc800, 0, 0, 0, 0);
   CHECK(ASK2(AS_IS, 1));
   mode(G_CYC_1CYCLE, 0x44c0, 0, 0, 0, 0);
   CHECK(ASK2(AS_IS, 1));
   mode(G_CYC_1CYCLE, 0x8400, 0, 0, 0, 0);
   CHECK(ASK2(AS_IS, 1));
   mode(G_CYC_1CYCLE, 0xc400, 0, 0, 0, 0);
   CHECK(ASKED(BLEND_INVSRCALPHA, BLEND_ZERO, 1));
   mode(G_CYC_1CYCLE, 0x0c48, 0, 0, 0, 0);
   CHECK(ASKED(BLEND_ZERO, BLEND_ONE, 1));
   mode(G_CYC_1CYCLE, 0xcc48, 0, 0, 0, 0);
   CHECK(ASKED(BLEND_ZERO, BLEND_ONE, 1));
   mode(G_CYC_1CYCLE, 0x4c40, 0, 0, 0, 0);
   CHECK(ASKED(BLEND_ZERO, BLEND_ONE, 1));
   /* the rest: source alpha, the alpha test on */
   mode(G_CYC_1CYCLE, 0x8000, 0, 0, 0, 0);
   CHECK(ASK2(SRCA, 1) && alphatest == 1);
   mode(G_CYC_1CYCLE, 0xc000, 0, 0, 0, 0);
   CHECK(ASK2(SRCA, 1) && alphatest == 1);
}

static void normal_disabled(void)
{
   reset();
   Blender_NormalAlphaBlender(&blender);
   CHECK(ASK2(SRCA, 1) && alphatest == -1);
   reset();
   Blender_DisableAlphaBlender(&blender);
   CHECK(ASK2(AS_IS, 1) && alphatest == -1);
}

int main(void)
{
   fill_copy();
   two_cycles();
   one_cycle();
   normal_disabled();
   printf("rice blender: %s\n", failures ? "FAILED" : "ok");
   return failures ? 1 : 0;
}
