/*
 * Texture Filtering
 *
 * Minimal gzip stream reader/writer over libretro-common's rdeflate /
 * rinflate, replacing the handful of zlib gz* entry points the texture
 * cache used.  The on-disk container is unchanged - RFC 1952 gzip, which
 * rdeflate emits for window_bits in 16..31 and rinflate auto-detects for
 * 32..47 - so caches written by earlier builds stay readable and caches
 * written here stay readable by them.
 *
 * Only the access pattern the cache actually uses is supported:
 * sequential write, sequential read, forward-only seek and eof.
 *
 * this is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 */

#ifndef __TXGZ_H__
#define __TXGZ_H__

#include <stdio.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct TxGzFile_ TxGzFile;

/* mode: "rb" to read, "wb" to write.  A trailing digit sets the deflate
 * level for writing ("wb1" = level 1), matching zlib's gzopen spelling. */
TxGzFile *txgz_open(const char *path, const char *mode);

/* Return bytes read, 0 at end of stream, -1 on error. */
int  txgz_read(TxGzFile *f, void *buf, unsigned len);

/* Return bytes written, -1 on error. */
int  txgz_write(TxGzFile *f, const void *buf, unsigned len);

/* Forward-only: decodes and discards. Returns 0 on success, -1 on error. */
int  txgz_skip(TxGzFile *f, long offset);

/* Non-zero once the decoded stream is exhausted. */
int  txgz_eof(TxGzFile *f);

/* Flushes (write mode) and releases the stream. Returns 0 on success. */
int  txgz_close(TxGzFile *f);

#ifdef __cplusplus
}
#endif

#endif /* __TXGZ_H__ */
