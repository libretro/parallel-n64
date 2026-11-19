/* test_gliden64_combiner_table -- GLideN64's combiner programs, by key.
 *
 * The programs built for each combine key went from a std::map to a hash
 * table in C (CombinerTable.c), sized at init so a new combiner mid-game
 * allocates nothing. Each program put in must be found by its key and no
 * other key, a second of the same key take the first's place, the table
 * hand them back lowest key first (the shader storage saves them in that
 * order), double past half full with all of them still found, and free
 * each once when cleared, keeping its slots, and only what was put after
 * when cleared again.
 *
 *   ./test_gliden64_combiner_table
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gli_combiner.h"

#define N 9000

static int failures;
static unsigned freed[N + 1];

static void check(int ok, const char *what)
{
   if (!ok)
   {
      printf("FAIL: %s\n", what);
      failures++;
   }
}

static void nop(void *p) { (void)p; }
static void nop_update(void *p, bool f) { (void)p; (void)f; }
static bool no_lod(const void *p) { (void)p; return 0; }
static gli_cprog programs[N + 1];
static void count_free(void *p) { freed[(gli_cprog *)p - programs]++; }
static const gli_cprog_ops ops = { nop, nop_update, no_lod, count_free };

static u64 key_of(unsigned i)
{
   /* spread, and some only their high bits apart */
   return (i & 1) ? ((u64)i << 40) | 0x55 : (u64)i * 0x100000001ULL + 7;
}

static int by_key(const void *a, const void *b)
{
   const u64 x = *(const u64 *)a, y = *(const u64 *)b;
   return x < y ? -1 : x > y;
}

int main(void)
{
   static gli_cprog *sorted[N + 1];
   static u64 want[N];
   gli_combiner_table t;
   gli_cprog again;
   unsigned i, found = 0, wrong = 0, order = 1, once = 1;
   u32 n;

   memset(&t, 0, sizeof(t));
   check(gli_combiners_find(&t, 1) == NULL, "nothing made: nothing found");
   check(gli_combiners_make(&t, GLI_COMBINER_SLOTS) && t.count == 0, "made empty");
   check(gli_combiners_find(&t, key_of(3)) == NULL, "empty: nothing found");

   for (i = 0; i < N; i++)
   {
      programs[i].ops = &ops;
      programs[i].key = key_of(i);
      gli_combiners_put(&t, &programs[i]);
   }
   check(t.count == N, "each put counted");
   check(t.mask + 1 >= 2 * N, "doubled past half full");
   for (i = 0; i < N; i++)
   {
      gli_cprog *p = gli_combiners_find(&t, key_of(i));
      found += p != NULL;
      wrong += p != NULL && p != &programs[i];
   }
   check(found == N && wrong == 0, "each found by its key, after doubling too");
   check(gli_combiners_find(&t, key_of(N)) == NULL && gli_combiners_find(&t, 0) == NULL, "no other key found");

   /* a second program of a key in the first's place */
   again.ops = &ops;
   again.key = key_of(77);
   gli_combiners_put(&t, &again);
   check(t.count == N && gli_combiners_find(&t, key_of(77)) == &again, "the same key: in the first's place");
   gli_combiners_put(&t, &programs[77]);

   n = gli_combiners_sorted(&t, sorted);
   for (i = 0; i < N; i++)
      want[i] = key_of(i);
   qsort(want, N, sizeof(want[0]), by_key);
   for (i = 0; i < n && i < N; i++)
      order &= sorted[i]->key == want[i];
   check(n == N && order, "handed back lowest key first");

   gli_combiners_clear(&t);
   for (i = 0; i < N; i++)
      once &= freed[i] == 1;
   check(once, "cleared: each freed once");
   check(t.count == 0 && t.slots != NULL && gli_combiners_find(&t, key_of(5)) == NULL, "cleared: empty, its slots kept");

   /* filled again after: only the new ones handed back and freed */
   for (i = 0; i < 3; i++)
      gli_combiners_put(&t, &programs[10 + i]);
   n = gli_combiners_sorted(&t, sorted);
   check(n == 3 && t.count == 3 && gli_combiners_find(&t, key_of(11)) == &programs[11], "filled again");
   gli_combiners_clear(&t);
   check(freed[10] == 2 && freed[12] == 2 && freed[13] == 1 && freed[9] == 1, "cleared again: the new ones");
   free(t.slots);
   free(t.order);

   printf("test_gliden64_combiner_table: %s\n", failures ? "FAIL" : "ok");
   return failures != 0;
}
