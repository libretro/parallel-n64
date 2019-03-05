#include "n64video.h"
#include "rdp.h"
#include "common.h"
#include "plugin.h"
#include "msg.h"
#include "screen.h"
#include "parallel_al.h"

#include <memory.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

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

static struct rdp_state** rdp_states;
static struct n64video_config config;
static struct plugin_api* plugin;

static bool init_lut;

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
    else
        return value;
}

static STRICTINLINE uint32_t irand(uint32_t* state)
{
    *state = *state * 0x343fd + 0x269ec3;
    return ((*state >> 16) & 0x7fff);
}

#include "rdp/rdp.c"
#include "vi/vi.c"

static uint32_t rdp_cmd_buf[CMD_BUFFER_SIZE][CMD_MAX_INTS];
static uint32_t rdp_cmd_buf_pos;

static uint32_t rdp_cmd_pos;
static uint32_t rdp_cmd_id;
static uint32_t rdp_cmd_len;

static bool rdp_cmd_sync[] = {
    false, // No_Op
    false, // ???
    false, // ???
    false, // ???
    false, // ???
    false, // ???
    false, // ???
    false, // ???
    false, // Fill_Triangle
    false, // Fill_ZBuffer_Triangle
    false, // Texture_Triangle
    false, // Texture_ZBuffer_Triangle
    false, // Shade_Triangle
    false, // Shade_ZBuffer_Triangle
    false, // Shade_Texture_Triangle
    false, // Shade_Texture_Z_Buffer_Triangle
    false, // ???
    false, // ???
    false, // ???
    false, // ???
    false, // ???
    false, // ???
    false, // ???
    false, // ???
    false, // ???
    false, // ???
    false, // ???
    false, // ???
    false, // ???
    false, // ???
    false, // ???
    false, // ???
    false, // ???
    false, // ???
    false, // ???
    false, // ???
    false, // Texture_Rectangle
    false, // Texture_Rectangle_Flip
    false, // Sync_Load
    false, // Sync_Pipe
    false, // Sync_Tile
    true,  // Sync_Full
    false, // Set_Key_GB
    false, // Set_Key_R
    false, // Set_Convert
    false, // Set_Scissor
    false, // Set_Prim_Depth
    false, // Set_Other_Modes
    false, // Load_TLUT
    false, // ???
    false, // Set_Tile_Size
    false, // Load_Block
    false, // Load_Tile
    false, // Set_Tile
    false, // Fill_Rectangle
    false, // Set_Fill_Color
    false, // Set_Fog_Color
    false, // Set_Blend_Color
    false, // Set_Prim_Color
    false, // Set_Env_Color
    false, // Set_Combine
    false, // Set_Texture_Image
    true,  // Set_Mask_Image
    true,  // Set_Color_Image
};

static void cmd_run_buffered(uint32_t worker_id)
{
    uint32_t pos;
    for (pos = 0; pos < rdp_cmd_buf_pos; pos++) {
        rdp_cmd(rdp_states[worker_id], rdp_cmd_buf[pos]);
    }
}

static void cmd_flush(void)
{
    // only run if there's something buffered
    if (rdp_cmd_buf_pos) {
        // let workers run all buffered commands in parallel
        parallel_run(cmd_run_buffered);
        // reset buffer by starting from the beginning
        rdp_cmd_buf_pos = 0;
    }
}

static void cmd_init(void)
{
    rdp_cmd_pos = 0;
    rdp_cmd_id = 0;
    rdp_cmd_len = CMD_MAX_INTS;
}

void n64video_config_defaults(struct n64video_config* config)
{
    config->parallel = true;
    config->num_workers = 0;
    config->vi.interp = VI_INTERP_NEAREST;
    config->vi.mode = VI_MODE_NORMAL;
    config->vi.widescreen = false;
    config->vi.hide_overscan = false;
}

void rdp_init_worker(uint32_t worker_id)
{
    rdp_create(&rdp_states[worker_id], parallel_num_workers(), worker_id);
}

void n64video_init(struct n64video_config* _config)
{
    if (_config) {
        config = *_config;
    }

    // initialize static lookup tables, once is enough
    if (!init_lut) {
        blender_init_lut();
        coverage_init_lut();
        combiner_init_lut();
        tex_init_lut();
        z_init_lut();

        init_lut = true;
    }

    // init externals
    screen_init(&config);
    plugin_init();

    // init internals
    rdram_init();
    vi_init();
    cmd_init();

    rdp_pipeline_crashed = 0;
    memset(&onetimewarnings, 0, sizeof(onetimewarnings));

    if (config.parallel) {
        parallel_alinit(config.num_workers);
        rdp_states = calloc(parallel_num_workers(), sizeof(struct rdp_state*));
        parallel_run(rdp_init_worker);
    } else {
        rdp_states = calloc(1, sizeof(struct rdp_state*));
        rdp_create(&rdp_states[0], 0, 0);
    }
}

void n64video_process_list(void)
{
    uint32_t** dp_reg = plugin_get_dp_registers();
    uint32_t dp_current_al = (*dp_reg[DP_CURRENT] & ~7) >> 2;
    uint32_t dp_end_al = (*dp_reg[DP_END] & ~7) >> 2;

    // don't do anything if the RDP has crashed or the registers are not set up correctly
    if (rdp_pipeline_crashed || dp_end_al <= dp_current_al) {
        return;
    }

    // while there's data in the command buffer...
    while (dp_end_al - dp_current_al > 0) {
        uint32_t i, toload;
        bool xbus_dma = (*dp_reg[DP_STATUS] & DP_STATUS_XBUS_DMA) != 0;
        uint32_t* dmem = (uint32_t*)plugin_get_dmem();
        uint32_t* cmd_buf = rdp_cmd_buf[rdp_cmd_buf_pos];

        // when reading the first int, extract the command ID and update the buffer length
        if (rdp_cmd_pos == 0) {
            if (xbus_dma) {
                cmd_buf[rdp_cmd_pos++] = dmem[dp_current_al++ & 0x3ff];
            } else {
                cmd_buf[rdp_cmd_pos++] = rdram_read_idx32(dp_current_al++);
            }

            rdp_cmd_id = CMD_ID(cmd_buf);
            rdp_cmd_len = rdp_commands[rdp_cmd_id].length >> 2;
        }

        // copy more data from the N64 to the local command buffer
        toload = MIN(dp_end_al - dp_current_al, rdp_cmd_len - 1);

        if (xbus_dma) {
            for (i = 0; i < toload; i++) {
                cmd_buf[rdp_cmd_pos++] = dmem[dp_current_al++ & 0x3ff];
            }
        } else {
            for (i = 0; i < toload; i++) {
                cmd_buf[rdp_cmd_pos++] = rdram_read_idx32(dp_current_al++);
            }
        }

        // if there's enough data for the current command...
        if (rdp_cmd_pos == rdp_cmd_len) {
            // check if parallel processing is enabled
            if (config.parallel) {
                // special case: sync_full always needs to be run in main thread
                if (rdp_cmd_id == CMD_ID_SYNC_FULL) {
                    // first, run all pending commands
                    cmd_flush();

                    // parameters are unused, so NULL is fine
                    rdp_sync_full(NULL, NULL);
                } else {
                    // increment buffer position
                    rdp_cmd_buf_pos++;

                    // flush buffer when it is full or when the current command requires a sync
                    if (rdp_cmd_buf_pos >= CMD_BUFFER_SIZE || rdp_cmd_sync[rdp_cmd_id]) {
                        cmd_flush();
                    }
                }
            } else {
                // run command directly
                rdp_cmd(rdp_states[0], cmd_buf);
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
    vi_close();
    parallel_close();
    plugin_close();
    screen_close();

    if (rdp_states) {
       uint32_t i;
        for (i = 0; i < config.num_workers; i++) {
            rdp_destroy(rdp_states[i]);
        }

        free(rdp_states);
        rdp_states = NULL;
    }
}
