/* parallel-rdp's submit composer (vulkan/submit_batch.c), through a
 * stand-in device table.
 *
 * Waits, command buffers and signals added in that order are one batch;
 * a command buffer after a signal, or a wait after either, begins
 * another. With splitting, a binary semaphore does not share a batch
 * with a timeline one. An asked-for batch break does nothing on an empty
 * batch, and empty batches are not submitted. Past the batch limit the
 * last batch is carried on.
 *
 * A batch with a timeline semaphore carries the timeline values of all
 * its waits and signals; a profiling iteration is chained in front.
 * Submitting is one call for all batches, or a call a batch with the
 * fence on the last; a failed call stops the rest.
 *
 * The composer keeps its storage: composing the same again allocates
 * nothing, seen as the arrays not moving. */
#include <stdio.h>
#include <string.h>
#include <boolean.h>

#include "submit_batch.h"

static unsigned failures;

#define CHECK(cond, what) \
	do \
	{ \
		if (!(cond)) \
		{ \
			fprintf(stderr, "test_vk_submit_batch: FAIL (line %d): %s\n", __LINE__, what); \
			failures++; \
		} \
	} while (0)

#define SEM(x)   ((VkSemaphore)(size_t)(0x100 + (x)))
#define CMDB(x)  ((VkCommandBuffer)(size_t)(0x200 + (x)))
#define QUEUE    ((VkQueue)(size_t)0x300)
#define FENCE    ((VkFence)(size_t)0x400)

static unsigned submit_calls;
static uint32_t submit_counts[16];
static VkFence submit_fences[16];
static VkResult fail_at_call_result = VK_SUCCESS;
static unsigned fail_at_call = ~0u;

static VkResult VKAPI_CALL fake_submit(VkQueue queue, uint32_t count, const VkSubmitInfo *submits, VkFence fence)
{
	(void)submits;
	CHECK(queue == QUEUE, "to the queue given");
	if (submit_calls < 16)
	{
		submit_counts[submit_calls] = count;
		submit_fences[submit_calls] = fence;
	}
	if (submit_calls++ == fail_at_call)
		return fail_at_call_result;
	return VK_SUCCESS;
}

static struct VolkDeviceTable table;

static void test_order(void)
{
	granite_submit_composer_t c;
	const VkSubmitInfo *s;
	unsigned n;

	granite_submit_composer_init(&c);
	n = granite_submit_composer_bake(&c, -1, &s);
	CHECK(n == 0, "nothing added, nothing to submit");

	/* waits, command buffers, signals: one batch */
	granite_submit_composer_begin(&c, false);
	granite_submit_composer_add_wait(&c, SEM(1), VK_PIPELINE_STAGE_TRANSFER_BIT, 0);
	granite_submit_composer_add_wait(&c, SEM(2), VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0);
	granite_submit_composer_add_command_buffer(&c, CMDB(1));
	granite_submit_composer_add_command_buffer(&c, CMDB(2));
	granite_submit_composer_add_command_buffer(&c, CMDB(3));
	granite_submit_composer_add_signal(&c, SEM(3), 0);
	n = granite_submit_composer_bake(&c, -1, &s);
	CHECK(n == 1, "waits, command buffers and signals in order are one batch");
	CHECK(s[0].sType == VK_STRUCTURE_TYPE_SUBMIT_INFO && s[0].pNext == NULL, "with no timeline info");
	CHECK(s[0].waitSemaphoreCount == 2 && s[0].pWaitSemaphores[0] == SEM(1) && s[0].pWaitSemaphores[1] == SEM(2) &&
			s[0].pWaitDstStageMask[0] == VK_PIPELINE_STAGE_TRANSFER_BIT &&
			s[0].pWaitDstStageMask[1] == VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, "its waits with their stages");
	CHECK(s[0].commandBufferCount == 3 && s[0].pCommandBuffers[0] == CMDB(1) && s[0].pCommandBuffers[2] == CMDB(3),
			"its command buffers");
	CHECK(s[0].signalSemaphoreCount == 1 && s[0].pSignalSemaphores[0] == SEM(3), "its signal");

	/* a command buffer after a signal, a wait after a command buffer */
	granite_submit_composer_begin(&c, false);
	granite_submit_composer_add_command_buffer(&c, CMDB(1));
	granite_submit_composer_add_signal(&c, SEM(1), 0);
	granite_submit_composer_add_command_buffer(&c, CMDB(2));
	granite_submit_composer_add_wait(&c, SEM(2), VK_PIPELINE_STAGE_TRANSFER_BIT, 0);
	granite_submit_composer_add_command_buffer(&c, CMDB(3));
	granite_submit_composer_add_signal(&c, SEM(3), 0);
	granite_submit_composer_add_signal(&c, SEM(4), 0);
	n = granite_submit_composer_bake(&c, -1, &s);
	CHECK(n == 3, "a command buffer after a signal and a wait after a command buffer begin batches");
	CHECK(s[0].commandBufferCount == 1 && s[0].pCommandBuffers[0] == CMDB(1) && s[0].signalSemaphoreCount == 1 &&
			s[0].waitSemaphoreCount == 0, "the first: a command buffer and its signal");
	CHECK(s[1].commandBufferCount == 1 && s[1].pCommandBuffers[0] == CMDB(2) && s[1].signalSemaphoreCount == 0 &&
			s[1].waitSemaphoreCount == 0, "the second: the command buffer after it");
	CHECK(s[2].waitSemaphoreCount == 1 && s[2].pWaitSemaphores[0] == SEM(2) && s[2].commandBufferCount == 1 &&
			s[2].pCommandBuffers[0] == CMDB(3) && s[2].signalSemaphoreCount == 2 && s[2].pSignalSemaphores[1] == SEM(4),
			"the third: the wait and what follows it");

	/* batch breaks */
	granite_submit_composer_begin(&c, false);
	granite_submit_composer_begin_batch(&c);
	granite_submit_composer_begin_batch(&c);
	CHECK(c.num_batches == 1, "a break on an empty batch does nothing");
	granite_submit_composer_add_signal(&c, SEM(1), 0);
	granite_submit_composer_begin_batch(&c);
	granite_submit_composer_begin_batch(&c);
	CHECK(c.num_batches == 2, "a break after something begins one batch");
	n = granite_submit_composer_bake(&c, -1, &s);
	CHECK(n == 1 && s[0].signalSemaphoreCount == 1, "an empty last batch is not submitted");
	granite_submit_composer_add_signal(&c, SEM(2), 0);
	n = granite_submit_composer_bake(&c, -1, &s);
	CHECK(n == 2 && s[1].signalSemaphoreCount == 1 && s[1].pSignalSemaphores[0] == SEM(2),
			"a signal after a break is a batch of its own");

	granite_submit_composer_free(&c);
}

static void test_timeline(void)
{
	granite_submit_composer_t c;
	const VkSubmitInfo *s;
	const VkTimelineSemaphoreSubmitInfoKHR *t;
	const VkPerformanceQuerySubmitInfoKHR *p;
	unsigned n;

	granite_submit_composer_init(&c);

	/* not split: binary and timeline together */
	granite_submit_composer_begin(&c, false);
	granite_submit_composer_add_wait(&c, SEM(1), VK_PIPELINE_STAGE_TRANSFER_BIT, 0);
	granite_submit_composer_add_wait(&c, SEM(2), VK_PIPELINE_STAGE_TRANSFER_BIT, 7);
	granite_submit_composer_add_command_buffer(&c, CMDB(1));
	granite_submit_composer_add_signal(&c, SEM(3), 9);
	granite_submit_composer_add_signal(&c, SEM(4), 0);
	n = granite_submit_composer_bake(&c, -1, &s);
	CHECK(n == 1, "unsplit, binary and timeline semaphores share a batch");
	t = (const VkTimelineSemaphoreSubmitInfoKHR *)s[0].pNext;
	CHECK(t && t->sType == VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO_KHR && t->pNext == NULL,
			"which has the timeline info");
	CHECK(t && t->waitSemaphoreValueCount == 2 && t->pWaitSemaphoreValues[0] == 0 && t->pWaitSemaphoreValues[1] == 7 &&
			t->signalSemaphoreValueCount == 2 && t->pSignalSemaphoreValues[0] == 9 && t->pSignalSemaphoreValues[1] == 0,
			"with a value for every wait and signal");

	/* profiling chained in front */
	n = granite_submit_composer_bake(&c, 3, &s);
	p = (const VkPerformanceQuerySubmitInfoKHR *)s[0].pNext;
	CHECK(n == 1 && p && p->sType == VK_STRUCTURE_TYPE_PERFORMANCE_QUERY_SUBMIT_INFO_KHR && p->counterPassIndex == 3 &&
			p->pNext == (const void *)&c.timeline_infos[0], "a profiling iteration is chained before it");

	/* split */
	granite_submit_composer_begin(&c, true);
	granite_submit_composer_add_wait(&c, SEM(1), VK_PIPELINE_STAGE_TRANSFER_BIT, 0);
	granite_submit_composer_add_wait(&c, SEM(2), VK_PIPELINE_STAGE_TRANSFER_BIT, 0);
	granite_submit_composer_add_wait(&c, SEM(3), VK_PIPELINE_STAGE_TRANSFER_BIT, 7);
	granite_submit_composer_add_command_buffer(&c, CMDB(1));
	granite_submit_composer_add_signal(&c, SEM(4), 9);
	granite_submit_composer_add_signal(&c, SEM(5), 0);
	granite_submit_composer_add_signal(&c, SEM(6), 0);
	n = granite_submit_composer_bake(&c, -1, &s);
	CHECK(n == 3, "split, the kinds do not share a batch");
	CHECK(s[0].waitSemaphoreCount == 2 && s[0].pNext == NULL && s[0].commandBufferCount == 0,
			"the binary waits, with no timeline info");
	t = (const VkTimelineSemaphoreSubmitInfoKHR *)s[1].pNext;
	CHECK(s[1].waitSemaphoreCount == 1 && s[1].commandBufferCount == 1 && s[1].signalSemaphoreCount == 1 && t &&
			t->waitSemaphoreValueCount == 1 && t->pWaitSemaphoreValues[0] == 7 && t->signalSemaphoreValueCount == 1 &&
			t->pSignalSemaphoreValues[0] == 9, "the timeline wait, the command buffer and the timeline signal");
	CHECK(s[2].signalSemaphoreCount == 2 && s[2].pSignalSemaphores[0] == SEM(5) && s[2].pNext == NULL &&
			s[2].commandBufferCount == 0, "the binary signals");

	/* the batch limit: the last batch is carried on */
	granite_submit_composer_begin(&c, false);
	for (n = 0; n < GRANITE_SUBMIT_MAX_BATCHES + 3; n++)
	{
		granite_submit_composer_add_command_buffer(&c, CMDB(n));
		granite_submit_composer_add_signal(&c, SEM(n), 0);
	}
	n = granite_submit_composer_bake(&c, -1, &s);
	CHECK(n == GRANITE_SUBMIT_MAX_BATCHES, "no more batches than the limit");
	CHECK(s[GRANITE_SUBMIT_MAX_BATCHES - 1].commandBufferCount == 4 &&
			s[GRANITE_SUBMIT_MAX_BATCHES - 1].signalSemaphoreCount == 4 &&
			s[GRANITE_SUBMIT_MAX_BATCHES - 1].pCommandBuffers[3] == CMDB(GRANITE_SUBMIT_MAX_BATCHES + 2),
			"the last one takes what follows");

	granite_submit_composer_free(&c);
}

static void test_submit_and_storage(void)
{
	granite_submit_composer_t c;
	const VkSemaphore *waits;
	const VkCommandBuffer *cmds;
	const VkSemaphore *signals;
	const VkSubmitInfo *s;
	unsigned i, n;

	granite_submit_composer_init(&c);

	/* more than the arrays start with, so they grow; two batches */
	for (n = 0; n < 2; n++)
	{
		granite_submit_composer_begin(&c, false);
		for (i = 0; i < 40; i++)
			granite_submit_composer_add_wait(&c, SEM(i), VK_PIPELINE_STAGE_TRANSFER_BIT, i & 1);
		for (i = 0; i < 100; i++)
			granite_submit_composer_add_command_buffer(&c, CMDB(i));
		for (i = 0; i < 40; i++)
			granite_submit_composer_add_signal(&c, SEM(100 + i), 0);
		granite_submit_composer_add_command_buffer(&c, CMDB(100));
		if (n == 0)
		{
			waits = c.waits;
			cmds = c.cmds;
			signals = c.signals;
		}
	}
	CHECK(c.waits == waits && c.cmds == cmds && c.signals == signals, "composing the same again allocates nothing");
	n = granite_submit_composer_bake(&c, -1, &s);
	CHECK(n == 2 && s[0].waitSemaphoreCount == 40 && s[0].commandBufferCount == 100 && s[0].signalSemaphoreCount == 40 &&
			s[0].pWaitSemaphores[39] == SEM(39) && s[0].pCommandBuffers[99] == CMDB(99) &&
			s[0].pSignalSemaphores[39] == SEM(139) && s[1].commandBufferCount == 1 && s[1].pCommandBuffers[0] == CMDB(100),
			"all of it is in the batches, past the first growth");

	/* all at once */
	submit_calls = 0;
	CHECK(granite_submit_composer_submit(&c, &table, QUEUE, FENCE, -1, false) == VK_SUCCESS, "submitted");
	CHECK(submit_calls == 1 && submit_counts[0] == 2 && submit_fences[0] == FENCE, "in one call, with the fence");

	/* a batch a call */
	submit_calls = 0;
	CHECK(granite_submit_composer_submit(&c, &table, QUEUE, FENCE, -1, true) == VK_SUCCESS, "submitted one by one");
	CHECK(submit_calls == 2 && submit_counts[0] == 1 && submit_counts[1] == 1 && submit_fences[0] == VK_NULL_HANDLE &&
			submit_fences[1] == FENCE, "a call a batch, the fence on the last");

	/* the first call fails: the second is not made */
	submit_calls = 0;
	fail_at_call = 0;
	fail_at_call_result = VK_ERROR_DEVICE_LOST;
	CHECK(granite_submit_composer_submit(&c, &table, QUEUE, FENCE, -1, true) == VK_ERROR_DEVICE_LOST, "a failed call is reported");
	CHECK(submit_calls == 1, "and stops the rest");
	fail_at_call = ~0u;

	/* a composer that could not hold something submits nothing */
	submit_calls = 0;
	c.failed = true;
	CHECK(granite_submit_composer_submit(&c, &table, QUEUE, FENCE, -1, false) == VK_ERROR_OUT_OF_HOST_MEMORY &&
			submit_calls == 0, "a composer that lost something submits nothing");
	granite_submit_composer_begin(&c, false);
	CHECK(!c.failed, "and starts afresh");

	/* an empty submission still reaches the queue, for the fence */
	submit_calls = 0;
	CHECK(granite_submit_composer_submit(&c, &table, QUEUE, FENCE, -1, false) == VK_SUCCESS && submit_calls == 1 &&
			submit_counts[0] == 0 && submit_fences[0] == FENCE, "an empty submission is submitted for its fence");

	granite_submit_composer_free(&c);
	CHECK(c.waits == NULL && c.cap_cmds == 0 && c.num_batches == 1, "freed, it is as new");
}

int main(void)
{
	table.vkQueueSubmit = fake_submit;

	test_order();
	test_timeline();
	test_submit_and_storage();

	if (failures)
	{
		fprintf(stderr, "test_vk_submit_batch: %u failure(s)\n", failures);
		return 1;
	}
	printf("test_vk_submit_batch: OK\n");
	return 0;
}
