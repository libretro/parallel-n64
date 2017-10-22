#pragma once

#include "core/core.h"

extern m64p_dynlib_handle CoreLibHandle;
extern void(*render_callback)(int);
extern void (*debug_callback)(void *, int, const char *);
extern void *debug_call_context;
