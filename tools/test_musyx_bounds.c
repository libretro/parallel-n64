/* test_musyx_bounds -- the MusyX voice loaders stay inside their buffers
 * and decode ADPCM books from the memory the microcode reads them from.
 *
 *  - a frame header selecting the top book reads the entries past the
 *    loaded 128 from DMEM after the table, wrapping onto the ucode data
 *    segment past the end of DMEM;
 *  - frame counts, PCM16 lengths and DMA segment sizes larger than the
 *    0x200-sample window are held to it;
 *  - the base volume decay is exact for any accumulated volume.
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

#define DRAM_BYTES 0x1000000u
#define UCODE_DATA 0x100000u
#define VOICE      0x200000u
#define BOOKS      0x201000u
#define ENCODED    0x202000u
#define GUARD      0x5a

static struct hle_t s_hle;
static unsigned char s_dmem[0x1000];

static void put_u8(uint32_t a, unsigned v)  { *dram_u8(&s_hle, a) = (uint8_t)v; }
static void put_u16(uint32_t a, unsigned v) { *dram_u16(&s_hle, a) = (uint16_t)v; }
static void put_u32(uint32_t a, uint32_t v) { *dram_u32(&s_hle, a) = v; }

static void put_catsrc(uint32_t catsrc, uint32_t ptr1, unsigned size1,
                       uint32_t ptr2, unsigned size2)
{
    put_u32(catsrc + CATSRC_PTR1, ptr1);
    put_u32(catsrc + CATSRC_PTR2, ptr2);
    put_u16(catsrc + CATSRC_SIZE1, size1);
    put_u16(catsrc + CATSRC_SIZE2, size2);
}

/* the window under test sits between two guard regions */
struct window {
    unsigned char pre[0x4000];
    int16_t samples[SAMPLE_BUFFER_SIZE];
    unsigned char post[0x4000];
};

static int guards_intact(const struct window* w)
{
    size_t i;
    for (i = 0; i < sizeof(w->pre); i++)
        if (w->pre[i] != GUARD || w->post[i] != GUARD)
            return 0;
    return 1;
}

static void test_adpcm_top_book(void)
{
    static struct window w;
    int16_t expected_table[256];
    int16_t expected[32];
    uint8_t encoded[SAMPLE_BUFFER_SIZE * 2 * 5 / 16 * 2];
    unsigned segbase, offset, k;

    /* one frame, header nibble 0xf3: book 0xf0, shift 3 */
    memset(encoded, 0, sizeof(encoded));
    for (k = 0; k < 4; k++)
        encoded[k] = (uint8_t)(0x10 * k + 3);
    encoded[8] = 0xf3;
    for (k = 9; k < 24; k++)
        encoded[k] = (uint8_t)(k * 37);
    for (k = 0; k < 24; k++)
        put_u8(ENCODED + k, encoded[k]);

    put_u8(VOICE + VOICE_ADPCM_FRAMES, 1);
    put_u8(VOICE + VOICE_ADPCM_FRAMES + 1, 0);
    put_u8(VOICE + VOICE_SKIP_SAMPLES, 0);
    put_u8(VOICE + VOICE_SKIP_SAMPLES + 1, 0);
    put_u32(VOICE + VOICE_ADPCM_TABLE_PTR, BOOKS);
    put_catsrc(VOICE + VOICE_CATSRC_0, ENCODED, 24, 0, 0);

    /* books 0..7 in DRAM; the eight above them are not loaded */
    for (k = 0; k < 128; k++) {
        put_u16(BOOKS + k * 2, 0x100 + k);
        expected_table[k] = (int16_t)(0x100 + k);
    }
    /* v2 loads its books to DMEM 0xf00, so entries 128..255 come from
     * DMEM 0x1000.. - the ucode data segment from offset 0 */
    *dmem_u32(&s_hle, TASK_UCODE_DATA) = UCODE_DATA;
    for (k = 128; k < 256; k++) {
        put_u16(UCODE_DATA + (k - 128) * 2, 0x2000 + k * 3);
        expected_table[k] = (int16_t)(0x2000 + k * 3);
    }

    memset(&w, GUARD, sizeof(w));
    load_samples_ADPCM(&s_hle, VOICE, w.samples, &segbase, &offset, 0xf00);
    CHECK(segbase == SAMPLE_BUFFER_SIZE - 32 && guards_intact(&w),
          "ADPCM: one frame lands at the top of the window");

    memset(expected, 0, sizeof(expected));
    adpcm_decode_frames(&s_hle, expected, encoded, expected_table, 1, 0);
    CHECK(memcmp(expected, w.samples + segbase, sizeof(expected)) == 0,
          "ADPCM: the top book reads its entries from the ucode data segment");

    /* v1 loads to DMEM 0xee0: entries 128..143 are DMEM 0xfe0..0xffe */
    for (k = 128; k < 144; k++) {
        *dmem_u16(&s_hle, (uint16_t)(0xee0 + k * 2)) = (uint16_t)(0x3000 + k);
        expected_table[k] = (int16_t)(0x3000 + k);
    }
    for (k = 144; k < 256; k++) {
        put_u16(UCODE_DATA + (0xee0 + k * 2 - 0x1000), 0x4000 + k);
        expected_table[k] = (int16_t)(0x4000 + k);
    }
    memset(&w, GUARD, sizeof(w));
    load_samples_ADPCM(&s_hle, VOICE, w.samples, &segbase, &offset, 0xee0);
    memset(expected, 0, sizeof(expected));
    adpcm_decode_frames(&s_hle, expected, encoded, expected_table, 1, 0);
    CHECK(memcmp(expected, w.samples + segbase, sizeof(expected)) == 0,
          "ADPCM: v1 books continue through DMEM before wrapping");
}

static void test_oversized_voices(void)
{
    static struct window w;
    unsigned segbase, offset;

    /* 255 frames would be 0x1fe0 samples */
    put_u8(VOICE + VOICE_ADPCM_FRAMES, 0xff);
    put_u8(VOICE + VOICE_ADPCM_FRAMES + 1, 0xff);
    put_u8(VOICE + VOICE_SKIP_SAMPLES, 0x3f);
    put_catsrc(VOICE + VOICE_CATSRC_0, ENCODED, 0xffff, ENCODED, 0xffff);
    put_catsrc(VOICE + VOICE_CATSRC_1, ENCODED, 0xffff, ENCODED, 0xffff);
    memset(&w, GUARD, sizeof(w));
    load_samples_ADPCM(&s_hle, VOICE, w.samples, &segbase, &offset, 0xf00);
    CHECK(segbase <= SAMPLE_BUFFER_SIZE && guards_intact(&w),
          "ADPCM: oversized frame counts and segments stay in the window");

    /* PCM16: 0xffff samples requested in each of two 0xffff-byte segments */
    put_u16(VOICE + VOICE_U16_40, 0xffff);
    put_u16(VOICE + VOICE_U16_42, 0xffff);
    put_u8(VOICE + VOICE_SKIP_SAMPLES, 0xff);
    memset(&w, GUARD, sizeof(w));
    load_samples_PCM16(&s_hle, VOICE, w.samples, &segbase, &offset);
    CHECK(segbase <= SAMPLE_BUFFER_SIZE && guards_intact(&w),
          "PCM16: oversized lengths and segments stay in the window");
}

static void test_base_volume(void)
{
    int32_t base_vol[4];
    int32_t expected[4];
    unsigned k;

    base_vol[0] = 0x40000000;
    base_vol[1] = -0x40000000;
    base_vol[2] = 0x7fff0000;
    base_vol[3] = 0x00012345;
    for (k = 0; k < 4; k++)
        expected[k] = (int32_t)(((int64_t)base_vol[k] * 0xf850) >> 16);

    update_base_vol(&s_hle, base_vol, 0, VOICE, 0, VOICE);
    CHECK(memcmp(base_vol, expected, sizeof(expected)) == 0,
          "base volume: the decay is the exact 0xf850/0x10000 product");
}

int main(void)
{
    s_hle.dram = (unsigned char*)calloc(1, DRAM_BYTES);
    s_hle.dmem = s_dmem;

    test_adpcm_top_book();
    test_oversized_voices();
    test_base_volume();

    free(s_hle.dram);
    if (s_failures) {
        printf("test_musyx_bounds: %d failure(s)\n", s_failures);
        return 1;
    }
    printf("test_musyx_bounds: all checks passed\n");
    return 0;
}
