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

#include <encodings/deflate.h>
#include <streams/file_stream.h>

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

/* ---- byte source ------------------------------------------------------
 *
 * A zip is read through this rather than from one whole-file buffer.  A zip
 * only needs three regions to yield an entry: the end-of-central-directory
 * record in the tail, the central directory, and the entry's own compressed
 * bytes.  Slurping the archive to reach them costs a full read and a
 * full-size allocation on top of the rom buffer itself, and for a MAME set
 * or a multi-rom archive most of what it reads is never looked at.
 *
 * The memory backend keeps the old in-memory entry points working (the
 * frontend sometimes hands the core the data directly, and the standalone
 * test harness builds archives in memory); the file backend seeks. */
struct zsrc
{
    const uint8_t* mem;   /* non-NULL: memory backend */
    RFILE*         fp;    /* non-NULL: file backend   */
    int64_t        size;
};

/* Read n bytes at off into dst. Returns 0 on success. */
static int zsrc_read(struct zsrc* z, int64_t off, void* dst, size_t n)
{
    if (off < 0 || n > (uint64_t)(z->size - off))
        return -1;
    if (z->mem) {
        memcpy(dst, z->mem + off, n);
        return 0;
    }
    if (filestream_seek(z->fp, off, RETRO_VFS_SEEK_POSITION_START) < 0)
        return -1;
    return (filestream_read(z->fp, dst, (int64_t)n) == (int64_t)n) ? 0 : -1;
}

/* Borrow a span without copying when possible: the memory backend can point
 * straight at it, the file backend has to stage it in the caller's buffer. */
static const uint8_t* zsrc_map(struct zsrc* z, int64_t off, size_t n,
                               uint8_t* stage)
{
    if (off < 0 || n > (uint64_t)(z->size - off))
        return NULL;
    if (z->mem)
        return z->mem + off;
    return zsrc_read(z, off, stage, n) == 0 ? stage : NULL;
}

/* Locate the end-of-central-directory record and read the central directory.
 * Returns entry count and, for the file backend, the buffer holding the
 * directory (caller frees via *cd_owned); entry names point into it.
 *
 * The EOCD lives in the last 22 bytes plus up to a 64 KiB comment, so only
 * that tail is examined - not the archive.  The backwards scan is over that
 * tail only and stops at the first signature, which for a comment-less
 * archive is the very first position tried. */
static int zip_list_src(struct zsrc* z, struct zip_entry* entries, int max,
                        uint8_t** cd_owned)
{
    uint8_t  tail[66*1024];
    uint8_t* cd = NULL;
    size_t   tail_len, i;
    int64_t  tail_off;
    const uint8_t* eocd = NULL;
    uint32_t cd_off, cd_size;
    uint16_t count;
    int n = 0;

    *cd_owned = NULL;

    if (z->size < 22)
        return 0;

    /* Two stages: an archive with no comment - which is nearly all of them -
     * ends with the 22-byte record itself, so probe a small tail first and
     * only pay for the full 64 KiB comment allowance when that misses.  A
     * fixed 64 KiB probe made the single-entry case read more from disk than
     * slurping the archive did. */
    {
        size_t probe = 4096;

        for (;;) {
            tail_len = (size_t)((z->size < (int64_t)probe) ? z->size : (int64_t)probe);
            tail_off = z->size - (int64_t)tail_len;
            if (zsrc_read(z, tail_off, tail, tail_len) != 0)
                return 0;

            for (i = tail_len - 22; ; --i) {
                if (tail[i] == 0x50 && rd32(tail + i) == 0x06054b50) { eocd = tail + i; break; }
                if (i == 0) break;
            }

            if (eocd != NULL || tail_len == (size_t)z->size || probe == sizeof(tail))
                break;
            probe = sizeof(tail);
        }
    }
    if (eocd == NULL)
        return 0;

    count   = rd16(eocd + 10);
    cd_size = rd32(eocd + 12);
    cd_off  = rd32(eocd + 16);

    if (count == 0 || cd_size == 0 || (int64_t)cd_off + cd_size > z->size)
        return 0;

    if (z->mem) {
        cd = (uint8_t*)(uintptr_t)(z->mem + cd_off);
    } else {
        if (!(cd = (uint8_t*)malloc(cd_size)))
            return 0;
        if (zsrc_read(z, cd_off, cd, cd_size) != 0) {
            free(cd);
            return 0;
        }
        *cd_owned = cd;
    }

    {
        uint32_t p = 0;
        while (n < count && n < max && p + 46 <= cd_size) {
            const uint8_t* e = cd + p;
            if (rd32(e) != 0x02014b50)
                break;
            entries[n].method    = rd16(e + 10);
            entries[n].csize     = rd32(e + 20);
            entries[n].usize     = rd32(e + 24);
            entries[n].name_len  = rd16(e + 28);
            entries[n].local_off = rd32(e + 42);
            entries[n].name      = (const char*)(e + 46);
            if (p + 46u + entries[n].name_len > cd_size)
                break;
            p += 46u + entries[n].name_len + rd16(e + 30) + rd16(e + 32);
            ++n;
        }
    }
    return n;
}

/* Decompress an entry into dst (usize bytes); returns 0 on success.
 *
 * The compressed bytes are streamed through a fixed window straight into the
 * destination, so the archive is never held in memory: peak cost is the rom
 * buffer plus this window, rather than the rom buffer plus the whole file.
 * The memory backend feeds rinflate the span directly, with no window and no
 * copy at all. */
#define ZIP_IN_WINDOW (64 * 1024)

static int zip_extract_src(struct zsrc* z, const struct zip_entry* e, uint8_t* dst)
{
    uint8_t  lh[30];
    int64_t  data_off;

    if ((int64_t)e->local_off + 30 > z->size)
        return -1;
    if (zsrc_read(z, e->local_off, lh, sizeof(lh)) != 0 || rd32(lh) != 0x04034b50)
        return -1;

    data_off = (int64_t)e->local_off + 30 + rd16(lh + 26) + rd16(lh + 28);
    if (data_off + e->csize > z->size)
        return -1;

    if (e->method == 0) {
        if (e->csize != e->usize)
            return -1;
        return zsrc_read(z, data_off, dst, e->usize);
    }

    if (e->method == 8) {
        /* Zip stores a bare deflate stream with no wrapper, which is what
         * negative window bits select - the same thing -MAX_WBITS meant to
         * inflateInit2(). */
        void*    stream = rinflate_new(-15);
        uint8_t* window = NULL;
        uint32_t left   = e->csize;
        size_t   produced = 0;
        int      ok = 0;

        if (!stream)
            return -1;

        if (!z->mem && !(window = (uint8_t*)malloc(ZIP_IN_WINDOW))) {
            rinflate_free(stream);
            return -1;
        }

        rinflate_set_out(stream, dst, e->usize);

        if (z->mem) {
            rinflate_set_in(stream, z->mem + data_off, left);
            left = 0;
        }

        for (;;) {
            size_t rd = 0, wn = 0;
            int    st;

            /* Refill only when the decoder has drained the window. */
            if (window && left > 0 && produced < e->usize) {
                size_t want = (left < ZIP_IN_WINDOW) ? left : ZIP_IN_WINDOW;
                if (zsrc_read(z, data_off, window, want) != 0)
                    break;
                rinflate_set_in(stream, window, want);
                data_off += (int64_t)want;
                left     -= (uint32_t)want;
            }

            st = rinflate_process(stream, &rd, &wn);
            produced += wn;

            if (st == RDEFLATE_PROCESS_END) {
                ok = 1;
                break;
            }
            if (st == RDEFLATE_PROCESS_ERROR)
                break;
            /* No progress and nothing left to feed: truncated stream. */
            if (rd == 0 && wn == 0 && left == 0)
                break;
        }

        free(window);
        rinflate_free(stream);
        return (ok && produced == e->usize) ? 0 : -1;
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

#define A64_MAX_ENTRIES 64

/* Returns 1 and fills out and out_size (malloc'd) when the zip produced a rom
 * image; 0 otherwise. Sets the g_aleck64_* game flags when the zip matched a
 * MAME Aleck64 set. */
static int aleck64_load_zip_src(struct zsrc* z, const char* prefer,
                                uint8_t** out, size_t* out_size)
{
    struct zip_entry entries[A64_MAX_ENTRIES];
    uint8_t* cd_owned = NULL;
    int n, i, g, f;
    int ret = 0;

    g_aleck64_enabled = 0;
    g_aleck64_e90 = 0;
    g_aleck64_mahjong = 0;
    g_aleck64_dpad_disabled = 0;
    g_game = NULL;

    n = zip_list_src(z, entries, A64_MAX_ENTRIES, &cd_owned);
    if (n == 0)
        goto done;

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
            goto done;
        for (f = 0; f < 2 && game->files[f].name != NULL; ++f) {
            if (zip_extract_src(z, found[f], rom + game->files[f].offset) != 0) {
                free(rom);
                goto done;
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
        ret = 1;
        goto done;
    }

    aleck64_apply_dips();

    /* The frontend passed "archive.zip#entry": load exactly that entry, so a
     * multi-rom archive gives the user the rom they picked rather than
     * whichever one happens to come first. */
    if (prefer != NULL && *prefer != '\0') {
        for (i = 0; i < n; ++i) {
            if (name_is(&entries[i], prefer) && entries[i].usize >= 0x1000) {
                uint8_t* rom = (uint8_t*)malloc(entries[i].usize);
                if (rom == NULL)
                    goto done;
                if (zip_extract_src(z, &entries[i], rom) != 0) {
                    free(rom);
                    goto done;
                }
                *out = rom;
                *out_size = entries[i].usize;
                ret = 1;
                goto done;
            }
        }
        /* Named entry absent or unusable: fall through to the scan below
         * rather than failing the load outright. */
    }

    /* plain rom inside a zip (the frontend no longer extracts for us) */
    for (i = 0; i < n; ++i) {
        static const char* exts[] = { ".z64", ".v64", ".n64", ".ndd", ".bin", ".u1" };
        size_t e;
        for (e = 0; e < sizeof(exts)/sizeof(exts[0]); ++e) {
            if (name_ends_with(&entries[i], exts[e]) && entries[i].usize >= 0x1000) {
                uint8_t* rom = (uint8_t*)malloc(entries[i].usize);
                if (rom == NULL)
                    goto done;
                if (zip_extract_src(z, &entries[i], rom) != 0) {
                    free(rom);
                    goto done;
                }
                *out = rom;
                *out_size = entries[i].usize;
                ret = 1;
                goto done;
            }
        }
    }

done:
    free(cd_owned);
    return ret;
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

int aleck64_load_zip_named(const uint8_t* data, size_t size, const char* prefer,
                           uint8_t** out, size_t* out_size)
{
    struct zsrc z;
    z.mem  = data;
    z.fp   = NULL;
    z.size = (int64_t)size;
    return aleck64_load_zip_src(&z, prefer, out, out_size);
}

int aleck64_load_zip(const uint8_t* data, size_t size, uint8_t** out, size_t* out_size)
{
    return aleck64_load_zip_named(data, size, NULL, out, out_size);
}

/* Path-based load: the archive is read where it lies, so nothing larger than
 * the rom itself is ever allocated. */
int aleck64_load_zip_path(const char* path, const char* prefer,
                          uint8_t** out, size_t* out_size)
{
    struct zsrc z;
    int ret;

    z.mem = NULL;
    z.fp  = filestream_open(path, RETRO_VFS_FILE_ACCESS_READ,
                            RETRO_VFS_FILE_ACCESS_HINT_NONE);
    if (!z.fp)
        return 0;

    z.size = filestream_get_size(z.fp);
    ret = (z.size > 0) ? aleck64_load_zip_src(&z, prefer, out, out_size) : 0;

    filestream_close(z.fp);
    return ret;
}
