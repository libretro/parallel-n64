#ifndef N64_H
#define N64_H

#include <stdint.h>

#define MI_INTR_DP		0x20		// Bit 5: DP intr

extern uint8_t *HEADER;
extern uint8_t *IMEM;
extern uint8_t *RDRAM;
extern uint64_t TMEM[512];
extern uint32_t RDRAMSize;
extern bool ConfigOpen;

#endif

