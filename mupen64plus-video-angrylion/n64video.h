#pragma once

#include <stdint.h>
#include <stdbool.h>

#define RDRAM_MAX_SIZE 0x800000

#ifdef __cplusplus
extern "C" {
#endif

enum dp_register
{
    DP_START,
    DP_END,
    DP_CURRENT,
    DP_STATUS,
    DP_CLOCK,
    DP_BUFBUSY,
    DP_PIPEBUSY,
    DP_TMEM,
    DP_NUM_REG
};

enum vi_register
{
    VI_STATUS,  // aka VI_CONTROL
    VI_ORIGIN,  // aka VI_DRAM_ADDR
    VI_WIDTH,
    VI_INTR,
    VI_V_CURRENT_LINE,
    VI_TIMING,
    VI_V_SYNC,
    VI_H_SYNC,
    VI_LEAP,    // aka VI_H_SYNC_LEAP
    VI_H_START, // aka VI_H_VIDEO
    VI_V_START, // aka VI_V_VIDEO
    VI_V_BURST,
    VI_X_SCALE,
    VI_Y_SCALE,
    VI_NUM_REG
};

enum vi_mode
{
    VI_MODE_NORMAL,     // color buffer with VI filter
    VI_MODE_COLOR,      // direct color buffer, unfiltered
    VI_MODE_DEPTH,      // depth buffer as grayscale
    VI_MODE_COVERAGE,   // coverage as grayscale
    VI_MODE_NUM
};

enum vi_interp
{
    VI_INTERP_NEAREST,
    VI_INTERP_LINEAR,
    VI_INTERP_NUM
};

struct n64video_config
{
    struct {
        enum vi_mode mode;
        enum vi_interp interp;
        bool widescreen;
        bool hide_overscan;
    } vi;
    bool parallel;
    uint32_t num_workers;
};

void n64video_config_defaults(struct n64video_config* config);
void n64video_init(struct n64video_config* config);
void n64video_update_screen(void);
void n64video_process_list(void);
void n64video_close(void);

#ifdef __cplusplus
}
#endif
