#pragma once

#include "trace.h"

#include <stdint.h>
#include <stdbool.h>

bool trace_read_open(const char* path);
bool trace_read_is_open(void);
void trace_read_header(uint32_t* rdram_size);
char trace_read_id(void);
void trace_read_cmd(uint32_t* cmd, uint32_t* length);
void trace_read_rdram(void);
void trace_read_vi(uint32_t** vi_reg);
void trace_read_close(void);
