/* Aleck64 MAME romset loader.
 *
 * Takes a MAME-format .zip (starsldr.zip, vivdolls.zip, ...), assembles the
 * program rom ("user2" region), and hands back a rom image the regular
 * mupen64plus cart path can boot (rom.c normalizes the v64 byte order).
 * Game layouts transposed from ares' mia/Database/Arcade.bml (ISC license,
 * Copyright (c) 2004-2025 ares team, Near et al).
 *
 * Also extracts a plain rom (.z64/.v64/.n64/.ndd) out of a zip, since the
 * core sets block_extract and must handle zipped roms itself.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <zlib.h>

#include "device/aleck64/aleck64.h"

#ifdef _MSC_VER
#define strncasecmp _strnicmp
#endif

/* ---- minimal in-memory zip reader (stored + deflate) ---- */

static uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t* p) { return p[0] | (p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

struct zip_entry
{
    const char* name;    /* points into central directory, NOT nul-terminated */
    uint16_t name_len;
    uint16_t method;
    uint32_t csize;
    uint32_t usize;
    uint32_t local_off;
};

/* iterate central directory; returns entry count, fills entries up to max */
static int zip_list(const uint8_t* data, size_t size, struct zip_entry* entries, int max)
{
    size_t i, scan;
    const uint8_t* eocd = NULL;
    uint32_t cd_off;
    uint16_t count;
    int n = 0;

    if (size < 22)
        return 0;

    scan = (size > 22 + 0xffff) ? size - (22 + 0xffff) : 0;
    for (i = size - 22; ; --i) {
        if (data[i] == 0x50 && rd32(data + i) == 0x06054b50) { eocd = data + i; break; }
        if (i == scan) break;
    }
    if (eocd == NULL)
        return 0;

    count  = rd16(eocd + 10);
    cd_off = rd32(eocd + 16);

    while (n < count && n < max && cd_off + 46 <= size) {
        const uint8_t* e = data + cd_off;
        if (rd32(e) != 0x02014b50)
            break;
        entries[n].method    = rd16(e + 10);
        entries[n].csize     = rd32(e + 20);
        entries[n].usize     = rd32(e + 24);
        entries[n].name_len  = rd16(e + 28);
        entries[n].local_off = rd32(e + 42);
        entries[n].name      = (const char*)(e + 46);
        cd_off += 46 + entries[n].name_len + rd16(e + 30) + rd16(e + 32);
        ++n;
    }
    return n;
}

/* decompress entry into dst (usize bytes); returns 0 on success */
static int zip_extract(const uint8_t* data, size_t size, const struct zip_entry* e, uint8_t* dst)
{
    const uint8_t* lh = data + e->local_off;
    const uint8_t* src;
    uint32_t data_off;

    if (e->local_off + 30 > size || rd32(lh) != 0x04034b50)
        return -1;
    data_off = e->local_off + 30 + rd16(lh + 26) + rd16(lh + 28);
    if (data_off + e->csize > size)
        return -1;
    src = data + data_off;

    if (e->method == 0) {
        if (e->csize != e->usize)
            return -1;
        memcpy(dst, src, e->usize);
        return 0;
    }

    if (e->method == 8) {
        z_stream s;
        int ret;
        memset(&s, 0, sizeof(s));
        if (inflateInit2(&s, -MAX_WBITS) != Z_OK)
            return -1;
        s.next_in   = (Bytef*)src;
        s.avail_in  = e->csize;
        s.next_out  = dst;
        s.avail_out = e->usize;
        ret = inflate(&s, Z_FINISH);
        inflateEnd(&s);
        return (ret == Z_STREAM_END && s.total_out == e->usize) ? 0 : -1;
    }

    return -1;
}

static int name_is(const struct zip_entry* e, const char* name)
{
    size_t len = strlen(name);
    if (e->name_len != len)
        return 0;
    return strncasecmp(e->name, name, len) == 0;
}

static int name_ends_with(const struct zip_entry* e, const char* ext)
{
    size_t len = strlen(ext);
    if (e->name_len < len)
        return 0;
    return strncasecmp(e->name + e->name_len - len, ext, len) == 0;
}

/* ---- Aleck64 game database (from ares' Arcade.bml, MAME romset 0.273) ---- */

struct a64_file { const char* name; uint32_t offset; };

struct a64_game
{
    const char* name;
    int e90;
    struct a64_file files[2];
};

static const struct a64_game a64_games[] = {
    { "11beat",   0, { { "nus-zhaj.u3",     0x0 }, { NULL, 0 } } },
    { "starsldr", 0, { { "nus-zhbj-0.u3",   0x0 }, { NULL, 0 } } },
    { "vivdolls", 0, { { "nus-zsaj-0.u3",   0x0 }, { NULL, 0 } } },
    { "mayjin3",  0, { { "nus-zscj-0.u3",   0x0 }, { NULL, 0 } } },
    { "doncdoon", 0, { { "ua3003-all01.u3", 0x0 }, { "ua3003-alh01.u4", 0x1000000 } } },
    { "kurufev",  0, { { "ua3088-all01.u3", 0x0 }, { "ua3088-alh04.u4", 0x1000000 } } },
    { "twrshaft", 0, { { "ua3012-all02.u3", 0x0 }, { NULL, 0 } } },
    { "hipai",    0, { { "ua2011-all02.u3", 0x0 }, { "ua2011-alh02.u4", 0x1000000 } } },
    { "hipai2",   0, { { "ua3029-all01.u3", 0x0 }, { "ua3029-alh01.u4", 0x1000000 } } },
    { "srmvs",    0, { { "nus-zsej-1.u2",   0x0 }, { NULL, 0 } } },
    { "srmvsa",   0, { { "nus-zsej-0.u2",   0x0 }, { NULL, 0 } } },
    { "mtetrisc", 1, { { "nus-zcaj.u4",     0x0 }, { NULL, 0 } } },
};

#define A64_MAX_ENTRIES 64

/* Returns 1 and sets *out/*out_size (malloc'd) when the zip produced a rom
 * image; 0 otherwise. Sets g_aleck64_enabled/g_aleck64_e90 when the zip
 * matched a MAME Aleck64 set. */
int aleck64_load_zip(const uint8_t* data, size_t size, uint8_t** out, size_t* out_size)
{
    struct zip_entry entries[A64_MAX_ENTRIES];
    int n, i, g, f;

    g_aleck64_enabled = 0;
    g_aleck64_e90 = 0;

    n = zip_list(data, size, entries, A64_MAX_ENTRIES);
    if (n == 0)
        return 0;

    /* MAME Aleck64 set: identified by its program rom filename */
    for (g = 0; g < (int)(sizeof(a64_games)/sizeof(a64_games[0])); ++g) {
        const struct a64_game* game = &a64_games[g];
        const struct zip_entry* found[2] = { NULL, NULL };
        size_t total = 0;
        uint8_t* rom;

        for (f = 0; f < 2 && game->files[f].name != NULL; ++f) {
            for (i = 0; i < n; ++i) {
                if (name_is(&entries[i], game->files[f].name)) { found[f] = &entries[i]; break; }
            }
            if (found[f] == NULL)
                break;
            if (game->files[f].offset + found[f]->usize > total)
                total = game->files[f].offset + found[f]->usize;
        }
        if (found[0] == NULL || (game->files[1].name != NULL && found[1] == NULL))
            continue;

        rom = (uint8_t*)malloc(total);
        if (rom == NULL)
            return 0;
        for (f = 0; f < 2 && game->files[f].name != NULL; ++f) {
            if (zip_extract(data, size, found[f], rom + game->files[f].offset) != 0) {
                free(rom);
                return 0;
            }
        }

        /* data is in MAME's byte-swapped (v64) order; rom.c normalizes it */
        g_aleck64_enabled = 1;
        g_aleck64_e90 = game->e90;
        *out = rom;
        *out_size = total;
        return 1;
    }

    /* plain rom inside a zip (the frontend no longer extracts for us) */
    for (i = 0; i < n; ++i) {
        static const char* exts[] = { ".z64", ".v64", ".n64", ".ndd", ".bin", ".u1" };
        size_t e;
        for (e = 0; e < sizeof(exts)/sizeof(exts[0]); ++e) {
            if (name_ends_with(&entries[i], exts[e]) && entries[i].usize >= 0x1000) {
                uint8_t* rom = (uint8_t*)malloc(entries[i].usize);
                if (rom == NULL)
                    return 0;
                if (zip_extract(data, size, &entries[i], rom) != 0) {
                    free(rom);
                    return 0;
                }
                *out = rom;
                *out_size = entries[i].usize;
                return 1;
            }
        }
    }

    return 0;
}
