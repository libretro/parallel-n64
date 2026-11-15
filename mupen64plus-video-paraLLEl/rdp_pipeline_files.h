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

/* The driver's pipeline cache and the list of the pipelines built, kept
 * in <system>/ParallelN64 from one session to the next: the list
 * outlives a cache another driver no longer takes. What was loaded is
 * kept to merge into, and to leave the files alone when nothing
 * changed. Content load and close only. C89. */

#include <stddef.h>
#include <stdint.h>
#include <boolean.h>
#include <retro_common_api.h>
#include "pipeline_record.h"

RETRO_BEGIN_DECLS

#define RDP_PIPELINE_CACHE_FILE "parallel_rdp_pipeline_cache.bin"
#define RDP_PIPELINE_LIST_FILE "parallel_rdp_pipelines.bin"

typedef struct rdp_pipeline_files
{
	/* <system> */
	char system_dir[4096];
	/* the list loaded */
	pipeline_record_t *list;
	size_t list_count;
	/* the cache loaded: -1 bytes for none */
	uint32_t cache_crc;
	int64_t cache_size;
} rdp_pipeline_files_t;

/* <system>/ParallelN64/@name in @s, the folder made if it is not there.
 * A file an earlier build left in <system> itself is moved over first,
 * so that what it holds is not built again. False without a folder. */
bool rdp_pipeline_file_path(const char *system_dir, char *s, size_t len, const char *name);

/* Loads both from @system_dir (NULL or empty: none): the cache's blob,
 * the caller's to free (NULL, 0 bytes: none), and the list */
void rdp_pipeline_files_load(rdp_pipeline_files_t *f, const char *system_dir, void **cache, size_t *cache_size);

/* Saves the list, @built (this session's, first) merged with the one
 * loaded, unless it is the one loaded; and @cache unless it is the one
 * loaded (NULL: none). Frees what was loaded. */
void rdp_pipeline_files_save(rdp_pipeline_files_t *f, const pipeline_record_t *built, size_t num_built,
		const void *cache, size_t cache_size);

/* Frees what was loaded */
void rdp_pipeline_files_free(rdp_pipeline_files_t *f);

RETRO_END_DECLS

#endif
