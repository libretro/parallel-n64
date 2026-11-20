/* GLideN64's combiner key journal (CombinerKeyJournal.c): keys pushed
 * from one thread all reach the file in order, what a crash cut short is
 * skipped and what comes after it still read, a journal for another
 * version is ignored, and a full queue refuses instead of waiting.
 *
 * Appended to a file of records: one opened only when the first key
 * comes, each key a record in order, the file handed back at its end
 * when the journal stops, and whether every key got there. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <streams/file_stream.h>
#if defined(__linux__)
#include <signal.h>
#include <sys/resource.h>
#endif

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

   printf("combiner journal: %u keys, queue full %u times\n", KEYS, refused);
   return failures ? 1 : 0;
}
