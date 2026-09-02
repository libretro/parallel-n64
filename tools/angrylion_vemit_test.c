/* angrylion_vemit_test -- generate, through the architecture-neutral interface, the kernel the
 * RDP span loop actually needs - four pixels' attributes stepped and
 * extracted - then run it and check every lane against the scalar code
 * it replaces. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/mman.h>
#include "rdp/al_vemit.h"

/* register assignment the generator picks; on x86 these are xmm0..5 */
#define V_ACC   0   /* r,g,b,a accumulator            */
#define V_STEP  1   /* drinc,dginc,dbinc,dainc        */
#define V_OUT   2   /* the >>14 extraction            */
#define V_TMP   3
#define V_MASK  4
#define V_ZERO  5

/* System V: rdi = attrs[4], rsi = steps[4], rdx = out, rcx = iterations
 * AArch64:   x0,        x1,          x2,       x3            */
#if defined(AL_VEMIT_X86)
#define A0 7   /* rdi */
#define A1 6   /* rsi */
#define A2 2   /* rdx */
#else
#define A0 0
#define A1 1
#define A2 2
#endif

static size_t gen_kernel(uint8_t *buf, int iters)
{
    uint8_t *p = buf;
    int i;
    /* load the accumulator and the per-pixel step */
    AL_V_LOAD(&p, V_ACC,  A0, 0);
    AL_V_LOAD(&p, V_STEP, A1, 0);
    for (i = 0; i < iters; i++)
    {
        /* what the scalar loop does per pixel: extract, then step.
         * sr = r >> 14 (arithmetic), then r += drinc */
        AL_V_SRA32(&p, V_OUT, V_ACC, 14);
        AL_V_STORE(&p, V_OUT, A2, i * 16);
        AL_V_ADD32(&p, V_ACC, V_ACC, V_STEP);
    }
    /* and the coverage-style mask work the pipeline also needs:
     * m = (acc > 0); sel = m ? acc : zero  */
    AL_V_XOR(&p, V_ZERO, V_ZERO, V_ZERO);
    AL_V_CMPGT32(&p, V_MASK, V_ACC, V_ZERO);
    AL_V_SELECT(&p, V_OUT, V_MASK, V_ACC, V_ZERO, V_TMP);
    AL_V_STORE(&p, V_OUT, A2, iters * 16);
    AL_V_RET(&p);
    return (size_t)(p - buf);
}

int main(void)
{
    const int iters = 8;
    uint8_t *code = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    int32_t attrs[4], steps[4];
    int32_t out[4 * 16];
    int32_t ref[4 * 16];
    size_t len;
    int i, k, bad = 0;

    if (code == MAP_FAILED) { perror("mmap"); return 1; }
    len = gen_kernel(code, iters);
    if (mprotect(code, 4096, PROT_READ | PROT_EXEC) != 0) { perror("mprotect"); return 1; }

    /* values of the magnitude a real primitive carries */
    attrs[0] =  0x12345678; steps[0] =  0x00030000;
    attrs[1] = -0x0abcdef0; steps[1] = -0x00018000;
    attrs[2] =  0x7f000000; steps[2] =  0x00100000;
    attrs[3] =  0x00000001; steps[3] = -0x00000001;

    /* the scalar reference: exactly what the span loop computes */
    {
        int32_t a[4]; memcpy(a, attrs, sizeof a);
        for (i = 0; i < iters; i++)
        {
            for (k = 0; k < 4; k++) ref[i * 4 + k] = a[k] >> 14;
            for (k = 0; k < 4; k++) a[k] += steps[k];
        }
        for (k = 0; k < 4; k++) ref[iters * 4 + k] = (a[k] > 0) ? a[k] : 0;
    }

    memset(out, 0xcd, sizeof out);
    ((void (*)(int32_t*, int32_t*, int32_t*))code)(attrs, steps, out);

    for (i = 0; i <= iters; i++)
        for (k = 0; k < 4; k++)
            if (out[i * 4 + k] != ref[i * 4 + k])
            {
                if (bad++ < 6)
                    printf("  MISMATCH iter %d lane %d: emitted %08x, scalar %08x\n",
                           i, k, out[i * 4 + k], ref[i * 4 + k]);
            }

    printf("%s backend: %zu bytes for %d iterations (%.1f bytes/pixel-group)\n",
#if defined(AL_VEMIT_X86)
           "x86-64 SSE2",
#else
           "AArch64 NEON",
#endif
           len, iters, (double)len / iters);
    printf("%d of %d lanes differ from the scalar reference\n", bad, (iters + 1) * 4);
    return bad != 0;
}
