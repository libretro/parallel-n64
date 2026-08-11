/* Standalone self-check for libretro/aleck64_mame.c
 * (zip parsing, game table, dipswitch defaults, mahjong flag).
 *
 * Build & run from the repo root:
 *   cc -Imupen64plus-core/src -Ilibretro-common/include \
 *      -o /tmp/test_a64 tools/test_aleck64_zip.c libretro/aleck64_mame.c \
 *      libretro-common/encodings/encoding_deflate.c \
 *      libretro-common/encodings/encoding_crc32.c \
 *      libretro-common/features/features_cpu.c \
 *      && /tmp/test_a64
 */
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <encodings/deflate.h>
#include <libretro.h>

#include "device/aleck64/aleck64.h"

/* globals normally defined in aleck64.c / libretro.c (not linked here) */
int g_aleck64_enabled;
int g_aleck64_e90;
int g_aleck64_mahjong;
int g_aleck64_dpad_disabled;
uint8_t g_aleck64_dipswitch[2];
retro_environment_t environ_cb; /* NULL -> aleck64_apply_dips uses defaults */

/* ---- minimal in-memory zip writer (enough for the loader under test) ---- */

struct zipw { uint8_t buf[0x3000000]; size_t len; size_t cd[16]; int n; };

static void w16(uint8_t* p, uint16_t v) { p[0] = v & 0xff; p[1] = v >> 8; }
static void w32(uint8_t* p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }

static void zip_add(struct zipw* z, const char* name, const uint8_t* data, uint32_t usize, int deflate_it)
{
    uint8_t* p = z->buf + z->len;
    uint32_t csize = usize;
    uint16_t method = 0;
    uint16_t nlen = (uint16_t)strlen(name);
    uint8_t* payload = p + 30 + nlen;

    if (deflate_it) {
        /* Bare deflate stream, no wrapper - negative window bits, as zip
         * requires and as the reader expects. */
        void  *stream = rdeflate_new(6, -15);
        size_t produced = 0;

        assert(stream != NULL);
        rdeflate_set_in(stream, data, usize);
        rdeflate_set_out(stream, payload, usize + 0x1000);
        rdeflate_finish(stream);

        for (;;) {
            size_t rd = 0, wn = 0;
            const int st = rdeflate_process(stream, &rd, &wn);

            produced += wn;
            if (st == RDEFLATE_PROCESS_END)
                break;
            assert(st != RDEFLATE_PROCESS_ERROR && (rd != 0 || wn != 0));
        }

        rdeflate_free(stream);
        csize = (uint32_t)produced;
        method = 8;
    }
    else {
        memcpy(payload, data, usize);
    }

    memset(p, 0, 30);
    w32(p, 0x04034b50);
    w16(p + 8, method);
    w32(p + 18, csize);
    w32(p + 22, usize);
    w16(p + 26, nlen);
    memcpy(p + 30, name, nlen);

    z->cd[z->n++] = z->len;
    z->len += 30 + nlen + csize;
}

static void zip_finish(struct zipw* z)
{
    size_t cd_start = z->len;
    int i;
    for (i = 0; i < z->n; ++i) {
        const uint8_t* lh = z->buf + z->cd[i];
        uint16_t nlen = (uint16_t)(lh[26] | (lh[27] << 8));
        uint8_t* p = z->buf + z->len;
        memset(p, 0, 46);
        w32(p, 0x02014b50);
        w16(p + 10, (uint16_t)(lh[8] | (lh[9] << 8)));          /* method */
        w32(p + 20, lh[18] | (lh[19]<<8) | ((uint32_t)lh[20]<<16) | ((uint32_t)lh[21]<<24)); /* csize */
        w32(p + 24, lh[22] | (lh[23]<<8) | ((uint32_t)lh[24]<<16) | ((uint32_t)lh[25]<<24)); /* usize */
        w16(p + 28, nlen);
        w32(p + 42, (uint32_t)z->cd[i]);
        memcpy(p + 46, lh + 30, nlen);
        z->len += 46 + nlen;
    }
    {
        uint8_t* p = z->buf + z->len;
        memset(p, 0, 22);
        w32(p, 0x06054b50);
        w16(p + 10, (uint16_t)z->n);
        w32(p + 12, (uint32_t)(z->len - cd_start));
        w32(p + 16, (uint32_t)cd_start);
        z->len += 22;
    }
}

static uint8_t* v64_file(uint32_t size)
{
    uint8_t* d = calloc(1, size);
    d[0] = 0x37; d[1] = 0x80; d[2] = 0x40; d[3] = 0x12; /* byte-swapped z64 header */
    d[0x100] = 'A'; d[0x101] = 'B';
    return d;
}

int main(void)
{
    static struct zipw z;
    uint8_t* rom;
    size_t rom_size;
    uint8_t* f;

    /* 1: single-file set, deflate entries (vivdolls) */
    memset(&z, 0, sizeof(z)); f = v64_file(0x10000);
    zip_add(&z, "nus-zsaj-0.u3", f, 0x10000, 1);
    zip_finish(&z); free(f);
    assert(aleck64_load_zip(z.buf, z.len, &rom, &rom_size) == 1);
    assert(g_aleck64_enabled == 1 && g_aleck64_e90 == 0 && g_aleck64_mahjong == 0);
    assert(rom_size == 0x10000);
    assert(rom[0] == 0x37 && rom[1] == 0x80 && rom[2] == 0x40 && rom[3] == 0x12);
    assert(rom[0x100] == 'A' && rom[0x101] == 'B');
    /* vivdolls dip defaults (ares): everything stays 1 */
    assert(g_aleck64_dipswitch[0] == 0xff && g_aleck64_dipswitch[1] == 0xff);
    free(rom);

    /* 2: starsldr, stored entry; dip defaults differ from 0xff */
    memset(&z, 0, sizeof(z)); f = v64_file(0x10000);
    zip_add(&z, "nus-zhbj-0.u3", f, 0x10000, 0);
    zip_finish(&z); free(f);
    assert(aleck64_load_zip(z.buf, z.len, &rom, &rom_size) == 1);
    assert(g_aleck64_enabled == 1);
    /* d0: coin=7, players 3 (3<<3), autolevel 3<<5, joystick 2D (bit7=0) = 0x7f
     * d1: diff 3, extend 3<<2, rapid 0, demo on (bit5=0), english (bit6=1), test off (bit7=1) = 0xcf */
    assert(g_aleck64_dipswitch[0] == 0x7f);
    assert(g_aleck64_dipswitch[1] == 0xcf);
    free(rom);

    /* 3: two-file set at offsets 0x0/0x1000000 (doncdoon) */
    memset(&z, 0, sizeof(z)); f = v64_file(0x10000);
    zip_add(&z, "ua3003-all01.u3", f, 0x10000, 0);
    f[0] = 'H'; f[1] = 'I';
    zip_add(&z, "ua3003-alh01.u4", f, 0x10000, 0);
    zip_finish(&z); free(f);
    assert(aleck64_load_zip(z.buf, z.len, &rom, &rom_size) == 1);
    assert(rom_size == 0x1000000 + 0x10000);
    assert(rom[0] == 0x37);
    assert(rom[0x1000000] == 'H' && rom[0x1000001] == 'I');
    free(rom);

    /* 4: mahjong game (hipai): flag + dip defaults */
    memset(&z, 0, sizeof(z)); f = v64_file(0x10000);
    zip_add(&z, "ua2011-all02.u3", f, 0x10000, 0);
    zip_add(&z, "ua2011-alh02.u4", f, 0x10000, 0);
    zip_finish(&z); free(f);
    assert(aleck64_load_zip(z.buf, z.len, &rom, &rom_size) == 1);
    assert(g_aleck64_mahjong == 1);
    /* d0: coin 7, freeplay off (bit7=1) = 0xff
     * d1: diff Normal=7, kuitan 0, continue 0, demo on (bit5=0), bit6=1, test off = 0xc7 */
    assert(g_aleck64_dipswitch[0] == 0xff);
    assert(g_aleck64_dipswitch[1] == 0xc7);
    free(rom);

    /* 5: plain z64 inside a zip (block_extract compatibility path) */
    memset(&z, 0, sizeof(z));
    f = calloc(1, 0x2000);
    f[0] = 0x80; f[1] = 0x37; f[2] = 0x12; f[3] = 0x40;
    zip_add(&z, "somegame.z64", f, 0x2000, 1);
    zip_finish(&z); free(f);
    assert(aleck64_load_zip(z.buf, z.len, &rom, &rom_size) == 1);
    assert(g_aleck64_enabled == 0);
    assert(rom_size == 0x2000 && rom[0] == 0x80 && rom[1] == 0x37);
    free(rom);

    /* 6: unrelated zip -> rejected */
    memset(&z, 0, sizeof(z));
    zip_add(&z, "readme.txt", (const uint8_t*)"hello", 5, 0);
    zip_finish(&z);
    assert(aleck64_load_zip(z.buf, z.len, &rom, &rom_size) == 0);
    assert(g_aleck64_enabled == 0);

    printf("aleck64 zip loader: all checks passed\n");
    return 0;
}
