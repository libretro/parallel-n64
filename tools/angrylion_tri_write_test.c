/* angrylion_tri_write_test -- the F3DEX2-family triangle writer
 * (rsp_tri_write) keeps its output word for word.
 *
 * Each case set runs a fixed pseudo-random stream of triangles through
 * the writer under one set of microcode constants and flags and hashes
 * every emitted word; the expected hashes are the scalar writer's. The
 * stream covers the whole 16-bit input ranges, degenerate and sliver
 * triangles, negative 1/w and both texture normalizers. The Makefile
 * builds the test twice, the second time with RSP_NO_SSE2, so the SSE2
 * attribute lanes and the scalar loop are both held to the same words.
 *
 * Built by `make check-angrylion` (top-level Makefile). Exit status 0 on
 * pass. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "rdp_emit_rsp.h"

typedef struct TwSet
{
    int32_t dx_scale, idy_scale, frac_mask, vcr_bound;
    int texrecip;
    uint32_t hash;
    unsigned long words;
} TwSet;

static TwSet k_sets[] =
{
    { 0x4000, 0x0008, 0xffff, 0x1cc, 0, 0xdb0e442eu, 150508 },
    { 0x4000, 0x0008, 0xffff, 0x1cc, 1, 0x6a4ab1bbu, 150544 },
    { 0x1000, 0x0020, 0xffff, 0x1cc, 0, 0x59415aaau, 150008 },
    { 0x4000, 0x0008, 0xfff8, 0x1cc, 0, 0x98d3da78u, 149524 }
};

static uint32_t s_seed;

static uint32_t rnd(void)
{
    s_seed = s_seed * 1664525u + 1013904223u;
    return s_seed >> 8 ^ s_seed << 13;
}

static int32_t rnd_range(int32_t lo, int32_t hi)
{
    return lo + (int32_t)(rnd() % (uint32_t)(hi - lo + 1));
}

static void mkvtx(RspTriVtx *v, int kind)
{
    memset(v, 0, sizeof(*v));
    switch (kind)
    {
    case 0:     /* on screen */
        v->x = (int16_t)rnd_range(0, 320 * 4);
        v->y = (int16_t)rnd_range(0, 240 * 4);
        break;
    case 1:     /* anywhere in the 16-bit range */
        v->x = (int16_t)rnd();
        v->y = (int16_t)rnd();
        break;
    default:    /* small, for slivers and degenerates */
        v->x = (int16_t)rnd_range(100, 104);
        v->y = (int16_t)rnd_range(100, 104);
        break;
    }
    v->z = (int32_t)(rnd() & 0x03ffffffu);
    v->r = (int32_t)(rnd() & 0xff);
    v->g = (int32_t)(rnd() & 0xff);
    v->b = (int32_t)(rnd() & 0xff);
    v->a = (int32_t)(rnd() & 0xff);
    v->s = (int16_t)rnd();
    v->t = (int16_t)rnd();
    v->invw = (rnd() & 7) ? (int32_t)(rnd() & 0x7fffffffu) | 0x100
                          : (int32_t)rnd();
    v->pw = (int32_t)(rnd() & 0x7fffffffu);
}

int main(void)
{
    int si, fails = 0;
    for (si = 0; si < (int)(sizeof(k_sets) / sizeof(k_sets[0])); si++)
    {
        TwSet *ts = &k_sets[si];
        uint32_t h = 2166136261u;
        unsigned long words = 0;
        int n;
        s_seed = 0x1234567u + (uint32_t)si * 0x9e3779b9u;
        rsp_tri_set_texrecip(ts->texrecip);
        for (n = 0; n < 6000; n++)
        {
            RspTriVtx v[3];
            int32_t ew[64];
            int k, w, flags = (int)(rnd() & 15), kind = (int)(rnd() % 3);
            for (k = 0; k < 3; k++)
                mkvtx(&v[k], k == 0 ? kind : (rnd() & 3) ? kind : 0);
            w = rsp_tri_write(ew, &v[0], &v[1], &v[2],
                              flags & 1, (flags >> 1) & 1, (flags >> 2) & 1,
                              (flags >> 3) & 1, (int)(rnd() & 7), (int)(rnd() & 7),
                              ts->dx_scale, ts->idy_scale, ts->frac_mask,
                              ts->vcr_bound);
            h = (h ^ (uint32_t)w) * 16777619u;
            for (k = 0; k < w; k++)
                h = (h ^ (uint32_t)ew[k]) * 16777619u;
            words += (unsigned long)(w > 0 ? w : 0);
        }
        rsp_tri_set_texrecip(0);
        printf("set %d: %lu words, hash %08x", si, words, (unsigned)h);
        if (h != ts->hash || words != ts->words)
        {
            printf(" -- FAIL, expected %lu words, hash %08x\n", ts->words,
                   (unsigned)ts->hash);
            fails++;
        }
        else
            printf("\n");
    }
    if (fails)
    {
        printf("angrylion_tri_write_test: %d failure(s)\n", fails);
        return 1;
    }
    printf("angrylion_tri_write_test: all checks passed\n");
    return 0;
}
