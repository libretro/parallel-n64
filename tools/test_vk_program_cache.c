/* parallel-rdp's shader, program and layout caches
 * (vulkan/program_cache.c), with the real layouts, set allocators and
 * pipeline maps through a stand-in device table.
 *
 * An object cache keeps objects by hash without a lock: one found is
 * the one added, a second with the same hash yields to the first, and
 * four threads adding the same hashes at once end up with one object a
 * hash and every other handed back.
 *
 * The same SPIR-V gives the same shader, made once, with the hash the
 * saved pipeline records carry. A program is its shaders: one compute
 * shader, or a vertex and a fragment shader, in that order; asked for
 * again it is the same program. Its pipeline layout comes from the
 * shaders' layouts, and programs with the same layout share one, as
 * layouts with the same sets share set allocators. A set the device can
 * push is pushed. A render pass is made once for a description.
 * Freeing the cache destroys every module, layout, template and render
 * pass once and hands each program's pipelines back. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <boolean.h>
#include <retro_atomic.h>
#include <rthreads/rthreads.h>

#include "program_cache.h"

static unsigned failures;

#define CHECK(cond, what) \
	do \
	{ \
		if (!(cond)) \
		{ \
			fprintf(stderr, "test_vk_program_cache: FAIL (line %d): %s\n", __LINE__, what); \
			failures++; \
		} \
	} while (0)

#define DEV ((VkDevice)(size_t)1)

/* ---- the stand-in device ---- */
static unsigned modules_created, modules_destroyed, set_layouts_created, set_layouts_destroyed;
static unsigned pipeline_layouts_created, pipeline_layouts_destroyed, templates_created, templates_destroyed;
static unsigned push_templates;
static size_t module_code_size;
static const uint32_t *module_code;
static bool fail_module;

static VkResult VKAPI_CALL fake_create_module(VkDevice d, const VkShaderModuleCreateInfo *info,
		const VkAllocationCallbacks *a, VkShaderModule *module)
{
	(void)a;
	CHECK(d == DEV && info->sType == VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, "a well-formed module");
	if (fail_module)
	{
		*module = (VkShaderModule)(size_t)0xdead;
		return VK_ERROR_OUT_OF_HOST_MEMORY;
	}
	module_code_size = info->codeSize;
	module_code = info->pCode;
	modules_created++;
	*module = (VkShaderModule)(size_t)(0x100 + modules_created);
	return VK_SUCCESS;
}

static void VKAPI_CALL fake_destroy_module(VkDevice d, VkShaderModule module, const VkAllocationCallbacks *a)
{
	(void)d; (void)a;
	CHECK(module != VK_NULL_HANDLE && module != (VkShaderModule)(size_t)0xdead, "only modules that were made are destroyed");
	modules_destroyed++;
}

static VkResult VKAPI_CALL fake_create_set_layout(VkDevice d, const VkDescriptorSetLayoutCreateInfo *info,
		const VkAllocationCallbacks *a, VkDescriptorSetLayout *layout)
{
	(void)d; (void)info; (void)a;
	set_layouts_created++;
	*layout = (VkDescriptorSetLayout)(size_t)(0x200 + set_layouts_created);
	return VK_SUCCESS;
}

static void VKAPI_CALL fake_destroy_set_layout(VkDevice d, VkDescriptorSetLayout layout, const VkAllocationCallbacks *a)
{
	(void)d; (void)layout; (void)a;
	set_layouts_destroyed++;
}

static VkResult VKAPI_CALL fake_create_pipeline_layout(VkDevice d, const VkPipelineLayoutCreateInfo *info,
		const VkAllocationCallbacks *a, VkPipelineLayout *layout)
{
	(void)d; (void)info; (void)a;
	pipeline_layouts_created++;
	*layout = (VkPipelineLayout)(size_t)(0x300 + pipeline_layouts_created);
	return VK_SUCCESS;
}

static void VKAPI_CALL fake_destroy_pipeline_layout(VkDevice d, VkPipelineLayout layout, const VkAllocationCallbacks *a)
{
	(void)d; (void)layout; (void)a;
	pipeline_layouts_destroyed++;
}

static VkResult VKAPI_CALL fake_create_template(VkDevice d, const VkDescriptorUpdateTemplateCreateInfo *info,
		const VkAllocationCallbacks *a, VkDescriptorUpdateTemplate *templ)
{
	(void)d; (void)a;
	templates_created++;
	if (info->templateType == VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_PUSH_DESCRIPTORS_KHR)
		push_templates++;
	*templ = (VkDescriptorUpdateTemplate)(size_t)(0x400 + templates_created);
	return VK_SUCCESS;
}

static void VKAPI_CALL fake_destroy_template(VkDevice d, VkDescriptorUpdateTemplate templ, const VkAllocationCallbacks *a)
{
	(void)d; (void)templ; (void)a;
	templates_destroyed++;
}

static unsigned render_passes_created, render_passes_destroyed;

static VkResult VKAPI_CALL fake_create_render_pass(VkDevice d, const VkRenderPassCreateInfo *info,
		const VkAllocationCallbacks *a, VkRenderPass *pass)
{
	(void)d; (void)info; (void)a;
	render_passes_created++;
	*pass = (VkRenderPass)(size_t)(0x500 + render_passes_created);
	return VK_SUCCESS;
}

static void VKAPI_CALL fake_destroy_render_pass(VkDevice d, VkRenderPass pass, const VkAllocationCallbacks *a)
{
	(void)d; (void)pass; (void)a;
	render_passes_destroyed++;
}

static struct VolkDeviceTable table;

/* ---- the object cache alone ---- */

typedef struct thing
{
	granite_cache_node_t node;
	unsigned tag;
} thing_t;

static unsigned visited, disposed;
static uint64_t visited_sum;

static void visit_thing(void *userdata, granite_cache_node_t *node)
{
	CHECK(userdata == &visited, "the caller's pointer comes back");
	visited++;
	visited_sum += node->hash;
}

static void dispose_thing(void *userdata, granite_cache_node_t *node)
{
	(void)userdata;
	disposed++;
	free(node);
}

static thing_t *new_thing(uint64_t hash, unsigned tag)
{
	thing_t *t = (thing_t *)calloc(1, sizeof(*t));
	t->node.hash = hash;
	t->tag = tag;
	return t;
}

static void test_object_cache(void)
{
	granite_object_cache_t cache;
	thing_t *a, *b, *same, *c;

	granite_object_cache_init(&cache);
	CHECK(granite_object_cache_find(&cache, 5) == NULL, "an empty cache has nothing");
	granite_object_cache_for_each(&cache, visit_thing, &visited);
	CHECK(visited == 0, "and nothing to visit");

	a = new_thing(5, 1);
	/* (the same bucket as 5) */
	b = new_thing(5 + GRANITE_OBJECT_CACHE_BUCKETS, 2);
	c = new_thing(6, 3);
	CHECK(granite_object_cache_add(&cache, &a->node) == &a->node && granite_object_cache_add(&cache, &b->node) == &b->node
			&& granite_object_cache_add(&cache, &c->node) == &c->node, "objects with their own hashes are added");
	CHECK(granite_object_cache_find(&cache, 5) == &a->node
			&& granite_object_cache_find(&cache, 5 + GRANITE_OBJECT_CACHE_BUCKETS) == &b->node
			&& granite_object_cache_find(&cache, 6) == &c->node, "each is found by its hash, also when two share a bucket");
	CHECK(granite_object_cache_find(&cache, 7) == NULL && granite_object_cache_find(&cache, 5 + 2 * GRANITE_OBJECT_CACHE_BUCKETS) == NULL,
			"a hash nothing was added for finds nothing");

	same = new_thing(5, 9);
	CHECK(granite_object_cache_add(&cache, &same->node) == &a->node, "a second object for a hash yields to the first");
	free(same);

	granite_object_cache_for_each(&cache, visit_thing, &visited);
	CHECK(visited == 3 && visited_sum == 5 + 5 + GRANITE_OBJECT_CACHE_BUCKETS + 6, "each is visited once");

	granite_object_cache_free(&cache, dispose_thing, NULL);
	CHECK(disposed == 3 && granite_object_cache_find(&cache, 5) == NULL, "freeing disposes of each once and empties the cache");
	granite_object_cache_free(&cache, dispose_thing, NULL);
	CHECK(disposed == 3, "an empty cache has nothing to dispose of");
}

#define THREADS 4
#define HASHES  2000

static granite_object_cache_t shared;
static retro_atomic_int_t yielded;
static thing_t *winners[THREADS][HASHES];

static void adder(void *data)
{
	unsigned thread = (unsigned)(size_t)data;
	unsigned i;
	for (i = 0; i < HASHES; i++)
	{
		thing_t *mine = new_thing(1000 + i, thread);
		thing_t *kept = (thing_t *)granite_object_cache_add(&shared, &mine->node);
		if (kept != mine)
		{
			free(mine);
			retro_atomic_fetch_add_int(&yielded, 1);
		}
		winners[thread][i] = kept;
	}
}

static void test_object_cache_threads(void)
{
	sthread_t *threads[THREADS];
	unsigned t, i, disagree = 0;

	granite_object_cache_init(&shared);
	retro_atomic_int_init(&yielded, 0);
	for (t = 0; t < THREADS; t++)
		threads[t] = sthread_create(adder, (void *)(size_t)t);
	for (t = 0; t < THREADS; t++)
	{
		if (threads[t])
			sthread_join(threads[t]);
		else
			adder((void *)(size_t)t);
	}

	for (i = 0; i < HASHES; i++)
		for (t = 1; t < THREADS; t++)
			if (winners[t][i] != winners[0][i] || winners[0][i] != (thing_t *)granite_object_cache_find(&shared, 1000 + i))
				disagree++;
	CHECK(disagree == 0, "every thread was handed the same object for a hash");
	CHECK(retro_atomic_load_acquire_int(&yielded) == (THREADS - 1) * HASHES, "and all but one object a hash yielded");
	visited = 0;
	granite_object_cache_for_each(&shared, visit_thing, &visited);
	CHECK(visited == HASHES, "one object a hash is in the cache");
	disposed = 0;
	granite_object_cache_free(&shared, dispose_thing, NULL);
	CHECK(disposed == HASHES, "and disposed of");
}

/* ---- the device's caches ---- */

static unsigned pipelines_destroyed;
static int destroy_token;

static void destroy_pipeline(void *userdata, const granite_pipeline_t *pipeline)
{
	CHECK(userdata == &destroy_token && pipeline->pipeline != VK_NULL_HANDLE, "a pipeline that was added comes back");
	pipelines_destroyed++;
}

static void test_program_cache(void)
{
	static const uint32_t code_a[] = { 0x07230203u, 1, 2, 3 };
	static const uint32_t code_a_again[] = { 0x07230203u, 1, 2, 3 };
	static const uint32_t code_b[] = { 0x07230203u, 1, 2, 4 };
	static const uint32_t code_c[] = { 0x07230203u, 9, 9, 9, 9 };
	granite_program_cache_t cache;
	granite_resource_layout_t ubo_layout, ssbo_layout;
	granite_shader_t *a, *b, *c, *bad;
	granite_program_t *compute, *compute_b, *graphics, *swapped;
	granite_cached_set_allocator_t *alloc, *alloc_push;
	granite_pipeline_t pipeline;
	granite_render_pass_desc_t pass_desc;
	granite_cached_render_pass_t *pass, *pass_clear;
	uint64_t expect;
	unsigned before;

	/* a uniform buffer at set 0, binding 0; a storage buffer at set 1,
	 * binding 2 */
	memset(&ubo_layout, 0, sizeof(ubo_layout));
	ubo_layout.sets[0].uniform_buffer_mask = 1u << 0;
	ubo_layout.sets[0].array_size[0] = 1;
	memset(&ssbo_layout, 0, sizeof(ssbo_layout));
	ssbo_layout.sets[1].storage_buffer_mask = 1u << 2;
	ssbo_layout.sets[1].array_size[2] = 1;

	granite_program_cache_init(&cache, DEV, &table, true, 32, 8, 2);

	/* ---- shaders ---- */
	a = granite_program_cache_shader(&cache, code_a, sizeof(code_a), &ubo_layout);
	CHECK(a && a->module != VK_NULL_HANDLE && modules_created == 1 && module_code == code_a
			&& module_code_size == sizeof(code_a), "a shader is made from its code");
	CHECK(!memcmp(&a->layout, &ubo_layout, sizeof(ubo_layout)), "with its layout");
	expect = granite_hash_u32(granite_hash_u32(granite_hash_u32(granite_hash_u32(GRANITE_HASH_INIT, 0x07230203u), 1), 2), 3);
	CHECK(a->node.hash == expect && expect == granite_hash_words(GRANITE_HASH_INIT, code_a, sizeof(code_a)),
			"its hash is the hash of its words");
	CHECK(granite_program_cache_shader(&cache, code_a_again, sizeof(code_a_again), NULL) == a && modules_created == 1,
			"the same code is the same shader, made once");
	CHECK(granite_program_cache_find_shader(&cache, expect) == a && granite_program_cache_find_shader(&cache, expect + 1) == NULL,
			"and is found by its hash");
	b = granite_program_cache_shader(&cache, code_b, sizeof(code_b), &ssbo_layout);
	c = granite_program_cache_shader(&cache, code_c, sizeof(code_c), NULL);
	CHECK(b && c && b != a && c != a && c != b && modules_created == 3, "other code is another shader");
	CHECK(c->layout.sets[0].uniform_buffer_mask == 0 && c->layout.push_constant_size == 0, "no layout given: an empty one");

	fail_module = true;
	bad = granite_program_cache_shader(&cache, code_c, sizeof(code_c) - 4, NULL);
	CHECK(bad && bad->module == VK_NULL_HANDLE, "a module the driver refuses leaves a shader without one");
	fail_module = false;

	/* ---- programs ---- */
	CHECK(granite_program_cache_compute(&cache, NULL) == NULL && granite_program_cache_graphics(&cache, a, NULL) == NULL
			&& granite_program_cache_graphics(&cache, NULL, b) == NULL, "no program without its shaders");

	compute = granite_program_cache_compute(&cache, a);
	CHECK(compute && compute->shaders[GRANITE_SHADER_STAGE_COMPUTE] == a && compute->shaders[GRANITE_SHADER_STAGE_VERTEX] == NULL
			&& compute->shaders[GRANITE_SHADER_STAGE_FRAGMENT] == NULL, "a compute program has its one shader");
	CHECK(compute->node.hash == granite_hash_u64(GRANITE_HASH_INIT, a->node.hash), "and the hash of that shader's hash");
	CHECK(compute->layout && compute->layout->impl.layout != VK_NULL_HANDLE && pipeline_layouts_created == 1
			&& compute->layout->layout.sets[0].uniform_buffer_mask == 1
			&& compute->layout->layout.stages_for_bindings[0][0] == VK_SHADER_STAGE_COMPUTE_BIT,
			"its pipeline layout is its shader's, for the compute stage");
	CHECK(compute->layout->bind.layout == compute->layout->impl.layout && compute->layout->bind.hash == compute->layout->node.hash
			&& compute->layout->bind.resource_layout == &compute->layout->layout
			&& compute->layout->bind.update_templates == compute->layout->impl.update_template
			&& compute->layout->bind.allocators[0] != NULL && compute->layout->bind.allocators[3] != NULL,
			"and what binds against it points into it");
	CHECK(compute->layout->bind.push_set_mask == 1 && push_templates == 1 && compute->layout->bind.allocators[0]->push,
			"set 0 is pushed on a device that pushes");
	CHECK(granite_program_cache_compute(&cache, a) == compute && pipeline_layouts_created == 1, "asked for again, the same program");
	CHECK(granite_program_cache_find_program(&cache, compute->node.hash) == compute
			&& granite_program_cache_find_program(&cache, 12345) == NULL, "found by its hash");

	/* another shader with the same layout shares the pipeline layout */
	ubo_layout.input_mask = 0;
	compute_b = granite_program_cache_compute(&cache, granite_program_cache_shader(&cache, code_c, 8, &ubo_layout));
	CHECK(compute_b && compute_b != compute && compute_b->layout == compute->layout && pipeline_layouts_created == 1,
			"programs with one layout share the pipeline layout");

	before = set_layouts_created;
	graphics = granite_program_cache_graphics(&cache, a, b);
	CHECK(graphics && graphics->shaders[GRANITE_SHADER_STAGE_VERTEX] == a && graphics->shaders[GRANITE_SHADER_STAGE_FRAGMENT] == b
			&& graphics->shaders[GRANITE_SHADER_STAGE_COMPUTE] == NULL, "a graphics program has its two shaders");
	CHECK(graphics->node.hash == granite_hash_u64(granite_hash_u64(GRANITE_HASH_INIT, a->node.hash), b->node.hash),
			"and the hash of both hashes, vertex first");
	CHECK(graphics->layout != compute->layout && pipeline_layouts_created == 2
			&& graphics->layout->layout.stages_for_bindings[0][0] == VK_SHADER_STAGE_VERTEX_BIT
			&& graphics->layout->layout.stages_for_bindings[1][2] == VK_SHADER_STAGE_FRAGMENT_BIT
			&& graphics->layout->layout.sets[1].storage_buffer_mask == 4, "its layout combines the two");
	/* its sets 2 and 3 are empty like the compute program's: shared */
	CHECK(graphics->layout->bind.allocators[3] == compute->layout->bind.allocators[3]
			&& graphics->layout->bind.allocators[0] != compute->layout->bind.allocators[0]
			&& set_layouts_created == before + 2, "layouts with the same set share its allocator");
	swapped = granite_program_cache_graphics(&cache, b, a);
	CHECK(swapped && swapped != graphics && swapped->node.hash != graphics->node.hash, "the shaders the other way round are another program");
	CHECK(granite_program_cache_graphics(&cache, a, b) == graphics, "asked for again, the same program");

	/* ---- set allocators ---- */
	alloc = granite_program_cache_set_allocator(&cache, &graphics->layout->layout.sets[1], graphics->layout->layout.stages_for_bindings[1], false);
	CHECK(alloc && &alloc->impl == graphics->layout->bind.allocators[1], "an allocator is found by its set and stages");
	before = set_layouts_created;
	alloc_push = granite_program_cache_set_allocator(&cache, &graphics->layout->layout.sets[1], graphics->layout->layout.stages_for_bindings[1], true);
	CHECK(alloc_push && alloc_push != alloc && alloc_push->impl.push && !alloc->impl.push && set_layouts_created == before + 1,
			"a pushed set is another allocator than an allocated one");
	CHECK(granite_program_cache_set_allocator(&cache, &graphics->layout->layout.sets[1], compute->layout->layout.stages_for_bindings[1], false) != alloc,
			"and so is the same set for other stages");

	CHECK(granite_program_cache_set_allocator(&cache, &graphics->layout->layout.sets[0], graphics->layout->layout.stages_for_bindings[1], false) != alloc,
			"and another set for the same stages");

	/* (nothing allocated: these only have to reach every allocator) */
	granite_program_cache_begin_frame(&cache);
	granite_program_cache_clear_sets(&cache);

	/* ---- render passes ---- */
	memset(&pass_desc, 0, sizeof(pass_desc));
	pass_desc.num_color = 1;
	pass_desc.color[0].format = VK_FORMAT_R8G8B8A8_UNORM;
	pass_desc.color[0].samples = 1;
	pass_desc.store_attachments = 1;
	pass = granite_program_cache_render_pass(&cache, &pass_desc, false);
	CHECK(pass && pass->impl.render_pass != VK_NULL_HANDLE && render_passes_created == 1 && pass->impl.num_color == 1
			&& pass->node.hash == granite_render_pass_hash(&pass_desc, false), "a render pass is made from its description");
	CHECK(granite_program_cache_render_pass(&cache, &pass_desc, false) == pass && render_passes_created == 1
			&& granite_program_cache_find_render_pass(&cache, pass->node.hash) == pass
			&& granite_program_cache_find_render_pass(&cache, pass->node.hash + 1) == NULL,
			"asked for again it is the same pass, found by its hash");
	pass_desc.clear_attachments = 1;
	pass_clear = granite_program_cache_render_pass(&cache, &pass_desc, false);
	CHECK(pass_clear && pass_clear != pass && render_passes_created == 2, "a pass that clears is another pass");
	CHECK(granite_program_cache_render_pass(&cache, &pass_desc, true)->node.hash == granite_render_pass_hash(&pass_desc, true),
			"a compatible pass goes by the compatible hash");

	/* ---- freed ---- */
	pipeline.pipeline = (VkPipeline)(size_t)0x900;
	pipeline.dynamic_mask = 0;
	granite_pipeline_map_add(&compute->pipelines, 1, &pipeline);
	granite_pipeline_map_add(&compute->pipelines, 2, &pipeline);
	granite_pipeline_map_add(&graphics->pipelines, 1, &pipeline);
	granite_program_cache_free(&cache, destroy_pipeline, &destroy_token);
	CHECK(pipelines_destroyed == 3, "each program's pipelines are handed back");
	CHECK(modules_created == 4 && modules_destroyed == 4, "every module that was made is destroyed, and no other");
	CHECK(pipeline_layouts_created == pipeline_layouts_destroyed && pipeline_layouts_created == 3
			&& templates_created == templates_destroyed && templates_created > 0
			&& set_layouts_created == set_layouts_destroyed && set_layouts_created > 0,
			"and every layout and template");
	CHECK(render_passes_created == render_passes_destroyed && render_passes_created >= 2, "and every render pass");
	CHECK(granite_program_cache_find_shader(&cache, expect) == NULL, "nothing is left in it");

	/* a device that does not push */
	push_templates = 0;
	granite_program_cache_init(&cache, DEV, &table, false, 0, 8, 1);
	a = granite_program_cache_shader(&cache, code_a, sizeof(code_a), &ubo_layout);
	compute = granite_program_cache_compute(&cache, a);
	CHECK(compute && compute->layout->bind.push_set_mask == 0 && push_templates == 0 && !compute->layout->bind.allocators[0]->push,
			"without push descriptors no set is pushed");
	granite_program_cache_free(&cache, NULL, NULL);
}

int main(void)
{
	memset(&table, 0, sizeof(table));
	table.vkCreateShaderModule = fake_create_module;
	table.vkDestroyShaderModule = fake_destroy_module;
	table.vkCreateDescriptorSetLayout = fake_create_set_layout;
	table.vkDestroyDescriptorSetLayout = fake_destroy_set_layout;
	table.vkCreatePipelineLayout = fake_create_pipeline_layout;
	table.vkDestroyPipelineLayout = fake_destroy_pipeline_layout;
	table.vkCreateDescriptorUpdateTemplate = fake_create_template;
	table.vkDestroyDescriptorUpdateTemplate = fake_destroy_template;
	table.vkCreateRenderPass = fake_create_render_pass;
	table.vkDestroyRenderPass = fake_destroy_render_pass;

	test_object_cache();
	test_object_cache_threads();
	test_program_cache();

	if (failures)
	{
		fprintf(stderr, "test_vk_program_cache: %u failure(s)\n", failures);
		return 1;
	}
	printf("test_vk_program_cache: ok\n");
	return 0;
}
