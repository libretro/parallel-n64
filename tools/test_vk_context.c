/* parallel-rdp's Vulkan context (vulkan/context.c), against a stand-in
 * loader and physical device.
 *
 * The context creates the device on the frontend's instance. A device
 * is scored by its type, and one below Vulkan 1.1 is not used. The
 * graphics queue does graphics and compute and can present; compute
 * gets a queue of its own where there is one, and transfer a queue that
 * only transfers, each falling back on the one before it; a driver that
 * schedules separate queues badly gets one queue for graphics and
 * compute.
 *
 * The device is created with the frontend's extensions, each of which
 * the device must have, and with the extensions the renderer uses where
 * the device has them and no others, each once. Their feature
 * structures are chained into the create info with what the device
 * supports. Of the core features only the frontend's and the three the
 * renderer uses are enabled. The host memory import no longer depends
 * on the external semaphore and memory extensions it never used. The
 * device goes through the frontend's own create function when there is
 * one, and is destroyed with the context unless it was given away. */
#include <stdio.h>
#include <string.h>
#include <boolean.h>

#include "context.h"
#include "frame_garbage.h" /* GRANITE_VK_FROM_BITS */

static unsigned failures;

#define CHECK(cond, what) \
	do \
	{ \
		if (!(cond)) \
		{ \
			fprintf(stderr, "test_vk_context: FAIL (line %d): %s\n", __LINE__, what); \
			failures++; \
		} \
	} while (0)

#define INSTANCE ((VkInstance)(size_t)0x10)
#define GPU(n)   ((VkPhysicalDevice)(size_t)(0x100 + (n)))
#define DEVICE   ((VkDevice)(size_t)0x1000)
#define SURFACE  GRANITE_VK_FROM_BITS(VkSurfaceKHR, 0x77)
#define QUEUE(family, index) ((VkQueue)(size_t)(0x2000 + (family) * 16 + (index)))

/* ---- the stand-in loader and device ---- */
static uint32_t instance_version = VK_API_VERSION_1_1;
static VkInstance loaded_instance;
static unsigned loader_inits, table_loads;

/* the physical devices: type and API version */
static struct
{
	VkPhysicalDeviceType type;
	uint32_t api;
} gpus[4];
static uint32_t num_gpus;

/* of the device asked about */
static const char *device_extensions[40];
static uint32_t num_device_extensions;
static VkQueueFamilyProperties families[8];
static uint32_t num_families;
static uint32_t presenting = ~0u;
static VkDriverId driver_id;
static bool has_int16 = true, has_cube_arrays = true;
static unsigned surface_queries;

/* the device created */
static unsigned devices_created, factory_calls, devices_destroyed, idle_waits;
static bool fail_create;
static VkPhysicalDevice created_gpu;
static char created_extensions[40][VK_MAX_EXTENSION_NAME_SIZE];
static uint32_t num_created_extensions;
static VkDeviceQueueCreateInfo created_queues[8];
static float created_priorities[8][4];
static uint32_t num_created_queues;
static VkPhysicalDeviceFeatures created_features;
static VkStructureType created_chain[32];
static unsigned num_created_chain;
static VkBool32 created_timeline, created_8bit, created_scalar;

void volkInitializeCustom(PFN_vkGetInstanceProcAddr handler)
{
	(void)handler;
	loader_inits++;
}

uint32_t volkGetInstanceVersion(void)
{
	return instance_version;
}

void volkLoadInstance(VkInstance instance)
{
	loaded_instance = instance;
}

static void VKAPI_CALL fake_get_queue(VkDevice d, uint32_t family, uint32_t index, VkQueue *queue)
{
	CHECK(d == DEVICE, "the queue of the device created");
	*queue = QUEUE(family, index);
}

static VkResult VKAPI_CALL fake_wait_idle(VkDevice d)
{
	CHECK(d == DEVICE, "the device created");
	idle_waits++;
	return VK_SUCCESS;
}

static void VKAPI_CALL fake_destroy_device(VkDevice d, const VkAllocationCallbacks *a)
{
	(void)a;
	CHECK(d == DEVICE, "the device created");
	devices_destroyed++;
}

void volkLoadDeviceTable(struct VolkDeviceTable *table, VkDevice device)
{
	CHECK(device == DEVICE, "the table of the device created");
	memset(table, 0, sizeof(*table));
	table->vkGetDeviceQueue = fake_get_queue;
	table->vkDeviceWaitIdle = fake_wait_idle;
	table->vkDestroyDevice = fake_destroy_device;
	table_loads++;
}

static VkResult VKAPI_CALL fake_enumerate_gpus(VkInstance instance, uint32_t *count, VkPhysicalDevice *out)
{
	uint32_t i;
	CHECK(instance == INSTANCE, "the frontend's instance");
	if (!out)
	{
		*count = num_gpus;
		return VK_SUCCESS;
	}
	for (i = 0; i < *count && i < num_gpus; i++)
		out[i] = GPU(i);
	*count = i;
	return VK_SUCCESS;
}

static void fill_props(VkPhysicalDevice gpu, VkPhysicalDeviceProperties *props)
{
	size_t n = (size_t)gpu - 0x100;
	memset(props, 0, sizeof(*props));
	props->deviceType = gpus[n].type;
	props->apiVersion = gpus[n].api;
	strcpy(props->deviceName, "stand-in");
}

static void VKAPI_CALL fake_props(VkPhysicalDevice gpu, VkPhysicalDeviceProperties *props)
{
	fill_props(gpu, props);
}

static void VKAPI_CALL fake_props2(VkPhysicalDevice gpu, VkPhysicalDeviceProperties2 *props)
{
	VkBaseOutStructure *next;
	fill_props(gpu, &props->properties);
	for (next = (VkBaseOutStructure *)props->pNext; next; next = next->pNext)
	{
		if (next->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES_KHR)
			((VkPhysicalDeviceDriverPropertiesKHR *)next)->driverID = driver_id;
		else if (next->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_MEMORY_HOST_PROPERTIES_EXT)
			((VkPhysicalDeviceExternalMemoryHostPropertiesEXT *)next)->minImportedHostPointerAlignment = 4096;
		else if (next->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES)
			((VkPhysicalDeviceSubgroupProperties *)next)->subgroupSize = 8;
		else if (next->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_PROPERTIES_EXT)
			((VkPhysicalDeviceSubgroupSizeControlPropertiesEXT *)next)->maxSubgroupSize = 32;
		else if (next->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PUSH_DESCRIPTOR_PROPERTIES_KHR)
			((VkPhysicalDevicePushDescriptorPropertiesKHR *)next)->maxPushDescriptors = 32;
		else
			CHECK(0, "a property structure nothing reads");
	}
}

static VkResult VKAPI_CALL fake_enumerate_extensions(VkPhysicalDevice gpu, const char *layer, uint32_t *count,
		VkExtensionProperties *out)
{
	uint32_t i;
	(void)gpu;
	CHECK(layer == NULL, "the device's own extensions");
	if (!out)
	{
		*count = num_device_extensions;
		return VK_SUCCESS;
	}
	for (i = 0; i < *count && i < num_device_extensions; i++)
	{
		memset(&out[i], 0, sizeof(out[i]));
		strcpy(out[i].extensionName, device_extensions[i]);
	}
	*count = i;
	return VK_SUCCESS;
}

static void VKAPI_CALL fake_memory_props(VkPhysicalDevice gpu, VkPhysicalDeviceMemoryProperties *props)
{
	(void)gpu;
	memset(props, 0, sizeof(*props));
	props->memoryTypeCount = 3;
}

static void VKAPI_CALL fake_queue_families(VkPhysicalDevice gpu, uint32_t *count, VkQueueFamilyProperties *out)
{
	uint32_t i;
	(void)gpu;
	if (!out)
	{
		*count = num_families;
		return;
	}
	for (i = 0; i < *count && i < num_families; i++)
		out[i] = families[i];
	*count = i;
}

static VkResult VKAPI_CALL fake_surface_support(VkPhysicalDevice gpu, uint32_t family, VkSurfaceKHR surface,
		VkBool32 *supported)
{
	(void)gpu;
	CHECK(surface == SURFACE, "the frontend's surface");
	surface_queries++;
	*supported = (presenting >> family) & 1;
	return VK_SUCCESS;
}

/* everything asked about is supported */
static void VKAPI_CALL fake_features2(VkPhysicalDevice gpu, VkPhysicalDeviceFeatures2 *features)
{
	VkBaseOutStructure *next;
	(void)gpu;
	memset(&features->features, 0, sizeof(features->features));
	features->features.shaderInt16 = has_int16;
	features->features.independentBlend = VK_TRUE;
	features->features.fragmentStoresAndAtomics = VK_TRUE;
	features->features.imageCubeArray = has_cube_arrays;
	features->features.samplerAnisotropy = VK_TRUE;
	features->features.shaderInt64 = VK_TRUE;
	for (next = (VkBaseOutStructure *)features->pNext; next; next = next->pNext)
	{
		/* (each of these starts with one VkBool32 after the header) */
		VkBool32 *first = (VkBool32 *)(next + 1);
		*first = VK_TRUE;
	}
}

static void record_device(VkPhysicalDevice gpu, const VkDeviceCreateInfo *info)
{
	const VkBaseInStructure *next;
	uint32_t i, k;

	created_gpu = gpu;
	CHECK(info->sType == VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO && info->pEnabledFeatures == NULL
			&& info->enabledLayerCount == 0, "a create info with its features in the chain");
	num_created_extensions = info->enabledExtensionCount;
	for (i = 0; i < info->enabledExtensionCount && i < 40; i++)
		strcpy(created_extensions[i], info->ppEnabledExtensionNames[i]);
	num_created_queues = info->queueCreateInfoCount;
	for (i = 0; i < info->queueCreateInfoCount && i < 8; i++)
	{
		created_queues[i] = info->pQueueCreateInfos[i];
		for (k = 0; k < info->pQueueCreateInfos[i].queueCount && k < 4; k++)
			created_priorities[i][k] = info->pQueueCreateInfos[i].pQueuePriorities[k];
	}
	num_created_chain = 0;
	created_timeline = created_8bit = created_scalar = VK_FALSE;
	memset(&created_features, 0, sizeof(created_features));
	for (next = (const VkBaseInStructure *)info->pNext; next; next = next->pNext)
	{
		if (num_created_chain < 32)
			created_chain[num_created_chain++] = next->sType;
		if (next->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2)
			created_features = ((const VkPhysicalDeviceFeatures2 *)next)->features;
		else if (next->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES_KHR)
			created_timeline = ((const VkPhysicalDeviceTimelineSemaphoreFeaturesKHR *)next)->timelineSemaphore;
		else if (next->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_8BIT_STORAGE_FEATURES_KHR)
			created_8bit = ((const VkPhysicalDevice8BitStorageFeaturesKHR *)next)->storageBuffer8BitAccess;
		else if (next->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SCALAR_BLOCK_LAYOUT_FEATURES_EXT)
			created_scalar = ((const VkPhysicalDeviceScalarBlockLayoutFeaturesEXT *)next)->scalarBlockLayout;
	}
}

static VkResult VKAPI_CALL fake_create_device(VkPhysicalDevice gpu, const VkDeviceCreateInfo *info,
		const VkAllocationCallbacks *a, VkDevice *device)
{
	(void)a;
	if (fail_create)
		return VK_ERROR_INITIALIZATION_FAILED;
	record_device(gpu, info);
	devices_created++;
	*device = DEVICE;
	return VK_SUCCESS;
}

static int factory_user;
static VkDevice factory_create(void *user, VkPhysicalDevice gpu, const VkDeviceCreateInfo *info)
{
	CHECK(user == &factory_user, "the frontend's pointer comes back");
	factory_calls++;
	if (fail_create)
		return VK_NULL_HANDLE;
	record_device(gpu, info);
	return DEVICE;
}

PFN_vkEnumeratePhysicalDevices vkEnumeratePhysicalDevices = fake_enumerate_gpus;
PFN_vkGetPhysicalDeviceProperties vkGetPhysicalDeviceProperties = fake_props;
PFN_vkGetPhysicalDeviceProperties2 vkGetPhysicalDeviceProperties2 = fake_props2;
PFN_vkEnumerateDeviceExtensionProperties vkEnumerateDeviceExtensionProperties = fake_enumerate_extensions;
PFN_vkGetPhysicalDeviceMemoryProperties vkGetPhysicalDeviceMemoryProperties = fake_memory_props;
PFN_vkGetPhysicalDeviceQueueFamilyProperties vkGetPhysicalDeviceQueueFamilyProperties = fake_queue_families;
PFN_vkGetPhysicalDeviceSurfaceSupportKHR vkGetPhysicalDeviceSurfaceSupportKHR = fake_surface_support;
PFN_vkGetPhysicalDeviceFeatures2 vkGetPhysicalDeviceFeatures2 = fake_features2;
PFN_vkCreateDevice vkCreateDevice = fake_create_device;

static bool created_with(const char *name)
{
	uint32_t i, n = 0;
	for (i = 0; i < num_created_extensions; i++)
		if (!strcmp(created_extensions[i], name))
			n++;
	CHECK(n <= 1, "an extension is enabled once");
	return n == 1;
}

static bool chained(VkStructureType type)
{
	unsigned i;
	for (i = 0; i < num_created_chain; i++)
		if (created_chain[i] == type)
			return true;
	return false;
}

/* ---- */

static void test_score(void)
{
	VkPhysicalDeviceProperties props;
	memset(&props, 0, sizeof(props));
	props.apiVersion = VK_API_VERSION_1_1;
	props.deviceType = VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
	CHECK(granite_context_device_score(&props) == 3, "a discrete GPU is best");
	props.deviceType = VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;
	CHECK(granite_context_device_score(&props) == 2, "then an integrated one");
	props.deviceType = VK_PHYSICAL_DEVICE_TYPE_CPU;
	CHECK(granite_context_device_score(&props) == 1, "then a software one");
	props.deviceType = VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU;
	CHECK(granite_context_device_score(&props) == 0, "anything else is not chosen over those");
	props.deviceType = VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
	props.apiVersion = VK_API_VERSION_1_0;
	CHECK(granite_context_device_score(&props) == 0, "nor is one below Vulkan 1.1");
}

static void test_queues(void)
{
	VkQueueFamilyProperties props[GRANITE_CONTEXT_MAX_QUEUE_FAMILIES + 2];
	bool present[GRANITE_CONTEXT_MAX_QUEUE_FAMILIES];
	granite_queue_pick_t pick;
	unsigned i;

	memset(props, 0, sizeof(props));

	/* one family, one queue: everything on it */
	props[0].queueFlags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT;
	props[0].queueCount = 1;
	memset(&pick, 0xff, sizeof(pick));
	CHECK(granite_context_pick_queues(props, 1, NULL, false, &pick), "one queue will do");
	CHECK(pick.family[GRANITE_QUEUE_GRAPHICS] == 0 && pick.index[GRANITE_QUEUE_GRAPHICS] == 0
			&& pick.family[GRANITE_QUEUE_COMPUTE] == 0 && pick.index[GRANITE_QUEUE_COMPUTE] == 0
			&& pick.family[GRANITE_QUEUE_TRANSFER] == 0 && pick.index[GRANITE_QUEUE_TRANSFER] == 0,
			"all three are that queue");
	CHECK(pick.counts[0] == 1 && pick.priorities[0][0] == 0.5f && pick.counts[1] == 0, "created once");

	/* one family, many queues: graphics and compute apart; transfer
	 * shares compute's, as the family does graphics */
	props[0].queueCount = 8;
	CHECK(granite_context_pick_queues(props, 1, NULL, false, &pick)
			&& pick.index[GRANITE_QUEUE_GRAPHICS] == 0 && pick.index[GRANITE_QUEUE_COMPUTE] == 1
			&& pick.family[GRANITE_QUEUE_TRANSFER] == 0 && pick.index[GRANITE_QUEUE_TRANSFER] == 1,
			"a second queue of the family for compute, shared by transfer");
	CHECK(pick.counts[0] == 2 && pick.priorities[0][0] == 0.5f && pick.priorities[0][1] == 1.0f,
			"compute's queue has the higher priority");
	CHECK(granite_context_pick_queues(props, 1, NULL, true, &pick)
			&& pick.index[GRANITE_QUEUE_COMPUTE] == 0 && pick.index[GRANITE_QUEUE_TRANSFER] == 0 && pick.counts[0] == 1,
			"told to use one queue, it does");

	/* graphics, compute-only and transfer-only families */
	props[0].queueCount = 1;
	props[1].queueFlags = VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT;
	props[1].queueCount = 2;
	props[2].queueFlags = VK_QUEUE_TRANSFER_BIT;
	props[2].queueCount = 1;
	CHECK(granite_context_pick_queues(props, 3, NULL, false, &pick)
			&& pick.family[GRANITE_QUEUE_GRAPHICS] == 0 && pick.family[GRANITE_QUEUE_COMPUTE] == 1
			&& pick.index[GRANITE_QUEUE_COMPUTE] == 0 && pick.family[GRANITE_QUEUE_TRANSFER] == 2
			&& pick.index[GRANITE_QUEUE_TRANSFER] == 0, "each kind of work gets the family made for it");
	CHECK(pick.counts[0] == 1 && pick.counts[1] == 1 && pick.counts[2] == 1 && pick.priorities[1][0] == 1.0f
			&& pick.priorities[2][0] == 0.5f, "one queue of each");

	/* no transfer-only family: a second compute queue */
	CHECK(granite_context_pick_queues(props, 2, NULL, false, &pick)
			&& pick.family[GRANITE_QUEUE_TRANSFER] == 1 && pick.index[GRANITE_QUEUE_TRANSFER] == 1
			&& pick.counts[1] == 2 && pick.priorities[1][1] == 0.5f, "transfer takes a compute queue of its own");
	props[1].queueCount = 1;
	CHECK(granite_context_pick_queues(props, 2, NULL, false, &pick)
			&& pick.family[GRANITE_QUEUE_TRANSFER] == 1 && pick.index[GRANITE_QUEUE_TRANSFER] == 0 && pick.counts[1] == 1,
			"or shares compute's when there is no other");

	/* presenting */
	props[3].queueFlags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
	props[3].queueCount = 1;
	memset(present, 0, sizeof(present));
	present[3] = true;
	CHECK(granite_context_pick_queues(props, 4, present, false, &pick)
			&& pick.family[GRANITE_QUEUE_GRAPHICS] == 3 && pick.family[GRANITE_QUEUE_COMPUTE] == 0,
			"graphics is on a family that can present; compute need not be");
	present[3] = false;
	CHECK(!granite_context_pick_queues(props, 4, present, false, &pick), "no family that presents: no graphics queue");

	/* graphics not on the first family, and nothing else for compute */
	memset(props, 0, sizeof(props));
	props[0].queueFlags = VK_QUEUE_SPARSE_BINDING_BIT;
	props[0].queueCount = 1;
	props[1].queueFlags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
	props[1].queueCount = 1;
	CHECK(granite_context_pick_queues(props, 2, NULL, false, &pick)
			&& pick.family[GRANITE_QUEUE_GRAPHICS] == 1 && pick.family[GRANITE_QUEUE_COMPUTE] == 1
			&& pick.family[GRANITE_QUEUE_TRANSFER] == 1 && pick.counts[1] == 1 && pick.counts[0] == 0,
			"compute and transfer fall back on the graphics queue wherever it is");
	props[2].queueFlags = VK_QUEUE_TRANSFER_BIT;
	props[2].queueCount = 1;

	/* nothing that does graphics and compute */
	props[0].queueFlags = VK_QUEUE_GRAPHICS_BIT;
	props[1].queueFlags = VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT;
	CHECK(!granite_context_pick_queues(props, 3, NULL, false, &pick), "graphics without compute will not do");
	CHECK(!granite_context_pick_queues(props, 0, NULL, false, &pick), "nor will no families at all");

	/* more families than are looked at */
	for (i = 0; i < GRANITE_CONTEXT_MAX_QUEUE_FAMILIES + 2; i++)
	{
		props[i].queueFlags = VK_QUEUE_TRANSFER_BIT;
		props[i].queueCount = 1;
	}
	props[GRANITE_CONTEXT_MAX_QUEUE_FAMILIES + 1].queueFlags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
	CHECK(!granite_context_pick_queues(props, GRANITE_CONTEXT_MAX_QUEUE_FAMILIES + 2, NULL, false, &pick),
			"families past the limit are not looked at");
}

static void standard_device(void)
{
	static const char *all[] = {
		VK_KHR_SWAPCHAIN_EXTENSION_NAME,
		VK_KHR_DRIVER_PROPERTIES_EXTENSION_NAME,
		VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME,
		VK_KHR_IMAGE_FORMAT_LIST_EXTENSION_NAME,
		VK_EXT_TOOLING_INFO_EXTENSION_NAME,
		VK_KHR_8BIT_STORAGE_EXTENSION_NAME,
		VK_KHR_16BIT_STORAGE_EXTENSION_NAME,
		VK_KHR_SHADER_FLOAT16_INT8_EXTENSION_NAME,
		VK_EXT_SUBGROUP_SIZE_CONTROL_EXTENSION_NAME,
		VK_EXT_SCALAR_BLOCK_LAYOUT_EXTENSION_NAME,
		VK_KHR_UNIFORM_BUFFER_STANDARD_LAYOUT_EXTENSION_NAME,
		VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME,
		VK_EXT_MEMORY_PRIORITY_EXTENSION_NAME,
		VK_EXT_MEMORY_BUDGET_EXTENSION_NAME,
		VK_EXT_PIPELINE_CREATION_CACHE_CONTROL_EXTENSION_NAME,
		VK_KHR_FORMAT_FEATURE_FLAGS_2_EXTENSION_NAME,
		VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME,
		/* what the device has and the renderer does not use */
		VK_KHR_SAMPLER_MIRROR_CLAMP_TO_EDGE_EXTENSION_NAME,
		VK_KHR_DRAW_INDIRECT_COUNT_EXTENSION_NAME,
		VK_EXT_CONSERVATIVE_RASTERIZATION_EXTENSION_NAME,
		VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME,
		VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME,
		VK_EXT_HOST_QUERY_RESET_EXTENSION_NAME,
		VK_EXT_SHADER_DEMOTE_TO_HELPER_INVOCATION_EXTENSION_NAME,
		VK_EXT_ASTC_DECODE_MODE_EXTENSION_NAME,
		VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
		VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME,
		VK_KHR_PRESENT_ID_EXTENSION_NAME
	};
	unsigned i;

	num_device_extensions = sizeof(all) / sizeof(all[0]);
	for (i = 0; i < num_device_extensions; i++)
		device_extensions[i] = all[i];

	num_gpus = 1;
	gpus[0].type = VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
	gpus[0].api = VK_API_VERSION_1_2;

	memset(families, 0, sizeof(families));
	families[0].queueFlags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT;
	families[0].queueCount = 4;
	families[1].queueFlags = VK_QUEUE_TRANSFER_BIT;
	families[1].queueCount = 1;
	/* (and one nothing here has a use for) */
	families[2].queueFlags = VK_QUEUE_SPARSE_BINDING_BIT;
	families[2].queueCount = 1;
	num_families = 3;
	presenting = ~0u;
	driver_id = VK_DRIVER_ID_MESA_RADV;
	instance_version = VK_API_VERSION_1_1;
	fail_create = false;
}

static void test_init(void)
{
	static const char *wanted[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME };
	static const char *missing[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME, "VK_EXT_not_there" };
	granite_context_t context;
	VkPhysicalDeviceFeatures required;
	unsigned before;

	memset(&required, 0, sizeof(required));
	required.samplerAnisotropy = VK_TRUE;

	CHECK(!granite_context_init_loader(NULL) && loader_inits == 0, "no loader without a function to load from");
	CHECK(granite_context_init_loader((PFN_vkGetInstanceProcAddr)(size_t)1) && loader_inits == 1, "the loader is set up");

	/* ---- a full device, on the GPU the frontend names ---- */
	standard_device();
	memset(&context, 0xff, sizeof(context));
	CHECK(granite_context_init(&context, INSTANCE, GPU(0), SURFACE, wanted, 2, &required, NULL, NULL),
			"a device is created");
	CHECK(loaded_instance == INSTANCE && context.instance == INSTANCE && context.gpu == GPU(0)
			&& context.device == DEVICE && context.owned_device && devices_created == 1 && created_gpu == GPU(0)
			&& table_loads == 1, "on the frontend's instance and GPU, with its own function table");
	CHECK(context.mem_props.memoryTypeCount == 3 && context.gpu_props.apiVersion == VK_API_VERSION_1_2
			&& !strcmp(context.gpu_props.deviceName, "stand-in"), "the device's properties are kept");

	/* queues */
	CHECK(context.queue_info.family_indices[GRANITE_QUEUE_GRAPHICS] == 0
			&& context.queue_info.queues[GRANITE_QUEUE_GRAPHICS] == QUEUE(0, 0)
			&& context.queue_info.family_indices[GRANITE_QUEUE_COMPUTE] == 0
			&& context.queue_info.queues[GRANITE_QUEUE_COMPUTE] == QUEUE(0, 1)
			&& context.queue_info.family_indices[GRANITE_QUEUE_TRANSFER] == 1
			&& context.queue_info.queues[GRANITE_QUEUE_TRANSFER] == QUEUE(1, 0), "the three queues are its picks");
	CHECK(num_created_queues == 2 && created_queues[0].queueFamilyIndex == 0 && created_queues[0].queueCount == 2
			&& created_priorities[0][0] == 0.5f && created_priorities[0][1] == 1.0f
			&& created_queues[1].queueFamilyIndex == 1 && created_queues[1].queueCount == 1
			&& created_priorities[1][0] == 0.5f, "and exactly those are created: none of a family nothing uses");
	CHECK(surface_queries == 3, "every family is asked whether it presents");

	/* extensions */
	CHECK(created_with(VK_KHR_SWAPCHAIN_EXTENSION_NAME) && created_with(VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME),
			"the frontend's extensions are enabled, one it shares with the renderer once");
	CHECK(created_with(VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME) && created_with(VK_KHR_IMAGE_FORMAT_LIST_EXTENSION_NAME)
			&& created_with(VK_KHR_8BIT_STORAGE_EXTENSION_NAME) && created_with(VK_KHR_16BIT_STORAGE_EXTENSION_NAME)
			&& created_with(VK_KHR_SHADER_FLOAT16_INT8_EXTENSION_NAME)
			&& created_with(VK_EXT_SUBGROUP_SIZE_CONTROL_EXTENSION_NAME)
			&& created_with(VK_EXT_SCALAR_BLOCK_LAYOUT_EXTENSION_NAME)
			&& created_with(VK_KHR_UNIFORM_BUFFER_STANDARD_LAYOUT_EXTENSION_NAME)
			&& created_with(VK_EXT_MEMORY_PRIORITY_EXTENSION_NAME) && created_with(VK_EXT_MEMORY_BUDGET_EXTENSION_NAME)
			&& created_with(VK_EXT_PIPELINE_CREATION_CACHE_CONTROL_EXTENSION_NAME)
			&& created_with(VK_KHR_FORMAT_FEATURE_FLAGS_2_EXTENSION_NAME)
			&& created_with(VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME), "with every extension the renderer uses");
	CHECK(num_created_extensions == 15, "and no others");
	CHECK(!created_with(VK_EXT_TOOLING_INFO_EXTENSION_NAME) && !created_with(VK_KHR_DRIVER_PROPERTIES_EXTENSION_NAME)
			&& !created_with(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME)
			&& !created_with(VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME), "what is only asked about is not enabled");
	CHECK(context.ext.supports_push_descriptor && context.ext.supports_external_memory_host
			&& context.ext.supports_driver_properties && context.ext.supports_memory_budget
			&& context.ext.supports_format_feature_flags2 && context.ext.supports_image_format_list
			&& context.ext.supports_tooling_info, "the context says what the device has");

	/* features */
	CHECK(created_chain[0] == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 && num_created_chain == 10
			&& chained(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_8BIT_STORAGE_FEATURES_KHR)
			&& chained(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES_KHR)
			&& chained(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FLOAT16_INT8_FEATURES_KHR)
			&& chained(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_FEATURES_EXT)
			&& chained(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SCALAR_BLOCK_LAYOUT_FEATURES_EXT)
			&& chained(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_UNIFORM_BUFFER_STANDARD_LAYOUT_FEATURES_KHR)
			&& chained(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES_KHR)
			&& chained(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PRIORITY_FEATURES_EXT)
			&& chained(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_CREATION_CACHE_CONTROL_FEATURES_EXT),
			"the features of those extensions are chained into the create info, and only those");
	CHECK(created_timeline && created_8bit && created_scalar && context.ext.timeline_semaphore_features.timelineSemaphore
			&& context.ext.storage_8bit_features.storageBuffer8BitAccess
			&& context.ext.pipeline_creation_cache_control_features.pipelineCreationCacheControl
			&& context.ext.memory_priority_features.memoryPriority, "with what the device supports turned on");
	CHECK(created_features.shaderInt16 && created_features.independentBlend && created_features.fragmentStoresAndAtomics
			&& created_features.samplerAnisotropy, "the core features the renderer uses and the frontend's are on");
	CHECK(!created_features.imageCubeArray && !created_features.shaderInt64,
			"a core feature neither asked for stays off");
	CHECK(!memcmp(&created_features, &context.ext.enabled_features, sizeof(created_features)),
			"and the context says which are on");

	/* properties */
	CHECK(context.ext.driver_properties.driverID == VK_DRIVER_ID_MESA_RADV
			&& context.ext.host_memory_properties.minImportedHostPointerAlignment == 4096
			&& context.ext.subgroup_properties.subgroupSize == 8
			&& context.ext.subgroup_size_control_properties.maxSubgroupSize == 32
			&& context.ext.push_descriptor_properties.maxPushDescriptors == 32, "the properties read later are filled in");

	/* destroyed with the context, once */
	granite_context_destroy(&context);
	CHECK(idle_waits == 1 && devices_destroyed == 1 && context.device == VK_NULL_HANDLE, "destroying waits, then destroys");
	granite_context_destroy(&context);
	CHECK(idle_waits == 1 && devices_destroyed == 1, "once");

	/* ---- a bare device: none of it ---- */
	standard_device();
	num_device_extensions = 0;
	has_int16 = false;
	families[0].queueCount = 1;
	num_families = 1;
	before = surface_queries;
	CHECK(granite_context_init(&context, INSTANCE, GPU(0), VK_NULL_HANDLE, NULL, 0, &required, NULL, NULL),
			"a device with no extensions is created too");
	CHECK(num_created_extensions == 0 && num_created_chain == 1 && !created_features.shaderInt16
			&& created_features.samplerAnisotropy, "with nothing it lacks");
	CHECK(!context.ext.supports_push_descriptor && !context.ext.supports_external_memory_host
			&& !context.ext.supports_driver_properties && !context.ext.supports_memory_budget
			&& !context.ext.supports_tooling_info && !context.ext.timeline_semaphore_features.timelineSemaphore,
			"and the context says so");
	CHECK(surface_queries == before, "without a surface no family is asked about presenting");
	CHECK(num_created_queues == 1 && created_queues[0].queueCount == 1
			&& context.queue_info.queues[GRANITE_QUEUE_COMPUTE] == QUEUE(0, 0)
			&& context.queue_info.queues[GRANITE_QUEUE_TRANSFER] == QUEUE(0, 0), "one queue for all three");
	/* given away: not destroyed */
	context.owned_device = false;
	before = devices_destroyed;
	granite_context_destroy(&context);
	CHECK(devices_destroyed == before && idle_waits == 2, "a device given away is waited for and left alone");
	has_int16 = true;

	/* the host memory import needs nothing but its own extension */
	standard_device();
	device_extensions[0] = VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME;
	num_device_extensions = 1;
	CHECK(granite_context_init(&context, INSTANCE, GPU(0), VK_NULL_HANDLE, NULL, 0, &required, NULL, NULL)
			&& context.ext.supports_external_memory_host && created_with(VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME)
			&& num_created_extensions == 1
			&& context.ext.host_memory_properties.minImportedHostPointerAlignment == 4096,
			"host memory is imported without the file descriptor extensions");
	granite_context_destroy(&context);

	/* ---- the frontend's own create function ---- */
	standard_device();
	before = devices_created;
	CHECK(granite_context_init(&context, INSTANCE, GPU(0), VK_NULL_HANDLE, NULL, 0, &required, factory_create,
				&factory_user) && factory_calls == 1 && devices_created == before && context.device == DEVICE
			&& created_with(VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME), "the frontend creates the device when it wants to");
	granite_context_destroy(&context);
	fail_create = true;
	CHECK(!granite_context_init(&context, INSTANCE, GPU(0), VK_NULL_HANDLE, NULL, 0, &required, factory_create,
				&factory_user) && context.device == VK_NULL_HANDLE && !context.owned_device,
			"and when it cannot there is no device");
	CHECK(!granite_context_init(&context, INSTANCE, GPU(0), VK_NULL_HANDLE, NULL, 0, &required, NULL, NULL)
			&& context.device == VK_NULL_HANDLE, "nor when the driver cannot");
	before = idle_waits;
	granite_context_destroy(&context);
	CHECK(idle_waits == before, "and nothing to destroy");
	fail_create = false;

	/* ---- what is refused ---- */
	standard_device();
	before = devices_created;
	CHECK(!granite_context_init(&context, INSTANCE, GPU(0), VK_NULL_HANDLE, missing, 2, &required, NULL, NULL),
			"an extension of the frontend's the device lacks");
	gpus[0].api = VK_API_VERSION_1_0;
	CHECK(!granite_context_init(&context, INSTANCE, GPU(0), VK_NULL_HANDLE, NULL, 0, &required, NULL, NULL),
			"a device below Vulkan 1.1");
	standard_device();
	instance_version = VK_API_VERSION_1_0;
	CHECK(!granite_context_init(&context, INSTANCE, GPU(0), VK_NULL_HANDLE, NULL, 0, &required, NULL, NULL),
			"a loader below Vulkan 1.1");
	standard_device();
	presenting = 0;
	CHECK(!granite_context_init(&context, INSTANCE, GPU(0), SURFACE, NULL, 0, &required, NULL, NULL),
			"a device that cannot present to the surface");
	CHECK(devices_created == before, "none of them creates a device");

	/* ---- a driver that wants one queue ---- */
	standard_device();
	driver_id = VK_DRIVER_ID_SAMSUNG_PROPRIETARY;
	CHECK(granite_context_init(&context, INSTANCE, GPU(0), VK_NULL_HANDLE, NULL, 0, &required, NULL, NULL)
			&& context.queue_info.queues[GRANITE_QUEUE_COMPUTE] == QUEUE(0, 0) && created_queues[0].queueCount == 1,
			"graphics and compute share a queue on a driver that schedules two badly");
	granite_context_destroy(&context);

	/* ---- no GPU named: the best there is ---- */
	standard_device();
	num_gpus = 4;
	gpus[0].type = VK_PHYSICAL_DEVICE_TYPE_CPU;
	gpus[1].type = VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
	gpus[1].api = VK_API_VERSION_1_0;
	gpus[2].type = VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;
	gpus[2].api = VK_API_VERSION_1_1;
	gpus[3].type = VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;
	gpus[3].api = VK_API_VERSION_1_2;
	CHECK(granite_context_init(&context, INSTANCE, VK_NULL_HANDLE, VK_NULL_HANDLE, NULL, 0, &required, NULL, NULL)
			&& context.gpu == GPU(2) && created_gpu == GPU(2),
			"the best device that can be used, the first of equals");
	granite_context_destroy(&context);
	gpus[1].api = VK_API_VERSION_1_1;
	CHECK(granite_context_init(&context, INSTANCE, VK_NULL_HANDLE, VK_NULL_HANDLE, NULL, 0, &required, NULL, NULL)
			&& context.gpu == GPU(1), "a discrete GPU over an integrated one");
	granite_context_destroy(&context);
	num_gpus = 0;
	CHECK(!granite_context_init(&context, INSTANCE, VK_NULL_HANDLE, VK_NULL_HANDLE, NULL, 0, &required, NULL, NULL),
			"and none without any");
}

int main(void)
{
	test_score();
	test_queues();
	test_init();

	if (failures)
	{
		fprintf(stderr, "test_vk_context: %u failure(s)\n", failures);
		return 1;
	}
	printf("test_vk_context: ok\n");
	return 0;
}
