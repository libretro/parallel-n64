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
#include "frame_context.h"

/* room for this many command buffers per queue before a list grows */
#define FRAME_SUBMISSIONS_INITIAL 16

bool granite_frame_context_init(granite_frame_context_t *frame, const granite_frame_env_t *env, unsigned index,
		const uint32_t *families, const VkSemaphore *timelines, unsigned threads)
{
	unsigned i, j;

	memset(frame, 0, sizeof(*frame));

	for (i = 0; i < GRANITE_FRAME_QUEUES; i++)
	{
		frame->cmd_pools[i] = (granite_command_pool_t *)malloc(threads * sizeof(granite_command_pool_t));
		frame->submissions[i] = (granite_command_buffer_t **)
			malloc(FRAME_SUBMISSIONS_INITIAL * sizeof(granite_command_buffer_t *));
	}

	for (i = 0; i < GRANITE_FRAME_QUEUES; i++)
	{
		if (frame->cmd_pools[i] && frame->submissions[i])
			continue;
		for (j = 0; j < GRANITE_FRAME_QUEUES; j++)
		{
			free(frame->cmd_pools[j]);
			free(frame->submissions[j]);
		}
		memset(frame, 0, sizeof(*frame));
		return false;
	}

	frame->env = env;
	frame->index = index;
	frame->threads = threads;
	granite_frame_sync_init(&frame->sync, timelines);

	for (i = 0; i < GRANITE_FRAME_QUEUES; i++)
	{
		frame->submission_capacity[i] = FRAME_SUBMISSIONS_INITIAL;
		for (j = 0; j < threads; j++)
			granite_command_pool_init(&frame->cmd_pools[i][j], env->device, env->table, families[i]);
	}
	return true;
}

void granite_frame_context_begin(granite_frame_context_t *frame)
{
	const granite_frame_env_t *env = frame->env;
	unsigned i, j;

	granite_frame_sync_wait(&frame->sync, env->device, env->table, env->timeline);

	/* This frame's GPU work is done: take what other threads released
	 * to it. */
	granite_release_queue_drain(env->releases, frame->index, &frame->garbage);
	granite_memory_release_queue_drain(env->memory_releases, frame->index, &frame->memory);

	for (i = 0; i < GRANITE_FRAME_QUEUES; i++)
		for (j = 0; j < frame->threads; j++)
			granite_command_pool_begin(&frame->cmd_pools[i][j]);

	granite_buffer_block_list_recycle(&frame->vbo_blocks, env->vbo);
	granite_buffer_block_list_recycle(&frame->ubo_blocks, env->ubo);

	granite_frame_garbage_collect(&frame->garbage, env->device, env->table, env->semaphores, env->fences);

	granite_memory_garbage_collect(&frame->memory, env->memory);
}

void granite_frame_context_trim(granite_frame_context_t *frame)
{
	unsigned i, j;
	for (i = 0; i < GRANITE_FRAME_QUEUES; i++)
		for (j = 0; j < frame->threads; j++)
			granite_command_pool_trim(&frame->cmd_pools[i][j]);
}

void granite_frame_context_free(granite_frame_context_t *frame)
{
	unsigned i, j;

	if (!frame->env)
		return;

	granite_frame_context_begin(frame);
	/* (begin gave the blocks back to their pools) */
	granite_buffer_block_list_free(&frame->vbo_blocks);
	granite_buffer_block_list_free(&frame->ubo_blocks);
	for (i = 0; i < GRANITE_FRAME_QUEUES; i++)
	{
		for (j = 0; j < frame->threads; j++)
			granite_command_pool_free(&frame->cmd_pools[i][j]);
		free(frame->cmd_pools[i]);
		free(frame->submissions[i]);
	}
	granite_frame_garbage_free(&frame->garbage);
	granite_memory_garbage_free(&frame->memory);
	memset(frame, 0, sizeof(*frame));
}

VkCommandBuffer granite_frame_context_request(granite_frame_context_t *frame, unsigned queue, unsigned thread)
{
	return granite_command_pool_request(&frame->cmd_pools[queue][thread]);
}

void granite_frame_context_submit(granite_frame_context_t *frame, unsigned queue, granite_command_buffer_t *cb)
{
	if (frame->num_submissions[queue] == frame->submission_capacity[queue])
	{
		unsigned cap = frame->submission_capacity[queue] * 2;
		granite_command_buffer_t **grown = (granite_command_buffer_t **)
			realloc(frame->submissions[queue], cap * sizeof(*grown));
		if (!grown)
			abort();
		frame->submissions[queue] = grown;
		frame->submission_capacity[queue] = cap;
	}
	granite_cb_ref(cb);
	frame->submissions[queue][frame->num_submissions[queue]++] = cb;
}

void granite_frame_context_submitted(granite_frame_context_t *frame, unsigned queue)
{
	unsigned i;
	for (i = 0; i < frame->num_submissions[queue]; i++)
		granite_cb_unref(frame->submissions[queue][i]);
	frame->num_submissions[queue] = 0;
}
