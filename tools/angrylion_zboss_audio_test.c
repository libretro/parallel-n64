/* angrylion_zboss_audio_test -- the BOSS microcode's audio commands
 * stay within defined C arithmetic on loud input.
 *
 * The resampler (opcode audio2) adds a 16.16 interpolation product to a
 * sample moved into the high half, and the ADPCM decoder (audio4) scales
 * a predictor dot product by up to 2^15; both sums pass 32 bits, and the
 * samples and nibbles they start from are negative half the time.  The
 * output keeps bits 16..31 of each sum, so the expected values here are
 * computed independently in 64-bit arithmetic; built with
 * -fsanitize=undefined the test also rejects signed overflow and shifts
 * of negative values on the way there.
 *
 * Built by `make check-core` (top-level Makefile). Exit status 0 on pass.
 */
#include "rdp_emit_zboss.c"

#include <stdlib.h>

/* The walker references the HLE activation glue for mid-list microcode
 * swaps; the audio commands never reach it. */
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
#define INPUT      0x1000u

static unsigned char rdram[RDRAM_SIZE];
static unsigned char dmem[0x1000];
static int s_failures;

#define CHECK(cond, what) \
    do { if (!(cond)) { printf("FAIL: %s\n", what); s_failures++; } } while (0)

static int clamp_short(int64_t v)
{
    return (int)(int16_t)(uint16_t)((uint64_t)v & 0xffffu);
}

static int64_t floor_shift16(int64_t v)
{
    return v / 65536 - ((v % 65536) < 0 ? 1 : 0);
}

static void put16(unsigned char *b, unsigned int a, int v)
{
    b[a ^ BO8] = (unsigned char)(((unsigned)v >> 8) & 0xffu);
    b[(a + 1) ^ BO8] = (unsigned char)((unsigned)v & 0xffu);
}

static void test_audio4(void)
{
    static const unsigned char input[9] = {
        0xf1,                    /* scale 2^15, book pair 1/2 */
        0x87, 0x7f, 0x80, 0x78,  /* nibbles -8 7 7 -1 -8 0 7 -8 */
        0x77, 0x88, 0xf7, 0x8f
    };
    int table[8][8];
    int expected[16];
    int v1 = -0x7000, v2 = 0x7123;
    int i, j, k, l2, ok;
    unsigned int n;

    s_rdram = rdram;
    s_rdram_size = RDRAM_SIZE;
    s_dmem = dmem;
    for (i = 0; i < 8; i++)
        for (j = 0; j < 8; j++) {
            table[i][j] = ((i + j) & 1) ? 0x7fff - i * 31 - j : -0x7ff0 + i * 17 + j;
            zb.audio_table[i][j] = (short)table[i][j];
        }
    for (n = 0; n < sizeof(input); n++)
        rdram[(INPUT + n) ^ BO8] = input[n];
    put16(dmem, 0, v1);
    put16(dmem, 2, v2);

    /* reference: 64-bit sums, 16-bit truncating read-out */
    {
        int index = (input[0] & 0xf) << 1;
        int64_t c1 = (int64_t)1 << ((input[0] >> 4) & 0x1f);
        for (l2 = 0; l2 < 2; l2++) {
            int coeff[8];
            for (n = 0; n < 4; n++) {
                int byte = (int)(signed char)input[1 + l2 * 4 + n];
                int lo = byte & 0xf;
                coeff[n * 2] = (byte - lo) / 16;  /* arithmetic >> 4 */
                coeff[n * 2 + 1] = lo >= 8 ? lo - 16 : lo;
            }
            for (i = 0; i < 8; i++) {
                int64_t res1 = 0, res2;
                for (j = 0, k = i; j < i; j++, k--)
                    res1 += (int64_t)table[index + 1][k - 1] * coeff[j];
                res1 += (int64_t)coeff[i] * 2048;
                res1 *= c1;
                res2 = (int64_t)v1 * table[index][i] + (int64_t)v2 * table[index + 1][i];
                expected[l2 * 8 + i] = clamp_short(floor_shift16(res1 * 32 + res2 * 32));
            }
            v1 = expected[l2 * 8 + 6];
            v2 = expected[l2 * 8 + 7];
        }
    }

    zb_audio4(0x009u, INPUT);

    ok = 1;
    for (l2 = 0; l2 < 2; l2++)
        for (i = 0; i < 8; i++)
            if (rd_s16d(dmem, 0x30u + (unsigned)(l2 * 16) + (unsigned)((i ^ 1) << 1))
                != expected[l2 * 8 + i])
                ok = 0;
    CHECK(ok, "audio4: loud ADPCM frames decode at full width");
}

static void test_audio2(void)
{
    static const int samples[5] = { 0x7fff, -2, -0x8000, -1, 0x7fff };
    static const int gains[2] = { 0xffff, 0x8001 };
    unsigned int dst0 = 0x200u, dst;
    unsigned int step = 0xffffu, base = 0xfffeu;
    int initial[8];
    int expected[8];
    int i, j, k, ok;

    s_dmem = dmem;
    memset(dmem, 0, sizeof(dmem));
    for (i = 0; i < 5; i++)
        put16(dmem, 0x30u + (unsigned)(i * 2), samples[i]);
    put16(dmem, 0x904u, gains[0]);
    put16(dmem, 0x906u, gains[1]);
    wr32(dmem, 0x10u, dst0);
    for (i = 0; i < 8; i++) {
        initial[i] = (i & 1) ? -0x7000 + i : 0x7000 - i;
        put16(dmem, dst0 + (unsigned)(i * 2), initial[i]);
    }

    /* reference: per output pair, interpolate between neighbouring
     * samples at the 16-bit phase, then mix into the destination */
    dst = 0;
    for (j = 0; j < 4; j++) {
        unsigned int pos = (unsigned int)j * step + base;
        int v9 = samples[pos >> 16];
        int v10 = samples[(pos >> 16) + 1];
        int v12 = (int)(int16_t)(uint16_t)((unsigned)(v10 - v9) & 0xffffu);
        int v13 = initial[dst];
        for (k = 0; k < 2; k++) {
            int64_t r = floor_shift16((int64_t)v9 * 65536 + (int64_t)v12 * (int64_t)(pos & 0xffffu));
            int res3 = clamp_short(r);
            r = floor_shift16((int64_t)v13 * 65536 + (int64_t)gains[k] * res3);
            expected[dst++] = clamp_short(r);
        }
    }

    zb_audio2((0x00u << 16) | step, (4u << 24) | base);

    ok = 1;
    for (i = 0; i < 8; i++)
        if (rd_s16d(dmem, dst0 + (unsigned)(i * 2)) != expected[i])
            ok = 0;
    CHECK(ok, "audio2: loud resampling mixes at full width");
}

int main(void)
{
    test_audio4();
    test_audio2();

    if (s_failures) {
        printf("angrylion_zboss_audio_test: %d failure(s)\n", s_failures);
        return 1;
    }
    printf("angrylion_zboss_audio_test: all checks passed\n");
    return 0;
}
