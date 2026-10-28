/* The pipeline list paraLLEl-RDP keeps across sessions
 * (parallel-rdp/vulkan/pipeline_record_list.c): what is saved loads back
 * the same, and a list that is damaged, cut short, of another version or
 * layout, or holds a record out of range loads as no list at all. The
 * merge keeps the first record of each pipeline, in order, up to a cap.
 * C89. */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pipeline_record_list.h"

#define LIST_PATH "test_pipeline_records.tmp"
#define HEADER_SIZE 24 /* magic, version, record size, count, CRC */

static int failures;

#define CHECK(cond) do { if (!(cond)) { \
   fprintf(stderr, "%s:%d: FAIL: %s\n", __FILE__, __LINE__, #cond); \
   failures++; } } while (0)

static void fill(pipeline_record_t *r, unsigned i, unsigned kind)
{
   unsigned j;
   memset(r, 0, sizeof(*r));
   r->kind          = kind;
   r->pipeline_hash = 0x1000u + i;
   r->program_hash  = 0x2000u + i;
   for (j = 0; j < PIPELINE_RECORD_SPEC_CONSTANTS; j++)
      r->spec_constants[j] = i * 31u + j;
   if (kind == PIPELINE_RECORD_GRAPHICS)
   {
      r->pass.recordable    = 1;
      r->pass.num_color     = 1;
      r->pass.formats[0]    = 37; /* VK_FORMAT_R8G8B8A8_UNORM */
      r->pass.samples[0]    = 1;
      r->pass.num_subpasses = 1;
      r->pass.subpasses[0].num_color = 1;
   }
}

static long file_size(const char *path)
{
   long n;
   FILE *f = fopen(path, "rb");
   if (!f)
      return -1;
   fseek(f, 0, SEEK_END);
   n = ftell(f);
   fclose(f);
   return n;
}

static unsigned char *read_all(const char *path, long *n)
{
   unsigned char *buf;
   FILE *f = fopen(path, "rb");
   *n = -1;
   if (!f)
      return NULL;
   fseek(f, 0, SEEK_END);
   *n = ftell(f);
   fseek(f, 0, SEEK_SET);
   buf = (unsigned char*)malloc((size_t)*n + 1);
   if (buf && fread(buf, 1, (size_t)*n, f) != (size_t)*n)
   {
      free(buf);
      buf = NULL;
   }
   fclose(f);
   return buf;
}

static void write_all(const char *path, const unsigned char *buf, long n)
{
   FILE *f = fopen(path, "wb");
   if (!f)
      return;
   fwrite(buf, 1, (size_t)n, f);
   fclose(f);
}

/* (the list's own CRC, so a record can be put out of range unnoticed) */
static unsigned long crc32_of(const unsigned char *p, size_t n)
{
   unsigned long crc = 0xffffffffUL;
   size_t i;
   int k;
   for (i = 0; i < n; i++)
   {
      crc ^= p[i];
      for (k = 0; k < 8; k++)
         crc = (crc >> 1) ^ (0xedb88320UL & (0UL - (crc & 1)));
   }
   return crc ^ 0xffffffffUL;
}

static void put32(unsigned char *p, unsigned long v)
{
   /* host order, as the list is written */
   uint32_t w = (uint32_t)v;
   memcpy(p, &w, 4);
}

static int loads(void)
{
   size_t count = 99;
   pipeline_record_t *r = pipeline_record_list_load(LIST_PATH, &count);
   int ok = r != NULL;
   if (!r)
      CHECK(count == 0);
   free(r);
   return ok;
}

/* Saves @records, applies @damage to the file, and says whether it loads. */
static int loads_damaged(const pipeline_record_t *records, size_t count,
      void (*damage)(unsigned char *buf, long *n))
{
   unsigned char *buf;
   long n;
   CHECK(pipeline_record_list_save(LIST_PATH, records, count));
   buf = read_all(LIST_PATH, &n);
   CHECK(buf != NULL);
   if (!buf)
      return 0;
   damage(buf, &n);
   write_all(LIST_PATH, buf, n);
   free(buf);
   return loads();
}

static void flip_record_byte(unsigned char *b, long *n) { b[*n - 5] ^= 0x40; }
static void cut_short(unsigned char *b, long *n) { (void)b; *n -= 1; }
static void cut_header(unsigned char *b, long *n) { (void)b; *n = HEADER_SIZE - 1; }
static void add_byte(unsigned char *b, long *n) { b[*n] = 0; *n += 1; }
static void bad_magic(unsigned char *b, long *n) { (void)n; b[0] = 'X'; }
static void bad_version(unsigned char *b, long *n) { (void)n; put32(b + 8, PIPELINE_RECORD_LIST_VERSION + 1); }
static void bad_record_size(unsigned char *b, long *n) { (void)n; put32(b + 12, sizeof(pipeline_record_t) - 8); }
static void bad_count(unsigned char *b, long *n) { (void)n; put32(b + 16, PIPELINE_RECORD_LIST_MAX + 1); }
static void bad_crc(unsigned char *b, long *n) { (void)n; b[20] ^= 1; }

/* a record changed and the CRC made right again: only the range checks
 * stand between it and the device */
static size_t resealed_field;
static unsigned long resealed_value;
static void reseal(unsigned char *b, long *n)
{
   put32(b + HEADER_SIZE + resealed_field, resealed_value);
   put32(b + 20, crc32_of(b + HEADER_SIZE, (size_t)(*n - HEADER_SIZE)));
}

static int loads_resealed(const pipeline_record_t *records, size_t count,
      size_t field, unsigned long value)
{
   resealed_field = field;
   resealed_value = value;
   return loads_damaged(records, count, reseal);
}

#define FIELD(f) offsetof(pipeline_record_t, f)

int main(void)
{
   pipeline_record_t records[8], merged[16], *loaded;
   size_t count, i, n;

   for (i = 0; i < 8; i++)
      fill(&records[i], (unsigned)i, (i & 1) ? PIPELINE_RECORD_GRAPHICS : PIPELINE_RECORD_COMPUTE);

   /* round trip */
   CHECK(pipeline_record_list_save(LIST_PATH, records, 8));
   CHECK(file_size(LIST_PATH) == (long)(HEADER_SIZE + 8 * sizeof(pipeline_record_t)));
   loaded = pipeline_record_list_load(LIST_PATH, &count);
   CHECK(loaded && count == 8);
   if (loaded)
      CHECK(!memcmp(loaded, records, sizeof(records)));
   free(loaded);

   /* no file, an empty list, too many to save */
   remove(LIST_PATH);
   CHECK(!loads());
   CHECK(!pipeline_record_list_load(NULL, &count) && count == 0);
   CHECK(pipeline_record_list_save(LIST_PATH, records, 0));
   CHECK(!loads());
   CHECK(!pipeline_record_list_save(LIST_PATH, records, PIPELINE_RECORD_LIST_MAX + 1));

   /* damaged */
   CHECK(!loads_damaged(records, 8, flip_record_byte));
   CHECK(!loads_damaged(records, 8, cut_short));
   CHECK(!loads_damaged(records, 8, cut_header));
   CHECK(!loads_damaged(records, 8, add_byte));
   CHECK(!loads_damaged(records, 8, bad_magic));
   CHECK(!loads_damaged(records, 8, bad_version));
   CHECK(!loads_damaged(records, 8, bad_record_size));
   CHECK(!loads_damaged(records, 8, bad_count));
   CHECK(!loads_damaged(records, 8, bad_crc));

   /* resealed: in range loads, out of range does not (records[1] is the
    * first graphics one) */
   CHECK(loads_resealed(records, 8, FIELD(spec_constants[3]), 12345));
   CHECK(!loads_resealed(records, 8, FIELD(kind), 2));
   CHECK(!loads_resealed(records, 8, sizeof(pipeline_record_t) + FIELD(subpass_index), PIPELINE_RECORD_SUBPASSES));
   CHECK(!loads_resealed(records, 8, sizeof(pipeline_record_t) + FIELD(pass.recordable), 0));
   CHECK(!loads_resealed(records, 8, sizeof(pipeline_record_t) + FIELD(pass.num_color), PIPELINE_RECORD_ATTACHMENTS + 1));
   CHECK(!loads_resealed(records, 8, sizeof(pipeline_record_t) + FIELD(pass.num_subpasses), PIPELINE_RECORD_SUBPASSES + 1));
   CHECK(!loads_resealed(records, 8, sizeof(pipeline_record_t) + FIELD(pass.subpasses[0].num_input), PIPELINE_RECORD_ATTACHMENTS + 1));
   CHECK(!loads_resealed(records, 8, sizeof(pipeline_record_t) + FIELD(pass.subpasses[0].depth_mode), 3));
   /* (a compute record's pass is never looked at) */
   CHECK(loads_resealed(records, 8, FIELD(pass.num_color), 1000));

   /* merge: this session's records first, then the loaded ones */
   memcpy(merged, records + 4, 4 * sizeof(*merged));   /* 4..7 */
   memcpy(merged + 4, records, 8 * sizeof(*merged));   /* 0..7 */
   merged[12] = records[2];
   merged[12].kind = PIPELINE_RECORD_GRAPHICS;         /* same hash, other kind */
   merged[12].pass = records[1].pass;
   n = pipeline_record_list_unique(merged, 13, 64);
   CHECK(n == 9);
   if (n == 9)
   {
      static const unsigned order[8] = { 4, 5, 6, 7, 0, 1, 2, 3 };
      for (i = 0; i < 8; i++)
         CHECK(merged[i].pipeline_hash == records[order[i]].pipeline_hash
               && merged[i].kind == records[order[i]].kind);
      CHECK(merged[8].pipeline_hash == records[2].pipeline_hash
            && merged[8].kind == PIPELINE_RECORD_GRAPHICS);
   }
   /* the cap drops the oldest, from the end */
   memcpy(merged, records, 8 * sizeof(*merged));
   memcpy(merged + 8, records, 8 * sizeof(*merged));
   n = pipeline_record_list_unique(merged, 16, 5);
   CHECK(n == 5);
   for (i = 0; i < n; i++)
      CHECK(merged[i].pipeline_hash == records[i].pipeline_hash);
   CHECK(pipeline_record_list_unique(merged, 1, 0) == 0);
   CHECK(pipeline_record_list_unique(merged, 0, 4) == 0);

   remove(LIST_PATH);
   if (failures)
   {
      fprintf(stderr, "test_pipeline_records: %d failed\n", failures);
      return 1;
   }
   printf("test_pipeline_records: ok\n");
   return 0;
}
