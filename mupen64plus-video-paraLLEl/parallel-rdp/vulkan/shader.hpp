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

#pragma once

#include <string.h>
#include "shader.h"
#include "pipeline_map.h"
#include "program_cache.h"
#include "resource_bindings.h"
#include "cookie.hpp"
#include "descriptor_set.hpp"
#include "hash.hpp"
#include "intrusive.hpp"
#include "limits.hpp"
#include "vulkan_headers.hpp"
#include "enum_cast.hpp"

// The layouts are C (shader.h). These give the rest of Granite the names
// it had.
namespace Vulkan
{
class Device;

static_assert(VULKAN_NUM_DESCRIPTOR_SETS == GRANITE_NUM_DESCRIPTOR_SETS, "The C layouts have other sets.");

enum class ShaderStage
{
	Vertex = GRANITE_SHADER_STAGE_VERTEX,
	TessControl = GRANITE_SHADER_STAGE_TESS_CONTROL,
	TessEvaluation = GRANITE_SHADER_STAGE_TESS_EVALUATION,
	Geometry = GRANITE_SHADER_STAGE_GEOMETRY,
	Fragment = GRANITE_SHADER_STAGE_FRAGMENT,
	Compute = GRANITE_SHADER_STAGE_COMPUTE,
	Count = GRANITE_SHADER_STAGE_COUNT
};

struct ResourceLayout : granite_resource_layout_t
{
	ResourceLayout()
	{
		memset(static_cast<granite_resource_layout_t *>(this), 0, sizeof(granite_resource_layout_t));
	}

	bool unserialize(const uint8_t *data, size_t size)
	{
		return granite_resource_layout_unserialize(this, data, size);
	}
};

struct CombinedResourceLayout : granite_combined_layout_t
{
	CombinedResourceLayout()
	{
		granite_combined_layout_init(this);
	}
};

static_assert(VULKAN_PUSH_CONSTANT_SIZE == GRANITE_PUSH_CONSTANT_SIZE, "The C bindings have other push constants.");
using ResourceBindings = granite_resource_bindings_t;

// Shaders, programs and pipeline layouts are C (program_cache.h). No
// members of their own: the C structs by other names.
class PipelineLayout : public granite_cached_pipeline_layout_t
{
public:
	PipelineLayout() = delete;

	const granite_combined_layout_t &get_resource_layout() const
	{
		return layout;
	}

	VkPipelineLayout get_layout() const
	{
		return impl.layout;
	}

	// What descriptor sets are bound against (resource_bindings.h).
	const granite_bind_layout_t &get_bind_layout() const
	{
		return bind;
	}
};

class Shader : public granite_shader_t
{
public:
	Shader() = delete;

	VkShaderModule get_module() const
	{
		return module;
	}

	Util::Hash get_hash() const
	{
		return node.hash;
	}
};

using Pipeline = granite_pipeline_t;

class Program : public granite_program_t
{
public:
	Program() = delete;

	Util::Hash get_hash() const
	{
		return node.hash;
	}

	inline const Shader *get_shader(ShaderStage stage) const
	{
		return static_cast<const Shader *>(shaders[Util::ecast(stage)]);
	}

	PipelineLayout *get_pipeline_layout() const
	{
		return static_cast<PipelineLayout *>(layout);
	}

	Pipeline get_pipeline(Util::Hash hash) const
	{
		return granite_pipeline_map_find(&pipelines, hash);
	}

	// The program's pipeline for the hash: this one, or the one another
	// thread added first, or none when out of memory. Unless it is this
	// one, this one is the caller's to destroy.
	Pipeline add_pipeline(Util::Hash hash, const Pipeline &pipeline)
	{
		return granite_pipeline_map_add(&pipelines, hash, &pipeline);
	}
};
}
