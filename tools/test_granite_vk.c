/* test_granite_vk -- the Vulkan functions Granite calls, loaded from the
 * frontend's vkGetInstanceProcAddr (parallel-rdp/vulkan-loader/granite_vk.c),
 * in place of volk.
 *
 * - the loader's version: 1.0 without vkEnumerateInstanceVersion, what it
 *   says with it, 0 before there is a loader;
 * - each instance function from the instance, by its own name;
 * - each device function from the device, by its own name, through the
 *   instance's vkGetDeviceProcAddr; one the device does not have, NULL;
 *   without vkGetDeviceProcAddr, none.
 *
 * Built by `make check-core` (top-level Makefile). C89. Exit status 0 on
 * pass.
 */
#include <stdio.h>
#include <string.h>
#include <boolean.h>

#include "granite_vk.h"

static unsigned failures;

#define CHECK(cond, what) \
	do \
	{ \
		if (!(cond)) \
		{ \
			fprintf(stderr, "test_granite_vk: FAIL (line %d): %s\n", __LINE__, what); \
			failures++; \
		} \
	} while (0)

#define INSTANCE ((VkInstance)(size_t)0x1234)
#define DEVICE ((VkDevice)(size_t)0x5678)

static unsigned calls[128];
static void VKAPI_CALL stub0(void) { calls[0]++; }
static void VKAPI_CALL stub1(void) { calls[1]++; }
static void VKAPI_CALL stub2(void) { calls[2]++; }
static void VKAPI_CALL stub3(void) { calls[3]++; }
static void VKAPI_CALL stub4(void) { calls[4]++; }
static void VKAPI_CALL stub5(void) { calls[5]++; }
static void VKAPI_CALL stub6(void) { calls[6]++; }
static void VKAPI_CALL stub7(void) { calls[7]++; }
static void VKAPI_CALL stub8(void) { calls[8]++; }
static void VKAPI_CALL stub9(void) { calls[9]++; }
static void VKAPI_CALL stub10(void) { calls[10]++; }
static void VKAPI_CALL stub11(void) { calls[11]++; }
static void VKAPI_CALL stub12(void) { calls[12]++; }
static void VKAPI_CALL stub13(void) { calls[13]++; }
static void VKAPI_CALL stub14(void) { calls[14]++; }
static void VKAPI_CALL stub15(void) { calls[15]++; }
static void VKAPI_CALL stub16(void) { calls[16]++; }
static void VKAPI_CALL stub17(void) { calls[17]++; }
static void VKAPI_CALL stub18(void) { calls[18]++; }
static void VKAPI_CALL stub19(void) { calls[19]++; }
static void VKAPI_CALL stub20(void) { calls[20]++; }
static void VKAPI_CALL stub21(void) { calls[21]++; }
static void VKAPI_CALL stub22(void) { calls[22]++; }
static void VKAPI_CALL stub23(void) { calls[23]++; }
static void VKAPI_CALL stub24(void) { calls[24]++; }
static void VKAPI_CALL stub25(void) { calls[25]++; }
static void VKAPI_CALL stub26(void) { calls[26]++; }
static void VKAPI_CALL stub27(void) { calls[27]++; }
static void VKAPI_CALL stub28(void) { calls[28]++; }
static void VKAPI_CALL stub29(void) { calls[29]++; }
static void VKAPI_CALL stub30(void) { calls[30]++; }
static void VKAPI_CALL stub31(void) { calls[31]++; }
static void VKAPI_CALL stub32(void) { calls[32]++; }
static void VKAPI_CALL stub33(void) { calls[33]++; }
static void VKAPI_CALL stub34(void) { calls[34]++; }
static void VKAPI_CALL stub35(void) { calls[35]++; }
static void VKAPI_CALL stub36(void) { calls[36]++; }
static void VKAPI_CALL stub37(void) { calls[37]++; }
static void VKAPI_CALL stub38(void) { calls[38]++; }
static void VKAPI_CALL stub39(void) { calls[39]++; }
static void VKAPI_CALL stub40(void) { calls[40]++; }
static void VKAPI_CALL stub41(void) { calls[41]++; }
static void VKAPI_CALL stub42(void) { calls[42]++; }
static void VKAPI_CALL stub43(void) { calls[43]++; }
static void VKAPI_CALL stub44(void) { calls[44]++; }
static void VKAPI_CALL stub45(void) { calls[45]++; }
static void VKAPI_CALL stub46(void) { calls[46]++; }
static void VKAPI_CALL stub47(void) { calls[47]++; }
static void VKAPI_CALL stub48(void) { calls[48]++; }
static void VKAPI_CALL stub49(void) { calls[49]++; }
static void VKAPI_CALL stub50(void) { calls[50]++; }
static void VKAPI_CALL stub51(void) { calls[51]++; }
static void VKAPI_CALL stub52(void) { calls[52]++; }
static void VKAPI_CALL stub53(void) { calls[53]++; }
static void VKAPI_CALL stub54(void) { calls[54]++; }
static void VKAPI_CALL stub55(void) { calls[55]++; }
static void VKAPI_CALL stub56(void) { calls[56]++; }
static void VKAPI_CALL stub57(void) { calls[57]++; }
static void VKAPI_CALL stub58(void) { calls[58]++; }
static void VKAPI_CALL stub59(void) { calls[59]++; }
static void VKAPI_CALL stub60(void) { calls[60]++; }
static void VKAPI_CALL stub61(void) { calls[61]++; }
static void VKAPI_CALL stub62(void) { calls[62]++; }
static void VKAPI_CALL stub63(void) { calls[63]++; }
static void VKAPI_CALL stub64(void) { calls[64]++; }
static void VKAPI_CALL stub65(void) { calls[65]++; }
static void VKAPI_CALL stub66(void) { calls[66]++; }
static void VKAPI_CALL stub67(void) { calls[67]++; }
static void VKAPI_CALL stub68(void) { calls[68]++; }
static void VKAPI_CALL stub69(void) { calls[69]++; }
static void VKAPI_CALL stub70(void) { calls[70]++; }
static void VKAPI_CALL stub71(void) { calls[71]++; }
static void VKAPI_CALL stub72(void) { calls[72]++; }
static void VKAPI_CALL stub73(void) { calls[73]++; }
static void VKAPI_CALL stub74(void) { calls[74]++; }
static void VKAPI_CALL stub75(void) { calls[75]++; }
static void VKAPI_CALL stub76(void) { calls[76]++; }
static void VKAPI_CALL stub77(void) { calls[77]++; }
static void VKAPI_CALL stub78(void) { calls[78]++; }
static void VKAPI_CALL stub79(void) { calls[79]++; }
static void VKAPI_CALL stub80(void) { calls[80]++; }
static void VKAPI_CALL stub81(void) { calls[81]++; }
static void VKAPI_CALL stub82(void) { calls[82]++; }
static void VKAPI_CALL stub83(void) { calls[83]++; }
static void VKAPI_CALL stub84(void) { calls[84]++; }
static void VKAPI_CALL stub85(void) { calls[85]++; }
static void VKAPI_CALL stub86(void) { calls[86]++; }
static void VKAPI_CALL stub87(void) { calls[87]++; }
static void VKAPI_CALL stub88(void) { calls[88]++; }
static void VKAPI_CALL stub89(void) { calls[89]++; }
static void VKAPI_CALL stub90(void) { calls[90]++; }
static void VKAPI_CALL stub91(void) { calls[91]++; }
static void VKAPI_CALL stub92(void) { calls[92]++; }
static void VKAPI_CALL stub93(void) { calls[93]++; }
static void VKAPI_CALL stub94(void) { calls[94]++; }
static void VKAPI_CALL stub95(void) { calls[95]++; }
static void VKAPI_CALL stub96(void) { calls[96]++; }
static void VKAPI_CALL stub97(void) { calls[97]++; }
static void VKAPI_CALL stub98(void) { calls[98]++; }
static void VKAPI_CALL stub99(void) { calls[99]++; }
static void VKAPI_CALL stub100(void) { calls[100]++; }
static void VKAPI_CALL stub101(void) { calls[101]++; }
static void VKAPI_CALL stub102(void) { calls[102]++; }
static void VKAPI_CALL stub103(void) { calls[103]++; }
static void VKAPI_CALL stub104(void) { calls[104]++; }
static void VKAPI_CALL stub105(void) { calls[105]++; }
static void VKAPI_CALL stub106(void) { calls[106]++; }
static void VKAPI_CALL stub107(void) { calls[107]++; }
static void VKAPI_CALL stub108(void) { calls[108]++; }
static void VKAPI_CALL stub109(void) { calls[109]++; }
static void VKAPI_CALL stub110(void) { calls[110]++; }
static void VKAPI_CALL stub111(void) { calls[111]++; }
static void VKAPI_CALL stub112(void) { calls[112]++; }
static void VKAPI_CALL stub113(void) { calls[113]++; }
static void VKAPI_CALL stub114(void) { calls[114]++; }
static void VKAPI_CALL stub115(void) { calls[115]++; }
static void VKAPI_CALL stub116(void) { calls[116]++; }
static void VKAPI_CALL stub117(void) { calls[117]++; }
static void VKAPI_CALL stub118(void) { calls[118]++; }
static void VKAPI_CALL stub119(void) { calls[119]++; }
static void VKAPI_CALL stub120(void) { calls[120]++; }
static void VKAPI_CALL stub121(void) { calls[121]++; }
static void VKAPI_CALL stub122(void) { calls[122]++; }
static void VKAPI_CALL stub123(void) { calls[123]++; }
static void VKAPI_CALL stub124(void) { calls[124]++; }
static void VKAPI_CALL stub125(void) { calls[125]++; }
static void VKAPI_CALL stub126(void) { calls[126]++; }
static void VKAPI_CALL stub127(void) { calls[127]++; }
static void (VKAPI_CALL *const stubs[128])(void) = { stub0, stub1, stub2, stub3, stub4, stub5, stub6, stub7, stub8, stub9, stub10, stub11, stub12, stub13, stub14, stub15, stub16, stub17, stub18, stub19, stub20, stub21, stub22, stub23, stub24, stub25, stub26, stub27, stub28, stub29, stub30, stub31, stub32, stub33, stub34, stub35, stub36, stub37, stub38, stub39, stub40, stub41, stub42, stub43, stub44, stub45, stub46, stub47, stub48, stub49, stub50, stub51, stub52, stub53, stub54, stub55, stub56, stub57, stub58, stub59, stub60, stub61, stub62, stub63, stub64, stub65, stub66, stub67, stub68, stub69, stub70, stub71, stub72, stub73, stub74, stub75, stub76, stub77, stub78, stub79, stub80, stub81, stub82, stub83, stub84, stub85, stub86, stub87, stub88, stub89, stub90, stub91, stub92, stub93, stub94, stub95, stub96, stub97, stub98, stub99, stub100, stub101, stub102, stub103, stub104, stub105, stub106, stub107, stub108, stub109, stub110, stub111, stub112, stub113, stub114, stub115, stub116, stub117, stub118, stub119, stub120, stub121, stub122, stub123, stub124, stub125, stub126, stub127 };

/* the names asked for, each its own stub */
static const char *names[128];
static unsigned num_names;
static bool have_version = true, have_device_proc = true;
static VkInstance asked_instance;
static VkDevice asked_device;
static unsigned bad_handles;

static PFN_vkVoidFunction stub_for(const char *name)
{
	unsigned i;
	for (i = 0; i < num_names; i++)
		if (!strcmp(names[i], name))
			return (PFN_vkVoidFunction)stubs[i];
	if (num_names == 128)
		return NULL;
	names[num_names] = name;
	return (PFN_vkVoidFunction)stubs[num_names++];
}

static VkResult VKAPI_CALL enumerate_version(uint32_t *version)
{
	*version = VK_MAKE_API_VERSION(0, 1, 3, 250);
	return VK_SUCCESS;
}

static PFN_vkVoidFunction VKAPI_CALL get_device_proc(VkDevice device, const char *name)
{
	if (device != DEVICE)
		bad_handles++;
	asked_device = device;
	/* (a function the device does not have) */
	if (!strcmp(name, "vkCmdPushDescriptorSetWithTemplateKHR"))
		return NULL;
	return stub_for(name);
}

static PFN_vkVoidFunction VKAPI_CALL get_instance_proc(VkInstance instance, const char *name)
{
	if (!strcmp(name, "vkEnumerateInstanceVersion"))
	{
		if (instance != VK_NULL_HANDLE)
			bad_handles++;
		return have_version ? (PFN_vkVoidFunction)enumerate_version : NULL;
	}
	if (instance != INSTANCE)
		bad_handles++;
	asked_instance = instance;
	if (!strcmp(name, "vkGetDeviceProcAddr"))
		return have_device_proc ? (PFN_vkVoidFunction)get_device_proc : NULL;
	return stub_for(name);
}

static bool is(PFN_vkVoidFunction f, const char *name)
{
	return f && f == stub_for(name);
}

int main(void)
{
	struct granite_vk_device_table table;
	unsigned before;

	CHECK(granite_vk_instance_version() == 0, "no loader, no version");
	granite_vk_init_loader(get_instance_proc);
	CHECK(granite_vk_instance_version() == VK_MAKE_API_VERSION(0, 1, 3, 250), "the loader's version");
	have_version = false;
	CHECK(granite_vk_instance_version() == VK_API_VERSION_1_0, "1.0 without vkEnumerateInstanceVersion");

	granite_vk_load_instance(INSTANCE);
	CHECK(asked_instance == INSTANCE && !bad_handles, "from the instance");
#define CHECK_INSTANCE(name) CHECK(name == (PFN_##name)get_device_proc || is((PFN_vkVoidFunction)name, #name), #name);
	GRANITE_VK_INSTANCE_FUNCTIONS(CHECK_INSTANCE)
#undef CHECK_INSTANCE
	CHECK(vkGetDeviceProcAddr == get_device_proc, "vkGetDeviceProcAddr, the instance's");

	before = num_names;
	granite_vk_load_device_table(&table, DEVICE);
	CHECK(asked_device == DEVICE && !bad_handles && table.loaded, "from the device");
	CHECK(num_names > before, "(by their own names)");
#define CHECK_DEVICE(name) \
	CHECK(!strcmp(#name, "vkCmdPushDescriptorSetWithTemplateKHR") ? !table.name : is((PFN_vkVoidFunction)table.name, #name), #name);
	GRANITE_VK_DEVICE_FUNCTIONS(CHECK_DEVICE)
#undef CHECK_DEVICE
	CHECK(!table.vkCmdPushDescriptorSetWithTemplateKHR, "one the device does not have, NULL");

	/* calling through the table calls what the device gave */
	{
		unsigned i, at = 128;
		for (i = 0; i < num_names; i++)
			if (!strcmp(names[i], "vkCmdDispatch"))
				at = i;
		memset(calls, 0, sizeof(calls));
		((void (VKAPI_CALL *)(void))table.vkCmdDispatch)();
		CHECK(at < 128 && calls[at] == 1, "a call through the table reaches what the device gave");
	}

	have_device_proc = false;
	granite_vk_load_instance(INSTANCE);
	memset(&table, 0x5a, sizeof(table));
	granite_vk_load_device_table(&table, DEVICE);
	CHECK(!table.vkCmdDispatch && !table.vkQueueSubmit && !table.loaded, "without vkGetDeviceProcAddr, none");

	if (failures)
	{
		fprintf(stderr, "test_granite_vk: %u failure(s)\n", failures);
		return 1;
	}
	printf("test_granite_vk: ok\n");
	return 0;
}
