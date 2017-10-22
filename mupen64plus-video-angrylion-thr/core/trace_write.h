#pragma once

#include "trace.h"

#include <stdint.h>
#include <stdbool.h>

bool trace_write_open(const char* path);
bool trace_write_is_open(void);
void trace_write_header(uint32_t rdram_size);
void trace_write_cmd(uint32_t* cmd, uint32_t length);
void trace_write_rdram(uint32_t offset, uint32_t length);
void trace_write_vi(uint32_t** vi_reg);
void trace_write_reset(void);
void trace_write_close(void);
