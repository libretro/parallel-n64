#ifndef VK_CACHE_H
#define VK_CACHE_H

/* GLideN64's Vulkan cache: one file a game, in the shaders folder beside
 * the GL one (GLideN64.<name>.Vulkan.cache).
 *
 * A header of three words (the magic, the keys' version, how many bytes
 * of pipeline cache), the driver's pipeline cache, then key records
 * (CombinerKeyJournal.h's), which the key journal appends to as a game
 * meets them, so a crash loses none. Host order. A file without its
 * header and pipeline cache whole is none; the records a crash cut short
 * are cut off.
 *
 * Before it, every game's keys were in one journal (GLideN64.vulkan.keys.
 * journal), with one pipeline cache beside it (.pipelines).
 *
 * C89. */

#include <stddef.h>
#include <stdint.h>
#include <streams/file_stream.h>
#include <CombinerKeyJournal.h>

#define VK_CACHE_MAGIC 0x564E4C47u /* "GLNV" */
#define VK_CACHE_VERSION 1u
#define VK_CACHE_HEAD 12

typedef struct vk_cache
{
	/* lent from the file until close */
	const void *pipelines;
	size_t pipelines_size;
	const unsigned char *records; /* the whole ones */
	size_t records_size;
	/* the file there, its header whole */
	int found;
	/* past the whole records: what a crash cut short */
	int cut;

	RFILE *file;
	void *copy;
} vk_cache;

/* The file at @path, mapped where it can be, read once where not.
 * Nothing there, or not whole: not found, nothing lent. */
void vk_cache_open(vk_cache *_c, const char *_path);

/* @_cb for each key record lent; how many */
unsigned vk_cache_keys(const vk_cache *_c, combiner_journal_key_t _cb, void *_ctx);

/* What was lent let go, and what a crash cut short cut off the file at
 * @_path, so that the journal appends to whole records */
void vk_cache_close(vk_cache *_c, const char *_path);

/* The file written, each part from where it is: the header, @_size bytes
 * of @_pipelines, @_recordsSize of @_records. Whether it was. */
int vk_cache_write(const char *_path, const void *_pipelines, size_t _size,
	const void *_records, size_t _recordsSize);

#endif
