#include <stdlib.h>
#include <string.h>

#include <boolean.h>
#include <file/file_path.h>
#include <retro_atomic.h>
#include <retro_dirent.h>
#include <retro_miscellaneous.h>
#include <rthreads/rthreads.h>
#include <rthreads/retro_eventcount.h>
#include <streams/file_stream.h>

#include "CombinerKeyJournal.h"

#define QUEUE_SIZE 256u /* a power of two */
#define BATCH      32u
#define LINE_SIZE  19u  /* 16 hex digits, a space, a hex digit, a newline */
#define HEADER     "gliden64-combiner-keys "
#define HEADER_LEN (sizeof(HEADER) - 1)
#define GAME_LINE  (1u + COMBINER_JOURNAL_GAME + 1u) /* @, the game, a newline */

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
   /* or lines, at @lines_path, made for @version where it is not there,
    * opened the same way */
   char                *lines_path;
   uint32_t             version;
   /* the game the lines are of, or none */
   char                 game[GAME_LINE];
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

/* The journal of lines: one there continued past a line a crash may have
 * cut short, or one made with its header */
static void open_lines(combiner_journal_t *j)
{
   char line[HEADER_LEN + 9];

   if (filestream_exists(j->lines_path))
   {
      if ((j->file = filestream_open(j->lines_path,
            RETRO_VFS_FILE_ACCESS_READ_WRITE | RETRO_VFS_FILE_ACCESS_UPDATE_EXISTING,
            RETRO_VFS_FILE_ACCESS_HINT_NONE)))
      {
         filestream_seek(j->file, 0, RETRO_VFS_SEEK_POSITION_END);
         filestream_write(j->file, "\n", 1);
      }
   }
   else if ((j->file = filestream_open(j->lines_path, RETRO_VFS_FILE_ACCESS_WRITE,
         RETRO_VFS_FILE_ACCESS_HINT_NONE)))
   {
      memcpy(line, HEADER, HEADER_LEN);
      write_hex(line + HEADER_LEN, j->version, 8);
      line[HEADER_LEN + 8] = '\n';
      filestream_write(j->file, line, (int64_t)sizeof(line));
   }
   /* this session's lines of its game */
   if (j->file && j->game[0])
      filestream_write(j->file, j->game, (int64_t)GAME_LINE);
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
      else if (j->lines_path && !j->file && !j->lost)
         open_lines(j);
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
      else if (j->path || j->lines_path)
         j->lost = 1;
      retro_atomic_store_release_size(&j->tail, tail);
   }
}

/* A journal whose file opens on its thread when the first key comes: of
 * records at @path, or of lines made for @version at @lines_path */
static combiner_journal_t *journal_new(const char *path, const char *lines_path, uint32_t version,
      const char *game)
{
   combiner_journal_t *j;
   const char *at = path ? path : lines_path;
   char *copy;
   size_t len;

   if (!at || !(j = (combiner_journal_t*)calloc(1, sizeof(*j))))
      return NULL;
   retro_atomic_size_init(&j->head, 0);
   retro_atomic_size_init(&j->tail, 0);
   retro_atomic_int_init(&j->stop, 0);
   len = strlen(at) + 1;
   if ((copy = (char*)malloc(len)))
   {
      memcpy(copy, at, len);
      if (path)
         j->path = copy;
      else
         j->lines_path = copy;
      j->version = version;
      if (game)
      {
         j->game[0] = '@';
         memcpy(j->game + 1, game, COMBINER_JOURNAL_GAME);
         j->game[GAME_LINE - 1] = '\n';
      }
      if (retro_eventcount_init(&j->work))
      {
         if ((j->thread = sthread_create(journal_thread, j)))
            return j;
         retro_eventcount_free(&j->work);
      }
      free(copy);
   }
   free(j);
   return NULL;
}

combiner_journal_t *combiner_journal_open(const char *path, uint32_t version)
{
   return journal_new(NULL, path, version, NULL);
}

combiner_journal_t *combiner_journal_open_game(const char *path, uint32_t version,
      const char *game)
{
   return journal_new(NULL, path, version, game);
}

combiner_journal_t *combiner_journal_append(const char *path)
{
   return journal_new(path, NULL, 0, NULL);
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
   free(j->lines_path);
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

combiner_journal_t *combiner_journal_open_game(const char *path, uint32_t version,
      const char *game)
{
   (void)path;
   (void)version;
   (void)game;
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

/* each byte's hex digit, 16 where it is none */
static const unsigned char hex_digit[256] = {
#define N16 16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16
   N16, N16, N16,
   0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 16, 16, 16, 16, 16, 16,
   N16, N16,
   16, 10, 11, 12, 13, 14, 15, 16, 16, 16, 16, 16, 16, 16, 16, 16,
   N16, N16, N16, N16, N16, N16, N16, N16, N16
#undef N16
};

static int read_hex(const char *in, unsigned digits, uint64_t *value)
{
   uint64_t v = 0;
   unsigned i, bad = 0;

   for (i = 0; i < digits; i++)
   {
      const unsigned d = hex_digit[(unsigned char)in[i]];
      bad |= d;
      v    = v << 4 | (d & 15);
   }
   if (bad & 16)
      return 0;
   *value = v;
   return 1;
}

/* The lines of a journal made for @version, past its header, or NULL */
static const char *journal_lines(const char *data, int64_t size, uint32_t version)
{
   uint64_t file_version;
   if (      data
         && (size_t)size >= HEADER_LEN + 9
         && !memcmp(data, HEADER, HEADER_LEN)
         && read_hex(data + HEADER_LEN, 8, &file_version)
         && file_version == version
         && data[HEADER_LEN + 8] == '\n')
      return data + HEADER_LEN + 9;
   return NULL;
}

/* a whole key line */
static int key_line(const char *p, size_t len, uint64_t *key, uint64_t *flags)
{
   return len == LINE_SIZE - 1
      && read_hex(p, 16, key)
      && p[16] == ' '
      && read_hex(p + 17, 1, flags);
}

unsigned combiner_journal_read_game(const char *path, uint32_t version, const char *game,
      combiner_journal_key_t cb, void *ctx)
{
   void *data = NULL;
   int64_t size = 0;
   unsigned count = 0;
   const char *p, *end;
   /* the lines before any game's are of every game */
   int mine = 1;

   if (!filestream_read_file(path, &data, &size) || !data)
      return 0;
   end = (const char*)data + size;
   if ((p = journal_lines((const char*)data, size, version)))
   {
      while (p < end)
      {
         const char *nl;
         uint64_t key;
         uint64_t flags;

         if (p + LINE_SIZE <= end && p[16] == ' ' && p[LINE_SIZE - 1] == '\n')
         {
            /* a key line: only a key line has its space there and its
             * newline after its flags; another game's passed over */
            if (!mine)
            {
               p += LINE_SIZE;
               continue;
            }
            if (read_hex(p, 16, &key) && read_hex(p + 17, 1, &flags))
            {
               cb(ctx, key, (unsigned)flags);
               count++;
               p += LINE_SIZE;
               continue;
            }
         }
         if (!(nl = (const char*)memchr(p, '\n', (size_t)(end - p))))
            break; /* the last line, cut short */
         /* a game's lines (one cut short is no game's), or a line that is
          * none, as a key line cut short */
         if (*p == '@')
            mine = !game || ((size_t)(nl - p) == GAME_LINE - 1
                  && !memcmp(p + 1, game, COMBINER_JOURNAL_GAME));
         p = nl + 1;
      }
   }
   free(data);
   return count;
}

unsigned combiner_journal_read(const char *path, uint32_t version,
      combiner_journal_key_t cb, void *ctx)
{
   return combiner_journal_read_game(path, version, NULL, cb, ctx);
}

/* a game's file of before: @prefix, its CRCs in hex, .keys */
static int old_name(const char *name, const char *prefix)
{
   const size_t p = strlen(prefix);
   size_t i;
   if (strlen(name) != p + COMBINER_JOURNAL_GAME + 5 || strncmp(name, prefix, p)
         || strcmp(name + p + COMBINER_JOURNAL_GAME, ".keys"))
      return 0;
   for (i = p; i < p + COMBINER_JOURNAL_GAME; i++)
      if (!((name[i] >= '0' && name[i] <= '9') || (name[i] >= 'a' && name[i] <= 'f')))
         return 0;
   return 1;
}

int combiner_journal_gather(const char *path, uint32_t version, const char *folder,
      const char *prefix, uint32_t old_version)
{
   struct RDIR *d;
   char   *out  = NULL;
   size_t  used = 0, room = 0;
   char   *names = NULL; /* the files read, each ended by a NUL */
   size_t  names_used = 0, names_room = 0;
   char    file[PATH_MAX_LENGTH];
   int     ok = 1;

   if (filestream_exists(path) || !(d = retro_opendir(folder)))
      return 0;
   while (retro_readdir(d))
   {
      const char *name = retro_dirent_get_name(d);
      void *data = NULL;
      int64_t size = 0;
      const char *p, *end;
      size_t len;

      if (!name || !old_name(name, prefix))
         continue;
      len = strlen(name) + 1;
      if (names_used + len > names_room)
      {
         char *more = (char*)realloc(names, names_room * 2 + len + 256);
         if (!more)
         {
            ok = 0;
            break;
         }
         names = more;
         names_room = names_room * 2 + len + 256;
      }
      memcpy(names + names_used, name, len);
      names_used += len;

      fill_pathname_join(file, folder, name, sizeof(file));
      if (!filestream_read_file(file, &data, &size) || !data)
         continue;
      /* its lines after a line of its game */
      if ((p = journal_lines((const char*)data, size, old_version)))
      {
         end = (const char*)data + size;
         if (used + GAME_LINE + (size_t)(end - p) > room)
         {
            const size_t want = (used + GAME_LINE + (size_t)(end - p)) * 2 + HEADER_LEN + 9;
            char *more = (char*)realloc(out, want);
            if (!more)
            {
               free(data);
               ok = 0;
               break;
            }
            out = more;
            room = want;
         }
         if (!used)
         {
            memcpy(out, HEADER, HEADER_LEN);
            write_hex(out + HEADER_LEN, version, 8);
            out[HEADER_LEN + 8] = '\n';
            used = HEADER_LEN + 9;
         }
         out[used] = '@';
         memcpy(out + used + 1, name + strlen(prefix), COMBINER_JOURNAL_GAME);
         out[used + GAME_LINE - 1] = '\n';
         used += GAME_LINE;
         while (p < end)
         {
            const char *nl = (const char*)memchr(p, '\n', (size_t)(end - p));
            uint64_t key, flags;
            if (!nl)
               break;
            if (key_line(p, (size_t)(nl - p), &key, &flags))
            {
               memcpy(out + used, p, LINE_SIZE);
               used += LINE_SIZE;
            }
            p = nl + 1;
         }
      }
      free(data);
   }
   retro_closedir(d);

   if (ok && names_used)
   {
      /* written whole before any goes */
      RFILE *f = NULL;
      size_t at;
      if (used)
      {
         ok = (f = filestream_open(path, RETRO_VFS_FILE_ACCESS_WRITE,
                  RETRO_VFS_FILE_ACCESS_HINT_NONE)) != NULL
            && filestream_write(f, out, (int64_t)used) == (int64_t)used;
         if (f)
            ok = filestream_close(f) == 0 && ok;
      }
      for (at = 0; ok && at < names_used; at += strlen(names + at) + 1)
      {
         fill_pathname_join(file, folder, names + at, sizeof(file));
         filestream_delete(file);
      }
   }
   free(out);
   free(names);
   return names_used != 0;
}
