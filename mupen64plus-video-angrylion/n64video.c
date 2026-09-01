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
    while (dp_end_al - dp_current_al > 0) {
        uint32_t i, toload;
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
            if (xbus_dma) {
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
        toload = MIN(dp_end_al - dp_current_al, rdp_cmd_len - rdp_cmd_pos);
        if (xbus_dma) {
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

    vi_close();
    parallel_close();
    rdram_close();
}
