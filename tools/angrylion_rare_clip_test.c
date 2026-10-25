/* angrylion_rare_clip_test -- Rare's GBI 1 clip overlay is recognised by
 * its code.
 *
 * GoldenEye 007's microcode seeds its clip list with the triangle
 * reversed, rotates each plane's output to start where the polygon comes
 * back inside, fans ascending pairs into the writer reversed, and divides
 * with the GBI 1 refinement. Clipped the stock way, the background quads
 * of its menus and levels were cut into a different set of triangles than
 * the LLE RSP draws (48 of the 66 in a menu frame matched; all 66 do
 * now). The probe finds the overlay's list seeding, and the divide's
 * opening and closing vector ops; a text with only the seeding gets the
 * fan without the divide, and a text without it neither.
 *
 * Built by `make check-angrylion` (top-level Makefile). Exit status 0 on pass.
 */
#include "rdp_emit_f3d.h"
#include "rdp_emit_frontend.h"
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
#define TEXT       0x2000u

static unsigned char rdram[RDRAM_SIZE];
static int s_failures;

#define CHECK(cond, what) \
    do { if (!(cond)) { printf("FAIL: %s\n", what); s_failures++; } } while (0)

static void put32(unsigned int addr, unsigned int v)
{
    rdram[(addr + 0) ^ 3u] = (unsigned char)(v >> 24);
    rdram[(addr + 1) ^ 3u] = (unsigned char)(v >> 16);
    rdram[(addr + 2) ^ 3u] = (unsigned char)(v >> 8);
    rdram[(addr + 3) ^ 3u] = (unsigned char)v;
}

int main(void)
{
    /* GoldenEye's list seeding at text + 0x101c, list base 0x0db8 */
    static const unsigned int seed[7] = {
        0xa4030db8u, 0xa4020dbau, 0xa4010dbcu, 0xa4000dbeu,
        0x34070db8u, 0x341e0db8u, 0x3406000cu
    };
    static const unsigned int divide[8] = {
        0x4b7d5ef2u, 0x4b7c5eb1u, 0, 0, 0, 0x4b5fd686u, 0x4b5fdecfu, 0x4b1ffe8eu
    };
    unsigned int i;

    CHECK(f3d_ucode_rare_clip(rdram, RDRAM_SIZE, TEXT) == 0,
          "a text without the overlay is not Rare's");
    for (i = 0; i < 7; i++)
        put32(TEXT + 0x101cu + i * 4u, seed[i]);
    CHECK(f3d_ucode_rare_clip(rdram, RDRAM_SIZE, TEXT) == 1,
          "the overlay's list seeding selects Rare's fan");
    for (i = 0; i < 8; i++)
        put32(TEXT + 0xf78u + i * 4u, divide[i]);
    CHECK(f3d_ucode_rare_clip(rdram, RDRAM_SIZE, TEXT) == 2,
          "with the GBI 1 divide, both");
    put32(TEXT + 0x101cu + 6u * 4u, 0x3406000du);
    CHECK(f3d_ucode_rare_clip(rdram, RDRAM_SIZE, TEXT) == 0,
          "a different list length is not Rare's");
    CHECK(f3d_ucode_rare_clip(rdram, RDRAM_SIZE, RDRAM_SIZE - 0x100u) == 0,
          "a text too close to the end of RDRAM is not read past it");

    if (s_failures)
    {
        printf("angrylion_rare_clip_test: %d failure(s)\n", s_failures);
        return 1;
    }
    printf("angrylion_rare_clip_test: all checks passed\n");
    return 0;
}
