#include "rdp_dump.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static FILE *rdp_file;
static uint8_t *rdp_dram_cache;

enum rdp_dump_cmd
{
   RDP_DUMP_CMD_INVALID = 0,
   RDP_DUMP_CMD_UPDATE_DRAM = 1,
   RDP_DUMP_CMD_BEGIN_COMMAND_LIST = 2,
   RDP_DUMP_CMD_END_COMMAND_LIST = 3,
   RDP_DUMP_CMD_RDP_COMMAND = 4,
   RDP_DUMP_CMD_EOF = 5,
};

bool rdp_dump_init(const char *path, uint32_t dram_size)
{
   if (rdp_file)
      return false;

   free(rdp_dram_cache);
   rdp_dram_cache = calloc(1, dram_size);
   if (!rdp_dram_cache)
      return false;

   rdp_file = fopen(path, "wb");
   if (!rdp_file)
   {
      free(rdp_dram_cache);
      rdp_dram_cache = NULL;
      return false;
   }

   fwrite("RDPDUMP1", 8, 1, rdp_file);
   fwrite(&dram_size, sizeof(dram_size), 1, rdp_file);
   return true;
}

void rdp_dump_end(void)
{
   if (!rdp_file)
      return;

   uint32_t cmd = RDP_DUMP_CMD_EOF;
   fwrite(&cmd, sizeof(cmd), 1, rdp_file);

   fclose(rdp_file);
   rdp_file = NULL;

   free(rdp_dram_cache);
   rdp_dram_cache = NULL;
}

void rdp_dump_flush_dram(const void *dram_, uint32_t size)
{
   if (!rdp_file)
      return;

   const uint8_t *dram = dram_;
   const uint32_t block_size = 4 * 1024;
   uint32_t i = 0;

   for (i = 0; i < size; i += block_size)
   {
      if (memcmp(dram + i, rdp_dram_cache + i, block_size))
      {
         uint32_t cmd = RDP_DUMP_CMD_UPDATE_DRAM;
         fwrite(&cmd, sizeof(cmd), 1, rdp_file);
         fwrite(&i, sizeof(i), 1, rdp_file);
         fwrite(&block_size, sizeof(block_size), 1, rdp_file);
         fwrite(dram + i, 1, block_size, rdp_file);
         memcpy(rdp_dram_cache + i, dram + i, block_size);
      }
   }
}

void rdp_dump_begin_command_list(void)
{
   if (!rdp_file)
      return;

   uint32_t cmd = RDP_DUMP_CMD_BEGIN_COMMAND_LIST;
   fwrite(&cmd, sizeof(cmd), 1, rdp_file);
}

void rdp_dump_end_command_list(void)
{
   if (!rdp_file)
      return;

   uint32_t cmd = RDP_DUMP_CMD_END_COMMAND_LIST;
   fwrite(&cmd, sizeof(cmd), 1, rdp_file);
}

void rdp_dump_emit_command(uint32_t command, const uint32_t *cmd_data, uint32_t cmd_words)
{
   if (!rdp_file)
      return;

   uint32_t cmd = RDP_DUMP_CMD_RDP_COMMAND;
   fwrite(&cmd, sizeof(cmd), 1, rdp_file);
   fwrite(&command, sizeof(command), 1, rdp_file);
   fwrite(&cmd_words, sizeof(cmd_words), 1, rdp_file);
   fwrite(cmd_data, sizeof(*cmd_data), cmd_words, rdp_file);
}
