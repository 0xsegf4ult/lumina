export module lumina.vulkan:buffer;

import vulkan_hpp;
import std;

export namespace lumina::vulkan
{

class Device;

enum class BufferDomain
{
	Host,
	Device,
	DeviceMapped
};

enum class BufferUsage 
{
	Undefined,
	StagingBuffer,
	VertexBuffer,
	IndexBuffer,
	UniformBuffer,
	StorageBuffer,
	IndirectBuffer
};

struct BufferKey
{
	BufferDomain domain;
	BufferUsage usage;
	vk::DeviceSize size;
	void* initial_data = nullptr;
	std::string debug_name;

	bool operator < (const BufferKey& other) const
	{
		return std::tie(domain, usage, size, initial_data, debug_name) < std::tie(other.domain, other.usage, other.size, other.initial_data, other.debug_name);
	}
};

constexpr vk::MemoryPropertyFlags decode_buffer_domain(BufferDomain dom)
{
        switch(dom)
        {
        case BufferDomain::Host:
                return vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
        case BufferDomain::Device:
                return vk::MemoryPropertyFlagBits::eDeviceLocal;
        case BufferDomain::DeviceMapped:
                return vk::MemoryPropertyFlagBits::eDeviceLocal | vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
        default:
                std::unreachable();
        }
}

constexpr vk::BufferUsageFlags decode_buffer_usage(BufferUsage usage)
{
        switch(usage)
        {
        case BufferUsage::Undefined:
        case BufferUsage::StagingBuffer:
                return vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst;
        case BufferUsage::VertexBuffer:
                return vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer;
        case BufferUsage::IndexBuffer:
                return vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst;
        case BufferUsage::UniformBuffer:
                return vk::BufferUsageFlagBits::eUniformBuffer;
        case BufferUsage::StorageBuffer:
                return vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eTransferSrc;
        case BufferUsage::IndirectBuffer:
                return vk::BufferUsageFlagBits::eIndirectBuffer | vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst;
        default:
                std::unreachable();
        }
}

struct Buffer
{
	Buffer(Device* _dev, vk::Buffer _buf, vk::DeviceMemory _mem, vk::DeviceSize _sz) noexcept : device{_dev}, handle{_buf}, memory{_mem}, size{_sz} {}
	~Buffer();

	Buffer(const Buffer& other) = delete;
	Buffer& operator=(const Buffer& other) = delete;

	Buffer(Buffer&& other) = delete;
	Buffer& operator=(Buffer&& other) = delete;

	void* mapped = nullptr;
	
	template <typename T>
	constexpr T* map() const
	{
		return reinterpret_cast<T*>(mapped);
	}

	Device* device;
	vk::Buffer handle;
	vk::DeviceMemory memory;
	vk::DeviceSize size;
};

using BufferHandle = std::unique_ptr<Buffer>;

}
