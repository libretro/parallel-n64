#ifndef RDP_DUMP_H
#define RDP_DUMP_H

#include <stdint.h>
#include <boolean.h>

bool rdp_dump_init(const char *path, uint32_t dram_size);
void rdp_dump_end(void);
void rdp_dump_flush_dram(const void *dram, uint32_t size);

void rdp_dump_begin_command_list(void);
void rdp_dump_end_command_list(void);
void rdp_dump_emit_command(uint32_t command, const uint32_t *cmd_data, uint32_t cmd_words);

#endif
