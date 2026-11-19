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
   /* key records appended to the existing file at @path, opened on the
    * journal's thread when the first key comes */
   char                *path;
   sthread_t           *thread;
   retro_eventcount_t   work;
   retro_atomic_size_t  head; /* the GL thread's */
   retro_atomic_size_t  tail; /* the journal thread's */
   retro_atomic_int_t   stop;
   int                  dropped; /* the GL thread's: a key not queued */
   int                  lost;    /* the journal thread's: keys not written */
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

/* a key record of a file of records */
static size_t format_record(char *out, const struct journal_entry *entry)
{
   out[0] = COMBINER_JOURNAL_RECORD_KEY;
   out[1] = (char)(entry->hwl & 15);
   memcpy(out + 2, &entry->key, sizeof(entry->key));
   return COMBINER_JOURNAL_RECORD_SIZE;
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

      while (tail != head && used + LINE_SIZE <= sizeof(lines))
      {
         const struct journal_entry *entry = &j->queue[tail & (QUEUE_SIZE - 1)];
         used += j->path ? format_record(lines + used, entry) : format_line(lines + used, entry);
         tail++;
      }
      if (j->path && !j->file && !j->lost
            && (j->file = filestream_open(j->path,
                  RETRO_VFS_FILE_ACCESS_READ_WRITE | RETRO_VFS_FILE_ACCESS_UPDATE_EXISTING,
                  RETRO_VFS_FILE_ACCESS_HINT_NONE)))
         filestream_seek(j->file, 0, RETRO_VFS_SEEK_POSITION_END);
      /* what reached the OS survives the process */
      if (j->file)
      {
         const int written = filestream_write(j->file, lines, (int64_t)used) == (int64_t)used;
         const int flushed = filestream_flush(j->file) == 0;
         if ((!written || !flushed) && j->path)
         {
            /* nothing written after a record cut short */
            filestream_close(j->file);
            j->file = NULL;
            j->lost = 1;
         }
      }
      else if (j->path)
         j->lost = 1;
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

combiner_journal_t *combiner_journal_append(const char *path)
{
   combiner_journal_t *j;
   size_t len;

   if (!path || !(j = (combiner_journal_t*)calloc(1, sizeof(*j))))
      return NULL;
   retro_atomic_size_init(&j->head, 0);
   retro_atomic_size_init(&j->tail, 0);
   retro_atomic_int_init(&j->stop, 0);
   len = strlen(path) + 1;
   if ((j->path = (char*)malloc(len)))
   {
      memcpy(j->path, path, len);
      if (retro_eventcount_init(&j->work))
      {
         if ((j->thread = sthread_create(journal_thread, j)))
            return j;
         retro_eventcount_free(&j->work);
      }
      free(j->path);
   }
   free(j);
   return NULL;
}

/* stops the thread, the queue written */
static void journal_stop(combiner_journal_t *j)
{
   retro_atomic_store_release_int(&j->stop, 1);
   retro_eventcount_notify(&j->work);
   sthread_join(j->thread);
   retro_eventcount_free(&j->work);
}

RFILE *combiner_journal_detach(combiner_journal_t *j, int *whole)
{
   RFILE *file;
   *whole = 0;
   if (!j)
      return NULL;
   journal_stop(j);
   file = j->file;
   *whole = !j->dropped && !j->lost;
   free(j->path);
   free(j);
   return file;
}

int combiner_journal_push(combiner_journal_t *j, uint64_t key, unsigned hwl)
{
   size_t head;

   if (!j)
      return 0;
   head = retro_atomic_load_acquire_size(&j->head);
   if (head - retro_atomic_load_acquire_size(&j->tail) >= QUEUE_SIZE)
   {
      j->dropped = 1;
      return 0;
   }
   j->queue[head & (QUEUE_SIZE - 1)].key = key;
   j->queue[head & (QUEUE_SIZE - 1)].hwl = hwl;
   retro_atomic_store_release_size(&j->head, head + 1);
   retro_eventcount_notify(&j->work);
   return 1;
}

void combiner_journal_close(combiner_journal_t *j)
{
   int whole;
   RFILE *file = combiner_journal_detach(j, &whole);
   if (file)
      filestream_close(file);
}

#else

combiner_journal_t *combiner_journal_open(const char *path, uint32_t version)
{
   (void)path;
   (void)version;
   return NULL;
}

combiner_journal_t *combiner_journal_append(const char *path)
{
   (void)path;
   return NULL;
}

RFILE *combiner_journal_detach(combiner_journal_t *j, int *whole)
{
   (void)j;
   *whole = 0;
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
