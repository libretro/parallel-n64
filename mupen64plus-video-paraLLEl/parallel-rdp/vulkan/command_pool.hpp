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

#include "vulkan_headers.hpp"
#include "small_vector.hpp"

namespace Vulkan
{
class Device;
class CommandPool
{
public:
	CommandPool(Device *device, uint32_t queue_family_index);
	~CommandPool();

	CommandPool(CommandPool &&) noexcept;
	CommandPool &operator=(CommandPool &&) noexcept;
	CommandPool(const CommandPool &) = delete;
	void operator=(const CommandPool &) = delete;

	void begin();
	void trim();
	VkCommandBuffer request_command_buffer();
	VkCommandBuffer request_secondary_command_buffer();
	void signal_submitted(VkCommandBuffer cmd);

private:
	Device *device;
	const VolkDeviceTable *table;
	VkCommandPool pool = VK_NULL_HANDLE;
	Util::Vector<VkCommandBuffer> buffers;
	Util::Vector<VkCommandBuffer> secondary_buffers;
#ifdef VULKAN_DEBUG
	Util::Vector<VkCommandBuffer> in_flight;
	bool in_flight_has(VkCommandBuffer cmd) const
	{
		for (auto c : in_flight)
			if (c == cmd)
				return true;
		return false;
	}
	void in_flight_erase(VkCommandBuffer cmd)
	{
		for (auto &c : in_flight)
			if (c == cmd)
			{
				c = in_flight.back();
				in_flight.pop_back();
				return;
			}
	}
#endif
	unsigned index = 0;
	unsigned secondary_index = 0;
};
}
