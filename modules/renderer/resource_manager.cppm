module;

#include <cassert>
#include <tracy/Tracy.hpp>

export module lumina.renderer:resource_manager;

import lumina.core;
import lumina.vfs;
import lumina.vulkan;

import :resource_format;
import :resource_storage;

import std;

using std::uint16_t, std::uint32_t, std::int32_t, std::size_t;

export namespace lumina::render
{

constexpr uint32_t stream_buffer_size = 512 * 1024 * 1024;

class ResourceManager
{
public:
	ResourceManager(vulkan::Device& dev);
	~ResourceManager();

	ResourceManager(const ResourceManager&) = delete;
	ResourceManager(ResourceManager&&) = delete;

	ResourceManager& operator=(const ResourceManager&) = delete;
	ResourceManager& operator=(ResourceManager&&) = delete;

	Mesh& get_mesh(Handle<Mesh> h);
	SkinnedMesh& get_skinned_mesh(Handle<SkinnedMesh> h);
	vulkan::Image* get_texture(Handle<Texture> h);
	Skeleton& get_skeleton(Handle<Skeleton> h);
	Animation& get_animation(Handle<Animation> h);

	MaterialTemplate& get_material_template(Handle64<Material> handle);
	template <typename Mtl>
	Mtl& get_material(Handle64<Material> h)
	{
		auto mat_offset = offset_from_material(h);
		auto tmp_hash = template_from_material(h);

		assert(type_hash<Mtl>::get() == tmp_hash);
		assert(material_storage.contains(tmp_hash));
		MTData& data = material_storage[tmp_hash];

		return *(reinterpret_cast<Mtl*>(data.cpu_material_data->map<std::byte>() + data.stride * mat_offset));
	}
	
	Handle<Mesh> load_mesh(const vfs::path& path);
	Handle<SkinnedMesh> load_skinned_mesh(const vfs::path& path);
	Handle<Mesh> skinned_mesh_instantiate(Handle<SkinnedMesh> skm);
	Handle<Texture> load_texture(const vfs::path& path);
	Handle<Skeleton> load_skeleton(const vfs::path& path);
	Handle<Animation> load_animation(const vfs::path& path);

	template <typename Mtl>
	Handle<MaterialTemplate> register_material_template(MaterialTemplate&& tmp)
	{
		auto& data = material_storage[type_hash<Mtl>::get()];
		data.stride = sizeof(Mtl);
		data.size = 1u;
		data.capacity = size_class_capacity(tmp.size_class);

		if(tmp.size_class != MaterialTemplate::SizeClass::Implicit)
		{
			data.cpu_material_data = device.create_buffer
			({
				.domain = vulkan::BufferDomain::Host,
				.usage = vulkan::BufferUsage::StagingBuffer,
				.size = sizeof(Mtl) * data.capacity,
				.debug_name = std::format("{}::cpu_data", tmp.name)
			});

			Mtl* nullmat = new (data.cpu_material_data->mapped) Mtl();
			data.metadata.resize(data.capacity);
			data.dirty.resize(data.capacity / 64 + 1);
			data.metadata[0] = "null";
			data.dirty[0] |= 1;

			data.gpu_material_data = device.create_buffer
			({
				.domain = vulkan::BufferDomain::Device,
				.usage = vulkan::BufferUsage::StorageBuffer,
				.size = sizeof(Mtl) * data.capacity,
				.debug_name = std::format("{}::mtl_buffer", tmp.name)
			});
		}
		
		data.tmp = tmp;
		return Handle<MaterialTemplate>{type_hash<Mtl>::get()};
	}

	template <typename Mtl>
	Handle64<Material> create_material(std::string&& name, Mtl&& mtl)
	{
		auto tmp_hash = type_hash<Mtl>::get();
		assert(material_storage.contains(tmp_hash));

		MTData& data = material_storage[tmp_hash];
		if(data.tmp.size_class == MaterialTemplate::SizeClass::Implicit)
			return material_from_handles(Handle<MaterialTemplate>(tmp_hash), 0u);

		std::scoped_lock<std::mutex> rlock{data.cpu_rlock};
		*(data.cpu_material_data->map<Mtl>() + data.size) = mtl;
		data.metadata[data.size] = std::move(name);
		data.dirty[data.size / 64] |= (1ull << (data.size % 64));
		data.size++;

		return material_from_handles(Handle<MaterialTemplate>(tmp_hash), data.size - 1);
	}

	void bind_mesh_vpos(vulkan::CommandBuffer& cmd);
	void bind_mesh_UV(vulkan::CommandBuffer& cmd);
	void bind_mesh_full(vulkan::CommandBuffer& cmd);
	MeshStorageBuffers get_mesh_buffers();
	vk::DescriptorSet get_texture_descriptor() const;
	vulkan::Buffer* get_material_buffer(Handle<MaterialTemplate> handle);

	std::string& get_texture_metadata(Handle<Texture> h);
	std::string& get_material_metadata(Handle64<Material> h);
	void set_material_dirty(Handle64<Material> h, bool dirty);

	void stream_resources();
private:
	std::pair<uint32_t, uint32_t> process_mesh_queue();
	uint32_t process_tex_queue();
	void copy_mesh_data(uint32_t assets, uint32_t instances);
	void copy_texture_data(uint32_t count);
	void copy_material_data();

	vulkan::Device& device;

	MeshStorage mesh_storage;
	TextureStorage texture_storage;
	MaterialStorage material_storage;
	std::vector<Skeleton> skeleton_storage;
	std::vector<Animation> animation_storage;
	
	std::unordered_map<uint32_t, Handle<Mesh>> loaded_meshes;
	std::unordered_map<uint32_t, Handle<SkinnedMesh>> loaded_skinned_meshes;
	std::unordered_map<uint32_t, Handle<Texture>> loaded_textures;
	std::unordered_map<uint32_t, Handle<Skeleton>> loaded_skeletons;
	std::unordered_map<uint32_t, Handle<Animation>> loaded_animations;

	vulkan::BufferHandle stream_buffer;
	uint32_t stream_buffer_head = 0;

	std::atomic<bool> transfer_running{false};
};

}
