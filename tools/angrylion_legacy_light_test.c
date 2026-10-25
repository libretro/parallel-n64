/* angrylion_legacy_light_test -- GoldenEye / Perfect Dark vertex lighting
 * follows their microcode's rounding.
 *
 * The Rare Fast3D builds light differently from the F3DEX family:
 *
 *  - the directional dot rounds each axis product to 16 bits (+0x8000)
 *    and saturates the running sum, and each light's contribution is
 *    clamped at zero before it is added to the ambient;
 *  - Perfect Dark's build also transforms and normalises the light
 *    direction with its own sequence (s8 * 128 inputs, a two-step fold,
 *    the reciprocal square root scaled by 64 and the result clamped to
 *    +-8191 before the final shift).
 *
 * The microcode text tells the two apart: GoldenEye's lighting loop
 * starts 4a052180 4a6630d0 4ac61990 4a8639c0 and Perfect Dark's
 * normaliser 4b9f2940 4a856186 4aa5698e 4ac5718e. The HLE used the stock
 * transform and dot for both: for the bright face below that is 7 levels
 * too bright, and some normalised directions land one step off. The
 * expected values are pinned from the legacy path that brought Perfect
 * Dark's lit scenes in line with cxd4 frame captures (one frame went
 * from 1.6% of pixels differing to 0.11%).
 *
 * Built by `make check-angrylion` (top-level Makefile). Exit status 0 on pass.
 */
#include "rdp_emit_frontend.h"
#include "rdp_emit_rsp.h"
#include "rdp_emit_f3d.h"
#include <stdio.h>
#include <string.h>

int s2dex1_ucode_match(const unsigned char *r, unsigned int size,
                       unsigned int ud, unsigned int ut)
{
    (void)r; (void)size; (void)ud; (void)ut;
    return 0;
}
void gsp_detect_ucode_params(GSPState *st, const unsigned char *r,
                             unsigned int size, unsigned int ud,
                             unsigned int ut)
{
    (void)st; (void)r; (void)size; (void)ud; (void)ut;
}

#define RDRAM_SIZE (64u * 1024u)
#define UCODE      0x4000u

static unsigned char rdram[RDRAM_SIZE];
static int s_failures;

#define CHECK(cond, what) \
    do { if (!(cond)) { printf("FAIL: %s\n", what); s_failures++; } } while (0)

static void put4(unsigned int at, unsigned int a, unsigned int b,
                 unsigned int c, unsigned int d)
{
    unsigned int w[4];
    w[0] = a; w[1] = b; w[2] = c; w[3] = d;
    memcpy(rdram + at, w, sizeof(w));
}

int main(void)
{
    /* a rotated, scaled modelview: the transform has fractions to round */
    static const int32_t mv[4][4] = {
        {  0x4000, 0x2000, 0,      0 },
        { -0x2000, 0x4000, 0,      0 },
        {  0,      0,      0x5000, 0 },
        {  0,      0,      0,      0x10000 }
    };
    static const int32_t ident[4][4] = {
        { 0x10000, 0, 0, 0 }, { 0, 0x10000, 0, 0 },
        { 0, 0, 0x10000, 0 }, { 0, 0, 0, 0x10000 }
    };
    int32_t dir[3], out[3], ldir[1][3], n[3];
    int32_t amb[3] = { 40, 40, 40 };
    int32_t rgb[1][3] = { { 200, 180, 160 } };

    f3d_set_rdram(rdram);
    f3d_set_rdram_size(RDRAM_SIZE);

    /* detection */
    memset(rdram, 0, sizeof(rdram));
    CHECK(f3d_ucode_legacy_lighting(rdram, RDRAM_SIZE, UCODE) == 0,
          "plain microcode text detected as legacy lighting");
    put4(UCODE + 0x400u, 0x4a052180u, 0x4a6630d0u, 0x4ac61990u, 0x4a8639c0u);
    CHECK(f3d_ucode_legacy_lighting(rdram, RDRAM_SIZE, UCODE) == 1,
          "GoldenEye lighting loop not detected");
    put4(UCODE + 0x800u, 0x4b9f2940u, 0x4a856186u, 0x4aa5698eu, 0x4ac5718eu);
    CHECK(f3d_ucode_legacy_lighting(rdram, RDRAM_SIZE, UCODE) == 2,
          "Perfect Dark normaliser not detected");

    /* Perfect Dark's direction transform: z rounds down one step where
     * the stock normaliser gives 105. */
    dir[0] = 56; dir[1] = 77; dir[2] = 122;
    rsp_light_dir_xfrm_legacy(mv, dir, out);
    CHECK(out[0] == 65 && out[1] == 33 && out[2] == 104,
          "legacy direction transform of (56,77,122)");
    dir[0] = 83; dir[1] = 94; dir[2] = -115;
    rsp_light_dir_xfrm_legacy(mv, dir, out);
    CHECK(out[0] == 82 && out[1] == 33 && out[2] == -92,
          "legacy direction transform of (83,94,-115)");

    /* The directional dot: a bright face saturates per light in the
     * legacy sum (232,213,193) where the stock dot reached 239,219,199. */
    dir[0] = 99; dir[1] = 66; dir[2] = -7;
    rsp_light_dir_xfrm_legacy(ident, dir, ldir[0]);
    n[0] = 113; n[1] = 118; n[2] = 79;
    rsp_light_vtx_legacy(n, amb, (const int32_t (*)[3])rgb,
                         (const int32_t (*)[3])ldir, 1, out);
    CHECK(out[0] == 232 && out[1] == 213 && out[2] == 193,
          "legacy lighting of a bright face");

    /* a face turned away keeps the ambient only */
    n[0] = -113; n[1] = -118; n[2] = -79;
    rsp_light_vtx_legacy(n, amb, (const int32_t (*)[3])rgb,
                         (const int32_t (*)[3])ldir, 1, out);
    CHECK(out[0] == 40 && out[1] == 40 && out[2] == 40,
          "legacy lighting of a face turned away");

    if (s_failures)
        return 1;
    printf("angrylion_legacy_light_test: all checks passed\n");
    return 0;
}
