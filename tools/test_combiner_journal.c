/* GLideN64's combiner key journal (CombinerKeyJournal.c): keys pushed
 * from one thread all reach the file in order, what a crash cut short is
 * skipped and what comes after it still read, a journal for another
 * version is ignored, and a full queue refuses instead of waiting. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <streams/file_stream.h>

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

   combiner_journal_remove(PATH);

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

   /* garbage lines are skipped */
   append("not a key at all\n0123456789abcdeg 0\n0123456789abcde 7\n");
   memset(&s, 0, sizeof(s));
   CHECK(combiner_journal_read(PATH, 5, in_order, &s) == KEYS + 1);

   combiner_journal_remove(PATH);
   CHECK(!filestream_exists(PATH));
   memset(&s, 0, sizeof(s));
   CHECK(combiner_journal_read(PATH, 5, in_order, &s) == 0);

   printf("combiner journal: %u keys, queue full %u times\n", KEYS, refused);
   return failures ? 1 : 0;
}
