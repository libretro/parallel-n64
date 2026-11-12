/* parallel-rdp's command pools (vulkan/command_pool.c), through a
 * stand-in device table: a frame's command buffers are handed out one
 * after another and each is a different one; the next frame gets the same
 * ones back in the same order after a single pool reset, and allocates
 * only for what it needs beyond them; a frame that used none resets
 * nothing; trimming frees every command buffer once and the pool goes on
 * working; freeing frees what is left and destroys the pool once; a pool
 * for a queue the device lacks, a pool that could not be created and an
 * allocation that fails all give VK_NULL_HANDLE and keep nothing. */
#include <stdio.h>
#include <string.h>
#include <boolean.h>

#include "command_pool.h"

static unsigned failures;

#define CHECK(cond, what) \
	do \
	{ \
		if (!(cond)) \
		{ \
			fprintf(stderr, "test_vk_command_pool: FAIL: %s\n", what); \
			failures++; \
		} \
	} while (0)

#define MAX_BUFFERS 64

static unsigned pools_created, pools_destroyed, resets, release_resets, trims;
static unsigned allocated, freed, live;
static bool fail_create, fail_allocate;
static char storage[MAX_BUFFERS];
static bool is_live[MAX_BUFFERS];

static VkResult VKAPI_CALL fake_create_pool(VkDevice d, const VkCommandPoolCreateInfo *info,
		const VkAllocationCallbacks *a, VkCommandPool *pool)
{
	(void)d; (void)a;
	if (fail_create)
		return VK_ERROR_OUT_OF_HOST_MEMORY;
	CHECK(info->sType == VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO && info->pNext == NULL
			&& info->flags == VK_COMMAND_POOL_CREATE_TRANSIENT_BIT && info->queueFamilyIndex == 3,
			"the pool is transient, of the queue family asked for");
	pools_created++;
	*pool = (VkCommandPool)(size_t)0x1234;
	return VK_SUCCESS;
}

static void VKAPI_CALL fake_destroy_pool(VkDevice d, VkCommandPool pool, const VkAllocationCallbacks *a)
{
	(void)d; (void)a;
	CHECK(pool == (VkCommandPool)(size_t)0x1234, "the pool destroyed is the pool created");
	CHECK(live == 0, "no command buffer outlives its pool");
	pools_destroyed++;
}

static VkResult VKAPI_CALL fake_reset_pool(VkDevice d, VkCommandPool pool, VkCommandPoolResetFlags flags)
{
	(void)d; (void)pool;
	if (flags & VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT)
		release_resets++;
	else
		resets++;
	return VK_SUCCESS;
}

static void VKAPI_CALL fake_trim_pool(VkDevice d, VkCommandPool pool, VkCommandPoolTrimFlags flags)
{
	(void)d; (void)pool; (void)flags;
	trims++;
}

static VkResult VKAPI_CALL fake_allocate(VkDevice d, const VkCommandBufferAllocateInfo *info, VkCommandBuffer *out)
{
	unsigned i;
	(void)d;
	if (fail_allocate)
		return VK_ERROR_OUT_OF_DEVICE_MEMORY;
	CHECK(info->sType == VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO && info->pNext == NULL
			&& info->level == VK_COMMAND_BUFFER_LEVEL_PRIMARY && info->commandBufferCount == 1,
			"one primary command buffer at a time");
	for (i = 0; i < MAX_BUFFERS; i++)
		if (!is_live[i])
			break;
	if (i == MAX_BUFFERS)
		return VK_ERROR_OUT_OF_DEVICE_MEMORY;
	is_live[i] = true;
	live++;
	allocated++;
	*out = (VkCommandBuffer)&storage[i];
	return VK_SUCCESS;
}

static void VKAPI_CALL fake_free(VkDevice d, VkCommandPool pool, uint32_t count, const VkCommandBuffer *buffers)
{
	uint32_t i;
	(void)d; (void)pool;
	for (i = 0; i < count; i++)
	{
		size_t n = (size_t)((const char *)buffers[i] - storage);
		CHECK(n < MAX_BUFFERS && is_live[n], "a command buffer is freed once");
		if (n < MAX_BUFFERS && is_live[n])
		{
			is_live[n] = false;
			live--;
			freed++;
		}
	}
}

int main(void)
{
	struct VolkDeviceTable table;
	granite_command_pool_t pool;
	VkCommandBuffer first[20], again;
	unsigned i, j;
	bool distinct = true, same = true;

	memset(&table, 0, sizeof(table));
	table.vkCreateCommandPool = fake_create_pool;
	table.vkDestroyCommandPool = fake_destroy_pool;
	table.vkResetCommandPool = fake_reset_pool;
	table.vkTrimCommandPool = fake_trim_pool;
	table.vkAllocateCommandBuffers = fake_allocate;
	table.vkFreeCommandBuffers = fake_free;

	granite_command_pool_init(&pool, (VkDevice)(size_t)1, &table, 3);
	CHECK(pools_created == 1, "a pool is created");
	granite_command_pool_begin(&pool);
	CHECK(resets == 0, "a frame that used no command buffer resets nothing");

	/* past the array's first growth */
	for (i = 0; i < 20; i++)
		first[i] = granite_command_pool_request(&pool);
	for (i = 0; i < 20; i++)
	{
		if (first[i] == VK_NULL_HANDLE)
			distinct = false;
		for (j = 0; j < i; j++)
			if (first[i] == first[j])
				distinct = false;
	}
	CHECK(distinct && allocated == 20, "a frame's command buffers are twenty different ones");

	granite_command_pool_begin(&pool);
	CHECK(resets == 1, "the next frame resets the pool once");
	for (i = 0; i < 20; i++)
		if (granite_command_pool_request(&pool) != first[i])
			same = false;
	CHECK(same && allocated == 20, "and gets the same command buffers back, in order, allocating none");
	again = granite_command_pool_request(&pool);
	CHECK(again != VK_NULL_HANDLE && allocated == 21, "one more than before allocates one");

	granite_command_pool_begin(&pool);
	fail_allocate = true;
	for (i = 0; i < 21; i++)
		granite_command_pool_request(&pool);
	CHECK(granite_command_pool_request(&pool) == VK_NULL_HANDLE, "an allocation that fails gives no command buffer");
	fail_allocate = false;
	CHECK(granite_command_pool_request(&pool) != VK_NULL_HANDLE && allocated == 22 && live == 22,
			"and the pool goes on once it can");

	granite_command_pool_trim(&pool);
	CHECK(release_resets == 1 && trims == 1 && freed == 22 && live == 0,
			"trimming frees every command buffer once and trims the pool");
	CHECK(granite_command_pool_request(&pool) != VK_NULL_HANDLE && allocated == 23, "a trimmed pool allocates again");
	granite_command_pool_begin(&pool);
	CHECK(resets == 3, "and resets");

	granite_command_pool_free(&pool);
	CHECK(freed == 23 && live == 0 && pools_destroyed == 1, "freeing frees what is left and destroys the pool");
	granite_command_pool_free(&pool);
	CHECK(pools_destroyed == 1, "once");

	/* a queue the device does not have */
	granite_command_pool_init(&pool, (VkDevice)(size_t)1, &table, VK_QUEUE_FAMILY_IGNORED);
	CHECK(pools_created == 1 && granite_command_pool_request(&pool) == VK_NULL_HANDLE, "no pool for a queue there is not");
	granite_command_pool_begin(&pool);
	granite_command_pool_trim(&pool);
	granite_command_pool_free(&pool);
	CHECK(resets == 3 && trims == 1 && pools_destroyed == 1, "and nothing done with it");

	fail_create = true;
	granite_command_pool_init(&pool, (VkDevice)(size_t)1, &table, 3);
	CHECK(granite_command_pool_request(&pool) == VK_NULL_HANDLE && allocated == 23,
			"a pool that could not be created hands out nothing");
	granite_command_pool_free(&pool);
	CHECK(pools_destroyed == 1, "and destroys nothing");

	if (failures)
	{
		fprintf(stderr, "test_vk_command_pool: %u failed\n", failures);
		return 1;
	}
	printf("test_vk_command_pool: ok\n");
	return 0;
}
