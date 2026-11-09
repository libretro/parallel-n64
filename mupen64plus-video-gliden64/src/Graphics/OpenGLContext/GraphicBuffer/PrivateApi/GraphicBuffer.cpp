/*  This file is part of Imagine.

	Imagine is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	Imagine is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with Imagine.  If not, see <http://www.gnu.org/licenses/> */

#include "gralloc.h"
#include "GraphicBuffer.h"
#include <cstring>

namespace opengl
{

static gralloc_module_t const *grallocMod{};
static alloc_device_t *allocDev{};

static void initAllocDev()
{
	if(allocDev)
		return;
	if(!libhardware_dl())
	{
		return;
	}
	if(hw_get_module(GRALLOC_HARDWARE_MODULE_ID, (hw_module_t const**)&grallocMod) != 0)
	{
		return;
	}
	gralloc_open((const hw_module_t*)grallocMod, &allocDev);
	if(!allocDev)
	{
		return;
	}
	if(!allocDev->alloc || !allocDev->free)
	{
		if(allocDev->common.close)
			gralloc_close(allocDev);
		allocDev = {};
		return;
	}
}

GraphicBuffer::GraphicBuffer()
{
	initAllocDev();
	common.incRef = [](struct android_native_base_t *){};
	common.decRef = [](struct android_native_base_t *){};
}

GraphicBuffer::~GraphicBuffer()
{
	if(handle)
	{
		allocDev->free(allocDev, handle);
	}
}

bool GraphicBuffer::reallocate(unsigned int w, unsigned int h, unsigned int f, unsigned int reqUsage)
{
	if(handle && w == (unsigned int)width && h == (unsigned int)height && f == (unsigned int)format && reqUsage == (unsigned int)usage)
		return true;
	if(handle)
	{
		allocDev->free(allocDev, handle);
		handle = nullptr;
	}
	return initSize(w, h, f, reqUsage);
}

bool GraphicBuffer::initSize(unsigned int w, unsigned int h, unsigned int f, unsigned int reqUsage)
{
	auto err = allocDev->alloc(allocDev, w, h, f, reqUsage, &handle, &stride);
	if(!err)
	{
		width = w;
		height = h;
		format = f;
		usage = reqUsage;
		return true;
	}
	return false;
}

bool GraphicBuffer::lock(unsigned int usage, void **vaddr)
{
	return lock(usage, 0, 0, width, height, vaddr);
}

bool GraphicBuffer::lock(unsigned int _usage, int _x0, int _y0, int _width, int _height, void **_vaddr)
{
	if (_x0 < 0 || _width > width ||
		_y0 < 0 || _height > height)
	{
		return false;
	}
	auto err = grallocMod->lock(grallocMod, handle, _usage, _x0, _y0, _width, _height, _vaddr);
	return !err;
}

void GraphicBuffer::unlock()
{
	grallocMod->unlock(grallocMod, handle);
}

unsigned int GraphicBuffer::getWidth()
{
	return width;
}

unsigned int GraphicBuffer::getHeight()
{
	return height;
}

unsigned int GraphicBuffer::getStride()
{
	return stride;
}

android_native_buffer_t *GraphicBuffer::getNativeBuffer()
{
	return static_cast<android_native_buffer_t*>(this);
}

bool GraphicBuffer::hasBufferMapper()
{
	return allocDev;
}

}
