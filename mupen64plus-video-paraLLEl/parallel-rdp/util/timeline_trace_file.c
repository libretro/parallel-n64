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

#include <stdio.h>
#include <stdlib.h>
#include <compat/strl.h>
#include <queues/mpsc_stack.h>
#include "timeline_trace_file.h"
#include "logging.h"
#include "thread_name.h"
#include "timer.h"
#include "rdp_worker.h"

/* The link is first, so the I/O thread takes an event straight off the
 * worker queue. Each is its own allocation, freed once written. */
typedef struct granite_trace_event
{
	mpsc_stack_node_t node;
	char desc[256];
	char tid[32];
	uint32_t pid;
	uint64_t start_ns;
	uint64_t end_ns;
} granite_trace_event_t;

struct granite_trace_file
{
	rdp_worker_t *worker;
	FILE *file;
	uint64_t base_ts;
};

static void trace_file_write(void *user, mpsc_stack_node_t *item)
{
	granite_trace_file_t *self = (granite_trace_file_t *)user;
	granite_trace_event_t *e = (granite_trace_event_t *)item;
	double start_us = (double)(int64_t)(e->start_ns - self->base_ts) * 1e-3;
	double end_us = (double)(int64_t)(e->end_ns - self->base_ts) * 1e-3;

	if (self->file && start_us <= end_us)
	{
		fprintf(self->file, "{ \"name\": \"%s\", \"ph\": \"B\", \"tid\": \"%s\", \"pid\": \"%u\", \"ts\": %f },\n",
				e->desc, e->tid, (unsigned)e->pid, start_us);
		fprintf(self->file, "{ \"name\": \"%s\", \"ph\": \"E\", \"tid\": \"%s\", \"pid\": \"%u\", \"ts\": %f },\n",
				e->desc, e->tid, (unsigned)e->pid, end_us);
	}

	free(e);
}

static void trace_file_name_io_thread(void *user)
{
	(void)user;
	granite_set_thread_name("json-trace-io");
}

granite_trace_file_t *granite_trace_file_new(const char *path)
{
	granite_trace_file_t *self = (granite_trace_file_t *)calloc(1, sizeof(*self));
	if (!self)
		return NULL;

	self->file = fopen(path, "w");
	if (!self->file)
		LOGE("Failed to open file: %s.\n", path);
	else
		fputs("[\n", self->file);

	self->base_ts = (uint64_t)granite_time_nsecs();
	self->worker = rdp_worker_new(trace_file_name_io_thread, trace_file_write, self);
	if (!self->worker)
	{
		if (self->file)
			fclose(self->file);
		free(self);
		return NULL;
	}
	return self;
}

void granite_trace_file_free(granite_trace_file_t *self)
{
	if (!self)
		return;
	rdp_worker_free(self->worker);
	/* left open-ended, so every event can end in "," */
	if (self->file)
		fclose(self->file);
	free(self);
}

void granite_trace_file_record(granite_trace_file_t *self,
		const char *desc, const char *tid, uint32_t pid,
		uint64_t start_ns, uint64_t end_ns)
{
	granite_trace_event_t *e = (granite_trace_event_t *)malloc(sizeof(*e));
	if (!e)
		return;
	strlcpy(e->desc, desc, sizeof(e->desc));
	strlcpy(e->tid, tid, sizeof(e->tid));
	e->pid = pid;
	e->start_ns = start_ns;
	e->end_ns = end_ns;
	rdp_worker_push(self->worker, &e->node);
}
