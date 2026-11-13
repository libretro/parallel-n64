/* Copyright (c) 2026 libretro team
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

#include "image.hpp"
#include "small_vector.hpp"

namespace RDP
{
// The VI's images, reused across frames instead of created every frame.
// An image handed out at frame serial S is free again at S + contexts:
// by then the device has begun the same frame context again, which waits
// for all of that context's work, as it does before destroying what was
// released in it. An image handed on (the scanout the frontend shows, the
// previous frame kept to blend with) is free only once the pool holds
// its last handle as well: whoever had it is done with it.
class VIImagePool
{
public:
	struct Entry
	{
		// As requested: the image's own may have been adjusted.
		Vulkan::ImageCreateInfo info;
		Vulkan::ImageHandle image;
		Vulkan::ImageViewHandle layer_views[2];
		uint64_t free_serial;
	};

	enum { MAX_ENTRIES = 24, IDLE_CONTEXTS = 4 };

	// A free entry for info, now busy for this frame, or nullptr.
	Entry *reuse(const Vulkan::ImageCreateInfo &info, bool layer_views, uint64_t serial, uint64_t contexts)
	{
		for (size_t i = 0; i < entries.size(); i++)
		{
			Entry &e = entries[i];
			if (e.free_serial <= serial && same(e.info, info) && (!layer_views || e.layer_views[0]) &&
			    unshared(e))
			{
				e.free_serial = serial + contexts;
				return &e;
			}
		}
		return nullptr;
	}

	// Makes room, then adds a busy entry for info; the caller creates
	// its image. Valid until the next reuse() or add().
	Entry *add(const Vulkan::ImageCreateInfo &info, uint64_t serial, uint64_t contexts)
	{
		size_t i;

		// When full, the free entry released longest ago makes room.
		if (entries.size() >= MAX_ENTRIES)
		{
			size_t oldest = entries.size();
			for (i = 0; i < entries.size(); i++)
				if (entries[i].free_serial <= serial && unshared(entries[i]) &&
				    (oldest == entries.size() || entries[i].free_serial < entries[oldest].free_serial))
					oldest = i;
			if (oldest != entries.size())
				remove(oldest);
		}

		entries.emplace_back();
		Entry &e = entries.back();
		e.info = info;
		e.free_serial = serial + contexts;
		return &e;
	}

	// Once a frame: drops what nothing has asked for in a while, so a
	// resolution change does not keep the old sizes alive.
	void trim(uint64_t serial, uint64_t contexts)
	{
		for (size_t i = 0; i < entries.size();)
		{
			if (entries[i].free_serial + IDLE_CONTEXTS * contexts <= serial)
				remove(i);
			else
				i++;
		}
	}

	size_t size() const
	{
		return entries.size();
	}

private:
	Util::Vector<Entry> entries;

	void remove(size_t i)
	{
		if (i + 1 != entries.size())
			entries[i] = Util::move(entries.back());
		entries.pop_back();
	}

	// An entry whose image failed to be created holds none.
	static bool unshared(const Entry &e)
	{
		return !e.image || e.image->only_reference();
	}

	static bool same(const Vulkan::ImageCreateInfo &a, const Vulkan::ImageCreateInfo &b)
	{
		return a.width == b.width && a.height == b.height && a.depth == b.depth &&
		       a.levels == b.levels && a.layers == b.layers && a.format == b.format &&
		       a.type == b.type && a.usage == b.usage && a.samples == b.samples &&
		       a.flags == b.flags && a.misc == b.misc;
	}
};
}
