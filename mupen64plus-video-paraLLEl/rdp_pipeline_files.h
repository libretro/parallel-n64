/* Copyright (c) 2020 Themaister
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */


#ifndef RDP_PIPELINE_FILES_H
#define RDP_PIPELINE_FILES_H

/* The list of the pipelines built and the driver's pipeline cache, in
 * one file kept in <system>/ParallelN64 from one session to the next
 * (parallel_rdp.cache): a header of six words (the magic, the version, a
 * record's size, how many records, their CRC-32, how many bytes of
 * pipeline cache), the records, then the pipeline cache. Host order.
 *
 * Opened once at content load, mapped where the VFS can, read once where
 * not; the list and the cache lent from it until the save, the pipeline
 * worker reading the list meanwhile. A file that is not whole is none;
 * a list whose CRC or records are wrong is none, the cache beside it
 * kept: the list outlives a cache another driver no longer takes, and
 * the cache a list that went bad.
 *
 * Written again at content close only where the list or the cache
 * changed. Before it there were two files (parallel_rdp_pipelines.bin,
 * parallel_rdp_pipeline_cache.bin): where the file is not there they are
 * read, and removed once it is written. C89. */

#include <stddef.h>
#include <stdint.h>
#include <boolean.h>
#include <retro_common_api.h>
#include <streams/file_stream.h>
#include "pipeline_record.h"

RETRO_BEGIN_DECLS

#define RDP_PIPELINE_FILE "parallel_rdp.cache"
#define RDP_PIPELINE_MAGIC 0x43445250u /* "PRDC" */
#define RDP_PIPELINE_VERSION 1u
#define RDP_PIPELINE_HEAD 24
/* before */
#define RDP_PIPELINE_CACHE_FILE "parallel_rdp_pipeline_cache.bin"
#define RDP_PIPELINE_LIST_FILE "parallel_rdp_pipelines.bin"

typedef struct rdp_pipeline_files
{
	/* <system>/ParallelN64/parallel_rdp.cache; empty without a system
	 * folder */
	char path[4096];
	/* lent until the save or free */
	const pipeline_record_t *list;
	size_t list_count;
	const void *cache;
	size_t cache_size;
	/* read from the files of before: the file is to be written */
	bool before;

	RFILE *file;
	void *copy;
	void *before_list, *before_cache;
} rdp_pipeline_files_t;

/* <system>/ParallelN64/@name in @s, the folder made if it is not there.
 * A file an earlier build left in <system> itself is moved over first,
 * so that what it holds is not built again. False without a folder. */
bool rdp_pipeline_file_path(const char *system_dir, char *s, size_t len, const char *name);

/* The file in @system_dir (NULL or empty: none), its list and cache lent */
void rdp_pipeline_files_load(rdp_pipeline_files_t *f, const char *system_dir);

/* The file written where the list, @built (this session's, first) merged
 * with the one loaded, or the cache, @cache (NULL or 0 bytes: the one
 * loaded kept), is not the one loaded, or where the files of before were
 * read; they removed once it is. Frees what was loaded. */
void rdp_pipeline_files_save(rdp_pipeline_files_t *f, const pipeline_record_t *built, size_t num_built,
		const void *cache, size_t cache_size);

/* What was loaded let go */
void rdp_pipeline_files_free(rdp_pipeline_files_t *f);

RETRO_END_DECLS

#endif
