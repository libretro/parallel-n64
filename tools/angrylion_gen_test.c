/* angrylion_gen_test -- the first generated pipeline stage.
 *
 * rgba_correct is the shade-colour correction the span loop applies to
 * every pixel: four independent channels, a branch on whether coverage
 * is full, and a nine-bit clamp table at the end. It is small enough to
 * read in one screen and complete enough to exercise everything the
 * generator has to do - a data-dependent branch turned into a mask, a
 * per-channel constant loaded once per span, a table lookup that stays
 * scalar - so it is where the generator is proved before it is scaled
 * up to the texture unit and the combiner.
 *
 * The four channels go in the four lanes, which is why this stage
 * vectorises so directly: one pixel per invocation, four channels at a
 * time, rather than four pixels at a time. The same generator emits the
 * across-pixels form once the stage list is complete.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/mman.h>
#include "rdp/al_vemit.h"

/* the clamp table the stage ends with, reproduced here */
static int32_t clamp9[512];
static void clamp9_init(void)
{
    int i;
    for (i = 0; i < 512; i++)
    {
        int v = (i & 0x180) == 0x180 ? (i | ~0x1ff) : i;   /* sign-extend 9 bits */
        clamp9[i] = v < 0 ? 0 : (v > 255 ? 255 : v);
    }
}

/* the C the generated code must match, one pixel, four channels */
static void rgba_correct_c(int32_t *rgba, int offx, int offy,
                           const int32_t *cd, const int32_t *ddy, uint32_t cvg)
{
    int k;
    if (cvg == 8)
    {
        for (k = 0; k < 4; k++) rgba[k] >>= 2;
    }
    else
    {
        for (k = 0; k < 4; k++)
        {
            int32_t summand = offx * cd[k] + offy * ddy[k];
            rgba[k] = ((rgba[k] << 2) + summand) >> 4;
        }
    }
    for (k = 0; k < 4; k++) rgba[k] = clamp9[rgba[k] & 0x1ff];
}

#if defined(AL_VEMIT_X86)
/* System V: rdi = rgba, rsi = scratch(offx*cd + offy*ddy), rdx = out,
 * rcx = cvg==8 mask (all ones or zero, broadcast) */
#define A_RGBA 7
#define A_SUMM 6
#define A_OUT  2
#define A_MASK 1
#else
#define A_RGBA 0
#define A_SUMM 1
#define A_OUT  2
#define A_MASK 3
#endif

#define V_RGBA 0
#define V_SUMM 1
#define V_FULL 2   /* the cvg==8 arm  */
#define V_PART 3   /* the other arm   */
#define V_MASK 4
#define V_TMP  5

static size_t gen_rgba_correct(uint8_t *buf)
{
    uint8_t *p = buf;
    AL_V_LOAD(&p, V_RGBA, A_RGBA, 0);
    AL_V_LOAD(&p, V_SUMM, A_SUMM, 0);
    AL_V_LOAD(&p, V_MASK, A_MASK, 0);

    /* full-coverage arm: r >> 2 */
    AL_V_SRA32(&p, V_FULL, V_RGBA, 2);

    /* partial arm: ((r << 2) + summand) >> 4 */
    AL_V_SLL32(&p, V_PART, V_RGBA, 2);
    AL_V_ADD32(&p, V_PART, V_PART, V_SUMM);
    AL_V_SRA32(&p, V_PART, V_PART, 4);

    /* the branch becomes a select */
    AL_V_SELECT(&p, V_RGBA, V_MASK, V_FULL, V_PART, V_TMP);

    AL_V_STORE(&p, V_RGBA, A_OUT, 0);
    AL_V_RET(&p);
    return (size_t)(p - buf);
}

int main(void)
{
    uint8_t *code;
    size_t len;
    int trial, k, bad = 0, cases = 0;

    clamp9_init();
    code = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (code == MAP_FAILED) { perror("mmap"); return 1; }
    len = gen_rgba_correct(code);
    if (mprotect(code, 4096, PROT_READ | PROT_EXEC) != 0) { perror("mprotect"); return 1; }

    for (trial = 0; trial < 4096; trial++)
    {
        int32_t rgba[4], cd[4], ddy[4], summ[4], mask[4], out[4], ref[4];
        int offx = (trial * 7) & 3, offy = (trial * 5) & 3;
        uint32_t cvg = (trial & 1) ? 8 : (trial & 7);

        for (k = 0; k < 4; k++)
        {
            rgba[k] = (int32_t)(trial * 2654435761u + (uint32_t)k * 40503u) >> 8;
            cd[k]   = (int32_t)(trial * 69069u + (uint32_t)k * 1103515245u) >> 18;
            ddy[k]  = (int32_t)(trial * 22695477u + (uint32_t)k * 12345u) >> 18;
            summ[k] = offx * cd[k] + offy * ddy[k];
            mask[k] = (cvg == 8) ? -1 : 0;
            ref[k]  = rgba[k];
        }
        rgba_correct_c(ref, offx, offy, cd, ddy, cvg);

        ((void (*)(int32_t*, int32_t*, int32_t*, int32_t*))code)(rgba, summ, out, mask);
        /* the clamp table stays scalar: it is a gather, not arithmetic */
        for (k = 0; k < 4; k++) out[k] = clamp9[out[k] & 0x1ff];

        for (k = 0; k < 4; k++)
        {
            cases++;
            if (out[k] != ref[k])
            {
                if (bad++ < 5)
                    printf("  MISMATCH trial %d ch %d cvg %u: generated %d, C %d\n",
                           trial, k, cvg, out[k], ref[k]);
            }
        }
    }

    printf("rgba_correct generated in %zu bytes; %d of %d channel results differ from the C\n",
           len, bad, cases);
    return bad != 0;
}
