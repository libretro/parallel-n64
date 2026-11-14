/* parallel-rdp's submissions (parallel-rdp/rdp_submit.c).
 *
 * Recorded work goes to the GPU after 8 passes in a row (the upscaled
 * ones too where only they are drawn), 256 primitives (native or
 * upscaled), when the GPU is idle, or 1 ms after the last submission;
 * with the command thread idle, after 32 primitives or 2 passes. A
 * submission's passes are host visible when their results land where
 * the CPU reads: the native passes, or the upscaled ones where only they
 * are drawn. Its last barrier makes their writes available to the host
 * or to the copy that reads them back, unless an upscaled pass already
 * did. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <boolean.h>

#include "rdp_submit.h"

static unsigned failures;

#define CHECK(cond, what) \
	do \
	{ \
		if (!(cond)) \
		{ \
			fprintf(stderr, "test_rdp_submit: FAIL (line %d): %s\n", __LINE__, what); \
			failures++; \
		} \
	} while (0)

#include "vk_test_device.h"

static rdp_submit_counts_t counts(unsigned passes, unsigned passes_upscaled, unsigned primitives,
		unsigned primitives_upscaled)
{
	rdp_submit_counts_t c;
	c.passes = passes;
	c.passes_upscaled = passes_upscaled;
	c.primitives = primitives;
	c.primitives_upscaled = primitives_upscaled;
	return c;
}

static void test_due(void)
{
	rdp_submit_counts_t c = counts(7, 7, 255, 255);
	CHECK(!rdp_submit_due(&c, false, false, 1000000), "not yet");
	CHECK(!rdp_submit_due(&c, true, false, 0), "not yet, super-sampled");
	CHECK(rdp_submit_due(&c, false, true, 0), "the GPU idle");
	CHECK(rdp_submit_due(&c, false, false, 1000001), "a millisecond on");
	c = counts(8, 0, 0, 0);
	CHECK(rdp_submit_due(&c, false, false, 0), "8 passes");
	c = counts(0, 8, 0, 0);
	CHECK(!rdp_submit_due(&c, false, false, 0), "8 upscaled passes after native ones");
	CHECK(rdp_submit_due(&c, true, false, 0), "8 super-sampled passes");
	c = counts(0, 0, 256, 0);
	CHECK(rdp_submit_due(&c, false, false, 0), "256 primitives");
	c = counts(0, 0, 0, 256);
	CHECK(rdp_submit_due(&c, false, false, 0), "256 upscaled primitives");

	c = counts(1, 9, 31, 99);
	CHECK(!rdp_submit_idle_due(&c), "idle: too little");
	c = counts(2, 0, 0, 0);
	CHECK(rdp_submit_idle_due(&c), "idle: 2 passes");
	c = counts(0, 0, 32, 0);
	CHECK(rdp_submit_idle_due(&c), "idle: 32 primitives");
}

static void test_take(void)
{
	rdp_submit_counts_t c = counts(3, 0, 50, 0);
	rdp_submit_taken_t t = rdp_submit_take(&c, false);
	CHECK(t.host_visible && !t.upscaled, "native passes");
	CHECK(!c.passes && !c.passes_upscaled && !c.primitives && !c.primitives_upscaled, "counts back to none");
	c = counts(3, 3, 50, 50);
	t = rdp_submit_take(&c, false);
	CHECK(t.host_visible && t.upscaled, "native and upscaled");
	c = counts(0, 3, 0, 50);
	t = rdp_submit_take(&c, false);
	CHECK(!t.host_visible && t.upscaled, "upscaled only, after native passes submitted");
	t = rdp_submit_take(&c, true);
	CHECK(!t.host_visible && !t.upscaled, "nothing");
	c = counts(0, 3, 0, 50);
	t = rdp_submit_take(&c, true);
	CHECK(t.host_visible, "super-sampled: the upscaled passes are read");
}

static void test_barrier(void)
{
	granite_command_buffer_t *cb;
	rdp_submit_taken_t t;
	int ev;

	fill_context();
	granite_device_init(&dev);
	granite_device_set_context(&dev, &context);

	num_events = 0;
	cb = granite_device_request_command_buffer(&dev, GRANITE_QUEUE_COMPUTE);
	t.host_visible = true;
	t.upscaled = false;
	rdp_submit_end_barrier(cb, &t, true);
	ev = find_event(EV_MEMORY_BARRIER, 0);
	CHECK(events[ev].a == VK_PIPELINE_STAGE_HOST_BIT && events[ev].b == VK_ACCESS_HOST_READ_BIT &&
	      events[ev].c == VK_ACCESS_MEMORY_WRITE_BIT, "to the host");
	rdp_submit_end_barrier(cb, &t, false);
	ev = find_event(EV_MEMORY_BARRIER, 1);
	CHECK(events[ev].a == VK_PIPELINE_STAGE_TRANSFER_BIT && events[ev].b == VK_ACCESS_TRANSFER_READ_BIT &&
	      events[ev].c == VK_ACCESS_MEMORY_WRITE_BIT, "to the read-back copy");
	t.upscaled = true;
	rdp_submit_end_barrier(cb, &t, true);
	ev = find_event(EV_MEMORY_BARRIER, 2);
	CHECK(events[ev].c == 0, "an upscaled pass flushed already");
	t.upscaled = false;
	t.host_visible = false;
	rdp_submit_end_barrier(cb, &t, true);
	ev = find_event(EV_MEMORY_BARRIER, 3);
	CHECK(events[ev].c == 0, "nothing the host reads");
	granite_device_submit(&dev, cb, NULL, 0, NULL);
	granite_device_free(&dev);
}

int main(void)
{
	test_due();
	test_take();
	test_barrier();
	if (failures)
	{
		fprintf(stderr, "test_rdp_submit: %u failure(s)\n", failures);
		return 1;
	}
	printf("test_rdp_submit: all checks passed\n");
	return 0;
}
