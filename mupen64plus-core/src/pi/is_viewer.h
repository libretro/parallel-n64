#ifndef M64P_PI_IS_VIEWER_H
#define M64P_PI_IS_VIEWER_H

#include <stddef.h>
#include <stdint.h>

void poweron_is_viewer(void);
void poweroff_is_viewer(void);

int read_is_viewer(void* opaque, uint32_t address, uint32_t* value);
int write_is_viewer(void* opaque, uint32_t address, uint32_t value, uint32_t mask);

#endif
