module lumina.vulkan:impl_buffer;

import :device;
import :buffer;
import :queues;

import vulkan_hpp;
import std;

import lumina.core.log;

namespace lumina::vulkan
{
	Buffer::~Buffer()
	{
		if(device)
		{
			device->release_resource(Queue::Graphics, {handle, 0});
			device->release_resource(Queue::Graphics, {memory, 0});
		}
	}
}
