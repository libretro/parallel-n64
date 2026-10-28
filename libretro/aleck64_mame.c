/* Aleck64 MAME romset loader.
 *
 * Takes a MAME-format set (starsldr.zip, vivdolls.zip, ...), assembles the
 * program rom ("user2" region), and hands back a rom image the regular
 * mupen64plus cart path can boot (rom.c normalizes the v64 byte order).
 * Game layouts transposed from ares' mia/Database/Arcade.bml (ISC license,
 * Copyright (c) 2004-2025 ares team, Near et al).
 *
 * Also extracts a plain rom (.z64/.v64/.n64/.ndd) out of a zip or 7z, since
 * the core sets block_extract and handles archives itself.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <7z/r7z_archive.h>
#include <encodings/utf.h>
#include <streams/file_stream.h>
#include <string/stdstring.h>
#include <zip/rzip_archive.h>

#include <libretro.h>

#include "device/aleck64/aleck64.h"

extern retro_environment_t environ_cb;

/* ---- archive access ----------------------------------------------------
 *
 * Zip and 7z are parsed by libretro-common, from memory or through
 * positioned reads of the file, so an archive on disk is never read whole:
 * a zip gives up its directory and the chosen member's bytes, a 7z its
 * header and the folder holding the member. */

#define A64_NAME_MAX 512
/* smallest member taken for a rom: the header plus the IPL3 */
#define A64_ROM_MIN  0x1000

struct a64_src
{
    RFILE*   fp;
    uint64_t pos;   /* where the handle stands, so sequential reads skip the seek */
};

struct a64_arc
{
    rzip_archive_t* zip;
    r7z_archive_t*  sz;
    uint32_t        count;
};

static int64_t a64_src_read(void* ud, uint64_t off, void* dst, size_t len)
{
    struct a64_src* s = (struct a64_src*)ud;
    int64_t rd;

    if (off != s->pos
        && filestream_seek(s->fp, (int64_t)off, RETRO_VFS_SEEK_POSITION_START) < 0) {
        s->pos = (uint64_t)-1;
        return -1;
    }
    rd     = filestream_read(s->fp, dst, (int64_t)len);
    s->pos = (rd >= 0) ? off + (uint64_t)rd : (uint64_t)-1;
    return rd;
}

int aleck64_archive_kind(const uint8_t* head, size_t len)
{
    static const uint8_t sig_7z[R7Z_SIGNATURE_SIZE] = { '7', 'z', 0xBC, 0xAF, 0x27, 0x1C };

    if (len >= 4 && head[0] == 'P' && head[1] == 'K' && head[2] == 0x03 && head[3] == 0x04)
        return A64_ARCHIVE_ZIP;
    if (len >= R7Z_SIGNATURE_SIZE && memcmp(head, sig_7z, R7Z_SIGNATURE_SIZE) == 0)
        return A64_ARCHIVE_7Z;
    return A64_ARCHIVE_NONE;
}

/* Opens from memory when @data is set, through @src otherwise. */
static int a64_arc_open(struct a64_arc* a, int kind, const uint8_t* data,
                        uint64_t size, struct a64_src* src)
{
    a->zip   = NULL;
    a->sz    = NULL;
    a->count = 0;

    if (kind == A64_ARCHIVE_ZIP) {
        if (rzip_archive_open(&a->zip, data, size,
                              data ? NULL : a64_src_read, src) != RZIP_OK)
            return 0;
        a->count = rzip_archive_num_entries(a->zip);
        return 1;
    }
    if (kind == A64_ARCHIVE_7Z) {
        int st;
        if (data)
            st = (size == (uint64_t)(size_t)size)
                ? r7z_archive_open(&a->sz, data, (size_t)size) : R7Z_ERROR_PARAM;
        else
            st = r7z_archive_open_read(&a->sz, size, a64_src_read, src);
        if (st != R7Z_OK)
            return 0;
        a->count = r7z_archive_num_entries(a->sz);
        return 1;
    }
    return 0;
}

static void a64_arc_close(struct a64_arc* a)
{
    rzip_archive_close(a->zip);
    r7z_archive_close(a->sz);
}

/* Size of a regular member; 0 for a directory or one this build cannot
 * hold in memory. */
static size_t a64_arc_size(const struct a64_arc* a, uint32_t i)
{
    uint64_t size;

    if (a->zip) {
        const rzip_entry_t* e = rzip_archive_entry(a->zip, i);
        if (!e || e->is_dir)
            return 0;
        size = e->size;
    } else {
        const r7z_entry_t* e = r7z_archive_entry(a->sz, i);
        if (!e || e->is_dir)
            return 0;
        size = e->size;
    }
    return (size == (uint64_t)(size_t)size) ? (size_t)size : 0;
}

/* The member's name as UTF-8: the zip's own string, or the 7z's UTF-16 name
 * converted into @buf. NULL when there is none or it does not fit. */
static const char* a64_arc_name(const struct a64_arc* a, uint32_t i, char* buf)
{
    const r7z_entry_t* e;
    size_t in_len = 0, out_len = 0;

    if (a->zip) {
        const rzip_entry_t* z = rzip_archive_entry(a->zip, i);
        return z ? z->name : NULL;
    }
    if (!(e = r7z_archive_entry(a->sz, i)) || !e->name)
        return NULL;
    while (e->name[in_len])
        ++in_len;
    if (!utf16_conv_utf8(NULL, &out_len, e->name, in_len) || out_len >= A64_NAME_MAX)
        return NULL;
    utf16_conv_utf8((uint8_t*)buf, &out_len, e->name, in_len);
    buf[out_len] = '\0';
    return buf;
}

/* Member @i decoded into @dst, which is @size bytes - the member's size. */
static int a64_arc_extract_into(struct a64_arc* a, uint32_t i, uint8_t* dst, size_t size)
{
    size_t len = 0;

    if (a->zip)
        return rzip_archive_extract_into(a->zip, i, dst, size, &len) == RZIP_OK
            && len == size;
    {
        const uint8_t* src = NULL;
        if (r7z_archive_entry_borrow(a->sz, i, &src, &len) != R7Z_OK || len != size)
            return 0;
        memcpy(dst, src, size);
        return 1;
    }
}

/* Member @i in a buffer of its own, for the caller to free(). A 7z member
 * that is its folder's whole output is handed over as the decoded folder,
 * with nothing copied. */
static uint8_t* a64_arc_extract(struct a64_arc* a, uint32_t i, size_t* out_size)
{
    uint8_t* buf = NULL;
    size_t   len = 0;

    if (a->zip) {
        if (rzip_archive_extract(a->zip, i, &buf, &len) != RZIP_OK)
            return NULL;
    } else if (r7z_archive_extract_detach(a->sz, i, &buf, &len) != R7Z_OK)
        return NULL;
    *out_size = len;
    return buf;
}

static int name_ends_with(const char* name, const char* ext)
{
    size_t len = strlen(name), elen = strlen(ext);
    return len >= elen && string_is_equal_noncase(name + len - elen, ext);
}

/* ---- Aleck64 game database (from ares' Arcade.bml, MAME romset 0.273) ---- */

struct a64_file { const char* name; uint32_t offset; };

/* which dipswitch layout the game uses (from ares' game-config/) */
enum a64_dips { DIPS_GENERIC, DIPS_STARSLDR, DIPS_VIVDOLLS, DIPS_HIPAI };

struct a64_game
{
    const char* name;
    /* CRC1/CRC2 of the assembled program rom's N64 header: what identifies a
     * single-file dump (No-Intro), which carries none of MAME's chip names */
    uint32_t crc1, crc2;
    int e90;
    int mahjong;
    int dpad_disabled;
    int dips;
    struct a64_file files[2];
};

static const struct a64_game a64_games[] = {
    { "11beat",   0xC8141D35, 0x4F631668, 0, 0, 1, DIPS_GENERIC,  { { "nus-zhaj.u3",     0x0 }, { NULL, 0 } } },
    { "starsldr", 0x4FEE467A, 0x86EA116C, 0, 0, 0, DIPS_STARSLDR, { { "nus-zhbj-0.u3",   0x0 }, { NULL, 0 } } },
    { "vivdolls", 0xBD188617, 0x80A547E4, 0, 0, 0, DIPS_VIVDOLLS, { { "nus-zsaj-0.u3",   0x0 }, { NULL, 0 } } },
    { "mayjin3",  0x4D6C646B, 0xB98AEEB5, 0, 0, 0, DIPS_GENERIC,  { { "nus-zscj-0.u3",   0x0 }, { NULL, 0 } } },
    { "doncdoon", 0xDA2FCCDF, 0xAB7AA3A6, 0, 0, 0, DIPS_GENERIC,  { { "ua3003-all01.u3", 0x0 }, { "ua3003-alh01.u4", 0x1000000 } } },
    { "kurufev",  0xA719E33C, 0x2AF6D3D9, 0, 0, 0, DIPS_GENERIC,  { { "ua3088-all01.u3", 0x0 }, { "ua3088-alh04.u4", 0x1000000 } } },
    { "twrshaft", 0xBC535CCD, 0x62DE9215, 0, 0, 0, DIPS_GENERIC,  { { "ua3012-all02.u3", 0x0 }, { NULL, 0 } } },
    { "hipai",    0xB921FCDF, 0xF7DF8D9B, 0, 1, 0, DIPS_HIPAI,    { { "ua2011-all02.u3", 0x0 }, { "ua2011-alh02.u4", 0x1000000 } } },
    { "hipai2",   0x2FA70CA1, 0xD4EE5A59, 0, 1, 0, DIPS_HIPAI,    { { "ua3029-all01.u3", 0x0 }, { "ua3029-alh01.u4", 0x1000000 } } },
    { "srmvs",    0xE49F338C, 0x94DE50FF, 0, 1, 0, DIPS_GENERIC,  { { "nus-zsej-1.u2",   0x0 }, { NULL, 0 } } },
    { "srmvsa",   0x0BA69E6C, 0x2F3E6D8D, 0, 1, 0, DIPS_GENERIC,  { { "nus-zsej-0.u2",   0x0 }, { NULL, 0 } } },
    { "mtetrisc", 0xE6FCB468, 0xCFAC7528, 1, 0, 0, DIPS_GENERIC,  { { "nus-zcaj.u4",     0x0 }, { NULL, 0 } } },
};

static const struct a64_game* g_game = NULL;

static void aleck64_select(const struct a64_game* game)
{
    g_aleck64_enabled = 1;
    g_aleck64_e90 = game->e90;
    g_aleck64_mahjong = game->mahjong;
    g_aleck64_dpad_disabled = game->dpad_disabled;
    g_game = game;
    aleck64_apply_dips();
}

/* Loads the rom from an opened archive: a MAME Aleck64 set assembled into
 * its program rom, else the member the frontend named, else the first one
 * with a rom extension. Returns 1 with the image in *out, -1 when the
 * archive holds nothing loadable. Sets the g_aleck64_* game flags when the
 * archive matched a MAME set. */
static int aleck64_load_arc(struct a64_arc* a, const char* prefer,
                            uint8_t** out, size_t* out_size)
{
    static const char* const exts[] = { ".z64", ".v64", ".n64", ".ndd", ".bin", ".u1" };
    char     name[A64_NAME_MAX];
    size_t   g, e;
    uint32_t i;

    g_aleck64_enabled = 0;
    g_aleck64_e90 = 0;
    g_aleck64_mahjong = 0;
    g_aleck64_dpad_disabled = 0;
    g_game = NULL;

    /* MAME Aleck64 set: identified by its program rom filenames */
    for (g = 0; g < sizeof(a64_games) / sizeof(a64_games[0]); ++g) {
        const struct a64_game* game = &a64_games[g];
        const int files = game->files[1].name ? 2 : 1;
        uint32_t  found[2];
        size_t    size[2];
        size_t    total = 0;
        uint8_t*  rom;
        int       f;

        for (f = 0; f < files; ++f) {
            for (i = 0; i < a->count; ++i) {
                const char* n = a64_arc_name(a, i, name);
                if (n && string_is_equal_noncase(n, game->files[f].name))
                    break;
            }
            if (i == a->count || !(size[f] = a64_arc_size(a, i)))
                break;
            found[f] = i;
            if (game->files[f].offset + size[f] > total)
                total = game->files[f].offset + size[f];
        }
        if (f < files)
            continue;

        /* calloc: a short first chip leaves a gap below the second */
        if (!(rom = (uint8_t*)calloc(1, total)))
            return -1;
        for (f = 0; f < files; ++f) {
            if (!a64_arc_extract_into(a, found[f], rom + game->files[f].offset, size[f])) {
                free(rom);
                return -1;
            }
        }

        /* data is in MAME's byte-swapped (v64) order; rom.c normalizes it */
        aleck64_select(game);
        *out      = rom;
        *out_size = total;
        return 1;
    }

    /* The frontend passed "archive#member": load exactly that member, so a
     * multi-rom archive gives the user the rom they picked. When it is absent
     * or unusable, fall through to the scan rather than failing the load. */
    if (prefer != NULL && *prefer != '\0') {
        for (i = 0; i < a->count; ++i) {
            const char* n = a64_arc_name(a, i, name);
            if (n && string_is_equal_noncase(n, prefer) && a64_arc_size(a, i) >= A64_ROM_MIN) {
                if ((*out = a64_arc_extract(a, i, out_size)) != NULL)
                    return 1;
                break;
            }
        }
    }

    for (i = 0; i < a->count; ++i) {
        const char* n = a64_arc_name(a, i, name);
        if (!n || a64_arc_size(a, i) < A64_ROM_MIN)
            continue;
        for (e = 0; e < sizeof(exts) / sizeof(exts[0]); ++e) {
            if (name_ends_with(n, exts[e]))
                return ((*out = a64_arc_extract(a, i, out_size)) != NULL) ? 1 : -1;
        }
    }

    return -1;
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

/* Big-endian 32-bit word at a header offset, whatever order the image is in:
 * z64 is stored big-endian, v64 byte-swapped in 16-bit units, n64 as
 * little-endian 32-bit words. */
static int rom_header_word(const uint8_t* rom, size_t off, uint32_t* out)
{
    const uint8_t* p = rom + off;

    switch ((uint32_t)rom[0] << 24 | (uint32_t)rom[1] << 16 | (uint32_t)rom[2] << 8 | rom[3])
    {
    case 0x80371240: *out = (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; return 1;
    case 0x37804012: *out = (uint32_t)p[1] << 24 | (uint32_t)p[0] << 16 | (uint32_t)p[3] << 8 | p[2]; return 1;
    case 0x40123780: *out = (uint32_t)p[3] << 24 | (uint32_t)p[2] << 16 | (uint32_t)p[1] << 8 | p[0]; return 1;
    default:         return 0;
    }
}

int aleck64_identify_rom(const uint8_t* rom, size_t size)
{
    uint32_t crc1, crc2;
    size_t g;

    if (g_aleck64_enabled)
        return 1;

    /* The archive loader clears the board state before it looks; a plain rom
     * never passes through it, so clear it here too or a game loaded after
     * an E90 title keeps its overlay and savestate block. */
    g_aleck64_e90 = 0;
    g_aleck64_mahjong = 0;
    g_aleck64_dpad_disabled = 0;
    g_game = NULL;

    if (rom == NULL || size < 0x40
        || !rom_header_word(rom, 0x10, &crc1) || !rom_header_word(rom, 0x14, &crc2))
        return 0;

    for (g = 0; g < sizeof(a64_games)/sizeof(a64_games[0]); ++g) {
        if (a64_games[g].crc1 == crc1 && a64_games[g].crc2 == crc2) {
            aleck64_select(&a64_games[g]);
            return 1;
        }
    }
    return 0;
}


int aleck64_load_archive(const uint8_t* data, size_t size, const char* prefer,
                         uint8_t** out, size_t* out_size)
{
    struct a64_arc a;
    int kind = aleck64_archive_kind(data, size);
    int ret;

    if (kind == A64_ARCHIVE_NONE)
        return 0;
    if (!a64_arc_open(&a, kind, data, size, NULL))
        return -1;
    ret = aleck64_load_arc(&a, prefer, out, out_size);
    a64_arc_close(&a);
    return ret;
}

int aleck64_load_archive_file(struct RFILE* fp, uint64_t size, int kind,
                              const char* prefer, uint8_t** out, size_t* out_size)
{
    struct a64_src src;
    struct a64_arc a;
    int64_t pos = filestream_tell(fp);
    int ret;

    src.fp  = fp;
    src.pos = (pos >= 0) ? (uint64_t)pos : (uint64_t)-1;
    if (!a64_arc_open(&a, kind, NULL, size, &src))
        return -1;
    ret = aleck64_load_arc(&a, prefer, out, out_size);
    a64_arc_close(&a);
    return ret;
}
