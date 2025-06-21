module lumina.vulkan;

import :device;
import :buffer;
import :queues;

import vulkan_hpp;
import std;

import lumina.core.log;

namespace lumina::vulkan
{
	Buffer::~Buffer() noexcept
	{
		if(device)
		{
			device->release_resource(Queue::Graphics, {handle, 0});
			device->release_resource(Queue::Graphics, {memory, 0});
		}
	}

	vk::DeviceAddress Buffer::device_address()
	{
		return device->get_handle().getBufferAddress
		({
			.buffer = handle
		});
	}
}
