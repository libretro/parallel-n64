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

#include <stdlib.h>
#include <string.h>
#include "pipeline_cache.h"
#include "logging.h"

typedef char granite_record_attachments_check[PIPELINE_RECORD_SPEC_CONSTANTS == GRANITE_NUM_TOTAL_SPEC_CONSTANTS ? 1 : -1];
typedef char granite_record_attribs_check[PIPELINE_RECORD_VERTEX_ATTRIBS == GRANITE_NUM_VERTEX_ATTRIBS ? 1 : -1];
typedef char granite_record_buffers_check[PIPELINE_RECORD_VERTEX_BUFFERS == GRANITE_NUM_VERTEX_BUFFERS ? 1 : -1];

#define HASH_PRIME (((uint64_t)0x100 << 32) | 0x000001b3u)
#define HASH_SEED  (((uint64_t)0xcbf29ce4u << 32) | 0x84222325u)

uint64_t granite_pipeline_cache_hash(const uint8_t *data, size_t size)
{
	uint64_t h = HASH_SEED;
	size_t i;

	for (i = 0; i < size; i++)
		h = (h * HASH_PRIME) ^ data[i];
	return h;
}

const uint8_t *granite_pipeline_cache_payload(const uint8_t *uuid, const uint8_t *blob, size_t size,
		size_t *payload_size)
{
	const uint8_t *payload;
	uint64_t stored;

	*payload_size = 0;
	if (!blob || size < GRANITE_PIPELINE_CACHE_HEADER_SIZE)
		return NULL;
	/* another driver's, or another version of this one */
	if (memcmp(blob, uuid, VK_UUID_SIZE) != 0)
		return NULL;

	memcpy(&stored, blob + VK_UUID_SIZE, sizeof(stored));
	payload = blob + GRANITE_PIPELINE_CACHE_HEADER_SIZE;
	size -= GRANITE_PIPELINE_CACHE_HEADER_SIZE;
	if (granite_pipeline_cache_hash(payload, size) != stored)
	{
		LOGW("Pipeline cache is corrupt, creating a fresh cache.\n");
		return NULL;
	}
	*payload_size = size;
	return payload;
}

bool granite_pipeline_cache_init(VkDevice device, const struct VolkDeviceTable *table, const uint8_t *uuid,
		const uint8_t *blob, size_t size, VkPipelineCache *cache)
{
	VkPipelineCacheCreateInfo info;

	memset(&info, 0, sizeof(info));
	info.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
	info.pInitialData = granite_pipeline_cache_payload(uuid, blob, size, &info.initialDataSize);

	if (*cache != VK_NULL_HANDLE)
		table->vkDestroyPipelineCache(device, *cache, NULL);
	*cache = VK_NULL_HANDLE;
	if (table->vkCreatePipelineCache(device, &info, NULL, cache) != VK_SUCCESS)
	{
		*cache = VK_NULL_HANDLE;
		return false;
	}
	return true;
}

size_t granite_pipeline_cache_blob_size(VkDevice device, const struct VolkDeviceTable *table, VkPipelineCache cache)
{
	size_t size = 0;

	if (cache == VK_NULL_HANDLE)
		return 0;
	if (table->vkGetPipelineCacheData(device, cache, &size, NULL) != VK_SUCCESS)
	{
		LOGE("Failed to get pipeline cache data.\n");
		return 0;
	}
	return size + GRANITE_PIPELINE_CACHE_HEADER_SIZE;
}

bool granite_pipeline_cache_blob(VkDevice device, const struct VolkDeviceTable *table, VkPipelineCache cache,
		const uint8_t *uuid, uint8_t *data, size_t size)
{
	uint8_t *payload = data + GRANITE_PIPELINE_CACHE_HEADER_SIZE;
	uint64_t hash;

	if (cache == VK_NULL_HANDLE || size < GRANITE_PIPELINE_CACHE_HEADER_SIZE)
		return false;

	size -= GRANITE_PIPELINE_CACHE_HEADER_SIZE;
	if (table->vkGetPipelineCacheData(device, cache, &size, payload) != VK_SUCCESS)
	{
		LOGE("Failed to get pipeline cache data.\n");
		return false;
	}

	/* (of what the driver wrote, which may be less than there was room for) */
	hash = granite_pipeline_cache_hash(payload, size);
	memcpy(data, uuid, VK_UUID_SIZE);
	memcpy(data + VK_UUID_SIZE, &hash, sizeof(hash));
	return true;
}

bool granite_pipeline_record_table_init(granite_pipeline_record_table_t *table)
{
	retro_atomic_int_init(&table->claimed, 0);
	retro_atomic_int_init(&table->written, 0);
	table->records = (pipeline_record_t *)calloc(GRANITE_PIPELINE_RECORDS, sizeof(*table->records));
	return table->records != NULL;
}

void granite_pipeline_record_table_free(granite_pipeline_record_table_t *table)
{
	free(table->records);
	table->records = NULL;
}

pipeline_record_t *granite_pipeline_record_table_claim(granite_pipeline_record_table_t *table)
{
	int slot;

	if (!table->records)
		return NULL;
	slot = retro_atomic_fetch_add_int(&table->claimed, 1);
	if (slot < 0 || slot >= GRANITE_PIPELINE_RECORDS)
		return NULL;
	return &table->records[slot];
}

void granite_pipeline_record_table_commit(granite_pipeline_record_table_t *table)
{
	retro_atomic_fetch_add_int(&table->written, 1);
}

size_t granite_pipeline_record_table_get(granite_pipeline_record_table_t *table, const pipeline_record_t **records)
{
	int written = retro_atomic_load_acquire_int(&table->written);

	*records = table->records;
	if (!table->records || written <= 0)
		return 0;
	/* (only a claimed record is committed: never more than there are) */
	return (size_t)written;
}

void granite_pipeline_record_fill(pipeline_record_t *record, uint32_t kind,
		const granite_pipeline_compile_t *compile, const granite_pipeline_record_ids_t *ids)
{
	unsigned i;

	record->kind = kind;
	record->pipeline_hash = compile->hash;
	record->program_hash = ids->program_hash;
	record->shader_hashes[0] = ids->shader_hashes[0];
	if (kind != PIPELINE_RECORD_COMPUTE)
		record->shader_hashes[1] = ids->shader_hashes[1];
	for (i = 0; i < 4; i++)
		record->static_words[i] = compile->static_state.words[i];
	for (i = 0; i < GRANITE_NUM_TOTAL_SPEC_CONSTANTS; i++)
		record->spec_constants[i] = compile->potential_static_state.spec_constants[i];
	record->spec_masks = compile->potential_static_state.spec_constant_mask |
		((uint32_t)compile->potential_static_state.internal_spec_constant_mask << 8);
	record->subgroup_size_tag = compile->subgroup_size_tag;

	if (kind != PIPELINE_RECORD_GRAPHICS)
		return;

	record->render_pass_hash = ids->render_pass_hash;
	record->subpass_index = compile->subpass_index;
	memcpy(record->blend_constants, compile->potential_static_state.blend_constants, sizeof(record->blend_constants));
	for (i = 0; i < GRANITE_NUM_VERTEX_ATTRIBS; i++)
	{
		record->attribs[i][0] = compile->attribs[i].binding;
		record->attribs[i][1] = (uint32_t)compile->attribs[i].format;
		record->attribs[i][2] = compile->attribs[i].offset;
	}
	for (i = 0; i < GRANITE_NUM_VERTEX_BUFFERS; i++)
	{
		record->strides[i] = (uint32_t)compile->strides[i];
		record->input_rates[i] = (uint32_t)compile->input_rates[i];
	}
	record->pass = *ids->pass;
}

void granite_pipeline_record_apply(const pipeline_record_t *record, granite_pipeline_compile_t *compile,
		VkPipelineCache cache)
{
	unsigned i;

	memset(compile, 0, sizeof(*compile));
	for (i = 0; i < 4; i++)
		compile->static_state.words[i] = record->static_words[i];
	for (i = 0; i < GRANITE_NUM_TOTAL_SPEC_CONSTANTS; i++)
		compile->potential_static_state.spec_constants[i] = record->spec_constants[i];
	compile->potential_static_state.spec_constant_mask = (uint8_t)(record->spec_masks & 0xff);
	compile->potential_static_state.internal_spec_constant_mask = (uint8_t)((record->spec_masks >> 8) & 0xff);
	compile->subgroup_size_tag = record->subgroup_size_tag;
	compile->cache = cache;

	if (record->kind != PIPELINE_RECORD_GRAPHICS)
		return;

	compile->subpass_index = record->subpass_index;
	memcpy(compile->potential_static_state.blend_constants, record->blend_constants,
			sizeof(compile->potential_static_state.blend_constants));
	for (i = 0; i < GRANITE_NUM_VERTEX_ATTRIBS; i++)
	{
		compile->attribs[i].binding = record->attribs[i][0];
		compile->attribs[i].format = (VkFormat)record->attribs[i][1];
		compile->attribs[i].offset = record->attribs[i][2];
	}
	for (i = 0; i < GRANITE_NUM_VERTEX_BUFFERS; i++)
	{
		compile->strides[i] = record->strides[i];
		compile->input_rates[i] = (VkVertexInputRate)record->input_rates[i];
	}
}
