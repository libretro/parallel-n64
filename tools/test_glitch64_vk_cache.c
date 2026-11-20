/* test_glitch64_vk_cache: Glide64's Vulkan cache file
 * (glide2gl/src/Glitch64/glitch64_vk_cache.c) on real files.
 *
 * Written: the header, the records and the pipeline cache, each from
 * where it is, and the files of before removed once it is; not written
 * where it cannot be opened, the files of before kept. Read: mapped where
 * the VFS maps it, read once where not; the records and the pipeline cache
 * lent from it. A file of another magic, version or size is none, and then
 * the files of before are read (the journal's records where it is of
 * this magic and version, and its pipeline cache beside it), marked as
 * read from before. Closed: nothing lent.
 *
 * The module is included here, its map through test_get_mapped_ptr, to
 * read without the map too. C89. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <streams/file_stream.h>
#include "glitch64_vk_cache.h"

const uint8_t *test_get_mapped_ptr(RFILE *stream, int64_t *len);
void *test_malloc(size_t n);
#define filestream_get_mapped_ptr test_get_mapped_ptr
#define malloc test_malloc
#include "glitch64_vk_cache.c"
#undef filestream_get_mapped_ptr
#undef malloc

static int s_failmalloc;
void *test_malloc(size_t n)
{
   return s_failmalloc ? NULL : malloc(n);
}

static int failures;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)

#define PATH    "test_glitch64_vk_cache.cache"
#define BEFORE  "test_glitch64_vk_cache.keys"
#define BEFORE2 "test_glitch64_vk_cache.keys.pipelines"
#define NOWHERE "no_such_dir_here/test.cache"
#define MAGIC   0x47364b4au
#define VERSION 2u
#define WORDS   18u

static int s_nomap;
static int s_mapped;
const uint8_t *test_get_mapped_ptr(RFILE *stream, int64_t *len)
{
   const uint8_t *p;
   if (s_nomap)
   {
      *len = 0;
      return NULL;
   }
   p = filestream_get_mapped_ptr(stream, len);
   s_mapped += p != NULL;
   return p;
}

static void put(const char *path, const void *data, size_t size)
{
   CHECK(filestream_write_file(path, data, (int64_t)size));
}

static int64_t size_of(const char *path)
{
   void *data = NULL;
   int64_t size = -1;
   if (!filestream_exists(path) || !filestream_read_file(path, &data, &size))
      size = -1;
   free(data);
   return size;
}

static void clear(void)
{
   filestream_delete(PATH);
   filestream_delete(BEFORE);
   filestream_delete(BEFORE2);
}

int main(void)
{
   static uint32_t records[3 * WORDS];
   static uint8_t pipes[100];
   static uint8_t file[16 + sizeof(records) + sizeof(pipes) + 8];
   g64vk_cache c;
   unsigned i;
   int pass;

   for (i = 0; i < 3 * WORDS; i++)
      records[i] = 0x01010101u * i + 7u;
   for (i = 0; i < sizeof(pipes); i++)
      pipes[i] = (uint8_t)(200 - i);
   clear();

   /* nothing there */
   g64vk_cache_open(&c, PATH, BEFORE, MAGIC, VERSION, WORDS);
   CHECK(!c.records && !c.count && !c.pipelines && !c.pipelines_size && !c.before);
   g64vk_cache_close(&c);

   for (pass = 0; pass < 2; pass++)
   {
      s_nomap = pass;
      s_mapped = 0;
      /* written, then read back: lent, whole */
      CHECK(g64vk_cache_write(PATH, BEFORE, VERSION, records, 3, WORDS, pipes, sizeof(pipes)));
      CHECK(size_of(PATH) == (int64_t)(16 + sizeof(records) + sizeof(pipes)));
      g64vk_cache_open(&c, PATH, BEFORE, MAGIC, VERSION, WORDS);
      CHECK(c.count == 3 && c.records && !memcmp(c.records, records, sizeof(records)));
      CHECK(c.pipelines_size == sizeof(pipes) && c.pipelines && !memcmp(c.pipelines, pipes, sizeof(pipes)));
      CHECK(!c.before);
      g64vk_cache_close(&c);
      CHECK(!c.records && !c.count && !c.pipelines && !c.pipelines_size);
      g64vk_cache_close(&c);
      CHECK(pass == 1 || s_mapped == 1);
      CHECK(pass == 0 || s_mapped == 0);
   }
   /* not mapped, and no memory to read it into: none */
   s_nomap = 1;
   s_failmalloc = 1;
   g64vk_cache_open(&c, PATH, BEFORE, MAGIC, VERSION, WORDS);
   CHECK(!c.records && !c.count && !c.pipelines && !c.before);
   g64vk_cache_close(&c);
   s_failmalloc = 0;
   s_nomap = 0;

   /* the header as written */
   {
      void *data = NULL;
      int64_t size = 0;
      uint32_t h[4];
      CHECK(filestream_read_file(PATH, &data, &size) && data);
      memcpy(h, data, sizeof(h));
      CHECK(h[0] == G64VK_CACHE_MAGIC && h[1] == VERSION && h[2] == 3 && h[3] == sizeof(pipes));
      memcpy(file, data, (size_t)size);
      free(data);
   }

   /* none, or no pipeline cache */
   CHECK(g64vk_cache_write(PATH, NULL, VERSION, NULL, 0, WORDS, NULL, 0));
   CHECK(size_of(PATH) == 16);
   put(BEFORE2, pipes, 40);
   g64vk_cache_open(&c, PATH, BEFORE, MAGIC, VERSION, WORDS);
   CHECK(c.count == 0 && !c.pipelines && !c.pipelines_size && !c.before);
   g64vk_cache_close(&c);
   filestream_delete(BEFORE2);
   CHECK(g64vk_cache_write(PATH, NULL, VERSION, records, 1, WORDS, NULL, 64));
   CHECK(size_of(PATH) == (int64_t)(16 + WORDS * 4));
   g64vk_cache_open(&c, PATH, NULL, MAGIC, VERSION, WORDS);
   CHECK(c.count == 1 && !c.pipelines && !c.pipelines_size);
   g64vk_cache_close(&c);

   /* another magic, version, a byte more or less, or a header cut short:
    * none */
   {
      const size_t whole = 16 + sizeof(records) + sizeof(pipes);
      size_t k;
      for (k = 0; k < 10; k++)
      {
         s_nomap = (int)(k & 1);
         size_t n = whole;
         memcpy(file + whole, "\0\0\0\0", 4);
         if (k / 2 == 0)
            file[0] ^= 1;
         else if (k / 2 == 1)
            file[4] ^= 1;
         else if (k / 2 == 2)
            n = whole + 1;
         else if (k / 2 == 3)
            n = whole - 1;
         else
            n = 15;
         put(PATH, file, n);
         g64vk_cache_open(&c, PATH, BEFORE, MAGIC, VERSION, WORDS);
         CHECK(!c.records && !c.count && !c.pipelines && !c.before);
         g64vk_cache_close(&c);
         if (k / 2 == 0)
            file[0] ^= 1;
         else if (k / 2 == 1)
            file[4] ^= 1;
      }
      s_nomap = 0;
      /* more records counted than there are */
      file[8] = 4;
      put(PATH, file, whole);
      g64vk_cache_open(&c, PATH, NULL, MAGIC, VERSION, WORDS);
      CHECK(!c.records && !c.pipelines);
      g64vk_cache_close(&c);
      file[8] = 3;
   }

   /* the files of before, where the file is not there or not whole */
   {
      /* two records and one a crash cut two bytes short of whole */
      static uint32_t journal[2 + 3 * WORDS];
      const size_t journal_size = sizeof(journal) - 2;
      journal[0] = MAGIC;
      journal[1] = VERSION;
      memcpy(journal + 2, records, 3 * WORDS * 4);
      for (pass = 0; pass < 2; pass++)
      {
         clear();
         /* one not whole, read into memory: let go before the journal is */
         s_nomap = pass;
         if (pass)
            put(PATH, file, 15);
         put(BEFORE, journal, journal_size);
         put(BEFORE2, pipes, 40);
         g64vk_cache_open(&c, PATH, BEFORE, MAGIC, VERSION, WORDS);
         CHECK(c.before && c.count == 2 && c.records && !memcmp(c.records, records, 2 * WORDS * 4));
         CHECK(c.pipelines_size == 40 && c.pipelines && !memcmp(c.pipelines, pipes, 40));
         g64vk_cache_close(&c);
         CHECK(!c.records && !c.pipelines);
      }
      s_nomap = 0;
      /* a journal of another magic or version: its records not; its
       * pipeline cache alone, or the journal alone */
      for (pass = 0; pass < 4; pass++)
      {
         clear();
         journal[pass & 1] ^= 1;
         put(BEFORE, journal, pass < 2 ? journal_size : 4);
         journal[pass & 1] ^= 1;
         g64vk_cache_open(&c, PATH, BEFORE, MAGIC, VERSION, WORDS);
         CHECK(c.before && !c.records && !c.count && !c.pipelines);
         g64vk_cache_close(&c);
      }
      clear();
      put(BEFORE2, pipes, 40);
      g64vk_cache_open(&c, PATH, BEFORE, MAGIC, VERSION, WORDS);
      CHECK(c.before && !c.records && c.pipelines_size == 40);
      g64vk_cache_close(&c);
      clear();
      put(BEFORE, journal, 8 + WORDS * 4);
      g64vk_cache_open(&c, PATH, BEFORE, MAGIC, VERSION, WORDS);
      CHECK(c.before && c.count == 1 && !c.pipelines);
      g64vk_cache_close(&c);

      /* not written: the files of before kept */
      put(BEFORE2, pipes, 40);
      CHECK(!g64vk_cache_write(NOWHERE, BEFORE, VERSION, records, 3, WORDS, pipes, sizeof(pipes)));
      CHECK(size_of(BEFORE) == (int64_t)(8 + WORDS * 4) && size_of(BEFORE2) == 40);
      /* written: gone */
      CHECK(g64vk_cache_write(PATH, BEFORE, VERSION, records, 3, WORDS, pipes, sizeof(pipes)));
      CHECK(!filestream_exists(BEFORE) && !filestream_exists(BEFORE2));
      g64vk_cache_open(&c, PATH, BEFORE, MAGIC, VERSION, WORDS);
      CHECK(!c.before && c.count == 3);
      g64vk_cache_close(&c);
      /* the journal alone gone too */
      put(BEFORE, journal, 8);
      CHECK(g64vk_cache_write(PATH, BEFORE, VERSION, records, 3, WORDS, pipes, sizeof(pipes)));
      CHECK(!filestream_exists(BEFORE));
   }

   clear();
   if (failures)
   {
      printf("test_glitch64_vk_cache: %d failures\n", failures);
      return 1;
   }
   printf("test_glitch64_vk_cache: ok\n");
   return 0;
}
