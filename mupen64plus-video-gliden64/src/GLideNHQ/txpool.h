/* Copyright (c) 2026 The RetroArch team
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef TXPOOL_H
#define TXPOOL_H

/* Fork-join pool for GLideNHQ's texture filters and quantizers.
 *
 * The threads are created once and park on an eventcount between jobs,
 * instead of being created and joined for every texture. txpool_run()
 * hands the same function to lanes 0..used-1, runs lane 0 itself and
 * returns when every lane is done. No lock anywhere.
 *
 * One caller at a time: GLideNHQ filters on the drawing thread only.
 * Without lock-free atomics, or if no thread starts, the pool has one
 * lane and txpool_run() just calls the function. */

#include <retro_common_api.h>

RETRO_BEGIN_DECLS

typedef struct txpool txpool_t;

typedef void (*txpool_fn_t)(void *ctx, unsigned lane);

/* @lanes counts the caller, so lanes - 1 threads are started. */
txpool_t *txpool_new(unsigned lanes);

/* Lanes actually available; 1 if no thread could be started. */
unsigned txpool_lanes(const txpool_t *p);

/* Runs fn(ctx, 0) .. fn(ctx, used - 1) and waits for all of them: in
 * parallel when @used <= txpool_lanes(), else one after another on the
 * caller. Every lane always runs. */
void txpool_run(txpool_t *p, txpool_fn_t fn, void *ctx, unsigned used);

/* Stops and joins the threads. Safe on NULL. */
void txpool_free(txpool_t *p);

RETRO_END_DECLS

#endif
