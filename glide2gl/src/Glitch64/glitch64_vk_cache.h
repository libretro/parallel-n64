/* Glide64's Vulkan cache: one file a game, beside the other plugins' in
 * the shaders folder.
 *
 * A header of four words (the magic, the records' version, how many
 * records, how many bytes of pipeline cache), the records (the combiners
 * met and the blend functions they drew with, @words words each), then
 * the driver's pipeline cache. Host order. A file that is not that whole
 * is none.
 *
 * Before it, a game had two files: its records as the journal
 * (<name>.keys: the magic, the version, the records) and the driver's
 * cache beside them (<name>.keys.pipelines). Where the file is not there
 * they are read instead, and removed once it is written.
 *
 * C89. */
#ifndef GLITCH64_VK_CACHE_H
#define GLITCH64_VK_CACHE_H

#include <stddef.h>
#include <stdint.h>
#include <streams/file_stream.h>

#define G64VK_CACHE_MAGIC 0x43343647u /* "G64C" */

typedef struct g64vk_cache
{
   /* the records and the pipeline cache read, lent until close */
   const uint32_t *records;
   unsigned count;
   const void *pipelines;
   size_t pipelines_size;
   /* read from the files of before: the file is to be written */
   int before;

   RFILE *file;
   void *copy, *copy2;
} g64vk_cache;

/* @path is the file's; @before_path the journal of before, whose cache
 * is at it with ".pipelines" after. @magic and @version are the journal's,
 * the version the records' in the file too. Nothing there: no records. */
void g64vk_cache_open(g64vk_cache *c, const char *path, const char *before_path,
      uint32_t magic, uint32_t version, unsigned words);
/* What was lent let go */
void g64vk_cache_close(g64vk_cache *c);

/* The file written: @count records of @words words, the pipeline cache;
 * the files of before removed once it is. Whether it was. */
int g64vk_cache_write(const char *path, const char *before_path, uint32_t version,
      const uint32_t *records, unsigned count, unsigned words,
      const void *pipelines, size_t pipelines_size);

#endif
