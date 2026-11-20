/* test_gliden64_vk_cache: GLideN64's Vulkan cache file
 * (src/Graphics/VulkanContext/vk_cache.c) on real files.
 *
 * Written: the header, the pipeline cache and the key records, each from
 * where it is; not written where it cannot be opened. Read: mapped where
 * the VFS maps it, read once where not; the pipeline cache and the whole
 * key records lent from it, the keys handed over as they are. A file of
 * another magic or version, too short, or whose pipeline cache runs past
 * its end is none. Records a crash cut short, or what is not a key
 * record, end the keys; closed with the file's path, the file is cut to
 * its whole records, and appended to, read whole again. Closed: nothing
 * lent.
 *
 * The module is included here, its map through test_get_mapped_ptr, to
 * read without the map too. C89. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <streams/file_stream.h>
#include "vk_cache.h"

const uint8_t *test_get_mapped_ptr(RFILE *stream, int64_t *len);
void *test_malloc(size_t n);
#define filestream_get_mapped_ptr test_get_mapped_ptr
#define malloc test_malloc
#include "vk_cache.c"
#undef filestream_get_mapped_ptr
#undef malloc

static int s_failmalloc;
void *test_malloc(size_t n)
{
	return s_failmalloc ? NULL : malloc(n);
}

static int failures;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)

#define PATH    "test_gliden64_vk_cache.cache"
#define NOWHERE "no_such_dir_here/test.cache"
#define RECORD  COMBINER_JOURNAL_RECORD_SIZE

static int s_nomap;
static int s_mapped;
const uint8_t *test_get_mapped_ptr(RFILE *stream, int64_t *len)
{
	const uint8_t *p;
	if (s_nomap) {
		*len = 0;
		return NULL;
	}
	p = filestream_get_mapped_ptr(stream, len);
	s_mapped += p != NULL;
	return p;
}

static void put(const void *data, size_t size)
{
	CHECK(filestream_write_file(PATH, data, (int64_t)size));
}

static int64_t size_of(const char *path)
{
	void *data = NULL;
	int64_t size = -1;
	if (!filestream_exists(path) || !filestream_read_file(path, &data, &size))
		size = -1;
	free(data);
	return size;
}

static uint64_t s_keys[8];
static unsigned s_hwl[8], s_n;
static void *s_ctx;
static void key(void *ctx, uint64_t k, unsigned hwl)
{
	if (s_n < 8) {
		s_keys[s_n] = k;
		s_hwl[s_n] = hwl;
	}
	s_n++;
	s_ctx = ctx;
}

static void record(unsigned char *r, uint64_t k, unsigned hwl)
{
	r[0] = COMBINER_JOURNAL_RECORD_KEY;
	r[1] = (unsigned char)hwl;
	memcpy(r + 2, &k, sizeof(k));
}

static void header(unsigned char *f, uint32_t magic, uint32_t version, uint32_t pipes)
{
	uint32_t h[3];
	h[0] = magic;
	h[1] = version;
	h[2] = pipes;
	memcpy(f, h, sizeof(h));
}

/* none: nothing lent */
static int none(const vk_cache *c)
{
	return !c->found && !c->pipelines && !c->pipelines_size && !c->records && !c->records_size && !c->cut &&
		!c->file && !c->copy;
}

int main(void)
{
	static unsigned char pipes[100];
	static unsigned char recs[3 * RECORD];
	static unsigned char file[VK_CACHE_HEAD + sizeof(pipes) + sizeof(recs) + 8];
	const uint64_t k0 = ((uint64_t)0x01234567u << 32) | 0x89abcdefu;
	const uint64_t k1 = ((uint64_t)0xfedcba98u << 32) | 0x76543210u;
	const uint64_t k2 = ((uint64_t)0x5a5a5a5au << 32) | 1u;
	vk_cache c;
	unsigned i;
	int pass;

	for (i = 0; i < sizeof(pipes); i++)
		pipes[i] = (unsigned char)(200 - i);
	record(recs, k0, 0);
	record(recs + RECORD, k1, 1);
	/* the hwl byte's four bits alone */
	record(recs + 2 * RECORD, k2, 0xf5);
	filestream_delete(PATH);

	/* nothing there, or no path */
	vk_cache_open(&c, PATH);
	CHECK(none(&c));
	vk_cache_close(&c, PATH);
	CHECK(size_of(PATH) == -1);
	memset(&c, 0xff, sizeof(c));
	vk_cache_open(&c, NULL);
	CHECK(none(&c));
	vk_cache_open(&c, "");
	CHECK(none(&c));

	for (pass = 0; pass < 2; pass++) {
		s_nomap = pass;
		s_mapped = 0;
		/* written, then read back: lent, whole */
		CHECK(vk_cache_write(PATH, pipes, sizeof(pipes), recs, sizeof(recs)));
		CHECK(size_of(PATH) == (int64_t)(VK_CACHE_HEAD + sizeof(pipes) + sizeof(recs)));
		vk_cache_open(&c, PATH);
		CHECK(c.found && !c.cut);
		CHECK(c.pipelines_size == sizeof(pipes) && c.pipelines && !memcmp(c.pipelines, pipes, sizeof(pipes)));
		CHECK(c.records_size == sizeof(recs) && c.records && !memcmp(c.records, recs, sizeof(recs)));
		CHECK(pass == 0 || c.copy != NULL);
		s_n = 0;
		CHECK(vk_cache_keys(&c, key, &c) == 3 && s_n == 3 && s_ctx == (void *)&c);
		CHECK(s_keys[0] == k0 && s_hwl[0] == 0 && s_keys[1] == k1 && s_hwl[1] == 1 && s_keys[2] == k2 && s_hwl[2] == 5);
		vk_cache_close(&c, PATH);
		CHECK(none(&c));
		CHECK(size_of(PATH) == (int64_t)(VK_CACHE_HEAD + sizeof(pipes) + sizeof(recs)));
		CHECK(pass == 1 || s_mapped == 1);
		CHECK(pass == 0 || s_mapped == 0);
	}
	s_nomap = 0;

	/* the header as written */
	{
		void *data = NULL;
		int64_t size = 0;
		uint32_t h[3];
		CHECK(filestream_read_file(PATH, &data, &size) && data);
		memcpy(h, data, sizeof(h));
		CHECK(h[0] == VK_CACHE_MAGIC && h[1] == VK_CACHE_VERSION && h[2] == sizeof(pipes));
		CHECK(size == (int64_t)(VK_CACHE_HEAD + sizeof(pipes) + sizeof(recs)) &&
			!memcmp((char *)data + VK_CACHE_HEAD, pipes, sizeof(pipes)) &&
			!memcmp((char *)data + VK_CACHE_HEAD + sizeof(pipes), recs, sizeof(recs)));
		free(data);
	}

	/* not mapped, and no memory to read it into: none */
	s_nomap = 1;
	s_failmalloc = 1;
	vk_cache_open(&c, PATH);
	CHECK(none(&c));
	s_failmalloc = 0;
	s_nomap = 0;

	/* no pipeline cache, no keys: the header alone */
	CHECK(vk_cache_write(PATH, NULL, 50, NULL, 30));
	CHECK(size_of(PATH) == VK_CACHE_HEAD);
	vk_cache_open(&c, PATH);
	CHECK(c.found && !c.cut && !c.pipelines && !c.pipelines_size && c.records && !c.records_size);
	s_n = 0;
	CHECK(vk_cache_keys(&c, key, NULL) == 0 && s_n == 0);
	vk_cache_close(&c, PATH);
	CHECK(size_of(PATH) == VK_CACHE_HEAD);
	/* keys alone */
	CHECK(vk_cache_write(PATH, pipes, 0, recs, RECORD));
	vk_cache_open(&c, PATH);
	CHECK(c.found && !c.pipelines && c.records_size == RECORD && !memcmp(c.records, recs, RECORD));
	vk_cache_close(&c, NULL);

	/* a byte of each */
	CHECK(vk_cache_write(PATH, pipes, 1, recs, 1));
	CHECK(size_of(PATH) == VK_CACHE_HEAD + 2);
	vk_cache_open(&c, PATH);
	CHECK(c.found && c.cut && c.pipelines_size == 1 && *(const unsigned char *)c.pipelines == pipes[0] && !c.records_size);
	vk_cache_close(&c, NULL);
#ifdef __linux__
	/* nowhere to put it: what the close says counts */
	CHECK(!vk_cache_write("/dev/full", pipes, sizeof(pipes), recs, sizeof(recs)));
#endif

	/* not written where it cannot be opened */
	CHECK(!vk_cache_write(NOWHERE, pipes, sizeof(pipes), recs, sizeof(recs)));
	CHECK(!vk_cache_write(NULL, pipes, sizeof(pipes), recs, sizeof(recs)));
	CHECK(!vk_cache_write("", pipes, sizeof(pipes), recs, sizeof(recs)));
	CHECK(size_of(NOWHERE) == -1);

	/* another magic, another version, too short, a pipeline cache past
	 * the end: none */
	memset(file, 0, sizeof(file));
	header(file, VK_CACHE_MAGIC ^ 1, VK_CACHE_VERSION, 4);
	put(file, VK_CACHE_HEAD + 4);
	vk_cache_open(&c, PATH);
	CHECK(none(&c));
	header(file, VK_CACHE_MAGIC, VK_CACHE_VERSION + 1, 4);
	put(file, VK_CACHE_HEAD + 4);
	vk_cache_open(&c, PATH);
	CHECK(none(&c));
	header(file, VK_CACHE_MAGIC, VK_CACHE_VERSION, 0);
	put(file, VK_CACHE_HEAD - 1);
	vk_cache_open(&c, PATH);
	CHECK(none(&c));
	header(file, VK_CACHE_MAGIC, VK_CACHE_VERSION, 5);
	put(file, VK_CACHE_HEAD + 4);
	vk_cache_open(&c, PATH);
	CHECK(none(&c));
	vk_cache_close(&c, PATH);
	CHECK(size_of(PATH) == VK_CACHE_HEAD + 4);
	/* one that ends where the file does */
	header(file, VK_CACHE_MAGIC, VK_CACHE_VERSION, 4);
	put(file, VK_CACHE_HEAD + 4);
	vk_cache_open(&c, PATH);
	CHECK(c.found && c.pipelines_size == 4 && c.pipelines == (const void *)((const unsigned char *)c.records - 4) &&
		!c.records_size && !c.cut);
	vk_cache_close(&c, PATH);

	/* a crash's: a record cut short, ending the keys; the file cut to the
	 * whole ones where closed with its path, appended to after */
	for (pass = 0; pass < 2; pass++) {
		const size_t whole = VK_CACHE_HEAD + 4 + 2 * RECORD;
		header(file, VK_CACHE_MAGIC, VK_CACHE_VERSION, 4);
		memcpy(file + VK_CACHE_HEAD + 4, recs, 2 * RECORD + 7);
		put(file, whole + 7);
		vk_cache_open(&c, PATH);
		CHECK(c.found && c.cut && c.records_size == 2 * RECORD);
		s_n = 0;
		CHECK(vk_cache_keys(&c, key, NULL) == 2 && s_keys[1] == k1);
		vk_cache_close(&c, pass ? NULL : PATH);
		CHECK(none(&c));
		CHECK(size_of(PATH) == (int64_t)(pass ? whole + 7 : whole));
	}
	/* what is not a key record ends them too */
	header(file, VK_CACHE_MAGIC, VK_CACHE_VERSION, 4);
	memcpy(file + VK_CACHE_HEAD + 4, recs, 3 * RECORD);
	file[VK_CACHE_HEAD + 4 + RECORD] = 'B';
	put(file, VK_CACHE_HEAD + 4 + 3 * RECORD);
	vk_cache_open(&c, PATH);
	CHECK(c.found && c.cut && c.records_size == RECORD);
	vk_cache_close(&c, PATH);
	CHECK(size_of(PATH) == VK_CACHE_HEAD + 4 + RECORD);
	/* then appended to, whole again */
	{
		RFILE *f = filestream_open(PATH, RETRO_VFS_FILE_ACCESS_READ_WRITE | RETRO_VFS_FILE_ACCESS_UPDATE_EXISTING,
			RETRO_VFS_FILE_ACCESS_HINT_NONE);
		CHECK(f != NULL);
		if (f != NULL) {
			filestream_seek(f, 0, RETRO_VFS_SEEK_POSITION_END);
			CHECK(filestream_write(f, recs + 2 * RECORD, RECORD) == RECORD);
			filestream_close(f);
		}
		vk_cache_open(&c, PATH);
		CHECK(c.found && !c.cut && c.records_size == 2 * RECORD);
		s_n = 0;
		vk_cache_keys(&c, key, NULL);
		CHECK(s_n == 2 && s_keys[0] == k0 && s_keys[1] == k2);
		vk_cache_close(&c, PATH);
	}

	filestream_delete(PATH);
	if (failures) {
		printf("test_gliden64_vk_cache: %d failures\n", failures);
		return 1;
	}
	printf("test_gliden64_vk_cache: ok\n");
	return 0;
}
