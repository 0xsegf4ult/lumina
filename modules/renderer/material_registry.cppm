module;

#include <cassert>
#include <tracy/Tracy.hpp>

export module lumina.renderer:material_registry;

import lumina.core;
import lumina.vulkan;
import std;

using std::uint32_t, std::uint64_t;

namespace lumina::render
{

export struct Material;

export struct MaterialTemplate
{
	std::string name;
	std::array<bool, 3> passes;

	enum class SizeClass
	{
		Implicit, // do not generate buffers
		Small, // ~1k materials
		Medium, // 16k materials
		Large // 64k materials
	} size_class;
};

constexpr uint32_t size_class_capacity(MaterialTemplate::SizeClass sc)
{
	switch(sc)
	{
	using enum MaterialTemplate::SizeClass;
	case Small:
		return 1024u;
	case Medium:
		return 16384u;
	case Large:
		return 65536u;
	case Implicit:
	default:
		return 0u;
	}
};

struct MaterialMetadata
{
	std::string name;
	bool dirty{false};
};

export Handle<MaterialTemplate> template_from_material(Handle64<Material> handle)
{
	return Handle<MaterialTemplate>{static_cast<uint32_t>((handle >> 32) & 0xFFFFFFFF)};
}

export uint32_t offset_from_material(Handle64<Material> handle)
{
	return static_cast<uint32_t>(handle & 0xFFFFFFFF);
}

export Handle64<Material> material_from_handles(Handle<MaterialTemplate> tmp, uint32_t offset)
{
	return Handle64<Material>{(static_cast<uint64_t>(tmp) << 32) | offset};
}

export class MaterialRegistry
{
public:
	MaterialRegistry(vulkan::Device* dev) : device{dev}
	{
	}

	MaterialRegistry(const MaterialRegistry&) = delete;
	MaterialRegistry(MaterialRegistry&&) = delete;

	MaterialRegistry& operator=(const MaterialRegistry&) = delete;
	MaterialRegistry& operator=(MaterialRegistry&&) = delete;

	template <typename Mtl>
	Mtl& get_material(Handle64<Material> handle)
	{
		auto mat_offset = offset_from_material(handle);
		auto tmp_hash = template_from_material(handle);

		assert(type_hash<Mtl>::get() == tmp_hash);
		assert(template_data.contains(tmp_hash));
		MTData& data = template_data[tmp_hash];

		auto* rawptr = data.cpu_material_data->map<std::byte>();
		return *(reinterpret_cast<Mtl*>(rawptr + data.stride * mat_offset));
	}

	MaterialTemplate& get_template(Handle64<Material> handle)
	{
		auto tmp_hash = template_from_material(handle);
		assert(template_data.contains(tmp_hash));
		return template_data[tmp_hash].tmp;
	}

	MaterialMetadata& get_metadata(Handle64<Material> handle)
	{
		auto tmp_hash = template_from_material(handle);
		auto mat_offset = offset_from_material(handle);
		return template_data[tmp_hash].metadata[mat_offset];
	}

	template <typename Mtl>
	Handle<MaterialTemplate> register_material_template(MaterialTemplate&& tmp)
	{
		auto& data = template_data[type_hash<Mtl>::get()];
		data.stride = sizeof(Mtl);
		data.size = 1u;
		data.capacity = size_class_capacity(tmp.size_class);

		if(tmp.size_class != MaterialTemplate::SizeClass::Implicit)
		{
			data.cpu_material_data = device->create_buffer
			({
				.domain = vulkan::BufferDomain::Host,
				.usage = vulkan::BufferUsage::StagingBuffer,
				.size = sizeof(Mtl) * data.capacity,
				.debug_name = std::format("mtl_{}::staging", tmp.name)
			});

			Mtl* nullmat = new (data.cpu_material_data->mapped) Mtl();
			data.metadata.push_back({"null", false});

			data.gpu_material_data = device->create_buffer
			({
				.domain = vulkan::BufferDomain::Device,
				.usage = vulkan::BufferUsage::StorageBuffer,
				.size = sizeof(Mtl) * data.capacity,
				.debug_name = std::format("mtl_{}::buffer", tmp.name)
			});
		}

		data.tmp = tmp;
		return Handle<MaterialTemplate>{type_hash<Mtl>::get()};
	}

	template <typename Mtl>
	Handle64<Material> create_material(std::string&& name, Mtl&& mtl)
	{
		auto tmp_hash = type_hash<Mtl>::get();
		assert(template_data.contains(tmp_hash));

		MTData& data = template_data[tmp_hash];
		if(data.tmp.size_class == MaterialTemplate::SizeClass::Implicit)
			return Handle64<Material>{static_cast<uint64_t>(tmp_hash) << 32};

		std::scoped_lock<std::mutex> rlock{data.cpu_rlock};
		*(data.cpu_material_data->map<Mtl>() + data.size) = mtl;
		data.size++;

		data.metadata.push_back({std::move(name), false});

		return material_from_handles(Handle<MaterialTemplate>{uint32_t(tmp_hash)}, data.size - 1);
	}

	vulkan::Buffer* get_material_buffer(Handle<MaterialTemplate> handle)
	{
		assert(template_data.contains(handle));
		assert(template_data[handle].tmp.size_class != MaterialTemplate::SizeClass::Implicit);
		return template_data[handle].gpu_material_data.get();
	}

	void upload_materials()
	{
		ZoneScoped;

		auto cb = device->request_command_buffer(vulkan::Queue::Graphics, "material_registry_copy");
		{
			for(auto& [type, data] : template_data)
			{
				if(data.tmp.size_class == MaterialTemplate::SizeClass::Implicit)
					continue;

				if(data.size == data.gpu_size)
					continue;

				vk::BufferCopy region{};
				region.size = data.stride * (data.size - data.gpu_size);
				region.srcOffset = data.stride * data.gpu_size;
				region.dstOffset = data.stride * data.gpu_size;
				cb.vk_object().copyBuffer(data.cpu_material_data->handle, data.gpu_material_data->handle, 1, &region);

				data.gpu_size = data.size;
			}

			cb.memory_barrier
			({
			 	{
				.src_stage = vk::PipelineStageFlagBits2::eTransfer,
				.src_access = vk::AccessFlagBits2::eTransferWrite,
				.dst_stage = vk::PipelineStageFlagBits2::eFragmentShader,
				.dst_access = vk::AccessFlagBits2::eShaderRead,
				}
			});
		}
		device->submit(cb);
	}

	void clear()
	{
		for(auto& [type, data] : template_data)
		{
			std::scoped_lock<std::mutex> lock{data.cpu_rlock};
			data.size = 1;
			data.gpu_size = 0;
			data.metadata.erase(data.metadata.begin() + 1, data.metadata.end());
		}
	}
private:
	vulkan::Device* device;

	struct MTData
	{
		uint32_t stride;
		uint32_t size;
		uint32_t gpu_size{0u};
		uint32_t capacity;

		std::mutex cpu_rlock;
		std::vector<MaterialMetadata> metadata;
		vulkan::BufferHandle cpu_material_data;
		vulkan::BufferHandle gpu_material_data;

		MaterialTemplate tmp;
	};

	std::unordered_map<uint32_t, MTData> template_data;
};

}
