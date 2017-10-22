#pragma once

#include <stdint.h>
#include <stdbool.h>

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

struct core_config
{
    struct {
        bool trace_record;
    } dp;
    struct {
        enum vi_mode mode;
        bool widescreen;
        bool overscan;
    } vi;
    bool parallel;
    uint32_t num_workers;
};

void core_init(struct core_config* config);
void core_close(void);
void core_config_update(struct core_config* config);
void core_config_defaults(struct core_config* config);
void core_dp_sync(void);
void core_dp_update(void);
void core_vi_update(void);
void core_screenshot(char* directory);
