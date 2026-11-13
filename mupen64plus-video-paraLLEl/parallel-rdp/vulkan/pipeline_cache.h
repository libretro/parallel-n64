/* Copyright (c) 2017-2022 Hans-Kristian Arntzen
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

#ifndef GRANITE_PIPELINE_CACHE_H
#define GRANITE_PIPELINE_CACHE_H

#include <stddef.h>
#include <stdint.h>
#include <boolean.h>
#include <retro_common_api.h>
#include <retro_atomic.h>
#include "volk.h"
#include "pipeline_compile.h"
#include "pipeline_record.h"

RETRO_BEGIN_DECLS

/* ---- the driver's pipeline cache, as kept on disk ----
 * The driver's data follows a header: the driver's pipeline cache UUID,
 * then a hash of the data. Data of another driver, or damaged, is not
 * handed to this one. */
#define GRANITE_PIPELINE_CACHE_HEADER_SIZE (VK_UUID_SIZE + sizeof(uint64_t))

uint64_t granite_pipeline_cache_hash(const uint8_t *data, size_t size);

/* The driver's data in @blob, with its size in @payload_size, when the
 * blob is of the driver with @uuid and intact; otherwise NULL and 0 */
const uint8_t *granite_pipeline_cache_payload(const uint8_t *uuid, const uint8_t *blob, size_t size,
		size_t *payload_size);

/* Makes the pipeline cache, from @blob where that can be used, in place
 * of the one in @cache (which may be VK_NULL_HANDLE). False, and no
 * cache, when the driver makes none. */
bool granite_pipeline_cache_init(VkDevice device, const struct VolkDeviceTable *table, const uint8_t *uuid,
		const uint8_t *blob, size_t size, VkPipelineCache *cache);

/* The size of the blob to keep for @cache; 0 when there is none */
size_t granite_pipeline_cache_blob_size(VkDevice device, const struct VolkDeviceTable *table, VkPipelineCache cache);
/* Writes that blob into @size bytes at @data. False when it does not
 * fit or the driver gives nothing. */
bool granite_pipeline_cache_blob(VkDevice device, const struct VolkDeviceTable *table, VkPipelineCache cache,
		const uint8_t *uuid, uint8_t *data, size_t size);

/* ---- the pipelines built in a session ----
 * Each pipeline kept is written down once, from whichever thread built
 * it; the list is saved for the next session to build them ahead. */
#define GRANITE_PIPELINE_RECORDS 4096

typedef struct granite_pipeline_record_table
{
	/* GRANITE_PIPELINE_RECORDS of them, or NULL */
	pipeline_record_t *records;
	/* slots handed out, and slots filled in */
	retro_atomic_int_t claimed;
	retro_atomic_int_t written;
} granite_pipeline_record_table_t;

/* False when there is no memory for the table: nothing is recorded */
bool granite_pipeline_record_table_init(granite_pipeline_record_table_t *table);
void granite_pipeline_record_table_free(granite_pipeline_record_table_t *table);

/* A record to fill in, from any thread; NULL when the table is full or
 * was never made. Followed by granite_pipeline_record_table_commit. */
pipeline_record_t *granite_pipeline_record_table_claim(granite_pipeline_record_table_t *table);
void granite_pipeline_record_table_commit(granite_pipeline_record_table_t *table);

/* The records written, in @records; returns how many. For when no
 * thread is building pipelines any more. */
size_t granite_pipeline_record_table_get(granite_pipeline_record_table_t *table, const pipeline_record_t **records);

/* What a record says of the pipeline's program and render pass */
typedef struct granite_pipeline_record_ids
{
	uint64_t program_hash;
	/* compute; or vertex, fragment. 0 for a shader the program lacks. */
	uint64_t shader_hashes[2];
	/* graphics only */
	uint64_t render_pass_hash;
	const pipeline_record_pass_t *pass;
} granite_pipeline_record_ids_t;

/* Writes the pipeline compiled from @compile down in @record. @kind:
 * PIPELINE_RECORD_COMPUTE or PIPELINE_RECORD_GRAPHICS. */
void granite_pipeline_record_fill(pipeline_record_t *record, uint32_t kind,
		const granite_pipeline_compile_t *compile, const granite_pipeline_record_ids_t *ids);

/* What @record's pipeline is compiled from, in @compile, to be built
 * into @cache. The hash is left 0: it is the caller's to compute, with
 * the program and pass it found, and compare to the record's. */
void granite_pipeline_record_apply(const pipeline_record_t *record, granite_pipeline_compile_t *compile,
		VkPipelineCache cache);

RETRO_END_DECLS

#endif
