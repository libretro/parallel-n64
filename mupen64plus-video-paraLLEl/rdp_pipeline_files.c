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

void rdp_pipeline_files_free(rdp_pipeline_files_t *f)
{
	if (f->file)
		filestream_close(f->file);
	free(f->copy);
	free(f->before_list);
	free(f->before_cache);
	f->file = NULL;
	f->copy = f->before_list = f->before_cache = NULL;
	f->list = NULL;
	f->list_count = 0;
	f->cache = NULL;
	f->cache_size = 0;
	f->before = false;
}

/* no whole file is larger: the most records, and a cache past any a
 * driver writes */
#define RDP_PIPELINE_MAX_FILE ((int64_t)RDP_PIPELINE_HEAD + (int64_t)PIPELINE_RECORD_LIST_MAX * \
		(int64_t)sizeof(pipeline_record_t) + 256 * 1024 * 1024)

/* The file's list and cache lent where it is whole */
static bool open_file(rdp_pipeline_files_t *f)
{
	const uint8_t *data = NULL;
	int64_t size = 0;
	uint32_t h[6];

	/* one open; the file lent where it can be mapped, read once where not */
	if (!(f->file = filestream_open(f->path, RETRO_VFS_FILE_ACCESS_READ, RETRO_VFS_FILE_ACCESS_HINT_FREQUENT_ACCESS)))
		return false;
	data = filestream_get_mapped_ptr(f->file, &size);
	if (!data && (size = filestream_get_size(f->file)) > 0 && size <= RDP_PIPELINE_MAX_FILE
			&& (f->copy = malloc((size_t)size))
			&& filestream_read(f->file, f->copy, size) == size)
		data = (const uint8_t *)f->copy;
	if (!data || size < RDP_PIPELINE_HEAD)
		return false;
	memcpy(h, data, sizeof(h));
	if (h[0] != RDP_PIPELINE_MAGIC || h[1] != RDP_PIPELINE_VERSION || h[2] != sizeof(pipeline_record_t)
			|| h[3] > PIPELINE_RECORD_LIST_MAX
			|| (uint64_t)RDP_PIPELINE_HEAD + (uint64_t)h[3] * sizeof(pipeline_record_t) + h[5] != (uint64_t)size)
		return false;
	if (h[5])
	{
		f->cache = data + RDP_PIPELINE_HEAD + (size_t)h[3] * sizeof(pipeline_record_t);
		f->cache_size = h[5];
	}
	if (h[3])
	{
		const pipeline_record_t *list = (const pipeline_record_t *)(data + RDP_PIPELINE_HEAD);
		if (encoding_crc32(0, (const uint8_t *)list, (size_t)h[3] * sizeof(*list)) == h[4]
				&& pipeline_record_list_valid(list, h[3]))
		{
			f->list = list;
			f->list_count = h[3];
		}
	}
	return true;
}

/* the files of before, copies */
static void open_before(rdp_pipeline_files_t *f, const char *system_dir)
{
	char path[PATH_MAX_LENGTH];
	int64_t size = 0;

	if (rdp_pipeline_file_path(system_dir, path, sizeof(path), RDP_PIPELINE_LIST_FILE) && filestream_exists(path))
	{
		f->before = true;
		f->list = (const pipeline_record_t *)(f->before_list = pipeline_record_list_load(path, &f->list_count));
	}
	if (rdp_pipeline_file_path(system_dir, path, sizeof(path), RDP_PIPELINE_CACHE_FILE) && filestream_exists(path))
	{
		f->before = true;
		if (filestream_read_file(path, &f->before_cache, &size) && f->before_cache && size > 0)
		{
			f->cache = f->before_cache;
			f->cache_size = (size_t)size;
		}
	}
}

void rdp_pipeline_files_load(rdp_pipeline_files_t *f, const char *system_dir)
{
	rdp_pipeline_files_free(f);
	f->path[0] = '\0';
	if (!rdp_pipeline_file_path(system_dir, f->path, sizeof(f->path), RDP_PIPELINE_FILE))
		return;
	if (open_file(f))
		return;
	/* not there, or not whole */
	rdp_pipeline_files_free(f);
	open_before(f, system_dir);
}

/* the header, @count records, @size bytes of cache, each from where it
 * is, replacing @path whole or not at all */
static bool write_file(const char *path, const pipeline_record_t *list, size_t count,
		const void *cache, size_t size)
{
	uint32_t h[6];
	RFILE *file;
	int ret;
	bool ok;

	h[0] = RDP_PIPELINE_MAGIC;
	h[1] = RDP_PIPELINE_VERSION;
	h[2] = sizeof(pipeline_record_t);
	h[3] = (uint32_t)count;
	h[4] = encoding_crc32(0, (const uint8_t *)list, count * sizeof(*list));
	h[5] = (uint32_t)size;
	if ((size_t)h[5] != size || !(file = filestream_open_atomic(path)))
		return false;
	ok = filestream_write(file, h, sizeof(h)) == (int64_t)sizeof(h)
		&& (!count || filestream_write(file, list, (int64_t)(count * sizeof(*list))) == (int64_t)(count * sizeof(*list)))
		&& (!size || filestream_write(file, cache, (int64_t)size) == (int64_t)size);
	ret = filestream_commit_atomic(file, path, ok);
	/* written but not renamed into place */
	if (ret == 1)
	{
		char tmp[PATH_MAX_LENGTH];
		if (filestream_atomic_temp_name(tmp, sizeof(tmp), path))
			filestream_delete(tmp);
	}
	return ret == 0;
}

static void remove_before(const rdp_pipeline_files_t *f)
{
	char path[PATH_MAX_LENGTH];
	char folder[PATH_MAX_LENGTH];
	fill_pathname_basedir(folder, f->path, sizeof(folder));
	fill_pathname_join_special(path, folder, RDP_PIPELINE_LIST_FILE, sizeof(path));
	if (filestream_exists(path))
		filestream_delete(path);
	fill_pathname_join_special(path, folder, RDP_PIPELINE_CACHE_FILE, sizeof(path));
	if (filestream_exists(path))
		filestream_delete(path);
}

void rdp_pipeline_files_save(rdp_pipeline_files_t *f, const pipeline_record_t *built, size_t num_built,
		const void *cache, size_t cache_size)
{
	pipeline_record_t *merged;
	void *kept = NULL;
	size_t count;
	bool changed;

	/* this session's first, so what is past the cap is the oldest */
	merged = (pipeline_record_t *)malloc((num_built + f->list_count + 1) * sizeof(*merged));
	if (!merged || !f->path[0])
	{
		free(merged);
		rdp_pipeline_files_free(f);
		return;
	}
	if (num_built)
		memcpy(merged, built, num_built * sizeof(*merged));
	if (f->list_count)
		memcpy(merged + num_built, f->list, f->list_count * sizeof(*merged));
	count = pipeline_record_list_unique(merged, num_built + f->list_count, PIPELINE_RECORD_LIST_MAX);
	changed = f->before || count != f->list_count || (count && memcmp(merged, f->list, count * sizeof(*merged)));
	if (!cache || !cache_size)
	{
		/* none this time: the one loaded kept */
		cache = f->cache;
		cache_size = f->cache_size;
	}
	else if (cache_size != f->cache_size || memcmp(cache, f->cache, cache_size))
		changed = true;
	/* the file let go before it is written over; the cache kept read
	 * out of it first */
	if (changed && cache == f->cache && cache_size && (kept = malloc(cache_size)))
	{
		memcpy(kept, cache, cache_size);
		cache = kept;
	}
	if (changed && (cache != f->cache || !cache_size))
	{
		const bool before = f->before;
		if (f->file)
			filestream_close(f->file);
		f->file = NULL;
		if (write_file(f->path, merged, count, cache, cache_size) && before)
			remove_before(f);
	}
	free(kept);
	free(merged);
	rdp_pipeline_files_free(f);
}
