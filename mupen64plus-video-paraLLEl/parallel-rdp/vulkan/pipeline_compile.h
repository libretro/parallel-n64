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

#ifndef GRANITE_PIPELINE_COMPILE_H
#define GRANITE_PIPELINE_COMPILE_H

#include <stddef.h>
#include <stdint.h>
#include <boolean.h>
#include <retro_common_api.h>
#include "volk.h"
#include "shader.h"
#include "render_pass.h"

RETRO_BEGIN_DECLS

#define GRANITE_NUM_VERTEX_ATTRIBS          16
#define GRANITE_NUM_VERTEX_BUFFERS          4
#define GRANITE_NUM_USER_SPEC_CONSTANTS     8
#define GRANITE_NUM_INTERNAL_SPEC_CONSTANTS 4
#define GRANITE_NUM_TOTAL_SPEC_CONSTANTS    (GRANITE_NUM_USER_SPEC_CONSTANTS + GRANITE_NUM_INTERNAL_SPEC_CONSTANTS)

/* What a command buffer has to set again; the dynamic ones are also the
 * state a pipeline leaves dynamic */
enum granite_command_buffer_dirty
{
	GRANITE_COMMAND_BUFFER_DIRTY_STATIC_STATE_BIT = 1 << 0,
	GRANITE_COMMAND_BUFFER_DIRTY_PIPELINE_BIT = 1 << 1,
	GRANITE_COMMAND_BUFFER_DIRTY_VIEWPORT_BIT = 1 << 2,
	GRANITE_COMMAND_BUFFER_DIRTY_SCISSOR_BIT = 1 << 3,
	GRANITE_COMMAND_BUFFER_DIRTY_DEPTH_BIAS_BIT = 1 << 4,
	GRANITE_COMMAND_BUFFER_DIRTY_STENCIL_REFERENCE_BIT = 1 << 5,
	GRANITE_COMMAND_BUFFER_DIRTY_STATIC_VERTEX_BIT = 1 << 6,
	GRANITE_COMMAND_BUFFER_DIRTY_PUSH_CONSTANTS_BIT = 1 << 7
};

/* The state baked into a pipeline. Hashed and kept in the pipeline
 * records as its four words: no field may move. */
typedef union granite_pipeline_state
{
	struct
	{
		unsigned depth_write : 1;
		unsigned depth_test : 1;
		unsigned blend_enable : 1;

		unsigned cull_mode : 2;
		unsigned front_face : 1;
		unsigned depth_bias_enable : 1;

		unsigned depth_compare : 3;

		unsigned stencil_test : 1;
		unsigned stencil_front_fail : 3;
		unsigned stencil_front_pass : 3;
		unsigned stencil_front_depth_fail : 3;
		unsigned stencil_front_compare_op : 3;
		unsigned stencil_back_fail : 3;
		unsigned stencil_back_pass : 3;
		unsigned stencil_back_depth_fail : 3;
		unsigned stencil_back_compare_op : 3;

		unsigned alpha_to_coverage : 1;
		unsigned alpha_to_one : 1;
		unsigned sample_shading : 1;

		unsigned src_color_blend : 5;
		unsigned dst_color_blend : 5;
		unsigned color_blend_op : 3;
		unsigned src_alpha_blend : 5;
		unsigned dst_alpha_blend : 5;
		unsigned alpha_blend_op : 3;
		unsigned primitive_restart : 1;
		unsigned topology : 4;

		unsigned wireframe : 1;
		unsigned subgroup_control_size : 1;
		unsigned subgroup_full_group : 1;
		unsigned subgroup_minimum_size_log2 : 3;
		unsigned subgroup_maximum_size_log2 : 3;
		/* (was conservative rasterization: kept so that the words stay
		 * as saved pipeline records have them) */
		unsigned unused : 1;

		/* four bits a colour attachment */
		uint32_t write_mask;
	} state;
	uint32_t words[4];
} granite_pipeline_state_t;

/* State that is baked in only when the pipeline uses it */
typedef struct granite_potential_state
{
	float blend_constants[4];
	/* the user's, then the internal ones */
	uint32_t spec_constants[GRANITE_NUM_TOTAL_SPEC_CONSTANTS];
	uint8_t spec_constant_mask;
	uint8_t internal_spec_constant_mask;
} granite_potential_state_t;

typedef struct granite_vertex_attrib_state
{
	uint32_t binding;
	VkFormat format;
	uint32_t offset;
} granite_vertex_attrib_state_t;

/* What a pipeline is compiled from, beside its program and render pass */
typedef struct granite_pipeline_compile
{
	granite_pipeline_state_t static_state;
	granite_potential_state_t potential_static_state;
	granite_vertex_attrib_state_t attribs[GRANITE_NUM_VERTEX_ATTRIBS];
	VkDeviceSize strides[GRANITE_NUM_VERTEX_BUFFERS];
	VkVertexInputRate input_rates[GRANITE_NUM_VERTEX_BUFFERS];

	unsigned subpass_index;
	uint64_t hash;
	VkPipelineCache cache;
	uint32_t subgroup_size_tag;
	/* what it is compiled from (program_cache.h): the device's, and no
	 * part of the hashes */
	struct granite_program *program;
	const struct granite_cached_render_pass *compatible_render_pass;
} granite_pipeline_compile_t;

/* The program a pipeline is compiled for */
typedef struct granite_pipeline_program
{
	uint64_t hash;
	VkPipelineLayout layout;
	const granite_combined_layout_t *resource_layout;
	/* VK_NULL_HANDLE for a stage it has no shader for */
	VkShaderModule modules[GRANITE_SHADER_STAGE_COUNT];
} granite_pipeline_program_t;

/* What the device can do for a pipeline */
typedef struct granite_pipeline_device
{
	VkDevice device;
	const struct VolkDeviceTable *table;
	/* a pipeline can be asked for from the cache alone, failing when it
	 * would have to be compiled */
	bool cache_control;
	/* the subgroup sizes it has */
	uint32_t min_subgroup_size;
	uint32_t max_subgroup_size;
} granite_pipeline_device_t;

/* The hash a program keeps the pipeline under, which is also in the
 * pipeline records on disk. For graphics, @active_vbos gets the vertex
 * buffers the program's attributes read. */
uint64_t granite_pipeline_hash_compute(const granite_pipeline_compile_t *compile,
		const granite_pipeline_program_t *program);
uint64_t granite_pipeline_hash_graphics(const granite_pipeline_compile_t *compile,
		const granite_pipeline_program_t *program, uint64_t render_pass_hash, uint32_t *active_vbos);

/* Compiles the pipeline. @cached_only: only if the driver has it in
 * @compile's cache already, so that the call returns at once; without
 * cache_control that cannot be asked and nothing is made. VK_NULL_HANDLE
 * when nothing is made; @result, if not NULL, gets the driver's answer
 * (VK_SUCCESS when it was not asked). The caller has checked that the
 * device takes the subgroup sizes @compile asks for. */
VkPipeline granite_pipeline_build_compute(const granite_pipeline_device_t *device,
		const granite_pipeline_compile_t *compile, const granite_pipeline_program_t *program,
		bool cached_only, VkResult *result);
/* @dynamic_mask gets the GRANITE_COMMAND_BUFFER_DIRTY_* state the
 * pipeline leaves dynamic */
VkPipeline granite_pipeline_build_graphics(const granite_pipeline_device_t *device,
		const granite_pipeline_compile_t *compile, const granite_pipeline_program_t *program,
		const granite_render_pass_t *render_pass, bool cached_only, uint32_t *dynamic_mask, VkResult *result);

RETRO_END_DECLS

#endif
