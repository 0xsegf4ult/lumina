module lumina.vulkan:impl_buffer;

import :device;
import :buffer;

import vulkan_hpp;
import std;

import lumina.core.log;

namespace lumina::vulkan
{
	Buffer::~Buffer()
	{
		if(device)
		{
			device->release_buffer(handle);
			device->release_memory(memory);
		}
	}
}
