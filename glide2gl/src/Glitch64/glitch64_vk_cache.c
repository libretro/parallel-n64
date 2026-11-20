/* Glide64's Vulkan cache, one file a game (glitch64_vk_cache.h). C89. */
#include <stdlib.h>
#include <string.h>

#include <compat/strl.h>

#include "glitch64_vk_cache.h"

#define HEAD_WORDS 4

/* the files of before: the journal's records, and its pipelines beside */
static void open_before(g64vk_cache *c, const char *before_path, uint32_t magic,
      uint32_t version, unsigned words)
{
   char pipelines[1040];
   void *data = NULL;
   int64_t size = 0;

   if (!before_path || !*before_path)
      return;
   if (filestream_exists(before_path) && filestream_read_file(before_path, &data, &size))
   {
      const uint32_t *w = (const uint32_t*)data;
      c->before = 1;
      c->copy = data;
      if (size >= 8 && w[0] == magic && w[1] == version)
      {
         c->records = w + 2;
         c->count = (unsigned)((size_t)(size - 8) / (words * 4u));
      }
   }
   strlcpy(pipelines, before_path, sizeof(pipelines));
   strlcat(pipelines, ".pipelines", sizeof(pipelines));
   if (filestream_exists(pipelines) && filestream_read_file(pipelines, &data, &size))
   {
      c->before = 1;
      c->copy2 = data;
      c->pipelines = data;
      c->pipelines_size = (size_t)size;
   }
}

void g64vk_cache_open(g64vk_cache *c, const char *path, const char *before_path,
      uint32_t magic, uint32_t version, unsigned words)
{
   const uint8_t *data = NULL;
   int64_t size = 0;

   memset(c, 0, sizeof(*c));
   /* one open; the file lent where it can be mapped, read once where not */
   if ((c->file = filestream_open(path, RETRO_VFS_FILE_ACCESS_READ, RETRO_VFS_FILE_ACCESS_HINT_FREQUENT_ACCESS)))
   {
      data = filestream_get_mapped_ptr(c->file, &size);
      if (!data && (size = filestream_get_size(c->file)) > 0 && (c->copy = malloc((size_t)size))
            && filestream_read(c->file, c->copy, size) == size)
         data = (const uint8_t*)c->copy;
   }
   if (data && size >= HEAD_WORDS * 4)
   {
      uint32_t h[HEAD_WORDS];
      memcpy(h, data, sizeof(h));
      if (h[0] == G64VK_CACHE_MAGIC && h[1] == version
            && (uint64_t)HEAD_WORDS * 4u + (uint64_t)h[2] * words * 4u + h[3] == (uint64_t)size)
      {
         c->records = (const uint32_t*)(data + HEAD_WORDS * 4);
         c->count = h[2];
         c->pipelines = h[3] ? data + HEAD_WORDS * 4 + (size_t)h[2] * words * 4 : NULL;
         c->pipelines_size = h[3];
         return;
      }
   }
   /* not there, or not whole */
   g64vk_cache_close(c);
   open_before(c, before_path, magic, version, words);
}

void g64vk_cache_close(g64vk_cache *c)
{
   if (c->file)
      filestream_close(c->file);
   free(c->copy);
   free(c->copy2);
   c->file = NULL;
   c->copy = c->copy2 = NULL;
   c->records = NULL;
   c->count = 0;
   c->pipelines = NULL;
   c->pipelines_size = 0;
}

int g64vk_cache_write(const char *path, const char *before_path, uint32_t version,
      const uint32_t *records, unsigned count, unsigned words,
      const void *pipelines, size_t pipelines_size)
{
   uint32_t h[HEAD_WORDS];
   const int64_t bytes = (int64_t)count * words * 4;
   RFILE *f;
   int ok;

   h[0] = G64VK_CACHE_MAGIC;
   h[1] = version;
   h[2] = count;
   h[3] = pipelines ? (uint32_t)pipelines_size : 0;
   if (!(f = filestream_open(path, RETRO_VFS_FILE_ACCESS_WRITE, RETRO_VFS_FILE_ACCESS_HINT_NONE)))
      return 0;
   /* each part from where it is */
   ok = filestream_write(f, h, sizeof(h)) == (int64_t)sizeof(h)
      && (!bytes || filestream_write(f, records, bytes) == bytes)
      && (!h[3] || filestream_write(f, pipelines, (int64_t)h[3]) == (int64_t)h[3]);
   ok = filestream_close(f) == 0 && ok;
   if (ok && before_path && *before_path)
   {
      char before_pipelines[1040];
      strlcpy(before_pipelines, before_path, sizeof(before_pipelines));
      strlcat(before_pipelines, ".pipelines", sizeof(before_pipelines));
      if (filestream_exists(before_path))
         filestream_delete(before_path);
      if (filestream_exists(before_pipelines))
         filestream_delete(before_pipelines);
   }
   return ok;
}
