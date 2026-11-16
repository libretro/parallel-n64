/*
 * Texture Filtering
 *
 * See TxGz.h.  Gzip stream I/O over libretro-common rdeflate / rinflate.
 *
 * this is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 */

#include <stdlib.h>
#include <string.h>

#include <encodings/deflate.h>
#include <streams/file_stream.h>

#include "TxGz.h"

/* One raw block per file read/write.  The cache streams tens of megabytes
 * through here in small records - the header fields are 1 to 8 bytes each -
 * so the buffering is what keeps this off the syscall path: zlib's gzread
 * did the same thing internally, and going unbuffered here would turn every
 * 4-byte field into its own read(). */
#define TXGZ_CHUNK (64 * 1024)

/* gzip container, per RFC 1952. */
#define TXGZ_WBITS_WRITE  (16 + 15)
/* 32 + 15: detect gzip or zlib from the stream. Older caches are gzip; the
 * autodetect costs nothing and makes a zlib-wrapped file readable too. */
#define TXGZ_WBITS_READ   (32 + 15)

struct TxGzFile_
{
   RFILE   *fp;
   void    *stream;      /* rdeflate or rinflate handle */
   uint8_t *raw;         /* compressed side  */
   uint8_t *buf;         /* decompressed side (read mode only) */
   size_t   raw_fill;    /* bytes of `raw` holding data       */
   size_t   raw_pos;     /* consumed prefix of `raw`          */
   size_t   buf_fill;
   size_t   buf_pos;
   int      writing;
   int      eos;         /* inflate reported END              */
   int      failed;
};

static void txgz_destroy(TxGzFile *f)
{
   if (!f)
      return;
   if (f->stream)
   {
      if (f->writing)
         rdeflate_free(f->stream);
      else
         rinflate_free(f->stream);
   }
   if (f->fp)
      filestream_close(f->fp);
   free(f->raw);
   free(f->buf);
   free(f);
}

TxGzFile *txgz_open(const char *path, const char *mode)
{
   TxGzFile *f;
   int writing = 0, level = 1;
   const char *m;

   if (!path || !mode)
      return NULL;

   for (m = mode; *m; m++)
   {
      if (*m == 'w' || *m == 'a')
         writing = 1;
      else if (*m >= '0' && *m <= '9')
         level = *m - '0';
   }

   if (!(f = (TxGzFile*)calloc(1, sizeof(*f))))
      return NULL;

   f->writing = writing;
   f->raw     = (uint8_t*)malloc(TXGZ_CHUNK);
   if (!f->raw)
   {
      txgz_destroy(f);
      return NULL;
   }

   if (writing)
      f->stream = rdeflate_new(level, TXGZ_WBITS_WRITE);
   else
   {
      f->stream = rinflate_new(TXGZ_WBITS_READ);
      f->buf    = (uint8_t*)malloc(TXGZ_CHUNK);
      if (!f->buf)
      {
         txgz_destroy(f);
         return NULL;
      }
   }
   if (!f->stream)
   {
      txgz_destroy(f);
      return NULL;
   }

   f->fp = filestream_open(path,
         writing ? RETRO_VFS_FILE_ACCESS_WRITE : RETRO_VFS_FILE_ACCESS_READ,
         RETRO_VFS_FILE_ACCESS_HINT_NONE);
   if (!f->fp)
   {
      txgz_destroy(f);
      return NULL;
   }

   return f;
}

/* Push whatever deflate has staged in `raw` out to the file. */
static int txgz_flush_raw(TxGzFile *f, size_t len)
{
   if (len == 0)
      return 0;
   if (filestream_write(f->fp, f->raw, len) != (int64_t)len)
   {
      f->failed = 1;
      return -1;
   }
   return 0;
}

int txgz_write(TxGzFile *f, const void *buf, unsigned len)
{
   const uint8_t *in = (const uint8_t*)buf;
   size_t remaining  = len;

   if (!f || !f->writing || f->failed)
      return -1;
   if (len == 0)
      return 0;

   rdeflate_set_in(f->stream, in, remaining);

   for (;;)
   {
      size_t rd = 0, wn = 0;
      int st;

      rdeflate_set_out(f->stream, f->raw, TXGZ_CHUNK);
      st = rdeflate_process(f->stream, &rd, &wn);

      if (st == RDEFLATE_PROCESS_ERROR)
      {
         f->failed = 1;
         return -1;
      }
      if (txgz_flush_raw(f, wn) < 0)
         return -1;

      remaining -= rd;
      /* NEXT with nothing consumed and nothing produced would spin. */
      if (remaining == 0)
         break;
      if (rd == 0 && wn == 0)
      {
         f->failed = 1;
         return -1;
      }
   }

   return (int)len;
}

/* Refill f->buf with decoded bytes.  Returns bytes produced, 0 at end of
 * stream, -1 on error. */
static int txgz_fill(TxGzFile *f)
{
   f->buf_fill = 0;
   f->buf_pos  = 0;

   while (f->buf_fill == 0)
   {
      size_t rd = 0, wn = 0;
      int st;

      if (f->raw_pos == f->raw_fill)
      {
         int64_t got = filestream_read(f->fp, f->raw, TXGZ_CHUNK);
         if (got < 0)
         {
            f->failed = 1;
            return -1;
         }
         f->raw_fill = (size_t)got;
         f->raw_pos  = 0;
         if (got == 0)
         {
            /* Input exhausted before END: truncated file. */
            if (!f->eos)
               f->eos = 1;
            return 0;
         }
      }

      rinflate_set_in(f->stream, f->raw + f->raw_pos, f->raw_fill - f->raw_pos);
      rinflate_set_out(f->stream, f->buf, TXGZ_CHUNK);
      st = rinflate_process(f->stream, &rd, &wn);

      if (st == RDEFLATE_PROCESS_ERROR)
      {
         f->failed = 1;
         return -1;
      }

      f->raw_pos  += rd;
      f->buf_fill  = wn;

      if (st == RDEFLATE_PROCESS_END)
      {
         f->eos = 1;
         break;
      }
      if (rd == 0 && wn == 0 && f->raw_pos == f->raw_fill)
         continue; /* need more input; loop refills */
   }

   return (int)f->buf_fill;
}

int txgz_read(TxGzFile *f, void *buf, unsigned len)
{
   uint8_t *out = (uint8_t*)buf;
   unsigned done = 0;

   if (!f || f->writing || f->failed)
      return -1;

   while (done < len)
   {
      size_t avail = f->buf_fill - f->buf_pos;

      if (avail == 0)
      {
         int got;
         if (f->eos && f->buf_fill == f->buf_pos)
         {
            /* Drain anything still pending after END before stopping. */
            if (txgz_fill(f) <= 0)
               break;
            continue;
         }
         got = txgz_fill(f);
         if (got < 0)
            return -1;
         if (got == 0)
            break;
         continue;
      }

      if (avail > (size_t)(len - done))
         avail = len - done;
      memcpy(out + done, f->buf + f->buf_pos, avail);
      f->buf_pos += avail;
      done       += avail;
   }

   return (int)done;
}

int txgz_skip(TxGzFile *f, long offset)
{
   uint8_t scratch[4096];

   if (!f || f->writing || offset < 0)
      return -1;

   while (offset > 0)
   {
      unsigned want = (offset > (long)sizeof(scratch))
         ? (unsigned)sizeof(scratch) : (unsigned)offset;
      int got = txgz_read(f, scratch, want);
      if (got <= 0)
         return -1;
      offset -= got;
   }
   return 0;
}

int txgz_eof(TxGzFile *f)
{
   if (!f)
      return 1;
   if (f->failed)
      return 1;
   if (f->buf_pos < f->buf_fill)
      return 0;
   return f->eos;
}

int txgz_close(TxGzFile *f)
{
   int ret = 0;

   if (!f)
      return -1;

   if (f->writing && !f->failed)
   {
      /* Nothing more to feed: emit the final block and the gzip trailer. */
      rdeflate_set_in(f->stream, NULL, 0);
      rdeflate_finish(f->stream);

      for (;;)
      {
         size_t rd = 0, wn = 0;
         int st;

         rdeflate_set_out(f->stream, f->raw, TXGZ_CHUNK);
         st = rdeflate_process(f->stream, &rd, &wn);

         if (st == RDEFLATE_PROCESS_ERROR)
         {
            ret = -1;
            break;
         }
         if (txgz_flush_raw(f, wn) < 0)
         {
            ret = -1;
            break;
         }
         if (st == RDEFLATE_PROCESS_END)
            break;
         if (wn == 0)
         {
            ret = -1;
            break;
         }
      }
   }

   if (f->failed)
      ret = -1;

   txgz_destroy(f);
   return ret;
}
