/* angrylion_texrecip_test -- F3DEX2 builds with the DIV-table texture
 * normalizer get it in the HLE triangle writer.
 *
 * The FIFO 2.05 builds of Beetle Adventure Racing, Donkey Kong 64 and
 * Knife Edge scale each vertex's texture coordinates by its 1/w times
 * the reciprocal of the largest 1/w, the reciprocal taken from the DIV
 * table (VRCPH/VRCPL), where the other builds form the exact quotient.
 * The two differ in the low bits of every perspective texture gradient.
 *
 *  - a task whose microcode text carries the eleven-instruction
 *    normalizer turns the path on, one without it leaves it off;
 *  - with the path on, the W base of a triangle whose top vertex has the
 *    largest 1/w is 0x7fff * (1/w * rcp(1/w)), not 0x7fff * 0x8000.
 *
 * Built by `make check-angrylion` (top-level Makefile). Exit status 0 on pass.
 */
#include "rdp_emit_f3dex2.h"
#include "rdp_emit_frontend.h"
#include "rdp_emit_rsp.h"
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
#define TEXT       0x1000u

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

/* the normalizer as the 2.05 builds carry it (Beetle Adventure Racing) */
static const unsigned int k_normalizer[11] = {
    0x4b1b4772u, 0x4b3b42b1u, 0x4a6d0390u, 0x4b0046f2u, 0x4bff05aau,
    0x4b0a6f45u, 0x4b0a774cu, 0xc8361005u, 0x4b1b738eu, 0xc8561405u,
    0x4b1b6b4fu
};

static void test_detection(void)
{
    unsigned int i;

    f3dex2_set_rdram(rdram);
    f3dex2_set_rdram_size(RDRAM_SIZE);

    memset(rdram, 0, sizeof(rdram));
    put32(TEXT + 4, 0xc81f201bu);
    for (i = 0; i < 11; i++)
        put32(TEXT + 0x200 + i * 4, k_normalizer[i]);
    f3dex2_set_task_ucode(rdram, TEXT);
    CHECK(rsp_tri_texrecip() == 1, "a text with the normalizer selects it");

    put32(TEXT + 0x200 + 10 * 4, 0x4b1b6b4eu);   /* one operand changed */
    f3dex2_set_task_ucode(rdram, TEXT);
    CHECK(rsp_tri_texrecip() == 0, "a text without the whole normalizer does not");
}

static void test_w_base(void)
{
    RspTriVtx v[3];
    int32_t off[64], on[64];
    int n_off, n_on;
    uint32_t wnorm, w;

    memset(v, 0, sizeof(v));
    v[0].x = 40 * 4;  v[0].y = 40 * 4;  v[0].s = 0x100; v[0].t = 0x080; v[0].invw = 0x00012345;
    v[1].x = 200 * 4; v[1].y = 60 * 4;  v[1].s = 0x900; v[1].t = 0x100; v[1].invw = 0x0000f000;
    v[2].x = 60 * 4;  v[2].y = 180 * 4; v[2].s = 0x200; v[2].t = 0x700; v[2].invw = 0x00010800;

    rsp_tri_set_texrecip(0);
    n_off = rsp_tri_write(off, &v[0], &v[1], &v[2], 1, 0, 0, 0, 0, 0,
                          0x4000, 8, 0xffff, 0x1cc);
    rsp_tri_set_texrecip(1);
    n_on = rsp_tri_write(on, &v[0], &v[1], &v[2], 1, 0, 0, 0, 0, 0,
                         0x4000, 8, 0xffff, 0x1cc);
    rsp_tri_set_texrecip(0);

    CHECK(n_off == 24 && n_on == 24, "a textured triangle is 24 words");
    if (n_off != 24 || n_on != 24)
        return;

    /* texture block: word 8 S|T int, 9 W int|-, 12 S|T frac, 13 W frac|- */
    CHECK(((uint32_t)off[9] >> 16) == 0x3fffu && ((uint32_t)off[13] >> 16) == 0x8000u,
          "the exact quotient gives the top vertex W = 0x7fff * 0x8000");

    wnorm = (uint32_t)(((uint64_t)0x00012345u * (uint32_t)rsp_rcp32(0x00012345)) >> 16);
    w = 0x7fffu * wnorm;
    CHECK(((uint32_t)on[9] >> 16) == (w >> 16) && ((uint32_t)on[13] >> 16) == (w & 0xffffu),
          "the DIV-table normalizer gives W = 0x7fff * (1/w * rcp(1/w))");
    CHECK(off[8] == on[8], "S and T bases are unchanged at the top vertex");
}

int main(void)
{
    test_detection();
    test_w_base();
    if (s_failures) {
        printf("angrylion_texrecip_test: %d failure(s)\n", s_failures);
        return 1;
    }
    printf("angrylion_texrecip_test: all checks passed\n");
    return 0;
}
