/* parallel-RDP's shader bank (parallel-rdp/shader_bank.c), with the
 * device's program cache stood in for.
 *
 * The bank makes every shader of the renderer and the VI, each in the
 * variant its defines pick: NO_STORAGE_8BIT, SUBGROUP, UBERSHADER and
 * SMALL_TYPES, asked of a resolver by shader name. For each of the 16
 * answers every slot must be filled, with SPIR-V out of the bank's
 * table and the stored layout read just before it; compute shaders
 * become programs, the VI's vertex and fragment shaders stay shaders;
 * a shader has as many variants as its defines make; and what is
 * picked is exactly what slangmosh's selection picks (a hash of all of
 * it, taken from the C++ it replaced). */
#include <stdio.h>
#include <string.h>
#include <boolean.h>

#include "shader_bank.h"

/* the hash of everything the bank picks, over the 16 answers */
#define EXPECTED_HASH_HI 0x7bc39aecu
#define EXPECTED_HASH_LO 0x0604958du

static unsigned failures;

#define CHECK(cond, what) \
	do \
	{ \
		if (!(cond)) \
		{ \
			fprintf(stderr, "test_shader_bank: FAIL (line %d): %s\n", __LINE__, what); \
			failures++; \
		} \
	} while (0)

#define SLOTS (sizeof(rdp_shader_bank_t) / sizeof(void *))

/* ---- the program cache, stood in for ---- */
static granite_device_t device;
static granite_shader_t shaders[64];
static granite_program_t programs[64];
static unsigned num_shaders, num_programs, bad_code, bad_layout, layouts_read, bad_cache;
static const uint32_t *shader_code[64];
static size_t shader_size[64];
static bool layout_fresh;
static uint64_t hash;

static void hash_bytes(const void *data, size_t size)
{
	const uint8_t *bytes = (const uint8_t *)data;
	size_t i;
	for (i = 0; i < size; i++)
		hash = (hash ^ bytes[i]) * (((uint64_t)0x100u << 32) | 0x1b3u);
}

bool granite_resource_layout_unserialize(granite_resource_layout_t *layout, const uint8_t *data, size_t size)
{
	/* the stored form: its magic, then the struct */
	static const uint8_t magic[6] = { 'G', 0, 'R', 0, 'A', 0 };
	if (size != GRANITE_RESOURCE_LAYOUT_STORED_SIZE || memcmp(data, magic, sizeof(magic)))
		bad_layout++;
	memcpy(layout, data + 4 * sizeof(uint16_t), sizeof(*layout));
	hash_bytes(data, size);
	layouts_read++;
	layout_fresh = true;
	return true;
}

granite_shader_t *granite_program_cache_shader(granite_program_cache_t *cache, const uint32_t *code, size_t size,
		const granite_resource_layout_t *layout)
{
	if (cache != &device.program_cache)
		bad_cache++;
	if (size < 20 || (size & 3) || code[0] != 0x07230203u || !layout || !layout_fresh)
		bad_code++;
	layout_fresh = false;
	hash_bytes(code, size);
	shader_code[num_shaders] = code;
	shader_size[num_shaders] = size;
	return &shaders[num_shaders++];
}

granite_program_t *granite_program_cache_compute(granite_program_cache_t *cache, const granite_shader_t *compute)
{
	if (cache != &device.program_cache || compute != &shaders[num_shaders - 1])
		bad_cache++;
	programs[num_programs].shaders[GRANITE_SHADER_STAGE_COMPUTE] = compute;
	hash_bytes("P", 1);
	return &programs[num_programs++];
}

/* ---- the defines ---- */
static unsigned config, bad_names;
static void *resolver_user;

static int resolve(void *user, const char *name, const char *define)
{
	static const char *const names[] = { "tmem_update", "span_setup", "tile_binning_combined", "ubershader",
		"depth_blend", "rasterizer", "extract_vram", "update_upscaled_domain_post", "update_upscaled_domain_pre",
		"update_upscaled_domain_resolve" };
	unsigned i;
	bool known = false;

	for (i = 0; i < sizeof(names) / sizeof(names[0]); i++)
		if (!strcmp(name, names[i]))
			known = true;
	if (!known || user != resolver_user)
		bad_names++;
	if (!strcmp(define, "NO_STORAGE_8BIT"))
		return config & 1;
	if (!strcmp(define, "SUBGROUP"))
		return (config >> 1) & 1;
	if (!strcmp(define, "UBERSHADER"))
		return (config >> 2) & 1;
	if (!strcmp(define, "SMALL_TYPES"))
		return (config >> 3) & 1;
	bad_names++;
	return 0;
}

/* the SPIR-V a slot was made from */
static const uint32_t *code_of(const rdp_shader_bank_t *bank, unsigned slot)
{
	const void *object = ((const void *const *)bank)[slot];
	const granite_shader_t *shader = (const granite_shader_t *)object;
	if ((const granite_program_t *)object >= programs && (const granite_program_t *)object < programs + 64)
		shader = ((const granite_program_t *)object)->shaders[GRANITE_SHADER_STAGE_COMPUTE];
	return shader_code[shader - shaders];
}

int main(void)
{
	/* how many variants each slot has over the 16 answers */
	static const unsigned variants[SLOTS] = { 2, 2, 1, 12, 6, 6, 3, 1, 1, 1, 1, 1, 1, 1, 2, 1, 1, 2, 2, 2, 1, 1, 1 };
	static const uint32_t *seen[SLOTS][16];
	static unsigned num_seen[SLOTS];
	rdp_shader_bank_t bank;
	unsigned slot, i;

	resolver_user = &config;
	hash = ((uint64_t)0xcbf29ce4u << 32) | 0x84222325u;
	CHECK(SLOTS == 23, "the bank has 23 slots");

	for (config = 0; config < 16; config++)
	{
		bool filled = true;

		num_shaders = 0;
		num_programs = 0;
		layouts_read = 0;
		memset(&bank, 0x55, sizeof(bank));
		rdp_shader_bank_init(&bank, &device, resolve, &config);

		for (slot = 0; slot < SLOTS; slot++)
			if (!((const void *const *)&bank)[slot])
				filled = false;
		CHECK(filled, "every slot is filled");
		CHECK(num_shaders == SLOTS && layouts_read == SLOTS, "one shader a slot, each with its layout read first");
		CHECK(num_programs == 14, "fourteen of them compute programs");
		CHECK(bank.tmem_update == &programs[0] && bank.clear_super_sampled_write_mask == &programs[13]
				&& bank.rasterizer == &programs[6], "the renderer's are programs");
		CHECK(bank.fullscreen >= shaders && bank.fullscreen < shaders + 64
				&& bank.vi_scale >= shaders && bank.vi_scale < shaders + 64
				&& bank.vi_divot[1] >= shaders && bank.vi_divot[1] < shaders + 64
				&& bank.vi_fetch[0] >= shaders && bank.vi_fetch[0] < shaders + 64
				&& bank.vi_blend_fields >= shaders && bank.vi_blend_fields < shaders + 64
				&& bank.vi_deinterlace_vert >= shaders && bank.vi_deinterlace_vert < shaders + 64
				&& bank.vi_deinterlace_frag >= shaders && bank.vi_deinterlace_frag < shaders + 64,
				"the VI's vertex and fragment shaders are shaders");
		CHECK(bank.vi_divot[0] != bank.vi_divot[1] && bank.vi_fetch[0] != bank.vi_fetch[1]
				&& code_of(&bank, 9) != code_of(&bank, 10), "with and without the fetch bug apart");

		for (slot = 0; slot < SLOTS; slot++)
		{
			const uint32_t *code = code_of(&bank, slot);
			for (i = 0; i < num_seen[slot]; i++)
				if (seen[slot][i] == code)
					break;
			if (i == num_seen[slot])
				seen[slot][num_seen[slot]++] = code;
		}
	}

	CHECK(bad_code == 0, "every shader is SPIR-V, made with the layout read for it");
	CHECK(bad_layout == 0, "every layout is a stored layout");
	CHECK(bad_cache == 0, "all made in the device's program cache, a program from the shader just made");
	CHECK(bad_names == 0, "the resolver is asked by shader name, for the four defines, with its userdata");
	for (slot = 0; slot < SLOTS; slot++)
		if (num_seen[slot] != variants[slot])
		{
			fprintf(stderr, "test_shader_bank: slot %u has %u variants, wanted %u\n", slot, num_seen[slot], variants[slot]);
			failures++;
		}
	if (hash != (((uint64_t)EXPECTED_HASH_HI << 32) | EXPECTED_HASH_LO))
	{
		fprintf(stderr, "test_shader_bank: what the bank picks hashes to 0x%08xu, 0x%08xu: not slangmosh's selection\n",
				(unsigned)(hash >> 32), (unsigned)hash);
		failures++;
	}

	if (failures)
	{
		fprintf(stderr, "test_shader_bank: %u failure(s)\n", failures);
		return 1;
	}
	printf("test_shader_bank: ok\n");
	return 0;
}
