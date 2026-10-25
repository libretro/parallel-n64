/* test_musyx_aux -- the MusyX v2 auxiliary sources (Indiana Jones, Battle
 * for Naboo) are mixed as the microcode mixes them.
 *
 * The stage was checked sample-exact against the cxd4 interpreter running
 * the retail MusyX v2 microcode, with sources injected into live Indiana
 * Jones and Battle for Naboo tasks (all four slots, filtered and not).
 * These checks pin that behaviour on synthetic input:
 *
 *  - all-zero taps pass the planar source through, weighted by the gains
 *    and with the destination kept at 0xffff/0x10000;
 *  - the per-channel and e50 output of a filtered slot matches the value
 *    recorded from the validated stage;
 *  - the four-sample history and the last two mixed bytes per destination
 *    land in the state block;
 *  - the carry the last filter fold leaves borrows from the first eight
 *    right-channel samples of the interleave.
 *
 * Built by `make check-core` (top-level Makefile). Exit status 0 on pass.
 */
#include "../mupen64plus-rsp-hle/src/musyx.c"

#include <stdio.h>
#include <stdlib.h>

void HleVerboseMessage(void* user_defined, const char *message, ...)
{
    (void)user_defined; (void)message;
}
void HleWarnMessage(void* user_defined, const char *message, ...)
{
    (void)user_defined; (void)message;
}
void rsp_break(struct hle_t* hle, unsigned int setbits)
{
    (void)hle; (void)setbits;
}

static int s_failures;

#define CHECK(cond, what) \
    do { if (!(cond)) { printf("FAIL: %s\n", what); s_failures++; } } while (0)

#define DESC   0x1000u
#define PLANAR 0x2000u
#define STATE  0x3000u

static struct hle_t s_hle;
static unsigned char s_dram[0x10000];
static unsigned char s_dmem[0x1000];
static uint32_t s_seed;

static uint16_t rnd(void)
{
    s_seed = s_seed * 1103515245u + 12345u;
    return (uint16_t)(s_seed >> 9);
}

static void put_u16(uint32_t a, unsigned v) { *dram_u16(&s_hle, a) = (uint16_t)v; }
static void put_u32(uint32_t a, uint32_t v) { *dram_u32(&s_hle, a) = v; }

static void slot(unsigned n, const uint16_t gains[4], const int16_t taps[4])
{
    unsigned k;
    for (k = 0; k < 4; k++) {
        put_u16(DESC + n * 24 + k * 2, gains[k]);
        put_u16(DESC + n * 24 + 8 + k * 2, (uint16_t)taps[k]);
    }
    put_u32(DESC + n * 24 + 16, PLANAR + n * 0x400);
    for (k = 0; k < 3 * SUBFRAME_SIZE; k++)
        put_u16(PLANAR + n * 0x400 + k * 2, rnd());
}

static void fill(musyx_t* m, int16_t v)
{
    unsigned i;
    for (i = 0; i < SUBFRAME_SIZE; i++)
        m->left[i] = m->right[i] = m->cc0[i] = m->e50[i] = v;
}

static uint32_t fnv(const musyx_t* m)
{
    const int16_t* parts[4];
    uint32_t h = 2166136261u;
    unsigned p, i;
    parts[0] = m->left; parts[1] = m->right; parts[2] = m->cc0; parts[3] = m->e50;
    for (p = 0; p < 4; p++)
        for (i = 0; i < SUBFRAME_SIZE; i++) {
            h = (h ^ (uint8_t)parts[p][i]) * 16777619u;
            h = (h ^ (uint8_t)((uint16_t)parts[p][i] >> 8)) * 16777619u;
        }
    return h;
}

int main(void)
{
    static musyx_t m;
    static const int16_t zero[4] = { 0, 0, 0, 0 };
    static const int16_t taps[4] = { 0x1000, -0x2400, 0x7fff, -0x8000 };
    static const uint16_t unity[4] = { 0xffff, 0xffff, 0xffff, 0xffff };
    static const uint16_t gains[4] = { 0x8000, 0x4000, 0xc000, 0x2000 };
    unsigned i;
    int ok;

    s_hle.dram = s_dram;
    s_hle.dmem = s_dmem;

    /* pass-through: zero taps, unity gains, silent destinations */
    s_seed = 1;
    slot(0, unity, zero);
    memset(s_dram + STATE, 0, 128);
    fill(&m, 0);
    auxiliary_stage_v2(&s_hle, &m, DESC, 1u, STATE);
    ok = 1;
    for (i = 0; i < SUBFRAME_SIZE; i++) {
        int32_t l = (int16_t)*dram_u16(&s_hle, PLANAR + i * 2);
        if (m.left[i] != clamp_s16((int32_t)(((int64_t)l * 0xffff) >> 16)))
            ok = 0;
    }
    CHECK(ok, "zero taps pass the source through the gain");
    CHECK(m.auxiliary_carry == 0, "zero taps leave no carry");
    ok = 1;
    for (i = 0; i < 4; i++) {
        uint16_t want = *dram_u16(&s_hle, PLANAR + (SUBFRAME_SIZE - 7 + i) * 2);
        uint16_t got = (uint16_t)((s_dram[(STATE + 32 + i * 2) ^ 3] << 8)
                                  | s_dram[(STATE + 33 + i * 2) ^ 3]);
        if (got != want)
            ok = 0;
    }
    CHECK(ok, "the history keeps the source samples the next filter needs");

    /* filtered: two slots over a busy destination */
    s_seed = 0x1234567u;
    slot(1, gains, taps);
    slot(3, unity, taps);
    memset(s_dram + STATE, 0x5a, 128);
    fill(&m, 0x1357);
    auxiliary_stage_v2(&s_hle, &m, DESC, 0xau, STATE);
    {
        uint32_t h = fnv(&m), st = 2166136261u;
        for (i = 0; i < 128; i++)
            st = (st ^ s_dram[(STATE + i) ^ 3]) * 16777619u;
        if (getenv("MUSYX_AUX_PRINT"))
            printf("subframes %08x state %08x carry %04x\n", h, st, m.auxiliary_carry);
        CHECK(h == 0xbcd7db9du, "filtered slots mix as the microcode does");
        CHECK(st == 0xc38691cbu, "filtered slots leave the microcode's state");
        CHECK(m.auxiliary_carry == 0x00ffu, "the last fold's carry");
    }

    /* the interleave borrows the carry on its first vector */
    m.auxiliary_carry = 0x81;
    for (i = 0; i < SUBFRAME_SIZE; i++)
        put_u16(0x4000 + i * 2, (uint16_t)(i * 3));
    interleave_stage_v2(&s_hle, &m, 0, 0x5000, 0x4000, 0x6000);
    CHECK(m.right[0] == -0 - 1 && m.right[7] == -21 - 1 && m.right[1] == -3
          && m.right[8] == -24 && m.auxiliary_carry == 0,
          "the interleave VSUB borrows the auxiliary carry");

    if (s_failures) {
        printf("test_musyx_aux: %d failure(s)\n", s_failures);
        return 1;
    }
    printf("test_musyx_aux: all checks passed\n");
    return 0;
}
