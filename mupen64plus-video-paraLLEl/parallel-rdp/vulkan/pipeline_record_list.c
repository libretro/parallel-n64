/* See pipeline_record_list.h. C89. */
#include <stdlib.h>
#include <string.h>

#include <streams/file_stream.h>
#include <encodings/crc32.h>

#include "pipeline_record_list.h"

static const char list_magic[8] = { 'G', 'R', 'P', 'I', 'P', 'E', 'L', 'S' };

/* magic, version, record size, count, CRC of the records */
typedef struct pipeline_record_list_header
{
   char     magic[8];
   uint32_t version;
   uint32_t record_size;
   uint32_t count;
   uint32_t crc;
} pipeline_record_list_header_t;

/* The fields every use of a record indexes or counts by, in range: a
 * list that passes its checksum but was written by something else is
 * still not trusted further than this. */
static bool record_in_range(const pipeline_record_t *r)
{
   unsigned i;
   const pipeline_record_pass_t *p = &r->pass;

   if (r->kind != PIPELINE_RECORD_COMPUTE && r->kind != PIPELINE_RECORD_GRAPHICS)
      return false;
   if (r->kind == PIPELINE_RECORD_COMPUTE)
      return true;
   if (r->subpass_index >= PIPELINE_RECORD_SUBPASSES || !p->recordable
         || p->num_color > PIPELINE_RECORD_ATTACHMENTS
         || p->num_subpasses > PIPELINE_RECORD_SUBPASSES)
      return false;
   for (i = 0; i < p->num_subpasses; i++)
   {
      const pipeline_record_subpass_t *s = &p->subpasses[i];
      if (s->num_color > PIPELINE_RECORD_ATTACHMENTS
            || s->num_input > PIPELINE_RECORD_ATTACHMENTS
            || s->num_resolve > PIPELINE_RECORD_ATTACHMENTS
            || s->depth_mode > 2)
         return false;
   }
   return true;
}

pipeline_record_t *pipeline_record_list_load(const char *path, size_t *count)
{
   void                          *buf  = NULL;
   int64_t                        len  = 0;
   pipeline_record_list_header_t  h;
   pipeline_record_t             *out;
   size_t                         i;

   *count = 0;
   if (!path || !filestream_read_file(path, &buf, &len) || !buf)
      return NULL;
   if ((uint64_t)len < sizeof(h))
      goto fail;
   memcpy(&h, buf, sizeof(h));
   if (memcmp(h.magic, list_magic, sizeof(list_magic))
         || h.version != PIPELINE_RECORD_LIST_VERSION
         || h.record_size != sizeof(pipeline_record_t)
         || h.count > PIPELINE_RECORD_LIST_MAX
         || (uint64_t)len != sizeof(h) + (uint64_t)h.count * sizeof(pipeline_record_t)
         || encoding_crc32(0, (const uint8_t*)buf + sizeof(h),
            (size_t)h.count * sizeof(pipeline_record_t)) != h.crc)
      goto fail;
   if (!h.count || !(out = (pipeline_record_t*)malloc(h.count * sizeof(*out))))
      goto fail;
   memcpy(out, (const uint8_t*)buf + sizeof(h), h.count * sizeof(*out));
   for (i = 0; i < h.count; i++)
      if (!record_in_range(&out[i]))
      {
         free(out);
         goto fail;
      }
   free(buf);
   *count = h.count;
   return out;

fail:
   free(buf);
   return NULL;
}

bool pipeline_record_list_save(const char *path,
      const pipeline_record_t *records, size_t count)
{
   pipeline_record_list_header_t h;
   uint8_t *buf;
   size_t   size;
   bool     ok;

   if (!path || count > PIPELINE_RECORD_LIST_MAX)
      return false;
   size = sizeof(h) + count * sizeof(*records);
   if (!(buf = (uint8_t*)malloc(size)))
      return false;
   memset(&h, 0, sizeof(h));
   memcpy(h.magic, list_magic, sizeof(list_magic));
   h.version     = PIPELINE_RECORD_LIST_VERSION;
   h.record_size = sizeof(pipeline_record_t);
   h.count       = (uint32_t)count;
   if (count)
      memcpy(buf + sizeof(h), records, count * sizeof(*records));
   h.crc = encoding_crc32(0, buf + sizeof(h), count * sizeof(*records));
   memcpy(buf, &h, sizeof(h));
   ok = filestream_write_file_atomic(path, buf, (int64_t)size);
   free(buf);
   return ok;
}

typedef struct record_order
{
   uint64_t hash;
   uint32_t kind;
   uint32_t index;
} record_order_t;

static int record_order_cmp(const void *a_, const void *b_)
{
   const record_order_t *a = (const record_order_t*)a_;
   const record_order_t *b = (const record_order_t*)b_;
   if (a->hash != b->hash)
      return a->hash < b->hash ? -1 : 1;
   if (a->kind != b->kind)
      return a->kind < b->kind ? -1 : 1;
   return a->index < b->index ? -1 : (a->index > b->index ? 1 : 0);
}

size_t pipeline_record_list_unique(pipeline_record_t *records,
      size_t count, size_t cap)
{
   record_order_t *order;
   uint8_t        *drop;
   size_t          i, n;

   if (count < 2)
      return count < cap ? count : cap;
   order = (record_order_t*)malloc(count * sizeof(*order));
   drop  = (uint8_t*)calloc(count, 1);
   if (!order || !drop)
   {
      free(order);
      free(drop);
      return count < cap ? count : cap;
   }
   for (i = 0; i < count; i++)
   {
      order[i].hash  = records[i].pipeline_hash;
      order[i].kind  = records[i].kind;
      order[i].index = (uint32_t)i;
   }
   /* by pipeline, then by place: the first of a run is the one kept */
   qsort(order, count, sizeof(*order), record_order_cmp);
   for (i = 1; i < count; i++)
      if (order[i].hash == order[i - 1].hash && order[i].kind == order[i - 1].kind)
         drop[order[i].index] = 1;
   for (i = 0, n = 0; i < count && n < cap; i++)
   {
      if (drop[i])
         continue;
      if (n != i)
         records[n] = records[i];
      n++;
   }
   free(order);
   free(drop);
   return n;
}
