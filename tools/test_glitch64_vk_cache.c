/* test_glitch64_vk_cache: Glide64's Vulkan cache file
 * (mupen64plus-video-glide64/src/Glitch64/glitch64_vk_cache.c) on real files.
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
 * Gathered, where the file every game shares is not there: each game's
 * file of before in a folder, its own or its journal with its pipeline
 * cache beside it, only those named so; their records each once by key,
 * the blend bits of every one together, a word after each of the games
 * that had it, by their names; the largest pipeline cache. Then removed,
 * only they.
 *
 * The module is included here, its map through test_get_mapped_ptr, to
 * read without the map too. C89. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <file/file_path.h>

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

   /* every game's files of before, gathered and removed */
   {
      static const char *const keep[4] = { "gather_dir/other.cache", "gather_dir/glide64-vulkan.cache",
         "gather_dir/glide64-vulkan_x.bin", "gather_dir/glide64-vulkan_.cache" };
      static uint32_t a[2 * WORDS], b[2 + 3 * WORDS], m[(WORDS + 1) * 4];
      const uint32_t g1 = g64vk_cache_game_bit("glide64-vulkan_0001", 19);
      const uint32_t g2 = g64vk_cache_game_bit("glide64-vulkan_0002", 19);
      uint32_t *got = NULL;
      void *gp = NULL;
      unsigned count = 99, k;
      size_t gsize = 99;
      path_mkdir("gather_dir");
      /* none there: nothing */
      CHECK(!g64vk_cache_gather("gather_dir", "glide64-vulkan_", MAGIC, VERSION, WORDS, WORDS - 1, &got, &count, &gp,
            &gsize) && !got && !count && !gp && !gsize);
      CHECK(!g64vk_cache_gather("no_such_dir_here", "glide64-vulkan_", MAGIC, VERSION, WORDS, WORDS - 1, &got, &count,
            &gp, &gsize));
      /* a game's own: records 2 and 0; another's journal: 1, 2 again with
       * another blend bit, 0, and a larger pipeline cache beside it */
      memcpy(a, records + 2 * WORDS, WORDS * 4);
      memcpy(a + WORDS, records, WORDS * 4);
      CHECK(g64vk_cache_write("gather_dir/glide64-vulkan_0001.cache", NULL, VERSION, a, 2, WORDS, pipes, 30));
      b[0] = MAGIC;
      b[1] = VERSION;
      memcpy(b + 2, records + WORDS, WORDS * 4);
      memcpy(b + 2 + WORDS, records + 2 * WORDS, WORDS * 4);
      b[2 + 2 * WORDS - 1] |= 0x80000000u;
      memcpy(b + 2 + 2 * WORDS, records, WORDS * 4);
      put("gather_dir/glide64-vulkan_0002.keys", b, sizeof(b));
      put("gather_dir/glide64-vulkan_0002.keys.pipelines", pipes, 50);
      for (k = 0; k < 4; k++)
         put(keep[k], "x", 1);
      CHECK(g64vk_cache_gather("gather_dir", "glide64-vulkan_", MAGIC, VERSION, WORDS, WORDS - 1, &got, &count, &gp,
            &gsize));
      CHECK(count == 3 && got != NULL);
      /* by key, each once, the blend bits of both, the games that had it */
      CHECK(g1 && !(g1 & (g1 - 1)) && g2 && !(g2 & (g2 - 1)));
      for (k = 0; k < 3; k++)
      {
         memcpy(m + k * (WORDS + 1), records + k * WORDS, WORDS * 4);
         m[k * (WORDS + 1) + WORDS] = k == 1 ? g2 : g1 | g2;
      }
      m[2 * (WORDS + 1) + WORDS - 1] |= 0x80000000u;
      qsort(m, 3, (WORDS + 1) * 4, by_key);
      CHECK(got && !memcmp(got, m, 3 * (WORDS + 1) * 4));
      CHECK(gsize == 50 && gp && !memcmp(gp, pipes, 50));
      free(got);
      free(gp);
      /* a journal of another version among them: there all the same, its
       * records not */
      b[1] = VERSION + 1;
      put("gather_dir/glide64-vulkan_0002.keys", b, sizeof(b));
      CHECK(g64vk_cache_gather("gather_dir", "glide64-vulkan_", MAGIC, VERSION, WORDS, WORDS - 1, &got, &count, &gp,
            &gsize));
      CHECK(count == 2 && gsize == 50);
      free(got);
      free(gp);
      /* removed: only they */
      g64vk_cache_remove_before("gather_dir", "glide64-vulkan_");
      CHECK(!filestream_exists("gather_dir/glide64-vulkan_0001.cache") &&
            !filestream_exists("gather_dir/glide64-vulkan_0002.keys") &&
            !filestream_exists("gather_dir/glide64-vulkan_0002.keys.pipelines"));
      for (k = 0; k < 4; k++)
      {
         CHECK(filestream_exists(keep[k]));
         filestream_delete(keep[k]);
      }
      /* none left: nothing gathered */
      CHECK(!g64vk_cache_gather("gather_dir", "glide64-vulkan_", MAGIC, VERSION, WORDS, WORDS - 1, &got, &count, &gp,
            &gsize) && !count && !got);
      g64vk_cache_remove_before("no_such_dir_here", "glide64-vulkan_");
      /* a file of before with no combiners: gathered all the same, to go */
      CHECK(g64vk_cache_write("gather_dir/glide64-vulkan_0003.cache", NULL, VERSION, NULL, 0, WORDS, pipes, 20));
      CHECK(g64vk_cache_gather("gather_dir", "glide64-vulkan_", MAGIC, VERSION, WORDS, WORDS - 1, &got, &count, &gp,
            &gsize) && !count && !got && gsize == 20);
      free(gp);
      /* two keys only their last word tells apart: two */
      memcpy(a, records, WORDS * 4);
      memcpy(a + WORDS, records, WORDS * 4);
      a[WORDS + WORDS - 2] ^= 1;
      CHECK(g64vk_cache_write("gather_dir/glide64-vulkan_0003.cache", NULL, VERSION, a, 2, WORDS, NULL, 0));
      CHECK(g64vk_cache_gather("gather_dir", "glide64-vulkan_", MAGIC, VERSION, WORDS, WORDS - 1, &got, &count, &gp,
            &gsize) && count == 2);
      free(got);
      free(gp);
      g64vk_cache_remove_before("gather_dir", "glide64-vulkan_");
      /* more games than the first rooms for their names */
      for (k = 0; k < 40; k++)
      {
         char name[64];
         sprintf(name, "gather_dir/glide64-vulkan_%04u.cache", k);
         CHECK(g64vk_cache_write(name, NULL, VERSION, records + (k % 3) * WORDS, 1, WORDS, NULL, 0));
      }
      CHECK(g64vk_cache_gather("gather_dir", "glide64-vulkan_", MAGIC, VERSION, WORDS, WORDS - 1, &got, &count, &gp,
            &gsize) && count == 3);
      free(got);
      free(gp);
      g64vk_cache_remove_before("gather_dir", "glide64-vulkan_");
      CHECK(!g64vk_cache_gather("gather_dir", "glide64-vulkan_", MAGIC, VERSION, WORDS, WORDS - 1, &got, &count, &gp,
            &gsize));
      rmdir("gather_dir");
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
