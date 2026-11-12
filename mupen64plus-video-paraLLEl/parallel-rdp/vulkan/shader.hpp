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

// What the command buffer keeps of a binding; the update templates read
// it as the C struct.
struct ResourceBinding
{
	union {
		VkDescriptorBufferInfo buffer;
		struct
		{
			VkDescriptorImageInfo fp;
			VkDescriptorImageInfo integer;
		} image;
		VkBufferView buffer_view;
	};
	VkDeviceSize dynamic_offset;
};
static_assert(sizeof(ResourceBinding) == sizeof(granite_resource_binding_t) &&
              offsetof(ResourceBinding, buffer) == offsetof(granite_resource_binding_t, u.buffer) &&
              offsetof(ResourceBinding, image.fp) == offsetof(granite_resource_binding_t, u.image.fp) &&
              offsetof(ResourceBinding, image.integer) == offsetof(granite_resource_binding_t, u.image.integer) &&
              offsetof(ResourceBinding, buffer_view) == offsetof(granite_resource_binding_t, u.buffer_view),
              "The update templates read another binding.");

struct ResourceBindings
{
	ResourceBinding bindings[VULKAN_NUM_DESCRIPTOR_SETS][VULKAN_NUM_BINDINGS];
	uint64_t cookies[VULKAN_NUM_DESCRIPTOR_SETS][VULKAN_NUM_BINDINGS];
	uint64_t secondary_cookies[VULKAN_NUM_DESCRIPTOR_SETS][VULKAN_NUM_BINDINGS];
	uint8_t push_constant_data[VULKAN_PUSH_CONSTANT_SIZE];
};

class PipelineLayout : public HashedObject<PipelineLayout>
{
public:
	PipelineLayout(Util::Hash hash, Device *device, const CombinedResourceLayout &layout);

	~PipelineLayout()
	{
		granite_pipeline_layout_free(&impl);
	}

	const CombinedResourceLayout &get_resource_layout() const
	{
		return layout;
	}

	VkPipelineLayout get_layout() const
	{
		return impl.layout;
	}

	DescriptorSetAllocator *get_allocator(unsigned set) const
	{
		return set_allocators[set];
	}

	VkDescriptorUpdateTemplate get_update_template(unsigned set) const
	{
		return impl.update_template[set];
	}

	uint32_t get_push_set_mask() const
	{
		return push_set_mask;
	}

private:
	granite_pipeline_layout_t impl;
	CombinedResourceLayout layout;
	DescriptorSetAllocator *set_allocators[VULKAN_NUM_DESCRIPTOR_SETS] = {};
	uint32_t push_set_mask = 0;
};

class Shader : public HashedObject<Shader>
{
public:
	Shader(Util::Hash binding, Device *device, const uint32_t *data, size_t size,
	       const ResourceLayout *layout = nullptr);
	~Shader();

	const ResourceLayout &get_layout() const
	{
		return layout;
	}

	VkShaderModule get_module() const
	{
		return module;
	}
	static Util::Hash hash(const uint32_t *data, size_t size);

private:
	Device *device;
	VkShaderModule module = VK_NULL_HANDLE;
	ResourceLayout layout;
};

using Pipeline = granite_pipeline_t;

class Program : public HashedObject<Program>
{
public:
	Program(Device *device, Shader *vertex, Shader *fragment);
	Program(Device *device, Shader *compute);
	~Program();

	inline const Shader *get_shader(ShaderStage stage) const
	{
		return shaders[Util::ecast(stage)];
	}

	void set_pipeline_layout(PipelineLayout *new_layout)
	{
		layout = new_layout;
	}

	PipelineLayout *get_pipeline_layout() const
	{
		return layout;
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

private:
	void set_shader(ShaderStage stage, Shader *handle);
	Device *device;
	Shader *shaders[Util::ecast(ShaderStage::Count)] = {};
	PipelineLayout *layout = nullptr;
	granite_pipeline_map_t pipelines;
};
}
