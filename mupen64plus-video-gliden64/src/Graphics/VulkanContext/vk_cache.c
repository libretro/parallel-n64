/* GLideN64's Vulkan cache, one file every game shares (vk_cache.h). C89. */
#include <stdlib.h>
#include <string.h>

#include "vk_cache.h"

void vk_cache_open(vk_cache *_c, const char *_path)
{
	const unsigned char *data = NULL;
	int64_t size = 0;

	memset(_c, 0, sizeof(*_c));
	if (_path == NULL || *_path == '\0')
		return;
	/* one open; the file lent where it can be mapped, read once where not */
	if ((_c->file = filestream_open(_path, RETRO_VFS_FILE_ACCESS_READ,
			RETRO_VFS_FILE_ACCESS_HINT_FREQUENT_ACCESS)) != NULL) {
		data = (const unsigned char *)filestream_get_mapped_ptr(_c->file, &size);
		if (data == NULL && (size = filestream_get_size(_c->file)) > 0 &&
				(_c->copy = malloc((size_t)size)) != NULL &&
				filestream_read(_c->file, _c->copy, size) == size)
			data = (const unsigned char *)_c->copy;
	}
	if (data != NULL && size >= VK_CACHE_HEAD) {
		uint32_t h[3];
		memcpy(h, data, sizeof(h));
		if (h[0] == VK_CACHE_MAGIC && h[1] == VK_CACHE_VERSION &&
				(uint64_t)h[2] <= (uint64_t)size - VK_CACHE_HEAD) {
			const size_t at = VK_CACHE_HEAD + (size_t)h[2];
			size_t end = at;
			while (end + COMBINER_JOURNAL_RECORD_SIZE <= (size_t)size &&
					data[end] == COMBINER_JOURNAL_RECORD_KEY)
				end += COMBINER_JOURNAL_RECORD_SIZE;
			_c->found = 1;
			_c->pipelines = h[2] != 0 ? data + VK_CACHE_HEAD : NULL;
			_c->pipelines_size = h[2];
			_c->records = data + at;
			_c->records_size = end - at;
			_c->cut = end < (size_t)size;
			return;
		}
	}
	/* not there, or not whole */
	vk_cache_close(_c, NULL);
}

unsigned vk_cache_keys(const vk_cache *_c, combiner_journal_key_t _cb, void *_ctx)
{
	size_t at;
	unsigned n = 0;
	for (at = 0; at + COMBINER_JOURNAL_RECORD_SIZE <= _c->records_size; at += COMBINER_JOURNAL_RECORD_SIZE) {
		const unsigned char *r = _c->records + at;
		uint64_t key;
		memcpy(&key, r + 2, sizeof(key));
		_cb(_ctx, key, r[1] & 15u);
		n++;
	}
	return n;
}

void vk_cache_close(vk_cache *_c, const char *_path)
{
	const int64_t whole = _c->cut ?
		(int64_t)(VK_CACHE_HEAD + _c->pipelines_size + _c->records_size) : 0;
	if (_c->file != NULL)
		filestream_close(_c->file);
	free(_c->copy);
	memset(_c, 0, sizeof(*_c));
	if (whole != 0 && _path != NULL && *_path != '\0') {
		RFILE *f = filestream_open(_path, RETRO_VFS_FILE_ACCESS_READ_WRITE |
			RETRO_VFS_FILE_ACCESS_UPDATE_EXISTING, RETRO_VFS_FILE_ACCESS_HINT_NONE);
		if (f != NULL) {
			filestream_truncate(f, whole);
			filestream_close(f);
		}
	}
}

int vk_cache_write(const char *_path, const void *_pipelines, size_t _size,
	const void *_records, size_t _recordsSize)
{
	uint32_t h[3];
	RFILE *f;
	int ok;

	if (_pipelines == NULL)
		_size = 0;
	if (_records == NULL)
		_recordsSize = 0;
	h[0] = VK_CACHE_MAGIC;
	h[1] = VK_CACHE_VERSION;
	h[2] = (uint32_t)_size;
	if ((size_t)h[2] != _size || _path == NULL || *_path == '\0' ||
			(f = filestream_open(_path, RETRO_VFS_FILE_ACCESS_WRITE, RETRO_VFS_FILE_ACCESS_HINT_NONE)) == NULL)
		return 0;
	/* each part from where it is */
	ok = filestream_write(f, h, sizeof(h)) == (int64_t)sizeof(h) &&
		(_size == 0 || filestream_write(f, _pipelines, (int64_t)_size) == (int64_t)_size) &&
		(_recordsSize == 0 || filestream_write(f, _records, (int64_t)_recordsSize) == (int64_t)_recordsSize);
	ok = filestream_close(f) == 0 && ok;
	return ok;
}
