#include "N64.h"

uint8_t *HEADER;
uint8_t *IMEM;
uint64_t TMEM[512];
uint8_t *RDRAM;

uint32_t RDRAMSize;

bool ConfigOpen = false;

extern "C" void gles2n64_reset(void)
{
}
