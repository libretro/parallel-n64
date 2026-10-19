/* test_fpu_compare -- the sixteen C.cond.fmt predicates on ordered and
 * unordered operands.
 *
 * Each predicate is three condition bits (less, equal, unordered) and a
 * signalling bit: the result is set when the operands' relation is one the
 * predicate names, and a signalling predicate raises Invalid Operation when
 * either operand is a NaN.  Every pair from {-1, 0, +1, quiet NaN,
 * signalling NaN} is checked in single and double precision.
 *
 * The core is built with -Ofast, so this test is too: the compares must not
 * depend on the compiler keeping NaN semantics.
 *
 * Built by `make check-core` (top-level Makefile). Exit status 0 on pass.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "device/r4300/fpu.h"

typedef void (*cmp_s_fn)(uint32_t*, const float*, const float*);
typedef void (*cmp_d_fn)(uint32_t*, const double*, const double*);

static const cmp_s_fn s_cmp_s[16] = {
    c_f_s, c_un_s, c_eq_s, c_ueq_s, c_olt_s, c_ult_s, c_ole_s, c_ule_s,
    c_sf_s, c_ngle_s, c_seq_s, c_ngl_s, c_lt_s, c_nge_s, c_le_s, c_ngt_s
};
static const cmp_d_fn s_cmp_d[16] = {
    c_f_d, c_un_d, c_eq_d, c_ueq_d, c_olt_d, c_ult_d, c_ole_d, c_ule_d,
    c_sf_d, c_ngle_d, c_seq_d, c_ngl_d, c_lt_d, c_nge_d, c_le_d, c_ngt_d
};
static const char* const s_names[16] = {
    "f", "un", "eq", "ueq", "olt", "ult", "ole", "ule",
    "sf", "ngle", "seq", "ngl", "lt", "nge", "le", "ngt"
};

/* -1, 0, +1, quiet NaN, signalling NaN; the first three in order */
static const uint32_t s_bits_s[5] = {
    0xbf800000u, 0x00000000u, 0x3f800000u, 0x7fc00000u, 0x7fbfffffu
};
static const uint32_t s_bits_d_hi[5] = {
    0xbff00000u, 0x00000000u, 0x3ff00000u, 0x7ff80000u, 0x7ff7ffffu
};

static int s_failures;

/* relation of operand a to b: 1 less, 2 equal, 4 unordered */
static unsigned relation(unsigned a, unsigned b)
{
    if (a >= 3 || b >= 3)
        return 4;
    return a < b ? 1 : a == b ? 2 : 0;
}

static void check(const char* fmt, unsigned cond, unsigned a, unsigned b, uint32_t fcr31)
{
    unsigned rel = relation(a, b);
    int want_cmp = ((cond & 4) && rel == 1) || ((cond & 2) && rel == 2)
                || ((cond & 1) && rel == 4);
    int want_invalid = (cond & 8) && rel == 4;
    int got_cmp = (fcr31 & FCR31_CMP_BIT) != 0;
    int got_invalid = (fcr31 & FCR31_CAUSE_INVALIDOP_BIT) != 0;

    if (got_cmp != want_cmp || got_invalid != want_invalid) {
        if (s_failures < 16)
            printf("FAIL: c.%s.%s operands %u,%u: condition %d (want %d), invalid %d (want %d)\n",
                   s_names[cond], fmt, a, b, got_cmp, want_cmp, got_invalid, want_invalid);
        s_failures++;
    }
}

int main(void)
{
    unsigned cond, a, b;

    for (cond = 0; cond < 16; cond++)
        for (a = 0; a < 5; a++)
            for (b = 0; b < 5; b++) {
                float fs, ft;
                double ds, dt;
                uint64_t bits;
                uint32_t fcr31;

                memcpy(&fs, &s_bits_s[a], sizeof(fs));
                memcpy(&ft, &s_bits_s[b], sizeof(ft));
                /* start from the opposite result so a no-op is caught */
                fcr31 = FCR31_CMP_BIT;
                s_cmp_s[cond](&fcr31, &fs, &ft);
                check("s", cond, a, b, fcr31);
                fcr31 = 0;
                s_cmp_s[cond](&fcr31, &fs, &ft);
                check("s", cond, a, b, fcr31);

                bits = (uint64_t)s_bits_d_hi[a] << 32 | (a == 4 ? 0xffffffffu : 0u);
                memcpy(&ds, &bits, sizeof(ds));
                bits = (uint64_t)s_bits_d_hi[b] << 32 | (b == 4 ? 0xffffffffu : 0u);
                memcpy(&dt, &bits, sizeof(dt));
                fcr31 = FCR31_CMP_BIT;
                s_cmp_d[cond](&fcr31, &ds, &dt);
                check("d", cond, a, b, fcr31);
                fcr31 = 0;
                s_cmp_d[cond](&fcr31, &ds, &dt);
                check("d", cond, a, b, fcr31);
            }

    if (s_failures) {
        printf("test_fpu_compare: %d failure(s)\n", s_failures);
        return 1;
    }
    printf("test_fpu_compare: all checks passed\n");
    return 0;
}
