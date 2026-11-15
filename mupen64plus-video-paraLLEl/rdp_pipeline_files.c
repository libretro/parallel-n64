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


#include <stdlib.h>
#include <string.h>
#include <streams/file_stream.h>
#include <file/file_path.h>
#include <retro_miscellaneous.h>
#include <encodings/crc32.h>
#include <compat/strl.h>
#include "rdp_pipeline_files.h"
#include "pipeline_record_list.h"

bool rdp_pipeline_file_path(const char *system_dir, char *s, size_t len, const char *name)
{
	char folder[PATH_MAX_LENGTH];
	char old[PATH_MAX_LENGTH];

	if (!system_dir || !*system_dir)
		return false;
	fill_pathname_join_special(folder, system_dir, "ParallelN64", sizeof(folder));
	if (!path_mkdir(folder))
		return false;
	fill_pathname_join_special(s, folder, name, len);
	fill_pathname_join_special(old, system_dir, name, sizeof(old));
	if (!filestream_exists(s) && filestream_exists(old))
		filestream_rename(old, s);
	return true;
}

static bool path_of(const rdp_pipeline_files_t *f, char *s, size_t len, const char *name)
{
	return rdp_pipeline_file_path(f->system_dir, s, len, name);
}

void rdp_pipeline_files_free(rdp_pipeline_files_t *f)
{
	free(f->list);
	f->list = NULL;
	f->list_count = 0;
}

void rdp_pipeline_files_load(rdp_pipeline_files_t *f, const char *system_dir, void **cache, size_t *cache_size)
{
	char path[PATH_MAX_LENGTH];
	void *blob = NULL;
	int64_t size = 0;

	rdp_pipeline_files_free(f);
	f->system_dir[0] = '\0';
	if (system_dir)
		strlcpy(f->system_dir, system_dir, sizeof(f->system_dir));
	f->cache_crc = 0;
	f->cache_size = -1;
	*cache = NULL;
	*cache_size = 0;

	if (path_of(f, path, sizeof(path), RDP_PIPELINE_CACHE_FILE) && filestream_read_file(path, &blob, &size) && blob)
	{
		f->cache_crc = encoding_crc32(0, (const uint8_t *)blob, (size_t)size);
		f->cache_size = size;
		*cache = blob;
		*cache_size = (size_t)size;
	}
	if (path_of(f, path, sizeof(path), RDP_PIPELINE_LIST_FILE))
		f->list = pipeline_record_list_load(path, &f->list_count);
}

void rdp_pipeline_files_save(rdp_pipeline_files_t *f, const pipeline_record_t *built, size_t num_built,
		const void *cache, size_t cache_size)
{
	char path[PATH_MAX_LENGTH];
	pipeline_record_t *merged;

	/* this session's first, so what is past the cap is the oldest */
	merged = (pipeline_record_t *)malloc((num_built + f->list_count + 1) * sizeof(*merged));
	if (merged)
	{
		size_t count;
		if (num_built)
			memcpy(merged, built, num_built * sizeof(*merged));
		if (f->list_count)
			memcpy(merged + num_built, f->list, f->list_count * sizeof(*merged));
		count = pipeline_record_list_unique(merged, num_built + f->list_count, PIPELINE_RECORD_LIST_MAX);
		if ((count != f->list_count || (count && memcmp(merged, f->list, count * sizeof(*merged))))
				&& path_of(f, path, sizeof(path), RDP_PIPELINE_LIST_FILE))
			pipeline_record_list_save(path, merged, count);
		free(merged);
	}
	rdp_pipeline_files_free(f);

	if (cache && cache_size
			&& ((int64_t)cache_size != f->cache_size
				|| encoding_crc32(0, (const uint8_t *)cache, cache_size) != f->cache_crc)
			&& path_of(f, path, sizeof(path), RDP_PIPELINE_CACHE_FILE))
		filestream_write_file_atomic(path, cache, (int64_t)cache_size);
}
