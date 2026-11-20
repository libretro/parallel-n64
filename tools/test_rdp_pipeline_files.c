/* test_rdp_pipeline_files -- the frontend's list of built pipelines and
 * the driver's pipeline cache, one file kept from one session to the next
 * (mupen64plus-video-paraLLEl/rdp_pipeline_files.c), in a folder of its
 * own.
 *
 * - <system>/ParallelN64 is made; a file an earlier build left in
 *   <system> moves there, unless one is there already; no system folder,
 *   no file;
 * - nothing saved: nothing loaded, and nothing to save is not written;
 * - saved, then loaded, mapped where the VFS maps it and read once where
 *   not: the header, the list (this session's first, each pipeline once)
 *   and the cache's bytes, lent from the file;
 * - the same again is not written (the file is left alone), another
 *   cache or list is; no cache this time keeps the one loaded, read out
 *   of the file before it is written over;
 * - a file not whole, of another magic, version or record size, or of
 *   too many records, is none; a list whose CRC or records are wrong is
 *   none, the cache kept;
 * - the two files of before read where the file is not there, and
 *   removed once it is written; kept where it cannot be.
 *
 * The module is included here, its map through test_get_mapped_ptr.
 * Built by `make check-core` (top-level Makefile). C89. Exit status 0 on
 * pass.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <streams/file_stream.h>
#include <file/file_path.h>
#include <encodings/crc32.h>

#include "rdp_pipeline_files.h"
#include "pipeline_record_list.h"

const uint8_t *test_get_mapped_ptr(RFILE *stream, int64_t *len);
void *test_malloc(size_t n);
#define filestream_get_mapped_ptr test_get_mapped_ptr
#define malloc test_malloc
#include "rdp_pipeline_files.c"
#undef filestream_get_mapped_ptr
#undef malloc

static int s_failmalloc, s_nomap, s_mapped;
void *test_malloc(size_t n)
{
	return s_failmalloc ? NULL : malloc(n);
}

const uint8_t *test_get_mapped_ptr(RFILE *stream, int64_t *len)
{
	const uint8_t *p;
	if (s_nomap)
	{
		*len = 0;
		return NULL;
	}
	p = filestream_get_mapped_ptr(stream, len);
	s_mapped += p != NULL;
	return p;
}

static unsigned failures;

#define CHECK(cond, what) \
	do \
	{ \
		if (!(cond)) \
		{ \
			fprintf(stderr, "test_rdp_pipeline_files: FAIL (line %d): %s\n", __LINE__, what); \
			failures++; \
		} \
	} while (0)

static char dir[512];
static char file_path[1024], list_path[1024], cache_path[1024];

static void in_dir(char *s, size_t len, const char *a, const char *b)
{
	if (b)
		snprintf(s, len, "%s/%s/%s", dir, a, b);
	else
		snprintf(s, len, "%s/%s", dir, a);
}

static void record(pipeline_record_t *r, uint64_t hash)
{
	memset(r, 0, sizeof(*r));
	r->pipeline_hash = hash;
	r->program_hash = hash * 3;
	r->kind = PIPELINE_RECORD_COMPUTE;
}

static void *read_all(const char *path, int64_t *len)
{
	void *buf = NULL;
	*len = 0;
	if (!filestream_exists(path) || !filestream_read_file(path, &buf, len))
		return NULL;
	return buf;
}

/* nothing lent, nothing held */
static int none(const rdp_pipeline_files_t *f)
{
	return !f->list && !f->list_count && !f->cache && !f->cache_size && !f->before && !f->file && !f->copy
		&& !f->before_list && !f->before_cache;
}

static void test_path(void)
{
	char s[1024], old[1024], want[1024];
	CHECK(!rdp_pipeline_file_path(NULL, s, sizeof(s), "x.bin"), "no system folder, no file");
	CHECK(!rdp_pipeline_file_path("", s, sizeof(s), "x.bin"), "nor an empty one");

	in_dir(old, sizeof(old), "a.bin", NULL);
	filestream_write_file(old, "old", 3);
	CHECK(rdp_pipeline_file_path(dir, s, sizeof(s), "a.bin"), "a path");
	in_dir(want, sizeof(want), "ParallelN64", "a.bin");
	CHECK(!strcmp(s, want), "in <system>/ParallelN64");
	CHECK(filestream_exists(want) && !filestream_exists(old), "the earlier build's file moves there");

	filestream_write_file(old, "older", 5);
	CHECK(rdp_pipeline_file_path(dir, s, sizeof(s), "a.bin") && filestream_exists(old),
			"not over one there already");
	{
		int64_t len;
		void *buf = read_all(want, &len);
		CHECK(buf && len == 3 && !memcmp(buf, "old", 3), "which stays");
		free(buf);
	}
	filestream_delete(old);
	filestream_delete(want);
}

static void test_no_folder(void)
{
	rdp_pipeline_files_t f;
	pipeline_record_t r;
	memset(&f, 0, sizeof(f));
	rdp_pipeline_files_load(&f, dir);
	rdp_pipeline_files_free(&f);
	rdp_pipeline_files_load(&f, NULL);
	CHECK(none(&f) && !f.path[0], "no system folder: nothing, not the last one's");
	record(&r, 1);
	rdp_pipeline_files_save(&f, &r, 1, "x", 1);
	CHECK(none(&f), "nothing saved, nothing held");
}

static void test_round_trip(int nomap)
{
	rdp_pipeline_files_t f;
	pipeline_record_t built[4], more[2];
	static const char cache[] = "the driver's cache";
	static const char other[] = "another driver's cache";
	char same[sizeof(cache)];

	s_nomap = nomap;
	s_mapped = 0;
	memset(&f, 0, sizeof(f));
	filestream_delete(file_path);

	rdp_pipeline_files_load(&f, dir);
	CHECK(none(&f) && !strcmp(f.path, file_path), "nothing saved, nothing loaded");
	rdp_pipeline_files_save(&f, NULL, 0, NULL, 0);
	CHECK(!filestream_exists(file_path), "nothing to save: not written");

	record(&built[0], 1);
	record(&built[1], 2);
	record(&built[2], 1);
	record(&built[3], 3);
	rdp_pipeline_files_load(&f, dir);
	rdp_pipeline_files_save(&f, built, 4, cache, sizeof(cache));
	CHECK(filestream_exists(file_path), "saved");
	CHECK(none(&f), "what was loaded let go");
	{
		int64_t len;
		uint8_t *buf = (uint8_t *)read_all(file_path, &len);
		uint32_t h[6];
		CHECK(buf && len == (int64_t)(RDP_PIPELINE_HEAD + 3 * sizeof(pipeline_record_t) + sizeof(cache)),
				"one file, its header, three records and the cache");
		if (buf && len >= RDP_PIPELINE_HEAD)
		{
			memcpy(h, buf, sizeof(h));
			CHECK(h[0] == RDP_PIPELINE_MAGIC && h[1] == RDP_PIPELINE_VERSION && h[2] == sizeof(pipeline_record_t)
					&& h[3] == 3 && h[5] == sizeof(cache), "its header");
			CHECK(h[4] == encoding_crc32(0, buf + RDP_PIPELINE_HEAD, 3 * sizeof(pipeline_record_t)), "its CRC");
			CHECK(!memcmp(buf + len - sizeof(cache), cache, sizeof(cache)), "the cache last");
		}
		free(buf);
	}

	rdp_pipeline_files_load(&f, dir);
	CHECK(f.cache && f.cache_size == sizeof(cache) && !memcmp(f.cache, cache, sizeof(cache)), "the cache's bytes");
	CHECK(f.list_count == 3 && f.list[0].pipeline_hash == 1 && f.list[1].pipeline_hash == 2
			&& f.list[2].pipeline_hash == 3, "the list, each pipeline once");
	CHECK(!f.before && f.file, "the file kept open");
	CHECK(nomap ? s_mapped == 0 && f.copy : s_mapped == 1 && !f.copy, "mapped where the VFS maps it, read where not");
	CHECK(!nomap || ((const uint8_t *)f.list == (const uint8_t *)f.copy + RDP_PIPELINE_HEAD
			&& (const uint8_t *)f.cache == (const uint8_t *)f.copy + RDP_PIPELINE_HEAD + 3 * sizeof(pipeline_record_t)),
			"lent from the file");

	/* the same again: the file is left alone */
	memcpy(same, f.cache, sizeof(same));
	filestream_delete(file_path);
	rdp_pipeline_files_save(&f, built, 4, same, sizeof(same));
	CHECK(!filestream_exists(file_path), "the same again is not written");
	CHECK(none(&f), "what was loaded is let go");

	/* saved again, then another session's */
	rdp_pipeline_files_save(&f, built, 4, cache, sizeof(cache));
	rdp_pipeline_files_load(&f, dir);
	record(&more[0], 7);
	record(&more[1], 2);
	rdp_pipeline_files_save(&f, more, 2, other, sizeof(other));
	rdp_pipeline_files_load(&f, dir);
	CHECK(f.cache && f.cache_size == sizeof(other) && !memcmp(f.cache, other, sizeof(other)), "another cache is written");
	CHECK(f.list_count == 4 && f.list[0].pipeline_hash == 7 && f.list[1].pipeline_hash == 2
			&& f.list[2].pipeline_hash == 1 && f.list[3].pipeline_hash == 3,
			"another list: this session's first, then the last's not in it");

	/* the same pipelines in another order; a cache of the same size */
	{
		pipeline_record_t again[4];
		static const char same_size[] = "Another driver's cache";
		record(&again[0], 3);
		record(&again[1], 1);
		record(&again[2], 2);
		record(&again[3], 7);
		filestream_delete(file_path);
		rdp_pipeline_files_save(&f, again, 4, other, sizeof(other));
		CHECK(filestream_exists(file_path), "a list of as many in another order is written");
		rdp_pipeline_files_load(&f, dir);
		CHECK(f.list_count == 4 && f.list[0].pipeline_hash == 3 && f.list[3].pipeline_hash == 7, "in its order");
		filestream_delete(file_path);
		rdp_pipeline_files_save(&f, again, 4, same_size, sizeof(same_size));
		CHECK(filestream_exists(file_path), "a cache of the same size, other bytes, is written");
		rdp_pipeline_files_load(&f, dir);
		CHECK(f.cache_size == sizeof(same_size) && !memcmp(f.cache, same_size, sizeof(same_size)), "its bytes");

		/* no cache this time: the one loaded kept, with a new list */
		rdp_pipeline_files_save(&f, more, 2, NULL, 0);
		rdp_pipeline_files_load(&f, dir);
		CHECK(f.list_count == 4 && f.list[0].pipeline_hash == 7 && f.list[1].pipeline_hash == 2
				&& f.list[2].pipeline_hash == 3, "the new list written");
		CHECK(f.cache_size == sizeof(same_size) && !memcmp(f.cache, same_size, sizeof(same_size)),
				"without a cache the one loaded stays");
		filestream_delete(file_path);
		rdp_pipeline_files_save(&f, more, 2, f.cache, 0);
		CHECK(!filestream_exists(file_path), "nor anything new: not written");

		/* no memory to keep the one loaded: not written over */
		rdp_pipeline_files_save(&f, again, 4, same_size, sizeof(same_size));
		rdp_pipeline_files_load(&f, dir);
		s_failmalloc = 1;
		filestream_delete(file_path);
		rdp_pipeline_files_save(&f, more, 2, NULL, 0);
		s_failmalloc = 0;
		CHECK(!filestream_exists(file_path), "no memory: not written");
	}
	/* no memory to merge into: nothing written, what was loaded let go */
	rdp_pipeline_files_load(&f, dir);
	rdp_pipeline_files_save(&f, more, 2, other, sizeof(other));
	rdp_pipeline_files_load(&f, dir);
	CHECK(f.file != NULL, "open");
	s_failmalloc = 1;
	filestream_delete(file_path);
	rdp_pipeline_files_save(&f, built, 4, cache, sizeof(cache));
	s_failmalloc = 0;
	CHECK(!filestream_exists(file_path) && none(&f), "no memory: not written, let go");

	/* a list alone; a cache alone */
	rdp_pipeline_files_load(&f, dir);
	rdp_pipeline_files_save(&f, more, 2, NULL, 0);
	rdp_pipeline_files_load(&f, dir);
	CHECK(f.list_count == 2 && !f.cache && !f.cache_size, "a list alone");
	filestream_delete(file_path);
	rdp_pipeline_files_load(&f, dir);
	rdp_pipeline_files_save(&f, NULL, 0, cache, sizeof(cache));
	rdp_pipeline_files_load(&f, dir);
	CHECK(!f.list && !f.list_count && f.cache_size == sizeof(cache), "a cache alone");
	rdp_pipeline_files_free(&f);
	CHECK(none(&f), "let go");

	/* not mapped, and no memory to read it into: none */
	s_nomap = 1;
	s_failmalloc = 1;
	rdp_pipeline_files_load(&f, dir);
	s_failmalloc = 0;
	CHECK(none(&f), "no memory: none");
	rdp_pipeline_files_free(&f);
	s_nomap = 0;
}

/* @file of @len, its header word @w set to @v, or cut to @cut, or a byte
 * at @flip flipped, written and loaded */
static void loaded(rdp_pipeline_files_t *f, const uint8_t *file, int64_t len, int w, uint32_t v, int64_t cut,
		int64_t flip)
{
	uint8_t *b = (uint8_t *)malloc((size_t)len);
	memcpy(b, file, (size_t)len);
	if (w >= 0)
		memcpy(b + 4 * w, &v, 4);
	if (flip >= 0)
		b[flip] ^= 0x10;
	filestream_write_file(file_path, b, cut >= 0 ? cut : len);
	free(b);
	rdp_pipeline_files_load(f, dir);
}

static void test_damaged(void)
{
	rdp_pipeline_files_t f;
	pipeline_record_t built[3];
	static const char cache[] = "a cache";
	uint8_t *file;
	int64_t len;

	memset(&f, 0, sizeof(f));
	record(&built[0], 1);
	record(&built[1], 2);
	record(&built[2], 3);
	filestream_delete(file_path);
	rdp_pipeline_files_load(&f, dir);
	rdp_pipeline_files_save(&f, built, 3, cache, sizeof(cache));
	file = (uint8_t *)read_all(file_path, &len);
	CHECK(file != NULL, "a file to damage");
	if (!file)
		return;

	loaded(&f, file, len, -1, 0, -1, -1);
	CHECK(f.list_count == 3 && f.cache_size == sizeof(cache), "whole");
	loaded(&f, file, len, -1, 0, len - 1, -1);
	CHECK(none(&f), "cut short: none");
	loaded(&f, file, len, -1, 0, RDP_PIPELINE_HEAD - 1, -1);
	CHECK(none(&f), "not a header: none");
	loaded(&f, file, len, 0, RDP_PIPELINE_MAGIC ^ 1, -1, -1);
	CHECK(none(&f), "another magic: none");
	loaded(&f, file, len, 1, RDP_PIPELINE_VERSION + 1, -1, -1);
	CHECK(none(&f), "another version: none");
	loaded(&f, file, len, 2, sizeof(pipeline_record_t) + 8, -1, -1);
	CHECK(none(&f), "another record size: none");
	loaded(&f, file, len, 3, PIPELINE_RECORD_LIST_MAX + 1, -1, -1);
	CHECK(none(&f), "too many records: none");
	loaded(&f, file, len, 5, sizeof(cache) + 1, -1, -1);
	CHECK(none(&f), "a cache past the end: none");

	/* as many records as a list holds */
	{
		pipeline_record_t *most = (pipeline_record_t *)calloc(PIPELINE_RECORD_LIST_MAX, sizeof(*most));
		unsigned i;
		for (i = 0; i < PIPELINE_RECORD_LIST_MAX; i++)
			record(&most[i], 100 + i);
		filestream_delete(file_path);
		rdp_pipeline_files_load(&f, dir);
		rdp_pipeline_files_save(&f, most, PIPELINE_RECORD_LIST_MAX, NULL, 0);
		rdp_pipeline_files_load(&f, dir);
		CHECK(f.list_count == PIPELINE_RECORD_LIST_MAX && f.list[PIPELINE_RECORD_LIST_MAX - 1].pipeline_hash
				== 100 + PIPELINE_RECORD_LIST_MAX - 1, "the most a list holds");
		rdp_pipeline_files_free(&f);
		free(most);
	}

	/* the list gone bad: none, the cache kept */
	loaded(&f, file, len, -1, 0, -1, RDP_PIPELINE_HEAD + 5);
	CHECK(!f.list && !f.list_count && f.cache_size == sizeof(cache) && !memcmp(f.cache, cache, sizeof(cache)),
			"a byte of the list flipped: no list, the cache kept");
	loaded(&f, file, len, 4, 0x12345678, -1, -1);
	CHECK(!f.list && f.cache_size == sizeof(cache), "another CRC: no list");
	{
		/* a record a device cannot build, its CRC right */
		uint8_t *b = (uint8_t *)malloc((size_t)len);
		uint32_t crc;
		pipeline_record_t r;
		memcpy(b, file, (size_t)len);
		memcpy(&r, b + RDP_PIPELINE_HEAD + sizeof(r), sizeof(r));
		r.kind = 77;
		memcpy(b + RDP_PIPELINE_HEAD + sizeof(r), &r, sizeof(r));
		crc = encoding_crc32(0, b + RDP_PIPELINE_HEAD, 3 * sizeof(r));
		memcpy(b + 16, &crc, 4);
		filestream_write_file(file_path, b, len);
		free(b);
		rdp_pipeline_files_load(&f, dir);
		CHECK(!f.list && f.cache_size == sizeof(cache), "a record out of range: no list");
	}
	/* written whole again by the next session */
	rdp_pipeline_files_save(&f, built, 3, NULL, 0);
	rdp_pipeline_files_load(&f, dir);
	CHECK(f.list_count == 3 && f.cache_size == sizeof(cache), "written whole again, the cache in it");
	rdp_pipeline_files_free(&f);
	free(file);
}

static void test_before(void)
{
	rdp_pipeline_files_t f;
	pipeline_record_t old[2], built[1];
	static const char cache[] = "the cache of before";
	char root_list[1024], tmp[1100];

	memset(&f, 0, sizeof(f));
	record(&old[0], 11);
	record(&old[1], 12);
	record(&built[0], 13);

	/* both: read, written into the file, removed */
	filestream_delete(file_path);
	CHECK(pipeline_record_list_save(list_path, old, 2), "a list of before");
	filestream_write_file(cache_path, cache, sizeof(cache));
	rdp_pipeline_files_load(&f, dir);
	CHECK(f.before && f.list_count == 2 && f.list[0].pipeline_hash == 11 && f.cache_size == sizeof(cache)
			&& !memcmp(f.cache, cache, sizeof(cache)), "the files of before read");
	CHECK(!f.file && f.before_list && f.before_cache, "copies of them");
	rdp_pipeline_files_save(&f, old, 2, f.cache, f.cache_size);
	CHECK(filestream_exists(file_path) && !filestream_exists(list_path) && !filestream_exists(cache_path),
			"nothing new: written all the same, they removed");
	rdp_pipeline_files_load(&f, dir);
	CHECK(!f.before && f.list_count == 2 && f.list[1].pipeline_hash == 12 && f.cache_size == sizeof(cache),
			"what they held in the file");
	rdp_pipeline_files_free(&f);

	/* one of them, the list, in <system> itself: moved, read */
	filestream_delete(file_path);
	in_dir(root_list, sizeof(root_list), RDP_PIPELINE_LIST_FILE, NULL);
	CHECK(pipeline_record_list_save(root_list, old, 2), "a list an earlier build left in <system>");
	rdp_pipeline_files_load(&f, dir);
	CHECK(f.before && f.list_count == 2 && !f.cache && !filestream_exists(root_list), "moved over and read");
	rdp_pipeline_files_save(&f, built, 1, NULL, 0);
	rdp_pipeline_files_load(&f, dir);
	CHECK(f.list_count == 3 && f.list[0].pipeline_hash == 13 && !filestream_exists(list_path), "merged, it removed");
	rdp_pipeline_files_free(&f);

	/* a cache of before alone */
	filestream_delete(file_path);
	filestream_write_file(cache_path, cache, sizeof(cache));
	rdp_pipeline_files_load(&f, dir);
	CHECK(f.before && !f.list && f.cache_size == sizeof(cache), "a cache of before alone");
	rdp_pipeline_files_save(&f, NULL, 0, f.cache, f.cache_size);
	CHECK(filestream_exists(file_path) && !filestream_exists(cache_path), "written, it removed");

	/* a cache of before; a list of before that is not whole is none, but
	 * of before all the same */
	filestream_delete(file_path);
	filestream_write_file(list_path, "junk", 4);
	filestream_write_file(cache_path, cache, sizeof(cache));
	rdp_pipeline_files_load(&f, dir);
	CHECK(f.before && !f.list && f.cache_size == sizeof(cache), "the cache, no list");
	rdp_pipeline_files_save(&f, NULL, 0, NULL, 0);
	CHECK(filestream_exists(file_path) && !filestream_exists(list_path) && !filestream_exists(cache_path),
			"written with the cache, both removed");

	/* where the file cannot be written: they stay, and no temporary
	 * file is left */
	filestream_delete(file_path);
	CHECK(pipeline_record_list_save(list_path, old, 2), "a list of before again");
	CHECK(path_mkdir(file_path), "a folder where the file goes");
	rdp_pipeline_files_load(&f, dir);
	CHECK(f.before && f.list_count == 2, "read");
	rdp_pipeline_files_save(&f, built, 1, cache, sizeof(cache));
	snprintf(tmp, sizeof(tmp), "%s.tmp", file_path);
	CHECK(filestream_exists(list_path) && !filestream_exists(tmp), "not written: they stay, nothing left behind");
	rmdir(file_path);
	/* nor where the temporary file cannot be made */
	CHECK(path_mkdir(tmp), "a folder where the temporary file goes");
	rdp_pipeline_files_load(&f, dir);
	rdp_pipeline_files_save(&f, built, 1, cache, sizeof(cache));
	CHECK(filestream_exists(list_path) && !filestream_exists(file_path), "not made: they stay");
	rmdir(tmp);
	filestream_delete(list_path);
}

int main(void)
{
	char cmd[700];
	snprintf(dir, sizeof(dir), "/tmp/test_rdp_pipeline_files.%d", (int)getpid());
	snprintf(cmd, sizeof(cmd), "rm -rf %s", dir);
	if (system(cmd) != 0 || !path_mkdir(dir))
		return 1;
	in_dir(file_path, sizeof(file_path), "ParallelN64", RDP_PIPELINE_FILE);
	in_dir(list_path, sizeof(list_path), "ParallelN64", RDP_PIPELINE_LIST_FILE);
	in_dir(cache_path, sizeof(cache_path), "ParallelN64", RDP_PIPELINE_CACHE_FILE);
	test_path();
	test_no_folder();
	test_round_trip(0);
	test_round_trip(1);
	test_damaged();
	test_before();
	if (system(cmd) != 0)
		return 1;

	if (failures)
	{
		fprintf(stderr, "test_rdp_pipeline_files: %u failure(s)\n", failures);
		return 1;
	}
	printf("test_rdp_pipeline_files: ok\n");
	return 0;
}
