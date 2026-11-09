#include <stdlib.h>
#include <string.h>

#include <boolean.h>
#include <retro_atomic.h>
#include <rthreads/rthreads.h>
#include <rthreads/retro_eventcount.h>
#include <streams/file_stream.h>

#include "CombinerKeyJournal.h"

#define QUEUE_SIZE 256u /* a power of two */
#define BATCH      32u
#define LINE_SIZE  19u  /* 16 hex digits, a space, a hex digit, a newline */
#define HEADER     "gliden64-combiner-keys "
#define HEADER_LEN (sizeof(HEADER) - 1)

#if defined(RETRO_ATOMIC_LOCK_FREE)

struct journal_entry
{
   uint64_t key;
   unsigned hwl;
};

struct combiner_journal
{
   RFILE               *file;
   sthread_t           *thread;
   retro_eventcount_t   work;
   retro_atomic_size_t  head; /* the GL thread's */
   retro_atomic_size_t  tail; /* the journal thread's */
   retro_atomic_int_t   stop;
   struct journal_entry queue[QUEUE_SIZE];
};

static void write_hex(char *out, uint64_t value, unsigned digits)
{
   static const char hex[] = "0123456789abcdef";
   while (digits--)
   {
      out[digits] = hex[value & 15];
      value     >>= 4;
   }
}

static size_t format_line(char *line, const struct journal_entry *entry)
{
   write_hex(line, entry->key, 16);
   line[16] = ' ';
   write_hex(line + 17, entry->hwl, 1);
   line[18] = '\n';
   return LINE_SIZE;
}

static void journal_thread(void *data)
{
   combiner_journal_t *j = (combiner_journal_t*)data;

   for (;;)
   {
      char   lines[BATCH * LINE_SIZE];
      size_t head = retro_atomic_load_acquire_size(&j->head);
      size_t tail = retro_atomic_load_acquire_size(&j->tail);
      size_t used = 0;

      if (head == tail)
      {
         int key;

         if (retro_atomic_load_acquire_int(&j->stop))
         {
            /* stop is raised after the last push */
            if (head == retro_atomic_load_acquire_size(&j->head))
               break;
            continue;
         }
         key = retro_eventcount_prepare_wait(&j->work);
         if (      head != retro_atomic_load_acquire_size(&j->head)
               || retro_atomic_load_acquire_int(&j->stop))
            retro_eventcount_cancel_wait(&j->work);
         else
            retro_eventcount_commit_wait(&j->work, key);
         continue;
      }

      while (tail != head && used < sizeof(lines))
      {
         used += format_line(lines + used, &j->queue[tail & (QUEUE_SIZE - 1)]);
         tail++;
      }
      /* what reached the OS survives the process */
      filestream_write(j->file, lines, (int64_t)used);
      filestream_flush(j->file);
      retro_atomic_store_release_size(&j->tail, tail);
   }
}

combiner_journal_t *combiner_journal_open(const char *path, uint32_t version)
{
   char line[HEADER_LEN + 9];
   combiner_journal_t *j = (combiner_journal_t*)calloc(1, sizeof(*j));

   if (!j)
      return NULL;
   retro_atomic_size_init(&j->head, 0);
   retro_atomic_size_init(&j->tail, 0);
   retro_atomic_int_init(&j->stop, 0);

   if (filestream_exists(path))
   {
      j->file = filestream_open(path,
            RETRO_VFS_FILE_ACCESS_READ_WRITE | RETRO_VFS_FILE_ACCESS_UPDATE_EXISTING,
            RETRO_VFS_FILE_ACCESS_HINT_NONE);
      if (j->file)
      {
         /* ends a line a crash may have cut short */
         filestream_seek(j->file, 0, RETRO_VFS_SEEK_POSITION_END);
         filestream_write(j->file, "\n", 1);
      }
   }
   else
   {
      j->file = filestream_open(path, RETRO_VFS_FILE_ACCESS_WRITE,
            RETRO_VFS_FILE_ACCESS_HINT_NONE);
      if (j->file)
      {
         memcpy(line, HEADER, HEADER_LEN);
         write_hex(line + HEADER_LEN, version, 8);
         line[HEADER_LEN + 8] = '\n';
         filestream_write(j->file, line, (int64_t)sizeof(line));
      }
   }
   if (j->file && filestream_flush(j->file) == 0 && retro_eventcount_init(&j->work))
   {
      if ((j->thread = sthread_create(journal_thread, j)))
         return j;
      retro_eventcount_free(&j->work);
   }
   if (j->file)
      filestream_close(j->file);
   free(j);
   return NULL;
}

int combiner_journal_push(combiner_journal_t *j, uint64_t key, unsigned hwl)
{
   size_t head;

   if (!j)
      return 0;
   head = retro_atomic_load_acquire_size(&j->head);
   if (head - retro_atomic_load_acquire_size(&j->tail) >= QUEUE_SIZE)
      return 0;
   j->queue[head & (QUEUE_SIZE - 1)].key = key;
   j->queue[head & (QUEUE_SIZE - 1)].hwl = hwl;
   retro_atomic_store_release_size(&j->head, head + 1);
   retro_eventcount_notify(&j->work);
   return 1;
}

void combiner_journal_close(combiner_journal_t *j)
{
   if (!j)
      return;
   retro_atomic_store_release_int(&j->stop, 1);
   retro_eventcount_notify(&j->work);
   sthread_join(j->thread);
   retro_eventcount_free(&j->work);
   filestream_close(j->file);
   free(j);
}

#else

combiner_journal_t *combiner_journal_open(const char *path, uint32_t version)
{
   (void)path;
   (void)version;
   return NULL;
}

int combiner_journal_push(combiner_journal_t *j, uint64_t key, unsigned hwl)
{
   (void)j;
   (void)key;
   (void)hwl;
   return 0;
}

void combiner_journal_close(combiner_journal_t *j)
{
   (void)j;
}

#endif

static int read_hex(const char *in, unsigned digits, uint64_t *value)
{
   uint64_t v = 0;
   unsigned i;

   for (i = 0; i < digits; i++)
   {
      char c = in[i];
      v <<= 4;
      if (c >= '0' && c <= '9')
         v |= (uint64_t)(c - '0');
      else if (c >= 'a' && c <= 'f')
         v |= (uint64_t)(c - 'a' + 10);
      else
         return 0;
   }
   *value = v;
   return 1;
}

unsigned combiner_journal_read(const char *path, uint32_t version,
      combiner_journal_key_t cb, void *ctx)
{
   void *data = NULL;
   int64_t size = 0;
   unsigned count = 0;
   const char *p, *end;
   uint64_t file_version;

   if (!filestream_exists(path) || !filestream_read_file(path, &data, &size) || !data)
      return 0;
   p   = (const char*)data;
   end = p + size;
   if (      (size_t)size >= HEADER_LEN + 9
         && !memcmp(p, HEADER, HEADER_LEN)
         && read_hex(p + HEADER_LEN, 8, &file_version)
         && file_version == version
         && p[HEADER_LEN + 8] == '\n')
   {
      p += HEADER_LEN + 9;
      while (p < end)
      {
         const char *nl = (const char*)memchr(p, '\n', (size_t)(end - p));
         uint64_t key;
         uint64_t flags;

         if (!nl)
            break; /* the last line, cut short */
         if (      (size_t)(nl - p) == LINE_SIZE - 1
               && read_hex(p, 16, &key)
               && p[16] == ' '
               && read_hex(p + 17, 1, &flags))
         {
            cb(ctx, key, (unsigned)flags);
            count++;
         }
         p = nl + 1;
      }
   }
   free(data);
   return count;
}

void combiner_journal_remove(const char *path)
{
   filestream_delete(path);
}
