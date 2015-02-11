#ifndef M64P_LIBRETRO_MEMORY_H
#define M64P_LIBRETRO_MEMORY_H

#include <stdint.h>

typedef struct _save_memory_data
{
   uint8_t eeprom[0x200];
   uint8_t mempack[4][0x8000];
   uint8_t sram[0x8000];
   uint8_t flashram[0x20000];

   /* Some games use 16Kbit (2048 bytes) eeprom saves, the initial
    * libretro-mupen64plus save file implementation failed to account
    * for these. The missing bytes are stored here to avoid breaking
    * saves of games unaffected by the issue. */
   uint8_t eeprom2[0x600];
} save_memory_data;

extern save_memory_data saved_memory;

#endif
