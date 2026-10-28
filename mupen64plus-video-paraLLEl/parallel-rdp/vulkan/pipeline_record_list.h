/* A file of pipeline records: what a device built, for the next one to
 * build ahead. Host byte order (the file is the machine's own, like the
 * driver's pipeline cache beside it). C89. */
#ifndef VULKAN_PIPELINE_RECORD_LIST_H
#define VULKAN_PIPELINE_RECORD_LIST_H

#include <stddef.h>
#include <boolean.h>

#include "pipeline_record.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PIPELINE_RECORD_LIST_VERSION 1u
/* no list grows past this; what goes is the oldest */
#define PIPELINE_RECORD_LIST_MAX     8192u

/* The records in the file at @path, in a buffer the caller frees, and
 * their number in @count; NULL with @count 0 when there is no file, or
 * one that is not a whole, unchanged list of this version. */
pipeline_record_t *pipeline_record_list_load(const char *path, size_t *count);

/* Writes @count records to @path, replacing it whole or not at all. */
bool pipeline_record_list_save(const char *path,
      const pipeline_record_t *records, size_t count);

/* Drops the records of @records whose pipeline was already listed earlier
 * in it, keeping the first of each and the order, then drops from the end
 * what is past @cap. Returns how many are left. */
size_t pipeline_record_list_unique(pipeline_record_t *records,
      size_t count, size_t cap);

#ifdef __cplusplus
}
#endif

#endif
