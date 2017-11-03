#pragma once

#include <stdint.h>

uint32_t** plugin_get_dp_registers(void);
uint32_t** plugin_get_vi_registers(void);
uint32_t plugin_get_rdram_size(void);
uint8_t* plugin_get_dmem(void);
uint8_t* plugin_get_rom_header(void);
