/* GLideN64's combiner key journal (CombinerKeyJournal.c): keys pushed
 * from one thread all reach the file in order, what a crash cut short is
 * skipped and what comes after it still read, a journal for another
 * version is ignored, and a full queue refuses instead of waiting.
 *
 * Appended to a file of records: one opened only when the first key
 * comes, each key a record in order, the file handed back at its end
 * when the journal stops, and whether every key got there.
 *
 * A journal every game shares: a session's lines after a line of its
 * game, made only when its first key comes; a game reading only its
 * lines, of each session of it, a game line cut short being no game's;
 * and the files a game of before gathered into it once, each in lines of
 * its game, then removed, those of other names left alone. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <streams/file_stream.h>
#if defined(__linux__)
#include <signal.h>
#include <sys/resource.h>
#endif

#include <file/file_path.h>

#include "CombinerKeyJournal.h"

#define PATH "test_combiner_journal.tmp"
#define KEYS 20000u

static unsigned failures;

#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)

static uint64_t key_of(unsigned i)
{
   return ((uint64_t)i * 0x9e3779b97f4a7c15ull) ^ 0xf00dull;
}

struct seen { unsigned next; unsigned bad; };

static void in_order(void *ctx, uint64_t key, unsigned hwl)
{
   struct seen *s = (struct seen*)ctx;
   if (key != key_of(s->next) || hwl != (s->next & 15))
      s->bad++;
   s->next++;
}

struct got { unsigned n; uint64_t keys[64]; unsigned hwl[64]; };

static void collect(void *ctx, uint64_t key, unsigned hwl)
{
   struct got *g = (struct got*)ctx;
   if (g->n < 64)
   {
      g->keys[g->n] = key;
      g->hwl[g->n]  = hwl;
   }
   g->n++;
}

static void write_file(const char *path, const char *text)
{
   FILE *f = fopen(path, "wb");
   CHECK(f != NULL);
   if (f)
   {
      fputs(text, f);
      fclose(f);
   }
}

static int file_is(const char *path, const char *text)
{
   void *data = NULL;
   int64_t size = 0;
   int same = filestream_read_file(path, &data, &size) && data
      && size == (int64_t)strlen(text) && !memcmp(data, text, (size_t)size);
   free(data);
   return same;
}

#define GAME_A "0123456789abcdef"
#define GAME_B "fedcba9876543210"
#define DIR    "test_combiner_journal.dir"
#define SHARED DIR "/p.keys"

static void games(void)
{
   combiner_journal_t *j;
   struct got g;

   filestream_delete(PATH);
   /* a session that meets no key: nothing */
   j = combiner_journal_open_game(PATH, 7, GAME_A);
   combiner_journal_close(j);
   CHECK(!filestream_exists(PATH));
   /* the first game's session, then the other's, then the first's again */
   j = combiner_journal_open_game(PATH, 7, GAME_A);
   CHECK(combiner_journal_push(j, 0x1111, 1));
   CHECK(combiner_journal_push(j, 0x2222, 2));
   combiner_journal_close(j);
   j = combiner_journal_open_game(PATH, 7, GAME_B);
   CHECK(combiner_journal_push(j, 0x3333, 3));
   combiner_journal_close(j);
   j = combiner_journal_open_game(PATH, 7, GAME_A);
   CHECK(combiner_journal_push(j, 0x4444, 4));
   combiner_journal_close(j);
   CHECK(file_is(PATH, "gliden64-combiner-keys 00000007\n"
            "@" GAME_A "\n" "0000000000001111 1\n" "0000000000002222 2\n"
            "\n@" GAME_B "\n" "0000000000003333 3\n"
            "\n@" GAME_A "\n" "0000000000004444 4\n"));
   memset(&g, 0, sizeof(g));
   CHECK(combiner_journal_read_game(PATH, 7, GAME_A, collect, &g) == 3);
   CHECK(g.n == 3 && g.keys[0] == 0x1111 && g.hwl[0] == 1 && g.keys[1] == 0x2222
         && g.keys[2] == 0x4444 && g.hwl[2] == 4);
   memset(&g, 0, sizeof(g));
   CHECK(combiner_journal_read_game(PATH, 7, GAME_B, collect, &g) == 1 && g.keys[0] == 0x3333);
   memset(&g, 0, sizeof(g));
   CHECK(combiner_journal_read_game(PATH, 8, GAME_A, collect, &g) == 0);
   /* every game's when none is asked */
   memset(&g, 0, sizeof(g));
   CHECK(combiner_journal_read(PATH, 7, collect, &g) == 4);

   /* a game line cut short is no game's; lines before any game's are
    * every game's; a game line of another length is no game's */
   write_file(PATH, "gliden64-combiner-keys 00000007\n"
         "0000000000000001 0\n"
         "@" GAME_A "\n" "0000000000000002 0\n"
         "@0123456789abcde\n" "0000000000000003 0\n"
         "@" GAME_A "0\n" "0000000000000004 0\n"
         "@" GAME_A "\n" "0000000000000005 0\n");
   memset(&g, 0, sizeof(g));
   CHECK(combiner_journal_read_game(PATH, 7, GAME_A, collect, &g) == 3);
   CHECK(g.keys[0] == 1 && g.keys[1] == 2 && g.keys[2] == 5);
   memset(&g, 0, sizeof(g));
   CHECK(combiner_journal_read_game(PATH, 7, GAME_B, collect, &g) == 1 && g.keys[0] == 1);
   /* lines that are no key lines, of this game and another's, and the
    * last cut short: none read */
   write_file(PATH, "gliden64-combiner-keys 00000007\n"
         "@" GAME_A "\n" "000000000000000g 0\n" "0000000000000006 z\n" "0000000000000007  \n"
         "0000000000000008 0\n"
         "@" GAME_B "\n" "000000000000000g 0\n" "0000000000000009 0\n"
         "@" GAME_A "\n" "000000000000000a 0\n" "000000000000000b");
   memset(&g, 0, sizeof(g));
   CHECK(combiner_journal_read_game(PATH, 7, GAME_A, collect, &g) == 2);
   CHECK(g.keys[0] == 8 && g.keys[1] == 0xa);
   memset(&g, 0, sizeof(g));
   CHECK(combiner_journal_read_game(PATH, 7, GAME_B, collect, &g) == 1 && g.keys[0] == 9);
   /* a game's line as it was cut short at its end */
   write_file(PATH, "gliden64-combiner-keys 00000007\n" "@" GAME_A);
   memset(&g, 0, sizeof(g));
   CHECK(combiner_journal_read_game(PATH, 7, GAME_A, collect, &g) == 0);
   filestream_delete(PATH);

   /* the files of before gathered once, in lines of each game */
   path_mkdir(DIR);
   write_file(DIR "/p_" GAME_A ".keys", "gliden64-combiner-keys 00000001\n"
         "000000000000000a 1\n" "cut short" );
   write_file(DIR "/p_" GAME_B ".keys", "gliden64-combiner-keys 00000001\n"
         "000000000000000b 2\n" "not a key line\n" "0000000000000011 z\n" "000000000000001g 0\n"
         "000000000000000c 3\n");
   /* of another version: removed, not read */
   write_file(DIR "/p_1111111122222222.keys", "gliden64-combiner-keys 00000009\n"
         "000000000000000d 0\n");
   /* of names of no game, or of another plugin: left alone */
   write_file(DIR "/p_0123.keys", "x");
   write_file(DIR "/p_0123456789ABCDEF.keys", "x");
   write_file(DIR "/q_" GAME_A ".keys", "x");
   write_file(DIR "/p_" GAME_A ".keys.old", "x");
   CHECK(combiner_journal_gather(SHARED, 7, DIR, "p_", 1));
   CHECK(!filestream_exists(DIR "/p_" GAME_A ".keys") && !filestream_exists(DIR "/p_" GAME_B ".keys")
         && !filestream_exists(DIR "/p_1111111122222222.keys"));
   /* only the whole key lines, after a line of each game */
   {
      int64_t size = 0;
      void *data = NULL;
      CHECK(filestream_read_file(SHARED, &data, &size) && size == 32 + 2 * 18 + 3 * 19);
      free(data);
   }
   CHECK(filestream_exists(DIR "/p_0123.keys") && filestream_exists(DIR "/p_0123456789ABCDEF.keys")
         && filestream_exists(DIR "/q_" GAME_A ".keys") && filestream_exists(DIR "/p_" GAME_A ".keys.old"));
   memset(&g, 0, sizeof(g));
   CHECK(combiner_journal_read_game(SHARED, 7, GAME_A, collect, &g) == 1 && g.keys[0] == 0xa && g.hwl[0] == 1);
   memset(&g, 0, sizeof(g));
   CHECK(combiner_journal_read_game(SHARED, 7, GAME_B, collect, &g) == 2 && g.keys[0] == 0xb
         && g.keys[1] == 0xc && g.hwl[1] == 3);
   memset(&g, 0, sizeof(g));
   CHECK(combiner_journal_read(SHARED, 7, collect, &g) == 3);
   /* there: nothing gathered */
   write_file(DIR "/p_" GAME_A ".keys", "gliden64-combiner-keys 00000001\n" "000000000000000e 0\n");
   CHECK(!combiner_journal_gather(SHARED, 7, DIR, "p_", 1));
   CHECK(filestream_exists(DIR "/p_" GAME_A ".keys"));
   memset(&g, 0, sizeof(g));
   CHECK(combiner_journal_read(SHARED, 7, collect, &g) == 3);
   /* and appended to as any */
   j = combiner_journal_open_game(SHARED, 7, GAME_B);
   CHECK(combiner_journal_push(j, 0xf, 5));
   combiner_journal_close(j);
   memset(&g, 0, sizeof(g));
   CHECK(combiner_journal_read_game(SHARED, 7, GAME_B, collect, &g) == 3 && g.keys[2] == 0xf);
   filestream_delete(SHARED);
   /* only files of another version: none written, they go */
   CHECK(combiner_journal_gather(SHARED, 7, DIR, "p_", 2));
   CHECK(!filestream_exists(SHARED) && !filestream_exists(DIR "/p_" GAME_A ".keys"));
   /* many games, of many keys each */
   {
      char name[64], line[32];
      unsigned n, k;
      int64_t size = 0;
      void *data = NULL;
      for (n = 0; n < 40; n++)
      {
         FILE *f;
         snprintf(name, sizeof(name), DIR "/p_%016x.keys", n + 1);
         if (!(f = fopen(name, "wb")))
            continue;
         fputs("gliden64-combiner-keys 00000001\n", f);
         for (k = 0; k < 50; k++)
         {
            snprintf(line, sizeof(line), "%016x %x\n", n * 1000 + k, k & 15);
            fputs(line, f);
         }
         fclose(f);
      }
      CHECK(combiner_journal_gather(SHARED, 7, DIR, "p_", 1));
      CHECK(filestream_read_file(SHARED, &data, &size) && size == 32 + 40 * (18 + 50 * 19));
      free(data);
      memset(&g, 0, sizeof(g));
      CHECK(combiner_journal_read_game(SHARED, 7, "0000000000000027", collect, &g) == 50
            && g.keys[0] == 38000 && g.keys[49] == 38049 && g.hwl[49] == 1);
      for (n = 0; n < 40; n++)
      {
         snprintf(name, sizeof(name), DIR "/p_%016x.keys", n + 1);
         CHECK(!filestream_exists(name));
      }
      filestream_delete(SHARED);
   }
   /* none: nothing */
   CHECK(!combiner_journal_gather(SHARED, 7, DIR, "p_", 1) && !filestream_exists(SHARED));
   CHECK(!combiner_journal_gather(SHARED, 7, DIR "/none", "p_", 1));
   filestream_delete(DIR "/p_0123.keys");
   filestream_delete(DIR "/p_0123456789ABCDEF.keys");
   filestream_delete(DIR "/q_" GAME_A ".keys");
   filestream_delete(DIR "/p_" GAME_A ".keys.old");
   filestream_delete(DIR);
}

static void append(const char *text)
{
   FILE *f = fopen(PATH, "ab");
   CHECK(f != NULL);
   fputs(text, f);
   fclose(f);
}

int main(void)
{
   combiner_journal_t *j;
   struct seen s;
   unsigned i, refused = 0;

   filestream_delete(PATH);

   /* a session that meets no key makes no journal */
   j = combiner_journal_open(PATH, 5);
   CHECK(j != NULL);
   combiner_journal_close(j);
   CHECK(!filestream_exists(PATH));

   /* all of them, in order, however the writer keeps up */
   j = combiner_journal_open(PATH, 5);
   CHECK(j != NULL);
   for (i = 0; i < KEYS; i++)
      while (!combiner_journal_push(j, key_of(i), i & 15))
         refused++;
   combiner_journal_close(j);
   memset(&s, 0, sizeof(s));
   CHECK(combiner_journal_read(PATH, 5, in_order, &s) == KEYS);
   CHECK(s.next == KEYS && !s.bad);

   /* the same file for another version: nothing */
   memset(&s, 0, sizeof(s));
   CHECK(combiner_journal_read(PATH, 6, in_order, &s) == 0);

   /* a line a crash cut short, then the next session */
   append("0123456789ab");
   j = combiner_journal_open(PATH, 5);
   CHECK(j != NULL);
   CHECK(combiner_journal_push(j, key_of(KEYS), KEYS & 15));
   combiner_journal_close(j);
   memset(&s, 0, sizeof(s));
   CHECK(combiner_journal_read(PATH, 5, in_order, &s) == KEYS + 1);
   CHECK(!s.bad);

   /* nor touches one there */
   {
      void *before = NULL, *after = NULL;
      int64_t n0 = 0, n1 = 0;
      CHECK(filestream_read_file(PATH, &before, &n0));
      j = combiner_journal_open(PATH, 5);
      combiner_journal_close(j);
      CHECK(filestream_read_file(PATH, &after, &n1));
      CHECK(n0 == n1 && n0 > 0 && !memcmp(before, after, (size_t)n0));
      free(before);
      free(after);
   }

   /* garbage lines are skipped */
   append("not a key at all\n0123456789abcdeg 0\n0123456789abcde 7\n");
   memset(&s, 0, sizeof(s));
   CHECK(combiner_journal_read(PATH, 5, in_order, &s) == KEYS + 1);

   filestream_delete(PATH);
   CHECK(!filestream_exists(PATH));
   memset(&s, 0, sizeof(s));
   CHECK(combiner_journal_read(PATH, 5, in_order, &s) == 0);

   /* records appended to a file there: none opened until a key comes */
   filestream_delete(PATH);
   j = combiner_journal_append(PATH);
   CHECK(j != NULL);
   {
      int whole = 0;
      CHECK(combiner_journal_detach(j, &whole) == NULL && whole == 1);
   }
   CHECK(!filestream_exists(PATH));
   /* no file: the keys lost */
   j = combiner_journal_append(PATH);
   CHECK(combiner_journal_push(j, 1, 0));
   {
      int whole = 1;
      CHECK(combiner_journal_detach(j, &whole) == NULL && whole == 0);
   }
   CHECK(!filestream_exists(PATH));
   append("HDR");
   {
      unsigned dropped = 0, n;
      int whole = 0;
      RFILE *f;
      void *data = NULL;
      int64_t size = 0;
      const unsigned char *d;
      j = combiner_journal_append(PATH);
      CHECK(j != NULL);
      /* each pushed again until taken: whole only where none was refused */
      for (i = 0; i < KEYS; i++)
         while (!combiner_journal_push(j, key_of(i), i & 15))
            dropped++;
      f = combiner_journal_detach(j, &whole);
      CHECK(f != NULL);
      CHECK(whole == (dropped == 0));
      /* at its end */
      filestream_write(f, "END", 3);
      filestream_close(f);
      CHECK(filestream_read_file(PATH, &data, &size));
      d = (const unsigned char *)data;
      n = KEYS;
      CHECK(size == 3 + (int64_t)n * COMBINER_JOURNAL_RECORD_SIZE + 3);
      CHECK(memcmp(d, "HDR", 3) == 0 && memcmp(d + size - 3, "END", 3) == 0);
      if (size == 3 + (int64_t)n * COMBINER_JOURNAL_RECORD_SIZE + 3)
      {
         unsigned bad = 0;
         for (i = 0; i < KEYS; i++)
         {
            const unsigned char *r = d + 3 + i * COMBINER_JOURNAL_RECORD_SIZE;
            uint64_t key;
            memcpy(&key, r + 2, 8);
            if (r[0] != COMBINER_JOURNAL_RECORD_KEY || r[1] != (i & 15) || key != key_of(i))
               bad++;
         }
         CHECK(bad == 0);
      }
      free(data);
      /* fewer than the queue holds: none refused, whole */
      j = combiner_journal_append(PATH);
      for (i = 0; i < 10; i++)
         CHECK(combiner_journal_push(j, key_of(i), 0));
      f = combiner_journal_detach(j, &whole);
      CHECK(whole == 1 && f != NULL && filestream_get_size(f) == size + 10 * COMBINER_JOURNAL_RECORD_SIZE);
      filestream_close(f);
#if defined(__linux__)
      /* the file can take no more: no record cut short, the keys lost */
      {
         struct rlimit old, lim;
         int64_t at;
         f = NULL;
         CHECK(filestream_read_file(PATH, &data, &size));
         free(data);
         at = size;
         signal(SIGXFSZ, SIG_IGN);
         getrlimit(RLIMIT_FSIZE, &old);
         lim = old;
         lim.rlim_cur = (rlim_t)at + COMBINER_JOURNAL_RECORD_SIZE + 5;
         setrlimit(RLIMIT_FSIZE, &lim);
         j = combiner_journal_append(PATH);
         for (i = 0; i < 10; i++)
            CHECK(combiner_journal_push(j, key_of(i), 0));
         f = combiner_journal_detach(j, &whole);
         setrlimit(RLIMIT_FSIZE, &old);
         CHECK(f == NULL && whole == 0);
         if (f)
            filestream_close(f);
         /* what it could take, past which nothing was written */
         CHECK(filestream_read_file(PATH, &data, &size));
         CHECK(size <= at + COMBINER_JOURNAL_RECORD_SIZE + 5);
         free(data);
         /* and nothing after, once it is lost */
         j = combiner_journal_append(PATH);
         CHECK(combiner_journal_push(j, key_of(1), 0));
         f = combiner_journal_detach(j, &whole);
         CHECK(f != NULL && whole == 1);
         filestream_close(f);
      }
#endif
      /* a key refused: not whole */
      j = combiner_journal_append(PATH);
      for (i = 0; i < 4 * KEYS && combiner_journal_push(j, key_of(i), 0); i++)
         ;
      f = combiner_journal_detach(j, &whole);
      CHECK(i < 4 * KEYS && whole == 0 && f != NULL);
      filestream_close(f);
      printf("combiner journal: appended %u keys, refused %u times\n", n, dropped);
   }
   {
      int whole = 1;
      CHECK(combiner_journal_detach(NULL, &whole) == NULL && whole == 0);
      CHECK(combiner_journal_append(NULL) == NULL);
   }
   filestream_delete(PATH);
   games();

   printf("combiner journal: %u keys, queue full %u times\n", KEYS, refused);
   return failures ? 1 : 0;
}
