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

/* the combiner equation, three colour channels in three lanes:
 *   ((a - b) * c) + (d << 8) + 0x80, kept to seventeen bits.
 * a, b and d arrive already sign-extended through the nine-bit table
 * (a gather, so it stays scalar); c arrives sign-extended in place. */
static size_t gen_combiner_eq(uint8_t *buf)
{
    /* one argument, one buffer: a at +0, b at +16, c at +32, d at +48,
     * the constant 0x80 at +64, the mask at +80, the result to +96 */
    uint8_t *p = buf;
    AL_V_LOAD(&p, 0, A_RGBA,  0);
    AL_V_LOAD(&p, 1, A_RGBA, 16);
    AL_V_LOAD(&p, 2, A_RGBA, 32);
    AL_V_LOAD(&p, 3, A_RGBA, 48);
    AL_V_SUB32(&p, 0, 0, 1);        /* a - b       */
    AL_V_LOAD(&p, 6, A_RGBA, 112);  /* 0x0000ffff  */
    AL_V_AND(&p, 2, 2, 6);          /* clear c's upper half: see AL_V_MADD16 */
    AL_V_MADD16(&p, 0, 0, 2);       /* * c         */
    AL_V_SLL32(&p, 3, 3, 8);        /* d << 8      */
    AL_V_ADD32(&p, 0, 0, 3);
    AL_V_LOAD(&p, 4, A_RGBA, 64);
    AL_V_ADD32(&p, 0, 0, 4);        /* + 0x80      */
    AL_V_LOAD(&p, 5, A_RGBA, 80);
    AL_V_AND(&p, 0, 0, 5);          /* & 0x1ffff   */
    AL_V_STORE(&p, 0, A_RGBA, 96);
    AL_V_RET(&p);
    return (size_t)(p - buf);
}

static int test_combiner_eq(void)
{
    uint8_t *code = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    size_t len;
    int t, k, bad = 0, cases = 0;
    if (code == MAP_FAILED) return 1;
    len = gen_combiner_eq(code);
    if (mprotect(code, 4096, PROT_READ | PROT_EXEC) != 0) return 1;
    for (t = 0; t < 4096; t++)
    {
        int32_t m[32];
        for (k = 0; k < 4; k++)
        {
            m[k]      = ((int32_t)(t * 2654435761u + (uint32_t)k * 7919u)    >> 23) % 256; /* a */
            m[4 + k]  = ((int32_t)(t * 69069u      + (uint32_t)k * 104729u)  >> 23) % 256; /* b */
            m[8 + k]  = ((int32_t)(t * 22695477u   + (uint32_t)k * 1299721u) >> 23) % 256; /* c */
            m[12 + k] = ((int32_t)(t * 1103515245u + (uint32_t)k * 12345u)   >> 23) % 256; /* d */
            m[16 + k] = 0x80;
            m[20 + k] = 0x1ffff;
            m[24 + k] = 0;
            m[28 + k] = 0x0000ffff;
        }
        ((void (*)(int32_t*))code)(m);
        for (k = 0; k < 4; k++)
        {
            int32_t want = (((m[k] - m[4 + k]) * m[8 + k]) + (m[12 + k] << 8) + 0x80) & 0x1ffff;
            cases++;
            if (m[24 + k] != want)
            {
                if (bad++ < 5)
                    printf("  combiner MISMATCH t=%d ch=%d: got %d want %d\n", t, k, m[24 + k], want);
            }
        }
    }
    printf("combiner equation generated in %zu bytes; %d of %d results differ from the C\n",
           len, bad, cases);
    return bad;
}

/* the blend equation's force_blend arm, three channels in three lanes:
 *   (p1 * blend1a + p2 * mulb) >> 5, kept to eight bits.
 * Both products fit in signed sixteen bits - the colour operands reach
 * 255 and the blend factors 32 - so the widening multiply is exact with
 * one upper half cleared, as it documents. */
static size_t gen_blend_eq(uint8_t *buf)
{
    /* p1 at +0, blend1a at +16, p2 at +32, mulb at +48,
     * 0xffff at +64, 0xff at +80, result to +96 */
    uint8_t *p = buf;
    AL_V_LOAD(&p, 6, A_RGBA, 64);
    AL_V_LOAD(&p, 0, A_RGBA,  0);
    AL_V_LOAD(&p, 1, A_RGBA, 16);
    AL_V_AND(&p, 1, 1, 6);
    AL_V_MADD16(&p, 0, 0, 1);       /* p1 * blend1a */
    AL_V_LOAD(&p, 2, A_RGBA, 32);
    AL_V_LOAD(&p, 3, A_RGBA, 48);
    AL_V_AND(&p, 3, 3, 6);
    AL_V_MADD16(&p, 2, 2, 3);       /* p2 * mulb    */
    AL_V_ADD32(&p, 0, 0, 2);
    AL_V_SRA32(&p, 0, 0, 5);
    AL_V_LOAD(&p, 4, A_RGBA, 80);
    AL_V_AND(&p, 0, 0, 4);          /* & 0xff       */
    AL_V_STORE(&p, 0, A_RGBA, 96);
    AL_V_RET(&p);
    return (size_t)(p - buf);
}

static int test_blend_eq(void)
{
    uint8_t *code = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    size_t len;
    int t, k, bad = 0, cases = 0;
    if (code == MAP_FAILED) return 1;
    len = gen_blend_eq(code);
    if (mprotect(code, 4096, PROT_READ | PROT_EXEC) != 0) return 1;
    for (t = 0; t < 4096; t++)
    {
        int32_t m[28], p1[4], b1[4], p2[4], mb[4];
        for (k = 0; k < 4; k++)
        {
            p1[k] = (int32_t)((t * 2654435761u + (uint32_t)k * 7919u) >> 24) & 0xff;
            b1[k] = (int32_t)((t * 69069u + (uint32_t)k * 104729u) >> 27) & 0x1f;
            p2[k] = (int32_t)((t * 22695477u + (uint32_t)k * 1299721u) >> 24) & 0xff;
            mb[k] = ((int32_t)((t * 1103515245u + (uint32_t)k * 12345u) >> 27) & 0x1f) + 1;
            m[k] = p1[k]; m[4 + k] = b1[k]; m[8 + k] = p2[k]; m[12 + k] = mb[k];
            m[16 + k] = 0x0000ffff; m[20 + k] = 0xff; m[24 + k] = 0;
        }
        ((void (*)(int32_t*))code)(m);
        for (k = 0; k < 4; k++)
        {
            int32_t want = ((p1[k] * b1[k] + p2[k] * mb[k]) >> 5) & 0xff;
            cases++;
            if (m[24 + k] != want)
            {
                if (bad++ < 5)
                    printf("  blend MISMATCH t=%d ch=%d: got %d want %d\n", t, k, m[24 + k], want);
            }
        }
    }
    printf("blend equation generated in %zu bytes; %d of %d results differ from the C\n",
           len, bad, cases);
    return bad;
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
    bad += test_combiner_eq();
    bad += test_blend_eq();
    return bad != 0;
}
