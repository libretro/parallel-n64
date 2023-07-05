#ifndef M64P_EXT_LIBPL_H
#define M64P_EXT_LIBPL_H

#include <stdint.h>

void init_libpl(void);
void free_libpl(void);

int read_libpl(void* opaque, uint32_t address, uint32_t* value);
int write_libpl(void* opaque, uint32_t address, uint32_t value, uint32_t mask);

void libpl_change_savestate_token(void);
void libpl_set_cheats_used(void);

#endif
