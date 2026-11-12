/* parallel-rdp's shader and pipeline layouts (vulkan/shader.c), through a
 * stand-in device table.
 *
 * A stored layout is read back as it was written, and one of another
 * size, version or magic is refused and leaves the layout alone.
 *
 * Combining the shaders of a program ORs their bindings, says which
 * stages use each set and binding, takes the vertex inputs and fragment
 * outputs, the larger push constant block for all the stages that have
 * one, and each stage's specialization constants. Baking gives unused
 * bindings one element, lists the sets in use, and reports an array that
 * another binding lies inside or that runs past the last binding. Two
 * shaders that disagree on an array size are reported. The hash differs
 * for layouts that differ and is equal for equal ones.
 *
 * Set 0 is a push set when the device pushes descriptors and takes all of
 * it, and not otherwise.
 *
 * The pipeline layout has the sets up to the last one in use and the push
 * constant range. Each set in use gets an update template with an entry
 * for each binding, of its kind, reading the right member of the binding
 * array; a push set's template is a push template with plain uniform
 * buffers. A refused layout or template leaves nothing dangling, and
 * everything made is destroyed. */
#include <stdio.h>
#include <string.h>
#include <boolean.h>

#include "shader.h"

static unsigned failures;

#define CHECK(cond, what) \
	do \
	{ \
		if (!(cond)) \
		{ \
			fprintf(stderr, "test_vk_shader: FAIL (line %d): %s\n", __LINE__, what); \
			failures++; \
		} \
	} while (0)

static VkPipelineLayoutCreateInfo pl_info;
static VkDescriptorSetLayout pl_sets[GRANITE_NUM_DESCRIPTOR_SETS];
static VkPushConstantRange pl_range;
static VkDescriptorUpdateTemplateCreateInfo t_info[GRANITE_NUM_DESCRIPTOR_SETS];
static VkDescriptorUpdateTemplateEntry t_entries[GRANITE_NUM_DESCRIPTOR_SETS][GRANITE_NUM_BINDINGS];
static unsigned layouts_created, layouts_destroyed, templates_created, templates_destroyed;
static bool fail_layout;
static int fail_template_set = -1;

static VkResult VKAPI_CALL fake_create_pipeline_layout(VkDevice d, const VkPipelineLayoutCreateInfo *info,
		const VkAllocationCallbacks *a, VkPipelineLayout *layout)
{
	uint32_t i;
	(void)d; (void)a;
	if (fail_layout)
	{
		/* a failed call leaves the handle undefined */
		*layout = (VkPipelineLayout)(size_t)0xdead;
		return VK_ERROR_OUT_OF_HOST_MEMORY;
	}
	pl_info = *info;
	for (i = 0; i < info->setLayoutCount; i++)
		pl_sets[i] = info->pSetLayouts[i];
	if (info->pushConstantRangeCount)
		pl_range = info->pPushConstantRanges[0];
	layouts_created++;
	*layout = (VkPipelineLayout)(size_t)0x1000;
	return VK_SUCCESS;
}

static void VKAPI_CALL fake_destroy_pipeline_layout(VkDevice d, VkPipelineLayout layout, const VkAllocationCallbacks *a)
{
	(void)d; (void)a;
	CHECK(layout == (VkPipelineLayout)(size_t)0x1000, "the layout made is the one destroyed");
	layouts_destroyed++;
}

static VkResult VKAPI_CALL fake_create_template(VkDevice d, const VkDescriptorUpdateTemplateCreateInfo *info,
		const VkAllocationCallbacks *a, VkDescriptorUpdateTemplate *templ)
{
	uint32_t i;
	(void)d; (void)a;
	if ((int)info->set == fail_template_set)
	{
		*templ = (VkDescriptorUpdateTemplate)(size_t)0xdead;
		return VK_ERROR_OUT_OF_HOST_MEMORY;
	}
	t_info[info->set] = *info;
	for (i = 0; i < info->descriptorUpdateEntryCount; i++)
		t_entries[info->set][i] = info->pDescriptorUpdateEntries[i];
	templates_created++;
	*templ = (VkDescriptorUpdateTemplate)(size_t)(0x2000 + info->set);
	return VK_SUCCESS;
}

static void VKAPI_CALL fake_destroy_template(VkDevice d, VkDescriptorUpdateTemplate templ, const VkAllocationCallbacks *a)
{
	(void)d; (void)templ; (void)a;
	templates_destroyed++;
}

static const VkDescriptorUpdateTemplateEntry *entry_of(unsigned set, uint32_t binding)
{
	uint32_t i;
	for (i = 0; i < t_info[set].descriptorUpdateEntryCount; i++)
		if (t_entries[set][i].dstBinding == binding)
			return &t_entries[set][i];
	return NULL;
}

static void test_unserialize(void)
{
	uint8_t stored[GRANITE_RESOURCE_LAYOUT_STORED_SIZE + 1];
	uint16_t magic[4];
	granite_resource_layout_t in, out, before;

	CHECK(sizeof(granite_descriptor_set_layout_t) == 80, "a set layout is stored in 80 bytes");
	CHECK(GRANITE_RESOURCE_LAYOUT_STORED_SIZE == 348, "a stored layout is the 348 bytes the shader bank holds");

	magic[0] = 'G'; magic[1] = 'R'; magic[2] = 'A'; magic[3] = 3;
	memset(&in, 0, sizeof(in));
	in.sets[1].uniform_buffer_mask = 5;
	in.sets[3].array_size[31] = 7;
	in.input_mask = 0x11;
	in.output_mask = 0x22;
	in.push_constant_size = 64;
	in.spec_constant_mask = 0x33;
	in.bindless_set_mask = 0x44;
	memcpy(stored, magic, sizeof(magic));
	memcpy(stored + sizeof(magic), &in, sizeof(in));

	memset(&out, 0xee, sizeof(out));
	CHECK(granite_resource_layout_unserialize(&out, stored, GRANITE_RESOURCE_LAYOUT_STORED_SIZE), "a stored layout is read");
	CHECK(memcmp(&in, &out, sizeof(in)) == 0, "and is what was stored");
	CHECK(out.sets[3].array_size[31] == 7 && out.bindless_set_mask == 0x44, "to its last members");

	memset(&out, 0xee, sizeof(out));
	before = out;
	CHECK(!granite_resource_layout_unserialize(&out, stored, GRANITE_RESOURCE_LAYOUT_STORED_SIZE - 1), "a short one is refused");
	CHECK(!granite_resource_layout_unserialize(&out, stored, GRANITE_RESOURCE_LAYOUT_STORED_SIZE + 1), "a long one is refused");
	stored[6] = 2;
	stored[7] = 0;
	CHECK(!granite_resource_layout_unserialize(&out, stored, GRANITE_RESOURCE_LAYOUT_STORED_SIZE), "another version is refused");
	memcpy(stored, magic, sizeof(magic));
	stored[0] = 'X';
	CHECK(!granite_resource_layout_unserialize(&out, stored, GRANITE_RESOURCE_LAYOUT_STORED_SIZE), "another magic is refused");
	CHECK(memcmp(&before, &out, sizeof(out)) == 0, "a refused one leaves the layout alone");
}

/* A vertex and a fragment shader sharing a uniform buffer */
static void make_program(granite_combined_layout_t *c)
{
	granite_resource_layout_t vert, frag;

	memset(&vert, 0, sizeof(vert));
	memset(&frag, 0, sizeof(frag));

	vert.input_mask = 0x7;
	vert.output_mask = 0xf0;
	vert.sets[0].uniform_buffer_mask = 1u << 0;
	vert.sets[0].array_size[0] = 1;
	vert.push_constant_size = 16;
	vert.spec_constant_mask = 0x1;

	frag.input_mask = 0xf0;
	frag.output_mask = 0x3;
	frag.sets[0].uniform_buffer_mask = 1u << 0;
	frag.sets[0].array_size[0] = 1;
	frag.sets[0].sampled_image_mask = (1u << 1) | (1u << 5);
	frag.sets[0].fp_mask = 1u << 1;
	frag.sets[0].array_size[1] = 3;
	frag.sets[0].array_size[5] = 1;
	frag.sets[2].storage_buffer_mask = 1u << 4;
	frag.sets[2].sampled_texel_buffer_mask = 1u << 6;
	frag.sets[2].storage_texel_buffer_mask = 1u << 7;
	frag.sets[2].separate_image_mask = 1u << 8;
	frag.sets[2].sampler_mask = 1u << 9;
	frag.sets[2].storage_image_mask = (1u << 10) | (1u << 11);
	frag.sets[2].fp_mask = (1u << 8) | (1u << 10);
	frag.sets[2].input_attachment_mask = 1u << 12;
	frag.sets[2].array_size[4] = 1;
	frag.sets[2].array_size[6] = 1;
	frag.sets[2].array_size[7] = 1;
	frag.sets[2].array_size[8] = 1;
	frag.sets[2].array_size[9] = 1;
	frag.sets[2].array_size[10] = 1;
	frag.sets[2].array_size[11] = 1;
	frag.sets[2].array_size[12] = 1;
	frag.push_constant_size = 40;
	frag.spec_constant_mask = 0x6;

	granite_combined_layout_init(c);
	CHECK(granite_combined_layout_add(c, GRANITE_SHADER_STAGE_VERTEX, &vert), "the vertex shader is added");
	CHECK(granite_combined_layout_add(c, GRANITE_SHADER_STAGE_FRAGMENT, &frag), "the fragment shader is added");
}

static void test_combine(void)
{
	granite_combined_layout_t c, d;
	granite_resource_layout_t comp;
	uint64_t h;

	make_program(&c);
	CHECK(c.attribute_mask == 0x7, "the vertex shader's inputs are the attributes");
	CHECK(c.render_target_mask == 0x3, "the fragment shader's outputs are the render targets");
	CHECK(c.sets[0].uniform_buffer_mask == 1 && c.sets[0].sampled_image_mask == 0x22 && c.sets[0].fp_mask == 2,
			"the bindings of both are in the set");
	CHECK(c.stages_for_bindings[0][0] == (VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT),
			"a binding both use has both stages");
	CHECK(c.stages_for_bindings[0][1] == VK_SHADER_STAGE_FRAGMENT_BIT, "a binding one uses has its stage");
	CHECK(c.stages_for_bindings[0][2] == 0, "a binding inside an array has none of its own");
	CHECK(c.stages_for_sets[0] == (VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT) &&
			c.stages_for_sets[1] == 0 && c.stages_for_sets[2] == VK_SHADER_STAGE_FRAGMENT_BIT &&
			c.stages_for_sets[3] == 0, "each set has the stages that use it");
	CHECK(c.push_constant_range.stageFlags == (VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT) &&
			c.push_constant_range.size == 40 && c.push_constant_range.offset == 0,
			"one push constant range, the larger block, for both stages");
	CHECK(c.spec_constant_mask[GRANITE_SHADER_STAGE_VERTEX] == 0x1 &&
			c.spec_constant_mask[GRANITE_SHADER_STAGE_FRAGMENT] == 0x6 &&
			c.spec_constant_mask[GRANITE_SHADER_STAGE_COMPUTE] == 0 && c.combined_spec_constant_mask == 0x7,
			"each stage's specialization constants, and all of them");

	CHECK(granite_combined_layout_bake(&c), "the program bakes");
	CHECK(c.descriptor_set_mask == 0x5, "sets 0 and 2 are in use");
	CHECK(c.sets[0].array_size[1] == 3 && c.sets[0].array_size[0] == 1, "arrays keep their size");
	CHECK(c.sets[0].array_size[20] == 1 && c.sets[2].array_size[0] == 1, "unused bindings of a set in use get one element");
	CHECK(c.sets[1].array_size[0] == 0, "an unused set is left alone");
	CHECK(c.push_constant_layout_hash != 0, "the push constant layout is hashed");

	/* equal programs, equal hashes; any difference, another */
	make_program(&d);
	granite_combined_layout_bake(&d);
	h = granite_combined_layout_hash(&c);
	CHECK(h == granite_combined_layout_hash(&d), "equal layouts hash alike");
	CHECK(c.push_constant_layout_hash == d.push_constant_layout_hash, "and their push constants");
	d.sets[2].array_size[4] = 2;
	CHECK(h != granite_combined_layout_hash(&d), "an array size changes the hash");
	d = c;
	d.stages_for_bindings[2][4] |= VK_SHADER_STAGE_VERTEX_BIT;
	CHECK(h != granite_combined_layout_hash(&d), "a stage changes the hash");
	d = c;
	d.push_constant_range.size = 48;
	CHECK(h != granite_combined_layout_hash(&d), "the push constant size changes the hash");
	d = c;
	d.spec_constant_mask[GRANITE_SHADER_STAGE_FRAGMENT] = 0x2;
	CHECK(h != granite_combined_layout_hash(&d), "a specialization constant changes the hash");
	d = c;
	d.attribute_mask = 0x3;
	CHECK(h != granite_combined_layout_hash(&d), "an attribute changes the hash");
	d = c;
	d.render_target_mask = 0x1;
	CHECK(h != granite_combined_layout_hash(&d), "a render target changes the hash");
	d = c;
	d.push_constant_range.size = 48;
	granite_combined_layout_bake(&d);
	CHECK(c.push_constant_layout_hash != d.push_constant_layout_hash, "another push constant block hashes differently");

	/* a compute shader: no attributes or render targets */
	memset(&comp, 0, sizeof(comp));
	comp.input_mask = 0xff;
	comp.output_mask = 0xff;
	comp.sets[1].storage_buffer_mask = 1u << 31;
	comp.sets[1].array_size[31] = 1;
	granite_combined_layout_init(&d);
	CHECK(granite_combined_layout_add(&d, GRANITE_SHADER_STAGE_COMPUTE, &comp) && granite_combined_layout_bake(&d),
			"a compute shader combines");
	CHECK(d.attribute_mask == 0 && d.render_target_mask == 0, "and has no attributes or render targets");
	CHECK(d.stages_for_sets[1] == VK_SHADER_STAGE_COMPUTE_BIT && d.descriptor_set_mask == 0x2 &&
			d.stages_for_bindings[1][31] == VK_SHADER_STAGE_COMPUTE_BIT, "the last binding is its own");
	CHECK(d.push_constant_range.stageFlags == 0, "no push constants, no range");
}

static void test_combine_errors(void)
{
	granite_combined_layout_t c;
	granite_resource_layout_t a, b;

	/* two shaders, two sizes for one array */
	memset(&a, 0, sizeof(a));
	a.sets[0].sampled_image_mask = 1u << 3;
	a.sets[0].array_size[3] = 2;
	b = a;
	b.sets[0].array_size[3] = 4;
	granite_combined_layout_init(&c);
	CHECK(granite_combined_layout_add(&c, GRANITE_SHADER_STAGE_VERTEX, &a), "the first size is taken");
	CHECK(!granite_combined_layout_add(&c, GRANITE_SHADER_STAGE_FRAGMENT, &b), "another size is reported");
	CHECK(c.sets[0].array_size[3] == 2, "and the first one stays");

	/* a binding inside the array of another */
	memset(&a, 0, sizeof(a));
	a.sets[1].sampled_image_mask = (1u << 3) | (1u << 5);
	a.sets[1].array_size[3] = 3;
	a.sets[1].array_size[5] = 1;
	granite_combined_layout_init(&c);
	granite_combined_layout_add(&c, GRANITE_SHADER_STAGE_FRAGMENT, &a);
	CHECK(!granite_combined_layout_bake(&c), "a binding inside an array is reported");
	a.sets[1].array_size[3] = 2;
	granite_combined_layout_init(&c);
	granite_combined_layout_add(&c, GRANITE_SHADER_STAGE_FRAGMENT, &a);
	CHECK(granite_combined_layout_bake(&c), "one right after it is fine");

	/* an array past the last binding of the last set: the stages looked
	 * at must be bindings of that set and nothing after them */
	memset(&a, 0, sizeof(a));
	a.sets[3].sampled_image_mask = 1u << 30;
	a.sets[3].array_size[30] = 2;
	granite_combined_layout_init(&c);
	granite_combined_layout_add(&c, GRANITE_SHADER_STAGE_FRAGMENT, &a);
	CHECK(granite_combined_layout_bake(&c), "an array ending at the last binding is fine");
	a.sets[3].array_size[30] = 4;
	granite_combined_layout_init(&c);
	granite_combined_layout_add(&c, GRANITE_SHADER_STAGE_FRAGMENT, &a);
	CHECK(!granite_combined_layout_bake(&c), "one running past it is reported");
	CHECK(c.stages_for_sets[3] == VK_SHADER_STAGE_FRAGMENT_BIT, "and what follows the bindings is untouched");
}

static void test_push_set(void)
{
	granite_combined_layout_t c;
	granite_resource_layout_t comp;

	make_program(&c);
	granite_combined_layout_bake(&c);
	/* set 0: a buffer, an array of three images and one more: five */
	CHECK(granite_combined_layout_push_set_mask(&c, true, 32) == 1, "set 0 is pushed when it fits");
	CHECK(granite_combined_layout_push_set_mask(&c, true, 5) == 1, "also when it just fits");
	CHECK(granite_combined_layout_push_set_mask(&c, true, 4) == 0, "not when it is too large");
	CHECK(granite_combined_layout_push_set_mask(&c, false, 32) == 0, "not when the device does not push");

	memset(&comp, 0, sizeof(comp));
	comp.sets[1].storage_buffer_mask = 1;
	comp.sets[1].array_size[0] = 1;
	granite_combined_layout_init(&c);
	granite_combined_layout_add(&c, GRANITE_SHADER_STAGE_COMPUTE, &comp);
	granite_combined_layout_bake(&c);
	CHECK(granite_combined_layout_push_set_mask(&c, true, 32) == 0, "no set 0, nothing to push");
}

static void test_pipeline_layout(void)
{
	static struct VolkDeviceTable table;
	const size_t stride = sizeof(granite_resource_binding_t);
	const size_t fp = offsetof(granite_resource_binding_t, u.image.fp);
	const size_t integer = offsetof(granite_resource_binding_t, u.image.integer);
	granite_combined_layout_t c;
	granite_pipeline_layout_t pl;
	VkDescriptorSetLayout sets[GRANITE_NUM_DESCRIPTOR_SETS];
	const VkDescriptorUpdateTemplateEntry *e;
	granite_resource_layout_t comp;
	unsigned i;

	table.vkCreatePipelineLayout = fake_create_pipeline_layout;
	table.vkDestroyPipelineLayout = fake_destroy_pipeline_layout;
	table.vkCreateDescriptorUpdateTemplate = fake_create_template;
	table.vkDestroyDescriptorUpdateTemplate = fake_destroy_template;
	for (i = 0; i < GRANITE_NUM_DESCRIPTOR_SETS; i++)
		sets[i] = (VkDescriptorSetLayout)(size_t)(0x100 + i);

	CHECK(fp != integer && offsetof(granite_resource_binding_t, u.buffer) == 0 &&
			offsetof(granite_resource_binding_t, u.buffer_view) == 0, "a binding is a buffer, a view or two images");

	make_program(&c);
	granite_combined_layout_bake(&c);

	/* set 0 allocated */
	CHECK(granite_pipeline_layout_init(&pl, (VkDevice)(size_t)1, &table, &c, sets, 0, 4), "the layout is made");
	CHECK(pl.layout == (VkPipelineLayout)(size_t)0x1000 && layouts_created == 1, "by the driver");
	CHECK(pl_info.sType == VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO && pl_info.setLayoutCount == 3,
			"with the sets up to the last in use");
	CHECK(pl_sets[0] == sets[0] && pl_sets[1] == sets[1] && pl_sets[2] == sets[2], "each its layout");
	CHECK(pl_info.pushConstantRangeCount == 1 && pl_range.size == 40 &&
			pl_range.stageFlags == (VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT),
			"and the push constant range");
	CHECK(templates_created == 2 && pl.update_template[0] != VK_NULL_HANDLE &&
			pl.update_template[1] == VK_NULL_HANDLE && pl.update_template[2] != VK_NULL_HANDLE &&
			pl.update_template[3] == VK_NULL_HANDLE, "a template for each set in use");

	CHECK(t_info[0].sType == VK_STRUCTURE_TYPE_DESCRIPTOR_UPDATE_TEMPLATE_CREATE_INFO &&
			t_info[0].templateType == VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_DESCRIPTOR_SET &&
			t_info[0].descriptorSetLayout == sets[0] && t_info[0].set == 0 &&
			t_info[0].pipelineLayout == pl.layout &&
			t_info[0].pipelineBindPoint == VK_PIPELINE_BIND_POINT_GRAPHICS, "set 0's writes a set");
	CHECK(t_info[0].descriptorUpdateEntryCount == 3, "with an entry a binding");
	e = entry_of(0, 0);
	CHECK(e && e->descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC && e->descriptorCount == 1 &&
			e->offset == 0 && e->stride == stride && e->dstArrayElement == 0,
			"a uniform buffer is dynamic, read from the first binding");
	e = entry_of(0, 1);
	CHECK(e && e->descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER && e->descriptorCount == 3 &&
			e->offset == fp + stride && e->stride == stride, "a float image array reads the float views");
	e = entry_of(0, 5);
	CHECK(e && e->descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER && e->descriptorCount == 1 &&
			e->offset == integer + 5 * stride, "an integer image reads the integer view");

	CHECK(t_info[2].descriptorUpdateEntryCount == 8 && t_info[2].descriptorSetLayout == sets[2] &&
			t_info[2].set == 2, "set 2 has its eight bindings");
	e = entry_of(2, 4);
	CHECK(e && e->descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER && e->offset == 4 * stride, "a storage buffer");
	e = entry_of(2, 6);
	CHECK(e && e->descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER && e->offset == 6 * stride,
			"a uniform texel buffer");
	e = entry_of(2, 7);
	CHECK(e && e->descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER && e->offset == 7 * stride,
			"a storage texel buffer");
	e = entry_of(2, 8);
	CHECK(e && e->descriptorType == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE && e->offset == fp + 8 * stride,
			"a separate float image");
	e = entry_of(2, 9);
	CHECK(e && e->descriptorType == VK_DESCRIPTOR_TYPE_SAMPLER && e->offset == fp + 9 * stride,
			"a sampler, where the float view's is");
	e = entry_of(2, 10);
	CHECK(e && e->descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE && e->offset == fp + 10 * stride,
			"a float storage image");
	e = entry_of(2, 11);
	CHECK(e && e->descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE && e->offset == integer + 11 * stride,
			"an integer storage image");
	e = entry_of(2, 12);
	CHECK(e && e->descriptorType == VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT && e->offset == integer + 12 * stride,
			"an input attachment");

	granite_pipeline_layout_free(&pl);
	CHECK(layouts_destroyed == 1 && templates_destroyed == 2, "all of it is destroyed");
	granite_pipeline_layout_free(&pl);
	CHECK(layouts_destroyed == 1 && templates_destroyed == 2, "once");

	/* set 0 pushed */
	CHECK(granite_pipeline_layout_init(&pl, (VkDevice)(size_t)1, &table, &c, sets, 1, 4), "a layout with a push set");
	CHECK(t_info[0].templateType == VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_PUSH_DESCRIPTORS_KHR,
			"set 0's template pushes");
	e = entry_of(0, 0);
	CHECK(e && e->descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, "its uniform buffer has no dynamic offset");
	CHECK(t_info[2].templateType == VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_DESCRIPTOR_SET, "set 2's still writes a set");
	e = entry_of(2, 4);
	CHECK(e && e->descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, "as before");
	granite_pipeline_layout_free(&pl);

	/* an immutable sampler is not written */
	c.sets[2].immutable_sampler_mask = 1u << 9;
	CHECK(granite_pipeline_layout_init(&pl, (VkDevice)(size_t)1, &table, &c, sets, 0, 4), "a layout with an immutable sampler");
	CHECK(t_info[2].descriptorUpdateEntryCount == 7 && entry_of(2, 9) == NULL, "has no entry for it");
	granite_pipeline_layout_free(&pl);
	c.sets[2].immutable_sampler_mask = 0;

	/* a refused template */
	layouts_created = layouts_destroyed = templates_created = templates_destroyed = 0;
	fail_template_set = 2;
	CHECK(!granite_pipeline_layout_init(&pl, (VkDevice)(size_t)1, &table, &c, sets, 0, 4), "a refused template is reported");
	CHECK(pl.layout != VK_NULL_HANDLE && pl.update_template[0] != VK_NULL_HANDLE &&
			pl.update_template[2] == VK_NULL_HANDLE, "what was made is kept");
	granite_pipeline_layout_free(&pl);
	CHECK(layouts_destroyed == 1 && templates_destroyed == 1, "and destroyed");
	fail_template_set = -1;

	/* a refused layout */
	layouts_created = layouts_destroyed = templates_created = templates_destroyed = 0;
	fail_layout = true;
	CHECK(!granite_pipeline_layout_init(&pl, (VkDevice)(size_t)1, &table, &c, sets, 0, 4), "a refused layout is reported");
	CHECK(pl.layout == VK_NULL_HANDLE && templates_created == 0, "and no template is made for it");
	granite_pipeline_layout_free(&pl);
	CHECK(layouts_destroyed == 0 && templates_destroyed == 0, "nothing to destroy");
	fail_layout = false;

	/* compute, no push constants */
	memset(&comp, 0, sizeof(comp));
	comp.sets[0].storage_image_mask = 1;
	comp.sets[0].array_size[0] = 1;
	granite_combined_layout_init(&c);
	granite_combined_layout_add(&c, GRANITE_SHADER_STAGE_COMPUTE, &comp);
	granite_combined_layout_bake(&c);
	CHECK(granite_pipeline_layout_init(&pl, (VkDevice)(size_t)1, &table, &c, sets, 0, 4), "a compute layout");
	CHECK(pl_info.setLayoutCount == 1 && pl_info.pushConstantRangeCount == 0, "has its set and no push constants");
	CHECK(t_info[0].pipelineBindPoint == VK_PIPELINE_BIND_POINT_COMPUTE, "and binds for compute");
	granite_pipeline_layout_free(&pl);

	/* no sets at all */
	granite_combined_layout_init(&c);
	granite_combined_layout_bake(&c);
	templates_created = 0;
	CHECK(granite_pipeline_layout_init(&pl, (VkDevice)(size_t)1, &table, &c, sets, 0, 4), "a layout without sets");
	CHECK(pl_info.setLayoutCount == 0 && templates_created == 0, "has none, and no templates");
	granite_pipeline_layout_free(&pl);
}

int main(void)
{
	test_unserialize();
	test_combine();
	test_combine_errors();
	test_push_set();
	test_pipeline_layout();

	if (failures)
	{
		fprintf(stderr, "test_vk_shader: %u failure(s)\n", failures);
		return 1;
	}
	printf("test_vk_shader: OK\n");
	return 0;
}
