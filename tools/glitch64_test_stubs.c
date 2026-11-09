/* The Glitch64 tests run with no shader cache on disk */
#include <boolean.h>
#include <encodings/crc32.h>
#include <file/file_path.h>
#include <streams/file_stream.h>

#include "plugin/plugin.h"

static unsigned char rom_header[0x40];
GFX_INFO gfx_info = { rom_header };

const char *retro_get_system_directory(void) { return ""; }

uint32_t encoding_crc32(uint32_t crc, const uint8_t *buf, size_t len)
{
   (void)buf;
   (void)len;
   return crc;
}

size_t fill_pathname_join(char *out_path, const char *dir, const char *path, size_t size)
{
   (void)dir;
   (void)path;
   if (size)
      out_path[0] = '\0';
   return 0;
}

bool path_mkdir(const char *dir)
{
   (void)dir;
   return false;
}

bool filestream_exists(const char *path)
{
   (void)path;
   return false;
}

int64_t filestream_read_file(const char *path, void **buf, int64_t *len)
{
   (void)path;
   (void)buf;
   (void)len;
   return 0;
}

RFILE *filestream_open(const char *path, unsigned mode, unsigned hints)
{
   (void)path;
   (void)mode;
   (void)hints;
   return NULL;
}

int64_t filestream_write(RFILE *stream, const void *data, int64_t len)
{
   (void)stream;
   (void)data;
   (void)len;
   return -1;
}

int filestream_close(RFILE *stream)
{
   (void)stream;
   return 0;
}
