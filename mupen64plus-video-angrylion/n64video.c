#include "n64video.h"
#include "common.h"
#include "msg.h"
#include "vdac.h"
#include "parallel_al.h"

#include <memory.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#ifdef HAVE_RDP_DUMP
#include "rdp_dump.h"
#endif

#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#define CLAMP(x, lo, hi) (((x) > (hi)) ? (hi) : (((x) < (lo)) ? (lo) : (x)))

#define SIGN16(x)   ((int16_t)(x))
#define SIGN8(x)    ((int8_t)(x))
#define SIGN(x, numb)	(((x) & ((1 << (numb)) - 1)) | -((x) & (1 << ((numb) - 1))))
#define SIGNF(x, numb)	((x) | -((x) & (1 << ((numb) - 1))))

#define TRELATIVE(x, y)     ((x) - ((y) << 3))
#define PIXELS_TO_BYTES(pix, siz) (((pix) << (siz)) >> 1)

// RGBA5551 to RGBA8888 helper
#define RGBA16_R(x) (((x) >> 8) & 0xf8)
#define RGBA16_G(x) (((x) & 0x7c0) >> 3)
#define RGBA16_B(x) (((x) & 0x3e) << 2)

// RGBA8888 helper
#define RGBA32_R(x) (((x) >> 24) & 0xff)
#define RGBA32_G(x) (((x) >> 16) & 0xff)
#define RGBA32_B(x) (((x) >> 8) & 0xff)
#define RGBA32_A(x) ((x) & 0xff)

// maximum number of commands to buffer for parallel processing
#define CMD_BUFFER_SIZE 1024
// maximum data size of a single command in bytes
#define CMD_MAX_SIZE 176

// maximum data size of a single command in 32 bit integers
#define CMD_MAX_INTS (CMD_MAX_SIZE / sizeof(int32_t))

// extracts the command ID from a command buffer
#define CMD_ID(cmd) ((*(cmd) >> 24) & 0x3f)
// list of command IDs
#define CMD_ID_NO_OP                           0x00
#define CMD_ID_FILL_TRIANGLE                   0x08
#define CMD_ID_FILL_ZBUFFER_TRIANGLE           0x09
#define CMD_ID_TEXTURE_TRIANGLE                0x0a
#define CMD_ID_TEXTURE_ZBUFFER_TRIANGLE        0x0b
#define CMD_ID_SHADE_TRIANGLE                  0x0c
#define CMD_ID_SHADE_ZBUFFER_TRIANGLE          0x0d
#define CMD_ID_SHADE_TEXTURE_TRIANGLE          0x0e
#define CMD_ID_SHADE_TEXTURE_Z_BUFFER_TRIANGLE 0x0f
#define CMD_ID_TEXTURE_RECTANGLE               0x24
#define CMD_ID_TEXTURE_RECTANGLE_FLIP          0x25
#define CMD_ID_SYNC_LOAD                       0x26
#define CMD_ID_SYNC_PIPE                       0x27
#define CMD_ID_SYNC_TILE                       0x28
#define CMD_ID_SYNC_FULL                       0x29
#define CMD_ID_SET_KEY_GB                      0x2a
#define CMD_ID_SET_KEY_R                       0x2b
#define CMD_ID_SET_CONVERT                     0x2c
#define CMD_ID_SET_SCISSOR                     0x2d
#define CMD_ID_SET_PRIM_DEPTH                  0x2e
#define CMD_ID_SET_OTHER_MODES                 0x2f
#define CMD_ID_LOAD_TLUT                       0x30
#define CMD_ID_SET_TILE_SIZE                   0x32
#define CMD_ID_LOAD_BLOCK                      0x33
#define CMD_ID_LOAD_TILE                       0x34
#define CMD_ID_SET_TILE                        0x35
#define CMD_ID_FILL_RECTANGLE                  0x36
#define CMD_ID_SET_FILL_COLOR                  0x37
#define CMD_ID_SET_FOG_COLOR                   0x38
#define CMD_ID_SET_BLEND_COLOR                 0x39
#define CMD_ID_SET_PRIM_COLOR                  0x3a
#define CMD_ID_SET_ENV_COLOR                   0x3b
#define CMD_ID_SET_COMBINE                     0x3c
#define CMD_ID_SET_TEXTURE_IMAGE               0x3d
#define CMD_ID_SET_MASK_IMAGE                  0x3e
#define CMD_ID_SET_COLOR_IMAGE                 0x3f
static struct n64video_config config;

static struct
{
    bool fillmbitcrashes, vbusclock, nolerp;
} onetimewarnings;

static int rdp_pipeline_crashed = 0;

static STRICTINLINE int32_t clamp(int32_t value, int32_t min, int32_t max)
{
    if (value < min)
        return min;
    else if (value > max)
        return max;
    return value;
}

static STRICTINLINE uint32_t irand(uint32_t* state)
{
    *state = *state * 0x343fd + 0x269ec3;
    return ((*state >> 16) & 0x7fff);
}
#include "n64video/rdp.c"
#include "n64video/vi.c"


/* Buffered commands: the parser fills the buffer and a flush runs it
 * across the workers on the calling thread. */
static uint32_t rdp_cmd_buf[CMD_BUFFER_SIZE][CMD_MAX_INTS];
static uint32_t rdp_cmd_buf_pos;
static uint32_t prev_img_addr[2];   /* [0]=color image, [1]=depth image */
static uint32_t prev_img_extent[2];
static bool prev_img_valid[2];

/* HIGH sync level: a texture load has to wait for buffered drawing only
 * when it reads memory that drawing writes. The parser shadows the state
 * the addresses depend on and the RDRAM ranges the buffered draws can
 * reach (colour and depth image, bounded by the scissor); a load whose
 * source overlaps one of them is preceded by a flush, any other load is
 * buffered like every other command. */
static struct
{
    uint32_t fb_address, fb_width, fb_size;
    uint32_t zb_address;
    uint32_t ti_address, ti_width, ti_size;
    uint32_t sc_rows;               /* scissor bottom, whole lines */
    uint32_t pend_lo[2], pend_hi[2]; /* [0]=colour, [1]=depth */
    bool pend_valid[2];
    /* RDRAM a buffered texture load reads, so drawing that would write
     * it waits: workers replay the batch at their own pace, and one
     * still reading a framebuffer as texture must not have another
     * drawing into it */
    uint32_t load_lo, load_hi;
    bool load_valid;
} hz;
static uint32_t flush_count;

/* Frame capture for the seam bisect. AL_CAPTURE=file, AL_CAPTURE_FRAME=N:
 * accumulate the command words of the first frame at or past VI N into a
 * static buffer, and flush the buffer to the file when that frame ends.
 * No file I/O in the parse hot path, no pointer kept across the frame. */
static uint32_t al_cap_buf[1 << 20];   /* up to 1M command words */
static uint32_t al_cap_words;
static uint32_t al_cap_vireg[16];
static long al_cap_frame, al_cap_target = -2, al_cap_locked = -1, al_cap_capvi = -1;
long al_cap_vi;   /* bumped once per VI by update_screen */

static void al_capture_cmd(const uint32_t *words, uint32_t n)
{
    uint32_t k;
    if (!getenv("AL_CAPTURE")) return;
    if (n == 0 || n > 64 || !words) return;
    if (al_cap_target == -2) al_cap_target = getenv("AL_CAPTURE_FRAME") ? atol(getenv("AL_CAPTURE_FRAME")) : 0;
    if (al_cap_locked >= 0) return;                 /* already have a frame */
    if (al_cap_vi < al_cap_target) return;          /* not yet */
    if (al_cap_words == 0)                           /* first command of it */
        for (k = 0; k < VI_NUM_REG && k < 16; k++)
            al_cap_vireg[k] = (config.gfx.vi_reg && config.gfx.vi_reg[k]) ? *config.gfx.vi_reg[k] : 0;
    if (al_cap_words + n + 1 > (uint32_t)(sizeof(al_cap_buf) / sizeof(al_cap_buf[0])))
        return;
    for (k = 0; k < n; k++) al_cap_buf[al_cap_words++] = words[k];
    /* mark the VI this frame belongs to so the flush fires at its end */
    al_cap_capvi = al_cap_vi;
}

void al_capture_vi(void);
static void al_capture_flush_if_ready(void)
{
    const char *env = getenv("AL_CAPTURE");
    if (!env || al_cap_locked >= 0 || al_cap_words == 0) return;
    if (al_cap_vi <= al_cap_capvi) return;          /* frame not finished */
    {
        FILE *f = fopen(env, "wb");
        if (f)
        {
            uint32_t sz = config.gfx.rdram_size, z = 0;
            uint32_t hsz = (uint32_t)sizeof(rdram_hidden);
            fwrite("ALCAP5", 1, 6, f); fwrite(&sz, 4, 1, f); fwrite(&hsz, 4, 1, f); fwrite(al_cap_vireg, 4, 16, f);
            if (config.gfx.rdram) fwrite(config.gfx.rdram, 1, sz, f);
            fwrite(rdram_hidden, 1, hsz, f);
            fwrite(al_cap_buf, 4, al_cap_words, f);
            fwrite(&z, 4, 1, f);
            fclose(f);
        }
        al_cap_locked = 1;
    }
    (void)al_cap_frame;
}
void al_capture_vi(void) { al_capture_flush_if_ready(); }

/* Upscaling: the console images the buffered drawing has written to
 * since they were last resolved. Only these are ever resolved back into
 * RDRAM - never an address the video interface merely points at, which
 * during boot can be anything. Console units: byte address, pixels per
 * row, rows the scissor allowed, pixel size code. */
#define AL_DIRTY_MAX 8
static struct
{
    uint32_t addr, width, rows, size;
} al_dirty[AL_DIRTY_MAX];
static uint32_t al_dirty_n;
static uint32_t al_batch_draws;   /* draw commands in the buffer being filled */

/* The geometry of every colour image the RDP has drawn into since init
 * - base, width, rows, size - kept regardless of resolves, so the video
 * interface can place a display origin relative to the image it lies
 * in. The pixel domain's layout is defined by where the RDP drew. */
#define AL_IMG_MAX 16
static struct { uint32_t addr, width, size, rows; } al_img[AL_IMG_MAX];
static uint32_t al_img_n;
static void al_note_image(uint32_t addr, uint32_t width, uint32_t rows, uint32_t size)
{
    uint32_t i;
    for (i = 0; i < al_img_n; i++)
        if (al_img[i].addr == addr && al_img[i].width == width && al_img[i].size == size)
        { if (rows > al_img[i].rows) al_img[i].rows = rows; return; }
    if (al_img_n == AL_IMG_MAX) { memmove(&al_img[0], &al_img[1], (AL_IMG_MAX - 1) * sizeof(al_img[0])); al_img_n--; }
    al_img[al_img_n].addr = addr; al_img[al_img_n].width = width; al_img[al_img_n].rows = rows; al_img[al_img_n].size = size; al_img_n++;
}

static void al_mark_dirty(void)
{
    uint32_t f = al_scale, addr, width, rows, i;
    if (f == 1 || state[0].fb_size < 2)
        return;
    addr  = state[0].fb_address / (f * f);
    width = state[0].fb_width / f;
    rows  = (state[0].clip.yl >> 2) / f + 1;
    if (!width || !rows)
        return;
    al_note_image(addr, width, rows, state[0].fb_size);
    for (i = 0; i < al_dirty_n; i++)
        if (al_dirty[i].addr == addr && al_dirty[i].width == width && al_dirty[i].size == state[0].fb_size)
        {
            if (rows > al_dirty[i].rows) al_dirty[i].rows = rows;
            return;
        }
    if (al_dirty_n == AL_DIRTY_MAX)
    {
        /* full: resolve the oldest to make room, it is drawn and done */
        n64video_resolve(al_dirty[0].addr, al_dirty[0].width, al_dirty[0].rows, al_dirty[0].size);
        memmove(&al_dirty[0], &al_dirty[1], (AL_DIRTY_MAX - 1) * sizeof(al_dirty[0]));
        al_dirty_n--;
    }
    al_dirty[al_dirty_n].addr  = addr;
    al_dirty[al_dirty_n].width = width;
    al_dirty[al_dirty_n].rows  = rows;
    al_dirty[al_dirty_n].size  = state[0].fb_size;
    al_dirty_n++;
}

static void al_resolve_all(void)
{
    uint32_t i;
    for (i = 0; i < al_dirty_n; i++)
        n64video_resolve(al_dirty[i].addr, al_dirty[i].width, al_dirty[i].rows, al_dirty[i].size);
    al_dirty_n = 0;
}

/* The drawn image a console byte address lies within: its console base
 * and width, for locating a display origin inside the pixel domain. */
int n64video_drawn_image_for(uint32_t addr, uint32_t *base, uint32_t *width)
{
    uint32_t i;
    for (i = 0; i < al_img_n; i++)
    {
        uint32_t bytes = PIXELS_TO_BYTES(al_img[i].width * al_img[i].rows, al_img[i].size);
        if (addr >= al_img[i].addr && addr < al_img[i].addr + bytes)
        { *base = al_img[i].addr; *width = al_img[i].width; return 1; }
    }
    return 0;
}

/* Resolve the drawn image a display origin lies within, if any. Called
 * by the video interface before it reads. */
void n64video_resolve_for_display(uint32_t origin)
{
    uint32_t i;
    if (al_scale == 1)
        return;
    for (i = 0; i < al_dirty_n; i++)
    {
        uint32_t bytes = PIXELS_TO_BYTES(al_dirty[i].width * al_dirty[i].rows, al_dirty[i].size);
        if (origin >= al_dirty[i].addr && origin < al_dirty[i].addr + bytes)
        {
            n64video_resolve(al_dirty[i].addr, al_dirty[i].width, al_dirty[i].rows, al_dirty[i].size);
            memmove(&al_dirty[i], &al_dirty[i + 1], (al_dirty_n - i - 1) * sizeof(al_dirty[0]));
            al_dirty_n--;
            return;
        }
    }
}

static void hz_pend_extend(int k, uint32_t lo, uint32_t hi)
{
    if (!hz.pend_valid[k] || lo < hz.pend_lo[k])
        hz.pend_lo[k] = lo;
    if (!hz.pend_valid[k] || hi > hz.pend_hi[k])
        hz.pend_hi[k] = hi;
    hz.pend_valid[k] = true;
}

static bool hz_pend_overlaps(uint32_t lo, uint32_t hi)
{
    int k;
    for (k = 0; k < 2; k++)
        if (hz.pend_valid[k] && lo < hz.pend_hi[k] && hi > hz.pend_lo[k])
            return true;
    return false;
}

static void hz_load_extend(uint32_t lo, uint32_t hi)
{
    if (!hz.load_valid || lo < hz.load_lo)
        hz.load_lo = lo;
    if (!hz.load_valid || hi > hz.load_hi)
        hz.load_hi = hi;
    hz.load_valid = true;
}

static bool hz_load_overlaps(uint32_t lo, uint32_t hi)
{
    return hz.load_valid && lo < hz.load_hi && hi > hz.load_lo;
}

/* returns true when the load must wait for the buffered drawing */
static bool hz_track(uint32_t cmd_id, const uint32_t *cmd)
{
    uint32_t lo, hi;
    switch (cmd_id)
    {
    case CMD_ID_SET_COLOR_IMAGE:
        hz.fb_size    = (cmd[0] >> 19) & 0x3;
        hz.fb_width   = (cmd[0] & 0x3ff) + 1;
        hz.fb_address = cmd[1] & 0xffffff;
        return false;
    case CMD_ID_SET_MASK_IMAGE:
        hz.zb_address = cmd[1] & 0xffffff;
        return false;
    case CMD_ID_SET_TEXTURE_IMAGE:
        hz.ti_size    = (cmd[0] >> 19) & 0x3;
        hz.ti_width   = (cmd[0] & 0x3ff) + 1;
        hz.ti_address = cmd[1] & 0xffffff;
        return false;
    case CMD_ID_SET_SCISSOR:
        hz.sc_rows = ((cmd[1] & 0xfff) >> 2) + 1;
        return false;
    case CMD_ID_FILL_TRIANGLE:
    case CMD_ID_FILL_ZBUFFER_TRIANGLE:
    case CMD_ID_TEXTURE_TRIANGLE:
    case CMD_ID_TEXTURE_ZBUFFER_TRIANGLE:
    case CMD_ID_SHADE_TRIANGLE:
    case CMD_ID_SHADE_ZBUFFER_TRIANGLE:
    case CMD_ID_SHADE_TEXTURE_TRIANGLE:
    case CMD_ID_SHADE_TEXTURE_Z_BUFFER_TRIANGLE:
    case CMD_ID_TEXTURE_RECTANGLE:
    case CMD_ID_TEXTURE_RECTANGLE_FLIP:
    case CMD_ID_FILL_RECTANGLE:
        {
            uint32_t clo = hz.fb_address;
            uint32_t chi = hz.fb_address
                + PIXELS_TO_BYTES(hz.fb_width * hz.sc_rows, hz.fb_size);
            uint32_t zlo = hz.zb_address;
            uint32_t zhi = hz.zb_address + hz.fb_width * hz.sc_rows * 2;
            hz_pend_extend(0, clo, chi);
            hz_pend_extend(1, zlo, zhi);
            /* writing what a buffered load is reading */
            return hz_load_overlaps(clo, chi) || hz_load_overlaps(zlo, zhi);
        }
    case CMD_ID_LOAD_BLOCK:
        /* whole-texel coordinates; sh runs linearly past the row */
        lo = hz.ti_address + PIXELS_TO_BYTES(hz.ti_width * (cmd[0] & 0xfff)
            + ((cmd[0] >> 12) & 0xfff), hz.ti_size);
        hi = hz.ti_address + PIXELS_TO_BYTES(hz.ti_width * (cmd[0] & 0xfff)
            + ((cmd[1] >> 12) & 0xfff) + 1, hz.ti_size) + 8;
        hz_load_extend(lo, hi);
        return hz_pend_overlaps(lo, hi);
    case CMD_ID_LOAD_TILE:
    case CMD_ID_LOAD_TLUT:
        /* 10.2 coordinates; the rows tl..th are read whole */
        lo = hz.ti_address + PIXELS_TO_BYTES(hz.ti_width
            * ((cmd[0] & 0xfff) >> 2), hz.ti_size);
        hi = hz.ti_address + PIXELS_TO_BYTES(hz.ti_width
            * (((cmd[1] & 0xfff) >> 2) + 1), hz.ti_size) + 8;
        hz_load_extend(lo, hi);
        return hz_pend_overlaps(lo, hi);
    default:
        return false;
    }
}

static uint32_t rdp_cmd_pos;
static uint32_t rdp_cmd_id;
static uint32_t rdp_cmd_len;

// table of commands that require thread synchronization in
// multithreaded mode
static bool rdp_cmd_sync[64];

static void cmd_run_buffered(uint32_t worker_id)
{
    uint32_t pos;
    for (pos = 0; pos < rdp_cmd_buf_pos; pos++)
        rdp_cmd(worker_id, rdp_cmd_buf[pos]);
}

static void cmd_flush(void)
{
    // only run if there's something buffered
    if (rdp_cmd_buf_pos) {
        // let workers run all buffered commands in parallel
        parallel_run(cmd_run_buffered);
        if (al_scale > 1 && al_batch_draws)
        {
            al_mark_dirty();
            al_batch_draws = 0;
        }
        // reset buffer by starting from the beginning
        rdp_cmd_buf_pos = 0;
        hz.pend_valid[0] = hz.pend_valid[1] = false;
        hz.load_valid = false;
        flush_count++;
    }
}


uint32_t n64video_flush_count(void)
{
    return flush_count;
}

// Synchronized image commands are pure per-worker state changes. Once all
// preceding raster work is complete, broadcasting them directly is cheaper
// than launching a worker batch whose only job is to update a few fields.
static void cmd_broadcast_state(uint32_t *cmd)
{
    uint32_t worker_id;
    for (worker_id = 0; worker_id < parallel_num_workers(); worker_id++)
        rdp_cmd(worker_id, cmd);
}

/* Flush, then apply a per-worker state command to every worker in
 * place. */
static void cmd_state_barrier(uint32_t *cmd)
{
    cmd_flush();
    cmd_broadcast_state(cmd);
}

static void cmd_sync_full(void)
{
    cmd_flush();
    /* the game may read what it just finished: resolve everything drawn */
    if (al_scale > 1)
        al_resolve_all();
    rdp_sync_full(0, NULL);
}
/* per command: the parse state of the next one */
static void cmd_init(void)
{
    rdp_cmd_pos = 0;
    rdp_cmd_id = 0;
    rdp_cmd_len = CMD_MAX_INTS;
}

void n64video_config_init(struct n64video_config* config)
{
    memset(config, 0, sizeof(*config));

    // config defaults that aren't false or 0
    config->parallel = true;
    config->vi.vsync = true;
    config->dp.compat = DP_COMPAT_MEDIUM;
}

void rdp_init_worker(uint32_t worker_id)
{
    rdp_init(worker_id, parallel_num_workers());
}
#ifdef HAVE_RDP_DUMP
static bool rdp_dump_in_command_list;
#endif

void n64video_init(struct n64video_config* _config)
{
    if (_config)
        config = *_config;

    // initialize static lookup tables and RDP state, once is enough
    static bool static_init;
    if (!static_init)
    {
        blender_init_lut();
        coverage_init_lut();
        combiner_init_lut();
        tex_init_lut();
        z_init_lut();
        fb_init(0);
        combiner_init(0);
        tex_init(0);
        rasterizer_init(0);

        static_init = true;
    }
#ifdef HAVE_RDP_DUMP
    const char *rdp_dump_path = getenv("RDP_DUMP");
    if (rdp_dump_path)
    {
        rdp_dump_init(rdp_dump_path, config.gfx.rdram_size, sizeof(rdram_hidden));
        // Force no MT when dumping for sanity.
        config.parallel = false;
    }
    rdp_dump_in_command_list = false;
#endif
    // enable sync switches depending on compatibility mode
    memset(rdp_cmd_sync, 0, sizeof(rdp_cmd_sync));
    switch (config.dp.compat) {
        case DP_COMPAT_HIGH:
            /* texture loads sync on demand, see hz_track() */
        case DP_COMPAT_MEDIUM:
            rdp_cmd_sync[CMD_ID_SET_MASK_IMAGE] = true;
            rdp_cmd_sync[CMD_ID_SET_COLOR_IMAGE] = true;
        case DP_COMPAT_LOW:
            rdp_cmd_sync[CMD_ID_SYNC_FULL] = true;
    }
    // init internals
    al_key_census = getenv("AL_KEY_CENSUS") != NULL;
    al_scale = config.upscale ? config.upscale : 1;
    if (al_scale > AL_SCALE_MAX)
        al_scale = AL_SCALE_MAX;
    /* powers of two only: 3x would not tile the sample grid */
    if (al_scale != 1 && al_scale != 2 && al_scale != 4)
        al_scale = 1;
    al_scale_log2 = al_scale == 4 ? 2 : (al_scale == 2 ? 1 : 0);
    config.upscale = al_scale;

    rdram_init();
    vi_init();
    rdp_cmd_buf_pos = 0;
    cmd_init();

    prev_img_valid[0] = prev_img_valid[1] = false;
    memset(&hz, 0, sizeof(hz));
    hz.sc_rows = 240;
    al_dirty_n = 0;
    al_batch_draws = 0;
    al_img_n = 0;
    rdp_pipeline_crashed = 0;
    memset(&onetimewarnings, 0, sizeof(onetimewarnings));

    if (config.parallel)
    {
       uint32_t i;
       // init worker system
       parallel_alinit(config.num_workers);

       // sync states from main worker
       for (i = 1; i < parallel_num_workers(); i++)
          memcpy(&state[i], &state[0], sizeof(struct rdp_state));
       // init workers
       parallel_run(rdp_init_worker);
    }
    else
        rdp_init(0, 1);
}
/* Host-side command overlay for the HLE graphics path. When set, command
 * words whose RDRAM word index falls inside [base_idx, base_idx + len)
 * are fetched from the host buffer instead of RDRAM. This lets the HLE
 * frontend keep its synthesized RDP command FIFO out of guest memory
 * (games with an Expansion Pak use all 8 MiB; parking the FIFO in the
 * top 256 KiB of RDRAM corrupted their heaps). Only the command fetch is
 * redirected: texture and image reads at the same addresses still see
 * real RDRAM. */
static const uint32_t* hle_cmd_buf;
static uint32_t hle_cmd_base_idx;
static uint32_t hle_cmd_len_words;
void n64video_set_hle_cmd_buffer(const uint32_t* buf, uint32_t base_byte_addr, uint32_t len_bytes)
{
    hle_cmd_buf = buf;
    hle_cmd_base_idx = base_byte_addr >> 2;
    hle_cmd_len_words = len_bytes >> 2;
}

static uint32_t rdp_fetch_cmd_word(uint32_t idx)
{
    if (hle_cmd_buf != NULL && (idx - hle_cmd_base_idx) < hle_cmd_len_words)
        return hle_cmd_buf[idx - hle_cmd_base_idx];
    return rdram_read_idx32(idx);
}
/* The hidden-bit array, two bits per 16-bit word with the first byte's
 * bit on top, for a host that wants the CPU to see the ninth bits the
 * RDP writes. */
uint8_t* n64video_hidden_store(size_t* size)
{
    if (size)
        *size = sizeof(rdram_hidden);
    return rdram_hidden;
}

/* ---------------------------------------------------------------------
 * Register writes behind a rectangle with no sync in between.
 *
 * The command processor runs ahead of the pixel pipeline. Once a
 * rectangle's edge walk is done it moves on to the next command while
 * the rectangle's last spans are still being drawn, so a colour register
 * written without a pipe sync lands part-way through the rectangle
 * before it. The model and its constants are the ones established in
 * cen64 (gitlab.com/jgemu/cen64, src/rdp/rdp_core.c, Rupert Carmichael)
 * against snapper64's hardware surfaces; lengths are in GCLK.
 *
 * 1-cycle and 2-cycle, SetEnvColor. Over the command box - W columns and
 * H rows counted inclusively, so the last column and the last row are
 * the dead ones the walker ends on - and with cyc cycles per pixel:
 *
 *   L = max(cyc*W + cyc - 1, 4)     clocks per span
 *   D = min(3*L - 2, 25) + OFF      command-processor lead
 *
 *   live pixel (r, c) is emitted at clock  r*L + cyc*c
 *   the k'th following command executes at (H-1)*L - D + k
 *
 * and a write takes effect at the first live pixel emitted at or after
 * its clock; one landing in the dead end-of-line slot moves to the start
 * of the next row. 3*L - 2 is a three-deep span buffer, 25 the fixed
 * pixel-pipeline latency. OFF is 1 when 2-cycle mode reads the
 * environment colour in its second combiner cycle and not its first.
 *
 * FILL, SetFillColor. The fill colour is latched once per span, so a
 * write recolours whole trailing rows: ns_fill_rows() below.
 *
 * Only what the hardware data covers is modelled: fill rectangles. The
 * rectangle is cut along the landing points into sub-rectangles, each
 * drawn with the colour the register held for it.
 *
 * Hardware verified against snapper64's "RDP Rect No-Sync-Env 1C",
 * "RDP Rect No-Sync-Env 2C" and "RDP Rect No-Sync-Fill" groups.
 * ------------------------------------------------------------------- */
#define NS_MAX_EVENTS 8
#define NS_FILL_MAX_LEAD 44
#define NS_QUEUE_WORDS 1024
static uint32_t ns_queue[NS_QUEUE_WORDS];
static uint32_t ns_q_pos, ns_q_len;
/* dispatcher-side copies of the state the split needs; the workers' own
 * state lags behind the dispatcher while commands sit in the buffer */
static uint32_t ns_cycle_type;
static uint32_t ns_sc_xh, ns_sc_yh, ns_sc_xl = 0xfff, ns_sc_yl = 0xfff;
static uint32_t ns_env, ns_fill, ns_fb_size;
static uint32_t ns_combine[2];

static void ns_emit(uint32_t w0, uint32_t w1)
{
    if (ns_q_len + 2 > NS_QUEUE_WORDS) return;
    ns_queue[ns_q_len++] = w0;
    ns_queue[ns_q_len++] = w1;
}

static void ns_emit_rect(uint32_t xh, uint32_t yh, uint32_t xl, uint32_t yl)
{
    ns_emit(((uint32_t)CMD_ID_FILL_RECTANGLE << 24) | ((xl & 0xfff) << 12) | (yl & 0xfff),
            ((xh & 0xfff) << 12) | (yh & 0xfff));
}

/* Does the given combiner cycle read the environment colour? It is mux
 * value 5 in every input field, and the RGB multiply field also selects
 * the environment alpha at 12. */
static bool ns_env_in_cycle(int cycle)
{
    uint32_t w0 = ns_combine[0], w1 = ns_combine[1];
    if (cycle == 0)
        return ((w0 >> 20) & 0xf) == 5 || ((w1 >> 28) & 0xf) == 5
            || ((w0 >> 15) & 0x1f) == 5 || ((w0 >> 15) & 0x1f) == 12
            || ((w1 >> 15) & 0x7) == 5 || ((w0 >> 12) & 0x7) == 5
            || ((w1 >> 12) & 0x7) == 5 || ((w0 >> 9) & 0x7) == 5
            || ((w1 >> 9) & 0x7) == 5;
    return ((w0 >> 5) & 0xf) == 5 || ((w1 >> 24) & 0xf) == 5
        || (w0 & 0x1f) == 5 || (w0 & 0x1f) == 12
        || ((w1 >> 6) & 0x7) == 5 || ((w1 >> 21) & 0x7) == 5
        || ((w1 >> 3) & 0x7) == 5 || ((w1 >> 18) & 0x7) == 5
        || (w1 & 0x7) == 5;
}

/* the live rows and columns of a rectangle after the scissor */
struct ns_box { uint32_t xh, yh, xl, yl; int32_t xa, xb, r0, r1; };

/* Emit the live pixels from (ra, ca) up to but not including (rb, cb), in
 * walk order, as up to three rectangles. cols is the live column count. */
static void ns_emit_range(const struct ns_box *bx, int32_t cols, int32_t rows,
                          int32_t ra, int32_t ca, int32_t rb, int32_t cb)
{
#define NS_Y0(rr) ((rr) == 0 ? bx->yh : (uint32_t)((bx->r0 + (rr)) << 2))
#define NS_Y1(rr) ((rr) == rows - 1 ? bx->yl : (uint32_t)((bx->r0 + (rr) + 1) << 2))
#define NS_X0(cc) ((cc) == 0 ? bx->xh : (uint32_t)((bx->xa + (cc)) << 2))
#define NS_X1(cc) ((cc) == cols ? bx->xl : (uint32_t)((bx->xa + (cc)) << 2))
    if (ra > rb || (ra == rb && ca >= cb))
        return;
    if (ra == rb)
    {
        ns_emit_rect(NS_X0(ca), NS_Y0(ra), NS_X1(cb), NS_Y1(ra));
        return;
    }
    if (ca > 0)
    {
        ns_emit_rect(NS_X0(ca), NS_Y0(ra), NS_X1(cols), NS_Y1(ra));
        ra++;
    }
    if (rb > ra)
        ns_emit_rect(NS_X0(0), NS_Y0(ra), NS_X1(cols), NS_Y1(rb - 1));
    if (cb > 0 && rb < rows)
        ns_emit_rect(NS_X0(0), NS_Y0(rb), NS_X1(cb), NS_Y1(rb));
#undef NS_Y0
#undef NS_Y1
#undef NS_X0
#undef NS_X1
}

static bool ns_box_of(const uint32_t *cmd, bool fill, struct ns_box *bx)
{
    uint32_t cxh, cxl, cyh, cyl;
    bx->xl = (cmd[0] >> 12) & 0xfff; bx->yl = cmd[0] & 0xfff;
    bx->xh = (cmd[1] >> 12) & 0xfff; bx->yh = cmd[1] & 0xfff;
    cxh = bx->xh > ns_sc_xh ? bx->xh : ns_sc_xh; cxl = bx->xl < ns_sc_xl ? bx->xl : ns_sc_xl;
    cyh = bx->yh > ns_sc_yh ? bx->yh : ns_sc_yh;
    /* FILL and COPY rectangles name their bottom row inclusively */
    cyl = fill ? (bx->yl | 3) : bx->yl; if (cyl > ns_sc_yl) cyl = ns_sc_yl;
    if (cxl < cxh || cyl <= cyh)
        return false;
    bx->xa = (int32_t)(cxh >> 2); bx->xb = (int32_t)(cxl >> 2);
    bx->r0 = (int32_t)(cyh >> 2); bx->r1 = (int32_t)((cyl - 1) >> 2);
    return true;
}

/* 1-cycle / 2-cycle: colors[0] is the environment colour the rectangle was
 * issued under, colors[1..k] what the SetEnvColor commands behind it load. */
static bool ns_split_env(const uint32_t *cmd, const uint32_t *colors, uint32_t k)
{
    struct ns_box bx;
    int32_t cyc = (ns_cycle_type == 1) ? 2 : 1;
    int32_t W, H, cols, rows, L, D, off, j;
    int32_t pr = 0, pc = 0;       /* start of the piece being built */
    uint32_t cur = 0;             /* colour index of that piece */

    if (!ns_box_of(cmd, false, &bx))
        return false;
    W = bx.xb - bx.xa + 1;        /* the box, dead column included */
    cols = W - 1;
    rows = bx.r1 - bx.r0 + 1;     /* live rows */
    H = rows + 1;
    if (cols < 1 || rows < 1)
        return false;
    /* the walker's last column only stays dead when xl is whole */
    if (bx.xl & 3) { cols = W; }

    L = cyc * W + cyc - 1; if (L < 4) L = 4;
    off = (cyc == 2 && !ns_env_in_cycle(0) && ns_env_in_cycle(1)) ? 1 : 0;
    D = 3 * L - 2; if (D > 25) D = 25; D += off;

    ns_q_pos = ns_q_len = 0;
    for (j = 0; j < (int32_t)k; j++)
    {
        int32_t t = (H - 1) * L - D + j, r, rem, c;
        if (t < 0) t = 0;
        r = t / L; rem = t - r * L; c = (rem + cyc - 1) / cyc;
        if (c > W - 2) { r++; c = 0; }
        if (r > H - 2)
            break;                /* this write and all later ones miss */
        ns_emit(((uint32_t)CMD_ID_SET_ENV_COLOR << 24), colors[cur]);
        ns_emit_range(&bx, cols, rows, pr, pc, r, c);
        pr = r; pc = c; cur = (uint32_t)j + 1;
    }
    ns_emit(((uint32_t)CMD_ID_SET_ENV_COLOR << 24), colors[cur]);
    ns_emit_range(&bx, cols, rows, pr, pc, rows, 0);
    /* leave the register holding the last colour loaded */
    ns_emit(((uint32_t)CMD_ID_SET_ENV_COLOR << 24), colors[k]);
    return true;
}

/* Rows of a FILL rectangle, counted from the last, that take a fill colour
 * written k GCLK after the command processor resumed. With bpp the colour
 * image's bits per pixel:
 *
 *   W   = 64-bit words the row covers, phi = 1 when the row starts later
 *         in its word than it ends, Wc = W - phi
 *   B   = 64-byte blocks of the colour image the row touches
 *   P   = max(9, Wc + 1)                 interior period
 *   B == 1:  lead = 35 + W + phi,   gap = max(9, 2*W + 6)
 *   B == 2:  lead = 34 + phi,       gap = W + 5
 *   B >= 3:  lead = 51 - 8*B + phi, gap = Wc + 1, the lead one lower when
 *            the row is flush with its blocks at both ends
 *
 * The row j back from the last latches at lead for j = 0 and at
 * lead - gap - (j-1)*P behind it; the first row of the primitive enters
 * an empty pipeline and latches P - 1 earlier still (one more when the
 * gap is at its floor). A single-row rectangle is its own case: a lead of
 * 21 whatever its width, and unreachable from five blocks up. */
static int32_t ns_fill_rows(int32_t fbsize, int32_t x0, int32_t x1, int32_t h, int32_t k)
{
    static const int32_t bpp_of[4] = { 4, 8, 16, 32 };
    int32_t bpp, ppw, b0, b1, w, phi, wc, blk, per, lead, gap, e, j, n;

    if (h <= 0 || x1 < x0 || k < 0 || (uint32_t)fbsize > 3u)
        return 0;
    bpp = bpp_of[fbsize]; ppw = 64 / bpp;
    b0 = (x0 * bpp) >> 3;
    b1 = (((x1 + 1) * bpp) >> 3) - 1;
    w = (b1 >> 3) - (b0 >> 3) + 1;
    phi = ((x0 % ppw) > (x1 % ppw)) ? 1 : 0;
    wc = w - phi;
    blk = (b1 >> 6) - (b0 >> 6) + 1;

    if (h == 1)
        return (blk <= 4 && k <= 21) ? 1 : 0;

    per = (wc + 1 > 9) ? (wc + 1) : 9;
    if (blk == 1)      { lead = 35 + w + phi; gap = (2 * w + 6 > 9) ? (2 * w + 6) : 9; }
    else if (blk == 2) { lead = 34 + phi;     gap = w + 5; }
    else
    {
        lead = 51 - 8 * blk + phi; gap = wc + 1;
        if ((b0 & 63) == 0 && ((b1 + 1) & 63) == 0)
            lead--;
    }
    e = per - 1;
    if (2 * w + 6 < 9)
        e--;
    n = 0;
    for (j = 0; j < h; j++)
    {
        int32_t lam = (j == 0) ? lead : lead - gap - (j - 1) * per;
        if (j == h - 1)
            lam -= e;
        if (lam >= k)
            n++;
    }
    return n;
}

/* FILL: rows[i] is the first row that takes fills[i + 1]; fills[0] is the
 * colour the rectangle was issued under. */
static bool ns_split_fill(const uint32_t *cmd, const struct ns_box *bx,
                          const int32_t *first_row, const uint32_t *fills, uint32_t k)
{
    int32_t rows = bx->r1 - bx->r0 + 1, cols = bx->xb - bx->xa + 1, pr = 0;
    uint32_t j, cur = 0;
    (void)cmd;
    ns_q_pos = ns_q_len = 0;
    for (j = 0; j < k; j++)
    {
        ns_emit(((uint32_t)CMD_ID_SET_FILL_COLOR << 24), fills[cur]);
        ns_emit_range(bx, cols, rows, pr, 0, first_row[j], 0);
        if (first_row[j] > pr) pr = first_row[j];
        cur = j + 1;
    }
    ns_emit(((uint32_t)CMD_ID_SET_FILL_COLOR << 24), fills[cur]);
    ns_emit_range(bx, cols, rows, pr, 0, rows, 0);
    return true;
}

void n64video_process_list(void)
{
    uint32_t** dp_reg = config.gfx.dp_reg;
    uint32_t dp_current_al;
    uint32_t dp_end_al;
    /* On a ROM reload, initiateGFX's n64video_config_init() memsets config
     * (clearing gfx.dp_reg to NULL) before romOpen restores it. With the
     * libco-free per-frame model the CPU resumes mid-stream, so the RSP can
     * feed an RDP list (run_task -> n64video_process_list) in that window with
     * gfx.dp_reg still NULL -> dp_reg[DP_CURRENT] dereferences NULL+offset.
     * Nothing can be processed yet; bail rather than crash. */
    if (dp_reg == NULL)
        return;
    /* The command buffer pointers are 24 bits wide on the part; the top
     * byte does not exist and nothing written there is kept.  Taking the
     * whole 32 believed rubbish above bit 23, and the RSP plugin writes
     * these through a raw pointer, so masking them where they are set is
     * not enough - they have to be masked where they are used.
     *
     * Junk Runner 64 ends up with an end of 0x091a7ce0 against a start of
     * 0x001a7cd8: one two-word command with 0x09 stranded over the top.
     * That asked the RDP for 37,748,738 words.  It walked out of RDRAM,
     * where the fetch reads back zero, and decoded that as commands for
     * the rest of the frame - 95% of the eighteen million words it
     * fetched were not commands, against 0.1% of the sixty-eight thousand
     * Super Mario 64 fetches over the same run - which is what replaced
     * the game's logo with a screen of torn scanlines. */
    dp_current_al = (*dp_reg[DP_CURRENT] & 0x00fffff8) >> 2;
    dp_end_al = (*dp_reg[DP_END] & 0x00fffff8) >> 2;
    // don't do anything if the RDP has crashed or the registers are not set up correctly
    if (rdp_pipeline_crashed || dp_end_al <= dp_current_al) {
        return;
    }
    // while there's data in the command buffer...
    while (ns_q_pos < ns_q_len || dp_end_al - dp_current_al > 0) {
        uint32_t i, toload;
        bool ns_synthetic = ns_q_pos < ns_q_len;
        /* An active HLE command buffer is the authoritative source for the
         * whole [start, end) window the HLE submit installed -- the DPC
         * XBUS bit must not reroute its fetch into DMEM. The bit is set by
         * LLE microcode (MTC0 from the RSP) and can arrive here through a
         * savestate: the F3DDKR family feeds the RDP over XBUS, so a state
         * saved under the cxd4 LLE RSP carries DPC_STATUS bit 0; restored
         * under the HLE RSP, the first synthesized list submitted before
         * the game's own DPC_STATUS write then decoded wrapped DMEM words
         * as RDP commands (and left the streaming decoder mid-command),
         * corrupting every frame after the load. */
        bool xbus_dma = (*dp_reg[DP_STATUS] & DP_STATUS_XBUS_DMA) != 0
                        && hle_cmd_buf == NULL;
        uint32_t* dmem = (uint32_t*)config.gfx.dmem;
        uint32_t* cmd_buf = rdp_cmd_buf[rdp_cmd_buf_pos];
        // when reading the first int, extract the command ID and update the buffer length
        if (rdp_cmd_pos == 0) {
            if (ns_synthetic) {
                cmd_buf[rdp_cmd_pos++] = ns_queue[ns_q_pos++];
            } else if (xbus_dma) {
                cmd_buf[rdp_cmd_pos++] = dmem[dp_current_al++ & 0x3ff];
            } else {
                cmd_buf[rdp_cmd_pos++] = rdp_fetch_cmd_word(dp_current_al++);
            }

            rdp_cmd_id = CMD_ID(cmd_buf);
            rdp_cmd_len = rdp_commands[rdp_cmd_id].length >> 2;
        }
        // copy more data from the N64 to the local command buffer
        /* Load only what is still missing from the command being decoded.
         * When a list ends mid-command the decoder keeps rdp_cmd_pos across
         * calls; asking for rdp_cmd_len-1 more words then overshoots
         * rdp_cmd_len, so the completion test never matches, the command is
         * never executed, and rdp_cmd_pos runs past the buffer slot. */
        toload = MIN(ns_synthetic ? ns_q_len - ns_q_pos : dp_end_al - dp_current_al,
                     rdp_cmd_len - rdp_cmd_pos);
        if (ns_synthetic) {
            /* synthesized commands are queued whole */
            for (i = 0; i < toload; i++) {
                cmd_buf[rdp_cmd_pos++] = ns_queue[ns_q_pos++];
            }
        } else if (xbus_dma) {
            for (i = 0; i < toload; i++) {
                cmd_buf[rdp_cmd_pos++] = dmem[dp_current_al++ & 0x3ff];
            }
        } else {
            for (i = 0; i < toload; i++) {
                cmd_buf[rdp_cmd_pos++] = rdp_fetch_cmd_word(dp_current_al++);
            }
        }

        // if there's enough data for the current command...
        if (rdp_cmd_pos == rdp_cmd_len) {
            /* dispatcher-side state for the no-sync rectangle split */
            if (rdp_cmd_id == CMD_ID_SET_OTHER_MODES)
                ns_cycle_type = (cmd_buf[0] >> 20) & 3;
            else if (rdp_cmd_id == CMD_ID_SET_SCISSOR) {
                ns_sc_xh = (cmd_buf[0] >> 12) & 0xfff; ns_sc_yh = cmd_buf[0] & 0xfff;
                ns_sc_xl = (cmd_buf[1] >> 12) & 0xfff; ns_sc_yl = cmd_buf[1] & 0xfff;
            } else if (rdp_cmd_id == CMD_ID_SET_ENV_COLOR)
                ns_env = cmd_buf[1];
            else if (rdp_cmd_id == CMD_ID_SET_FILL_COLOR)
                ns_fill = cmd_buf[1];
            else if (rdp_cmd_id == CMD_ID_SET_COLOR_IMAGE)
                ns_fb_size = (cmd_buf[0] >> 19) & 3;
            else if (rdp_cmd_id == CMD_ID_SET_COMBINE) {
                ns_combine[0] = cmd_buf[0]; ns_combine[1] = cmd_buf[1];
            } else if (rdp_cmd_id == CMD_ID_FILL_RECTANGLE && !ns_synthetic && ns_cycle_type != 2) {
#define NS_PEEK(a) (xbus_dma ? dmem[(a) & 0x3ff] : rdp_fetch_cmd_word(a))
                uint32_t colors[NS_MAX_EVENTS + 1], k = 0, at = dp_current_al;
                bool split = false;
                if (ns_cycle_type < 2) {
                    /* SetEnvColor commands directly behind the rectangle:
                     * any other command closes the window */
                    colors[0] = ns_env;
                    while (k < NS_MAX_EVENTS && dp_end_al - at >= 2
                           && ((NS_PEEK(at) >> 24) & 0x3f) == CMD_ID_SET_ENV_COLOR) {
                        colors[++k] = NS_PEEK(at + 1);
                        at += 2;
                    }
                    split = k && ns_split_env(cmd_buf, colors, k);
                } else {
                    /* FILL: a no-op costs a clock and keeps the window open;
                     * anything else closes it */
                    struct ns_box bx;
                    int32_t first_row[NS_MAX_EVENTS], clock = 0;
                    if (ns_box_of(cmd_buf, true, &bx)) {
                        int32_t h = bx.r1 - bx.r0 + 1;
                        colors[0] = ns_fill;
                        while (k < NS_MAX_EVENTS && dp_end_al - at >= 2 && clock <= NS_FILL_MAX_LEAD) {
                            uint32_t id = (NS_PEEK(at) >> 24) & 0x3f;
                            if (id == CMD_ID_NO_OP) { clock++; at += 2; continue; }
                            if (id != CMD_ID_SET_FILL_COLOR)
                                break;
                            {
                                int32_t n = ns_fill_rows((int32_t)ns_fb_size, bx.xa, bx.xb, h, clock);
                                clock++;
                                if (n <= 0)
                                    break;   /* missed; nothing later can land */
                                first_row[k] = h - n < 0 ? 0 : h - n;
                                colors[++k] = NS_PEEK(at + 1);
                                at += 2;
                            }
                        }
                        /* trailing no-ops that were skipped but led nowhere
                         * are harmless to drop: they do nothing */
                        split = k && ns_split_fill(cmd_buf, &bx, first_row, colors, k);
                    }
                }
#undef NS_PEEK
                if (split) {
                    /* the pieces replace the rectangle and the colour loads */
                    dp_current_al = at;
                    cmd_buf[0] = 0; cmd_buf[1] = 0;
                    rdp_cmd_id = CMD_ID_NO_OP;
                }
            }
            al_capture_cmd(cmd_buf, rdp_cmd_len);
#ifdef HAVE_RDP_DUMP
            if (!rdp_dump_in_command_list)
            {
                rdp_dump_flush_dram(config.gfx.rdram, config.gfx.rdram_size);
                rdp_dump_flush_hidden_dram(rdram_hidden, sizeof(rdram_hidden));
                rdp_dump_in_command_list = true;
            }
            if (rdp_cmd_id == CMD_ID_SYNC_FULL)
            {
                rdp_dump_signal_complete();
                rdp_dump_in_command_list = false;
            }
            else
            {
                rdp_dump_emit_command(rdp_cmd_id, cmd_buf, rdp_cmd_len);
            }
#endif
            // check if parallel processing is enabled
            if (config.parallel) {
                /* set below for an image switch that the workers cannot
                 * be left to reach at their own pace */
                bool sync_state_barrier = false;

                if (config.dp.compat == DP_COMPAT_HIGH
                        && hz_track(rdp_cmd_id, cmd_buf)) {
                    if (   rdp_cmd_id == CMD_ID_LOAD_BLOCK
                        || rdp_cmd_id == CMD_ID_LOAD_TILE
                        || rdp_cmd_id == CMD_ID_LOAD_TLUT) {
                        /* A load of memory the batch draws to fills each
                         * worker's own TMEM, so it is per-worker state
                         * like the image commands: finish the drawing,
                         * then run the load for every worker in place.
                         * Buffered instead, it would leave one worker
                         * reading the framebuffer while another has moved
                         * on to drawing into it, and cost a second
                         * dispatch to keep them apart. */
                        sync_state_barrier = true;
                    } else {
                        /* the ranges this command contributes were
                         * recorded against the batch being flushed;
                         * record them again against the one it is
                         * buffered into, or the next command to touch the
                         * same memory sees nothing pending. The second
                         * call reports no conflict: what it tested
                         * against has just been drained. */
                        cmd_flush();
                        hz_track(rdp_cmd_id, cmd_buf);
                        memcpy(rdp_cmd_buf[0], cmd_buf, rdp_cmd_len * sizeof(uint32_t));
                        cmd_buf = rdp_cmd_buf[0];
                    }
                }

                // A mid-frame SET_COLOR_IMAGE that overlaps the previous
                // color image (render-to-subimage, e.g. Ocarina of Time's
                // pause-screen character box) makes draws before and after
                // the switch target the same RDRAM through different
                // scanline layouts. Workers replay the buffer at
                // independent paces, so without a barrier the two draw
                // groups race and produce interleaved-scanline streaks.
                // Ordinary buffer switches (cfb/zbuf/next frame) do not
                // overlap and keep the fast path at every sync level.
                if (rdp_cmd_id == CMD_ID_SET_COLOR_IMAGE ||
                    rdp_cmd_id == CMD_ID_SET_MASK_IMAGE) {
                    // Mid-frame retargeting hazard: when a new color or
                    // depth image overlaps memory that the previous color
                    // or depth image covers, draws before and after the
                    // switch address the same RDRAM through different
                    // scanline layouts (render-to-subimage and
                    // buffer-as-zbuffer tricks; Ocarina of Time's pause
                    // screen does both for the character box). Workers
                    // replay the command buffer at independent paces, so
                    // without a barrier the two draw groups race and leave
                    // interleaved-scanline streaks. A switch with an
                    // identical configuration, or to a region one full
                    // image away (the standard cfb/zbuf layout), keeps the
                    // fast path at every sync level.
                    uint32_t naddr = cmd_buf[1] & 0xffffff;
                    uint32_t next;
                    bool hazard = false;
                    int k;
                    if (rdp_cmd_id == CMD_ID_SET_COLOR_IMAGE) {
                        uint32_t siz   = (cmd_buf[0] >> 19) & 3;
                        uint32_t width = (cmd_buf[0] & 0x3ff) + 1;
                        uint32_t rowb  = (siz == 3) ? width * 4
                                       : (siz == 2) ? width * 2
                                       : (siz == 1) ? width
                                       : width / 2;
                        next = rowb * 240;
                    } else {
                        // depth image: fixed 16-bit, width follows the
                        // color image; use the color image's extent as the
                        // estimate.
                        next = prev_img_valid[0] ? prev_img_extent[0]
                                                 : 320 * 2 * 240;
                    }
                    for (k = 0; k < 2; k++) {
                        uint32_t d, lim;
                        if (!prev_img_valid[k])
                            continue;
                        d   = naddr > prev_img_addr[k]
                            ? naddr - prev_img_addr[k]
                            : prev_img_addr[k] - naddr;
                        lim = prev_img_extent[k] > next
                            ? prev_img_extent[k] : next;
                        if (d < lim &&
                            !(naddr == prev_img_addr[k] && next == prev_img_extent[k]))
                            hazard = true;
                    }
                    if (hazard) {
                        /* Only an overlapping switch needs the workers
                         * brought together. Every worker replays the
                         * batch in order, so a switch that overlaps
                         * nothing is applied at the right point in each
                         * worker's own stream and can simply be
                         * buffered, whatever the sync level: the barrier
                         * exists for the memory those draw groups share,
                         * not for the state change itself. */
                        sync_state_barrier = rdp_cmd_sync[rdp_cmd_id];
                        cmd_flush();
                        /* the words of this command were parsed into the
                         * slot at the pre-flush buffer position; the
                         * flush rewinds the position to 0, so move them
                         * to the slot that is about to be registered.
                         * A barrier is broadcast from cmd_buf below and
                         * needs no slot. */
                        if (!sync_state_barrier) {
                            memcpy(rdp_cmd_buf[0], cmd_buf, rdp_cmd_len * sizeof(uint32_t));
                            cmd_buf = rdp_cmd_buf[0];
                        }
                    }
                    k = (rdp_cmd_id == CMD_ID_SET_COLOR_IMAGE) ? 0 : 1;
                    prev_img_addr[k]   = naddr;
                    prev_img_extent[k] = next;
                    prev_img_valid[k]  = true;
                }
                // special case: sync_full always needs to be run in main thread
                if (rdp_cmd_id == CMD_ID_SYNC_FULL) {
                    cmd_sync_full();
                } else if (sync_state_barrier) {
                    /* finish the preceding drawing, then apply the state
                     * change - an overlapping image switch, or a load of
                     * memory that drawing touched - to every worker in
                     * place */
                    cmd_state_barrier(cmd_buf);
                } else {
                    if (rdp_cmd_id >= CMD_ID_FILL_TRIANGLE && rdp_cmd_id <= CMD_ID_SHADE_TEXTURE_Z_BUFFER_TRIANGLE
                            || rdp_cmd_id == CMD_ID_TEXTURE_RECTANGLE || rdp_cmd_id == CMD_ID_TEXTURE_RECTANGLE_FLIP
                            || rdp_cmd_id == CMD_ID_FILL_RECTANGLE)
                        al_batch_draws++;
                    // increment buffer position
                    rdp_cmd_buf_pos++;
                    /* flush when the batch is full; the image commands
                     * are ordering points only where they overlap, which
                     * is handled above */
                    if (rdp_cmd_buf_pos >= CMD_BUFFER_SIZE) {
                        cmd_flush();
                    }
                }
            } else {
                // run command directly
                rdp_cmd(0, cmd_buf);
                if (al_scale > 1 && (rdp_cmd_id >= CMD_ID_FILL_TRIANGLE && rdp_cmd_id <= CMD_ID_SHADE_TEXTURE_Z_BUFFER_TRIANGLE
                        || rdp_cmd_id == CMD_ID_TEXTURE_RECTANGLE || rdp_cmd_id == CMD_ID_TEXTURE_RECTANGLE_FLIP
                        || rdp_cmd_id == CMD_ID_FILL_RECTANGLE))
                    al_mark_dirty();
                if (al_scale > 1 && rdp_cmd_id == CMD_ID_SYNC_FULL)
                    al_resolve_all();
            }
            // send Z-buffer address to VI for "depth" output mode
            if (rdp_cmd_id == CMD_ID_SET_MASK_IMAGE) {
                vi_set_zbuffer_address(cmd_buf[1] & 0x0ffffff);
            }

            // reset current command buffer to prepare for the next one
            cmd_init();
        }
    }

    // update DP registers to indicate that all bytes have been read
    *dp_reg[DP_START] = *dp_reg[DP_CURRENT] = *dp_reg[DP_END];
}
void n64video_close(void)
{
#ifdef HAVE_RDP_DUMP
    if (rdp_dump_in_command_list)
        rdp_dump_in_command_list = false;
    rdp_dump_end();
#endif

    al_key_report();
    vi_close();
    parallel_close();
    rdram_close();
}
