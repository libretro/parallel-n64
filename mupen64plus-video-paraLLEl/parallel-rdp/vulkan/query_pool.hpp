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

#include "small_vector.hpp"
#include "vulkan_headers.hpp"
#include "vulkan_common.hpp"
#include "object_pool.hpp"

namespace Vulkan
{
class Device;

class PerformanceQueryPool
{
public:
	void init_device(Device *device, uint32_t queue_family_index);
	~PerformanceQueryPool();

	void begin_command_buffer(VkCommandBuffer cmd);
	void end_command_buffer(VkCommandBuffer cmd);

	void report();

private:
	Device *device = nullptr;
	uint32_t queue_family_index = 0;
	VkQueryPool pool = VK_NULL_HANDLE;
	Util::Vector<VkPerformanceCounterResultKHR> results;
	Util::Vector<VkPerformanceCounterKHR> counters;
	Util::Vector<VkPerformanceCounterDescriptionKHR> counter_descriptions;
	Util::Vector<uint32_t> active_indices;
};

class QueryPoolResult;

struct QueryPoolResultDeleter
{
	void operator()(QueryPoolResult *query);
};

class QueryPoolResult : public Util::IntrusivePtrEnabled<QueryPoolResult, QueryPoolResultDeleter, HandleCounter>
{
public:
	friend struct QueryPoolResultDeleter;

	void signal_timestamp_ticks(uint64_t ticks)
	{
		timestamp_ticks = ticks;
		has_timestamp = true;
	}

	uint64_t get_timestamp_ticks() const
	{
		return timestamp_ticks;
	}

	bool is_signalled() const
	{
		return has_timestamp;
	}

	bool is_device_timebase() const
	{
		return device_timebase;
	}

private:
	friend class Util::ObjectPool<QueryPoolResult>;

	explicit QueryPoolResult(Device *device_, bool device_timebase_)
		: device(device_), device_timebase(device_timebase_)
	{}

	Device *device;
	uint64_t timestamp_ticks = 0;
	bool has_timestamp = false;
	bool device_timebase = false;
};

using QueryPoolHandle = Util::IntrusivePtr<QueryPoolResult>;

class QueryPool
{
public:
	explicit QueryPool(Device *device);

	~QueryPool();

	void begin();

	QueryPoolHandle write_timestamp(VkCommandBuffer cmd, VkPipelineStageFlagBits stage);

private:
	Device *device;
	const VolkDeviceTable &table;

	struct Pool
	{
		VkQueryPool pool = VK_NULL_HANDLE;
		Util::Vector<uint64_t> query_results;
		Util::Vector<QueryPoolHandle> cookies;
		unsigned index = 0;
		unsigned size = 0;
	};
	Util::Vector<Pool> pools;
	unsigned pool_index = 0;

	void add_pool();

	bool supports_timestamp = false;
};

class TimestampInterval : public Util::IntrusiveHashMapEnabled<TimestampInterval>
{
public:
	explicit TimestampInterval(const char *tag);

	void accumulate_time(double t);
	double get_time_per_iteration() const;
	double get_time_per_accumulation() const;
	const char *get_tag() const;
	void mark_end_of_frame_context();
	uint64_t get_total_frame_iterations() const;
	uint64_t get_total_accumulations() const;
	void reset();

private:
	char tag[64];
	double total_time = 0.0;
	uint64_t total_frame_iterations = 0;
	uint64_t total_accumulations = 0;
};

struct TimestampIntervalReport
{
	double time_per_accumulation;
	double time_per_frame_context;
	double accumulations_per_frame_context;
};

typedef void (*TimestampIntervalReportCallback)(void *userdata, const char *tag, const TimestampIntervalReport &report);

class TimestampIntervalManager
{
public:
	TimestampInterval *get_timestamp_tag(const char *tag);
	void mark_end_of_frame_context();
	void reset();
	void log_simple(TimestampIntervalReportCallback func = nullptr, void *userdata = nullptr) const;

private:
	Util::IntrusiveHashMap<TimestampInterval> timestamps;
};
}
