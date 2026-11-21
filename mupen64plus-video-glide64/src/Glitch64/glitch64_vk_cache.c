/* Glide64's Vulkan cache, one file every game shares (glitch64_vk_cache.h). C89. */
#include <stdlib.h>
#include <string.h>

#include <compat/strl.h>
#include <file/file_path.h>
#include <retro_dirent.h>

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

/* a game's file of before: 1 its own, 2 its journal, 3 the pipeline
 * cache beside that, 0 none */
static int game_file(const char *name, const char *prefix)
{
   static const char *const ends[3] = { ".cache", ".keys", ".keys.pipelines" };
   const size_t n = strlen(name), p = strlen(prefix);
   int i;
   if (strncmp(name, prefix, p))
      return 0;
   for (i = 0; i < 3; i++)
   {
      const size_t e = strlen(ends[i]);
      if (n > p + e && !strcmp(name + n - e, ends[i]))
         return i + 1;
   }
   return 0;
}

uint32_t g64vk_cache_game_bit(const char *stem, size_t n)
{
   uint32_t h = 2166136261u;
   size_t i;
   for (i = 0; i < n; i++)
      h = (h ^ (unsigned char)stem[i]) * 16777619u;
   return 1u << ((h ^ (h >> 16)) & 31u);
}

static size_t s_key_bytes;

static int by_key(const void *a, const void *b)
{
   return memcmp(a, b, s_key_bytes);
}

int g64vk_cache_gather(const char *folder, const char *prefix, uint32_t magic, uint32_t version,
      unsigned words, unsigned key_words, uint32_t **records, unsigned *count,
      void **pipelines, size_t *size)
{
   struct RDIR *d = retro_opendir(folder);
   char path[1040];
   const unsigned out = words + 1;
   unsigned room = 0, i, j, n = 0;
   int any = 0;

   *records = NULL;
   *count = 0;
   *pipelines = NULL;
   *size = 0;
   if (!d)
      return 0;
   while (retro_readdir(d))
   {
      const char *name = retro_dirent_get_name(d);
      const int kind = name ? game_file(name, prefix) : 0;
      g64vk_cache c;
      if (kind != 1 && kind != 2)
         continue;
      fill_pathname_join(path, folder, name, sizeof(path));
      /* a journal is read as the file of before of none */
      g64vk_cache_open(&c, kind == 1 ? path : "", kind == 2 ? path : NULL, magic, version, words);
      any = 1;
      if (c.records && c.count)
      {
         if (*count + c.count > room)
         {
            const unsigned more_room = *count + c.count + room;
            uint32_t *more = (uint32_t*)realloc(*records, (size_t)more_room * out * 4);
            if (more)
            {
               *records = more;
               room = more_room;
            }
         }
         if (*count + c.count <= room)
         {
            /* its game's bit after each */
            const uint32_t bit = g64vk_cache_game_bit(name, strlen(name) - (kind == 1 ? 6 : 5));
            for (j = 0; j < c.count; j++)
            {
               uint32_t *r = *records + (size_t)(*count + j) * out;
               memcpy(r, c.records + (size_t)j * words, (size_t)words * 4);
               r[words] = bit;
            }
            *count += c.count;
         }
      }
      if (c.pipelines_size > *size)
      {
         void *copy = malloc(c.pipelines_size);
         if (copy)
         {
            memcpy(copy, c.pipelines, c.pipelines_size);
            free(*pipelines);
            *pipelines = copy;
            *size = c.pipelines_size;
         }
      }
      g64vk_cache_close(&c);
   }
   retro_closedir(d);
   if (!*count)
      return any;
   /* each once, the words after its key those of every one */
   s_key_bytes = (size_t)key_words * 4;
   qsort(*records, *count, (size_t)out * 4, by_key);
   for (i = 0; i < *count; i++)
   {
      const uint32_t *r = *records + (size_t)i * out;
      uint32_t *last = *records + (size_t)(n ? n - 1 : 0) * out;
      if (n && !by_key(last, r))
         for (j = key_words; j < out; j++)
            last[j] |= r[j];
      else
         memmove(*records + (size_t)n++ * out, r, (size_t)out * 4);
   }
   *count = n;
   return 1;
}

void g64vk_cache_remove_before(const char *folder, const char *prefix)
{
   struct RDIR *d = retro_opendir(folder);
   char path[1040];
   char **names = NULL;
   unsigned count = 0, room = 0, i;

   if (!d)
      return;
   /* the names first: nothing removed under the listing */
   while (retro_readdir(d))
   {
      const char *name = retro_dirent_get_name(d);
      char *copy;
      if (!name || !game_file(name, prefix))
         continue;
      if (count == room)
      {
         char **more = (char**)realloc(names, (size_t)(room * 2 + 16) * sizeof(*names));
         if (!more)
            break;
         names = more;
         room = room * 2 + 16;
      }
      if (!(copy = (char*)malloc(strlen(name) + 1)))
         break;
      strcpy(copy, name);
      names[count++] = copy;
   }
   retro_closedir(d);
   for (i = 0; i < count; i++)
   {
      fill_pathname_join(path, folder, names[i], sizeof(path));
      filestream_delete(path);
      free(names[i]);
   }
   free(names);
}
