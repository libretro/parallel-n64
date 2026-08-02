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

#include <libretro.h>

#include "device/aleck64/aleck64.h"

#ifdef _MSC_VER
#define strncasecmp _strnicmp
#endif

extern retro_environment_t environ_cb;

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
        if (cd_off + 46 + entries[n].name_len > size)
            break;
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

/* which dipswitch layout the game uses (from ares' game-config/) */
enum a64_dips { DIPS_GENERIC, DIPS_STARSLDR, DIPS_VIVDOLLS, DIPS_HIPAI };

struct a64_game
{
    const char* name;
    int e90;
    int mahjong;
    int dpad_disabled;
    int dips;
    struct a64_file files[2];
};

static const struct a64_game a64_games[] = {
    { "11beat",   0, 0, 1, DIPS_GENERIC,  { { "nus-zhaj.u3",     0x0 }, { NULL, 0 } } },
    { "starsldr", 0, 0, 0, DIPS_STARSLDR, { { "nus-zhbj-0.u3",   0x0 }, { NULL, 0 } } },
    { "vivdolls", 0, 0, 0, DIPS_VIVDOLLS, { { "nus-zsaj-0.u3",   0x0 }, { NULL, 0 } } },
    { "mayjin3",  0, 0, 0, DIPS_GENERIC,  { { "nus-zscj-0.u3",   0x0 }, { NULL, 0 } } },
    { "doncdoon", 0, 0, 0, DIPS_GENERIC,  { { "ua3003-all01.u3", 0x0 }, { "ua3003-alh01.u4", 0x1000000 } } },
    { "kurufev",  0, 0, 0, DIPS_GENERIC,  { { "ua3088-all01.u3", 0x0 }, { "ua3088-alh04.u4", 0x1000000 } } },
    { "twrshaft", 0, 0, 0, DIPS_GENERIC,  { { "ua3012-all02.u3", 0x0 }, { NULL, 0 } } },
    { "hipai",    0, 1, 0, DIPS_HIPAI,    { { "ua2011-all02.u3", 0x0 }, { "ua2011-alh02.u4", 0x1000000 } } },
    { "hipai2",   0, 1, 0, DIPS_HIPAI,    { { "ua3029-all01.u3", 0x0 }, { "ua3029-alh01.u4", 0x1000000 } } },
    { "srmvs",    0, 1, 0, DIPS_GENERIC,  { { "nus-zsej-1.u2",   0x0 }, { NULL, 0 } } },
    { "srmvsa",   0, 1, 0, DIPS_GENERIC,  { { "nus-zsej-0.u2",   0x0 }, { NULL, 0 } } },
    { "mtetrisc", 1, 0, 0, DIPS_GENERIC,  { { "nus-zcaj.u4",     0x0 }, { NULL, 0 } } },
};

static const struct a64_game* g_game = NULL;

/* PIF boot rom from the romset (pifdata.bin), for the LLE boot */
static uint8_t l_pifdata[2048];
static int l_pifdata_present = 0;

int aleck64_pifdata(const uint8_t** out)
{
    *out = l_pifdata;
    return l_pifdata_present ? (int)sizeof(l_pifdata) : 0;
}

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
    g_aleck64_mahjong = 0;
    g_aleck64_dpad_disabled = 0;
    g_game = NULL;
    l_pifdata_present = 0;

    n = zip_list(data, size, entries, A64_MAX_ENTRIES);
    if (n == 0)
        return 0;

    for (i = 0; i < n; ++i) {
        if (name_is(&entries[i], "pifdata.bin") && entries[i].usize == sizeof(l_pifdata)) {
            if (zip_extract(data, size, &entries[i], l_pifdata) == 0)
                l_pifdata_present = 1;
            break;
        }
    }

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
        g_aleck64_mahjong = game->mahjong;
        g_aleck64_dpad_disabled = game->dpad_disabled;
        g_game = game;
        aleck64_apply_dips();
        *out = rom;
        *out_size = total;
        return 1;
    }

    aleck64_apply_dips();

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

/* ---- dipswitches, driven by the core options ---- */

static const char* opt(const char* key, const char* dflt)
{
    struct retro_variable var;
    var.key = key;
    var.value = NULL;
    if (environ_cb != NULL && environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value != NULL)
        return var.value;
    return dflt;
}

static int opt_is(const char* key, const char* dflt, const char* value)
{
    return strcmp(opt(key, dflt), value) == 0;
}

/* option value index in list, or 0 (= default first entry) if not found */
static unsigned opt_index(const char* key, const char* dflt, const char* const* values, unsigned count)
{
    const char* v = opt(key, dflt);
    unsigned i;
    for (i = 0; i < count; ++i) {
        if (strcmp(v, values[i]) == 0)
            return i;
    }
    return 0;
}

#define SET_BITS(d, shift, mask, val) ((d) = ((d) & ~((mask) << (shift))) | (((val) & (mask)) << (shift)))

static const char* const coinage_values[8] = {
    "1 Coin 1 Credit",  "1 Coin 2 Credits", "1 Coin 3 Credits", "1 Coin 4 Credits",
    "2 Coins 1 Credit", "3 Coins 1 Credit", "4 Coins 1 Credit", "5 Coins 1 Credit"
};
static const char* const difficulty_values[4] = { "Normal", "Easy", "Hard", "Hardest" };
static const char* const four_to_one[4] = { "4", "3", "2", "1" };

/* Dip meanings and encodings per game, from ares' game-config/. All bits
 * default to 1; the settings below overwrite their fields exactly like ares'
 * setting->modify(default) chain does at power-on. */
void aleck64_apply_dips(void)
{
    uint8_t d0 = 0xff, d1 = 0xff;
    unsigned i;

    if (g_game == NULL)
        return;

    /* test mode: dip2 bit7 on every game that defines dips */
    SET_BITS(d1, 7, 0x1, !opt_is("parallel-n64-aleck64-testmode", "disabled", "enabled"));

    switch (g_game->dips)
    {
    case DIPS_STARSLDR: {
        static const uint8_t coin_enc[8] = { 7, 6, 5, 4, 3, 2, 1, 0 };
        static const char* const players[4] = { "3", "4", "2", "1" };
        static const char* const autolevel[4] = { "Normal", "Slow", "Fast1", "Fast2" };
        static const char* const extend[4] = { "30,000,000", "50,000,000", "70,000,000", "None" };
        SET_BITS(d0, 0, 0x7, coin_enc[opt_index("parallel-n64-aleck64-coinage", coinage_values[0], coinage_values, 8)]);
        i = opt_index("parallel-n64-aleck64-players", players[0], players, 4);
        SET_BITS(d0, 3, 0x3, 3 - i);
        i = opt_index("parallel-n64-aleck64-autolevel", autolevel[0], autolevel, 4);
        SET_BITS(d0, 5, 0x3, 3 - i);
        SET_BITS(d0, 7, 0x1, opt_is("parallel-n64-aleck64-joystick", "2D", "3D"));
        i = opt_index("parallel-n64-aleck64-difficulty", difficulty_values[0], difficulty_values, 4);
        SET_BITS(d1, 0, 0x3, 3 - i);
        i = opt_index("parallel-n64-aleck64-extend", extend[0], extend, 4);
        SET_BITS(d1, 2, 0x3, 3 - i);
        SET_BITS(d1, 4, 0x1, opt_is("parallel-n64-aleck64-rapid", "disabled", "enabled"));
        SET_BITS(d1, 5, 0x1, !opt_is("parallel-n64-aleck64-demosound", "enabled", "enabled"));
        SET_BITS(d1, 6, 0x1, !opt_is("parallel-n64-aleck64-language", "English", "Japanese"));
        break;
    }

    case DIPS_VIVDOLLS: {
        static const uint8_t coin_enc[8] = { 7, 3, 5, 1, 6, 2, 4, 0 };
        SET_BITS(d0, 0, 0x7, coin_enc[opt_index("parallel-n64-aleck64-coinage", coinage_values[0], coinage_values, 8)]);
        i = opt_index("parallel-n64-aleck64-difficulty", difficulty_values[0], difficulty_values, 4);
        SET_BITS(d1, 0, 0x3, 3 - i);
        i = opt_index("parallel-n64-aleck64-lives", four_to_one[0], four_to_one, 4);
        SET_BITS(d1, 2, 0x3, 3 - i);
        break;
    }

    case DIPS_HIPAI: {
        static const uint8_t coin_enc[8] = { 7, 3, 5, 4, 6, 2, 1, 0 };
        static const uint8_t diff_enc[4] = { 7, 4, 2, 0 }; /* Normal, Easy, Hard, Most Hard */
        SET_BITS(d0, 0, 0x7, coin_enc[opt_index("parallel-n64-aleck64-coinage", coinage_values[0], coinage_values, 8)]);
        SET_BITS(d0, 7, 0x1, !opt_is("parallel-n64-aleck64-freeplay", "disabled", "enabled"));
        SET_BITS(d1, 0, 0x7, diff_enc[opt_index("parallel-n64-aleck64-difficulty", difficulty_values[0], difficulty_values, 4)]);
        SET_BITS(d1, 3, 0x1, opt_is("parallel-n64-aleck64-kuitan", "disabled", "enabled"));
        SET_BITS(d1, 4, 0x1, opt_is("parallel-n64-aleck64-continue", "disabled", "enabled"));
        SET_BITS(d1, 5, 0x1, !opt_is("parallel-n64-aleck64-demosound", "enabled", "enabled"));
        break;
    }

    default:
        break;
    }

    g_aleck64_dipswitch[0] = d0;
    g_aleck64_dipswitch[1] = d1;
}
