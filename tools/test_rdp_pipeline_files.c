/* test_rdp_pipeline_files -- the frontend's pipeline cache and list of
 * built pipelines kept from one session to the next
 * (mupen64plus-video-paraLLEl/rdp_pipeline_files.c), in a folder of its
 * own.
 *
 * - <system>/ParallelN64 is made; a file an earlier build left in
 *   <system> moves there, unless one is there already; no system folder,
 *   no files;
 * - nothing saved: nothing loaded;
 * - saved, then loaded: the cache's bytes and the list, this session's
 *   first, each pipeline once;
 * - the same again is not written (the files are left alone), another
 *   cache or list is.
 *
 * Built by `make check-core` (top-level Makefile). C89. Exit status 0 on
 * pass.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <streams/file_stream.h>
#include <file/file_path.h>

#include "rdp_pipeline_files.h"

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
		void *buf = NULL;
		int64_t len = 0;
		CHECK(filestream_read_file(want, &buf, &len) && len == 3 && !memcmp(buf, "old", 3), "which stays");
		free(buf);
	}
}

static void test_round_trip(void)
{
	rdp_pipeline_files_t f;
	pipeline_record_t built[4], more[2];
	char cache_path[1024], list_path[1024];
	static const char cache[] = "the driver's cache";
	static const char other[] = "another driver's cache";
	void *blob;
	size_t size;

	memset(&f, 0, sizeof(f));
	in_dir(cache_path, sizeof(cache_path), "ParallelN64", RDP_PIPELINE_CACHE_FILE);
	in_dir(list_path, sizeof(list_path), "ParallelN64", RDP_PIPELINE_LIST_FILE);

	rdp_pipeline_files_load(&f, dir, &blob, &size);
	CHECK(!blob && !size && !f.list && !f.list_count && f.cache_size == -1, "nothing saved, nothing loaded");

	record(&built[0], 1);
	record(&built[1], 2);
	record(&built[2], 1);
	record(&built[3], 3);
	rdp_pipeline_files_save(&f, built, 4, cache, sizeof(cache));
	CHECK(filestream_exists(cache_path) && filestream_exists(list_path), "both saved");

	rdp_pipeline_files_load(&f, dir, &blob, &size);
	CHECK(blob && size == sizeof(cache) && !memcmp(blob, cache, size), "the cache's bytes");
	CHECK(f.list_count == 3 && f.list[0].pipeline_hash == 1 && f.list[1].pipeline_hash == 2
			&& f.list[2].pipeline_hash == 3, "the list, each pipeline once");

	/* the same again: the files are left alone */
	remove(cache_path);
	remove(list_path);
	rdp_pipeline_files_save(&f, built, 4, blob, size);
	CHECK(!filestream_exists(cache_path) && !filestream_exists(list_path), "the same again is not written");
	CHECK(!f.list && !f.list_count, "what was loaded is freed");
	free(blob);

	/* saved again, then another session's */
	rdp_pipeline_files_save(&f, built, 4, cache, sizeof(cache));
	rdp_pipeline_files_load(&f, dir, &blob, &size);
	free(blob);
	record(&more[0], 7);
	record(&more[1], 2);
	rdp_pipeline_files_save(&f, more, 2, other, sizeof(other));
	rdp_pipeline_files_load(&f, dir, &blob, &size);
	CHECK(blob && size == sizeof(other) && !memcmp(blob, other, size), "another cache is written");
	CHECK(f.list_count == 4 && f.list[0].pipeline_hash == 7 && f.list[1].pipeline_hash == 2
			&& f.list[2].pipeline_hash == 1 && f.list[3].pipeline_hash == 3,
			"another list: this session's first, then the last's not in it");
	free(blob);

	/* the same pipelines in another order, a cache of the same size */
	{
		pipeline_record_t again[4];
		static const char same_size[] = "Another driver's cache";
		record(&again[0], 3);
		record(&again[1], 1);
		record(&again[2], 2);
		record(&again[3], 7);
		rdp_pipeline_files_save(&f, more, 2, other, sizeof(other));
		rdp_pipeline_files_load(&f, dir, &blob, &size);
		free(blob);
		remove(cache_path);
		remove(list_path);
		rdp_pipeline_files_save(&f, again, 4, same_size, sizeof(same_size));
		CHECK(filestream_exists(list_path), "a list of as many in another order is written");
		CHECK(filestream_exists(cache_path), "a cache of the same size, other bytes, is written");
		rdp_pipeline_files_load(&f, dir, &blob, &size);
		CHECK(f.list_count == 4 && f.list[0].pipeline_hash == 3 && f.list[3].pipeline_hash == 7, "in its order");
		remove(cache_path);
		rdp_pipeline_files_save(&f, again, 4, blob, 0);
		CHECK(!filestream_exists(cache_path), "an empty cache is not written");
		free(blob);
		rdp_pipeline_files_load(&f, dir, &blob, &size);
		free(blob);
		rdp_pipeline_files_save(&f, more, 2, other, sizeof(other));
	}

	/* no cache this time: the old one stays */
	rdp_pipeline_files_save(&f, more, 2, NULL, 0);
	CHECK(filestream_exists(cache_path), "without a cache the saved one stays");
	rdp_pipeline_files_free(&f);
}

int main(void)
{
	char cmd[700];
	snprintf(dir, sizeof(dir), "/tmp/test_rdp_pipeline_files.%d", (int)getpid());
	snprintf(cmd, sizeof(cmd), "rm -rf %s", dir);
	if (system(cmd) != 0 || !path_mkdir(dir))
		return 1;
	test_path();
	test_round_trip();
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
