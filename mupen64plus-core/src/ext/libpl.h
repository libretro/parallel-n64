#ifndef M64P_EXT_LIBPL_H
#define M64P_EXT_LIBPL_H

#include <stdint.h>

#define LPL_USED_CHEATS 0x1
#define LPL_USED_SAVESTATES 0x2
#define LPL_USED_SLOWDOWN 0x4
#define LPL_USED_FRAME_ADVANCE 0x8

extern uint8_t g_cheatStatus;

void init_libpl(void);
void free_libpl(void);

int read_libpl(void* opaque, uint32_t address, uint32_t* value);
int write_libpl(void* opaque, uint32_t address, uint32_t value, uint32_t mask);

void libpl_change_savestate_token(void);
void libpl_set_cheats_used(void);

#endif