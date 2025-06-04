module;

#include <cassert>
#include <tracy/Tracy.hpp>

module lumina.renderer;

import lumina.core;
import lumina.vfs;
import lumina.vulkan;

import :animation;
import :resource_format;
import :resource_storage;

import std;

using std::uint16_t, std::uint32_t, std::uint64_t, std::size_t;

namespace lumina::render
{

void init_mesh_storage(vulkan::Device& device, MeshStorage& data)
{
	data.gpu_vertex_pos_buffer = device.create_buffer
	({
		.domain = vulkan::BufferDomain::Device,
		.usage = vulkan::BufferUsage::VertexBuffer,
		.size = sizeof(Mesh::Vertex::pos_type) * data.gpu_vertcap,
		.debug_name = "mesh_vertex_pos"
	});

	data.gpu_vertex_attr_buffer = device.create_buffer
	({
		.domain = vulkan::BufferDomain::Device,
		.usage = vulkan::BufferUsage::VertexBuffer,
		.size = sizeof(Mesh::Vertex::Attributes) * data.gpu_vertcap,
		.debug_name = "mesh_vertex_attr"
	});

	data.gpu_index_buffer = device.create_buffer
	({
		.domain = vulkan::BufferDomain::Device,
		.usage = vulkan::BufferUsage::IndexBuffer,
		.size = sizeof(uint32_t) * data.gpu_idxcap,
		.debug_name = "mesh_index_buffer"
	});

	data.gpu_skinned_vertices = device.create_buffer
	({
		.domain = vulkan::BufferDomain::Device,
		.usage = vulkan::BufferUsage::StorageBuffer,
		.size = sizeof(SkinnedMesh::Vertex) * data.gpu_sk_vertcap,
		.debug_name = "mesh_vertex_skinned"
	});

	data.gpu_meshlod_buffer = device.create_buffer
	({
		.domain = vulkan::BufferDomain::Device,
		.usage = vulkan::BufferUsage::StorageBuffer,
		.size = sizeof(Mesh::LODLevel) * data.gpu_lodcap,
		.debug_name = "mesh_lod_levels"
	});

	data.meshes.push_back(Mesh{"mesh::null", {}, 0, 0, false, false});
	data.sk_meshes.push_back(SkinnedMesh{"skinned_mesh::null", {}, 0, 0, 0, 0, false});
}

void descriptor_update(vulkan::Device& device, TextureStorage& data, std::span<Handle<Texture>> hnd)
{
	ZoneScoped;

	for(auto& tex : hnd)
	{
		auto& texture = data.textures[tex];

		data.img_info.push_back
		({
			.sampler = device.get_prefab_sampler(vulkan::SamplerPrefab::TextureAnisotropic),
			.imageView = texture->get_default_view()->get_handle(),
			.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal
		});
	}

	vk::WriteDescriptorSet write
	{
		.dstSet = data.dset,
		.dstBinding = 0,
		.dstArrayElement = hnd[0],
		.descriptorCount = static_cast<uint32_t>(hnd.size()),
		.descriptorType = vk::DescriptorType::eCombinedImageSampler,
		.pImageInfo = data.img_info.data()
	};

	device.get_handle().updateDescriptorSets(1u, &write, 0u, nullptr);
	data.img_info.clear();
}
	
void init_texture_storage(vulkan::Device& device, TextureStorage& data)
{
	data.dsl = device.get_descriptor_set_layout
	({
		// binding 0 is SAMPLED_IMAGE FS VARIABLE_COUNT
		.sampled_image_bindings = 0b1,
		.fs_bindings = 0b1,
		.variable_bindings = 0b1
	}, /* is_push= */ false);

	vk::DescriptorPoolSize texpool
	{
		.type = vk::DescriptorType::eCombinedImageSampler,
		.descriptorCount = TextureStorage::max_resources
	};
	
	data.dpool = device.get_handle().createDescriptorPool
	({
		.flags = vk::DescriptorPoolCreateFlagBits::eUpdateAfterBind,
		.maxSets = 1u,
		.poolSizeCount = 1u,
		.pPoolSizes = &texpool
	});

	uint32_t max_binding = TextureStorage::max_resources - 1u;
	vk::StructureChain<vk::DescriptorSetAllocateInfo, vk::DescriptorSetVariableDescriptorCountAllocateInfo> alloc_chain =
	{
		{
			.descriptorPool = data.dpool,
			.descriptorSetCount = 1,
			.pSetLayouts = &data.dsl
		},
		{
			.descriptorSetCount = 1,
			.pDescriptorCounts = &max_binding
		}
	};

	data.dset = device.get_handle().allocateDescriptorSets(alloc_chain.get<vk::DescriptorSetAllocateInfo>())[0];

	std::array<std::byte, 16> nulltex;
	nulltex.fill(std::byte{255});
	vulkan::ImageHandle nullimg = device.create_image
	({
		.width = 2,
		.height = 2,
		.format = vk::Format::eR8G8B8A8Unorm,
		.usage = vulkan::ImageUsage::ShaderRead,
		.debug_name = "null_texture",
		.initial_data = nulltex.data()
	});

		
	data.texture_metadata.emplace_back(nullimg->get_key().debug_name);
	data.textures.emplace_back(std::move(nullimg));
	
	Handle<Texture> hnd{0};
	descriptor_update(device, data, {&hnd, 1});
	data.next_slot = 1;

	data.copy_cmd.resize(16);
}

ResourceManager::ResourceManager(vulkan::Device& dev) : device{dev}
{
	init_mesh_storage(device, mesh_storage);
	init_texture_storage(device, texture_storage);

	stream_buffer = device.create_buffer
	({
		.domain = vulkan::BufferDomain::Host,
		.usage = vulkan::BufferUsage::StagingBuffer,
		.size = stream_buffer_size,
		.debug_name = "resource_manager::streambuffer"
	});
}

ResourceManager::~ResourceManager()
{
	device.get_handle().destroyDescriptorPool(texture_storage.dpool);
}
	
Mesh& ResourceManager::get_mesh(Handle<Mesh> h)
{
	auto& data = mesh_storage;
	std::scoped_lock<std::shared_mutex> lock{data.mesh_meta_lock};
	return data.meshes[h];
}

SkinnedMesh& ResourceManager::get_skinned_mesh(Handle<SkinnedMesh> h)
{
	auto& data = mesh_storage;
	std::scoped_lock<std::shared_mutex> lock{data.sk_mesh_meta_lock};
	return data.sk_meshes[h];
}

vulkan::Image* ResourceManager::get_texture(Handle<Texture> h)
{
	return texture_storage.textures[h].get();
}

Skeleton& ResourceManager::get_skeleton(Handle<Skeleton> h)
{
	assert(h);
	return skeleton_storage[h - 1];
}

Animation& ResourceManager::get_animation(Handle<Animation> h)
{
	assert(h);
	return animation_storage[h - 1];
}

MaterialTemplate& ResourceManager::get_material_template(Handle64<Material> h)
{
	auto tmp_hash = template_from_material(h);
	assert(material_storage.contains(tmp_hash));
	return material_storage[tmp_hash].tmp;
}

Handle<Mesh> ResourceManager::load_mesh(const vfs::path& path)
{
	ZoneScoped;

	auto phash = fnv::hash(path.c_str());
	if(loaded_meshes.contains(phash))
		return loaded_meshes[phash];

	auto& data = mesh_storage;
	auto mesh_file = vfs::open_unscoped(path, vfs::access_readonly);
	if(!mesh_file.has_value())
	{
		log::error("resource_manager: loading mesh {} failed, {}", path.string(), vfs::file_open_error(mesh_file.error()));
		return Handle<Mesh>{0};
	}

	std::unique_lock<std::shared_mutex> m_lock{data.mesh_meta_lock};
	if(data.meshes.size() <= data.next_mesh)
		data.meshes.insert(data.meshes.end(), (data.next_mesh + 1) - data.meshes.size(), Mesh{});

	int32_t vertex_offset = data.gpu_vbuf_head;
	uint32_t index_offset = data.gpu_ibuf_head;
	uint32_t lod0_offset = data.gpu_lodbuf_head;

	const auto* mesh_data = vfs::map<std::byte>(*mesh_file, vfs::access_readonly);
	const auto* header = reinterpret_cast<const MeshFormat::Header*>(mesh_data);
	bool is_static = (header->vert_format == MeshFormat::VertexFormat::Static);
	if(header->magic != MeshFormat::fmt_magic || header->vmajor != MeshFormat::fmt_major_version || !is_static)
	{
		log::error("resource_manager: loading mesh {} failed, invalid file", path.string());
		vfs::close(*mesh_file);
		return Handle<Mesh>{0};
	}

	Mesh l_mesh
	{
		.name = path.filename().string(),
		.bounds = {header->sphere, header->aabb},
		.lod_count = header->num_lods,
		.lod0_offset = lod0_offset
	};

	uint32_t vcount = 0;
	uint32_t icount = 0;
	const auto* lod_table = reinterpret_cast<const MeshFormat::MeshLOD*>(mesh_data + header->lod_offset);
	for(uint32_t i = 0; i < l_mesh.lod_count; i++)
	{
		l_mesh.lods[i] = 
		{
			vertex_offset + lod_table[i].vertex_offset,
			lod_table[i].vertex_count,
			index_offset + lod_table[i].index_offset,
			lod_table[i].index_count
		};
		if(i == 0 || (i > 0 && lod_table[i].vertex_offset != 0))
			vcount += lod_table[i].vertex_count;

		icount += lod_table[i].index_count;
	}

	if(data.gpu_vbuf_head + vcount > data.gpu_vertcap)
	{
		log::error("resource_manager: vertex buffer overflowed");
		vfs::close(*mesh_file);
		return Handle<Mesh>{0};
	}
	data.gpu_vbuf_head += vcount;

	if(data.gpu_ibuf_head + icount > data.gpu_idxcap)
	{
		log::error("resource_manager: index buffer overflowed");
		vfs::close(*mesh_file);
		return Handle<Mesh>{0};
	}
	data.gpu_ibuf_head += icount;

	if(data.gpu_lodbuf_head + l_mesh.lod_count > data.gpu_lodcap)
	{
		log::error("resource_manager: LOD buffer overflowed");
		vfs::close(*mesh_file);
		return Handle<Mesh>{0};
	}
	data.gpu_lodbuf_head += l_mesh.lod_count;
	data.meshes[data.next_mesh] = l_mesh;

	Handle<Mesh> mh{data.next_mesh++};
	loaded_meshes[phash] = mh;
	m_lock.unlock();

	std::scoped_lock<std::mutex> q_lock{data.queue_lock};
	data.async_queue.push_back({*mesh_file, mh, false});
	return mh;
}

Handle<SkinnedMesh> ResourceManager::load_skinned_mesh(const vfs::path& path)
{
	ZoneScoped;

	auto phash = fnv::hash(path.c_str());
	if(loaded_skinned_meshes.contains(phash))
		return loaded_skinned_meshes[phash];

	auto& data = mesh_storage;
	auto mesh_file = vfs::open_unscoped(path, vfs::access_readonly);
	if(!mesh_file.has_value())
	{
		log::error("resource_manager: loading skinned mesh {} failed, {}", path.string(), vfs::file_open_error(mesh_file.error()));
		return Handle<SkinnedMesh>{0};
	}

	std::unique_lock<std::shared_mutex> m_lock{data.sk_mesh_meta_lock};
	if(data.sk_meshes.size() <= data.next_sk_mesh)
		data.sk_meshes.insert(data.sk_meshes.end(), (data.next_sk_mesh + 1) - data.sk_meshes.size(), SkinnedMesh{});

	int32_t vertex_offset = data.gpu_sk_vbuf_head;
	uint32_t index_offset = data.gpu_ibuf_head;

	const auto* mesh_data = vfs::map<std::byte>(*mesh_file, vfs::access_readonly);
	const auto* header = reinterpret_cast<const MeshFormat::Header*>(mesh_data);
	bool is_skinned = (header->vert_format == MeshFormat::VertexFormat::Skinned);
	if(header->magic != MeshFormat::fmt_magic || header->vmajor != MeshFormat::fmt_major_version || !is_skinned)
	{
		log::error("resource_manager: loading skinned mesh {} failed, invalid file", path.string());
		vfs::close(*mesh_file);
		return Handle<SkinnedMesh>{0};
	}
	
	const auto* lod_table = reinterpret_cast<const MeshFormat::MeshLOD*>(mesh_data + header->lod_offset);

	auto vcount = lod_table[0].vertex_count;
	auto icount = lod_table[0].index_count;

	SkinnedMesh l_mesh
	{
		.name = path.filename().string(),
		.bounds = {header->sphere, header->aabb},
		.ssbo_vertex_offset = vertex_offset + static_cast<int32_t>(lod_table[0].vertex_offset),
		.vertex_count = vcount,
		.ib_index_offset = index_offset + lod_table[0].index_offset,
		.index_count = icount
	};

	if(data.gpu_sk_vbuf_head + lod_table[0].vertex_count > data.gpu_sk_vertcap)
	{
		log::error("resource_manager: skinned vertex buffer overflowed");
		vfs::close(*mesh_file);
		return Handle<SkinnedMesh>{0};
	}
	data.gpu_sk_vbuf_head += vcount;

	if(data.gpu_ibuf_head + icount > data.gpu_idxcap)
	{
		log::error("resource_manager: index buffer overflowed");
		vfs::close(*mesh_file);
		return Handle<SkinnedMesh>{0};
	}
	data.gpu_ibuf_head += icount;
	
	data.sk_meshes[data.next_sk_mesh] = l_mesh;
	Handle<SkinnedMesh> mh{data.next_sk_mesh++};
	loaded_skinned_meshes[phash] = mh;
	m_lock.unlock();

	std::scoped_lock<std::mutex> q_lock{data.queue_lock};
	data.async_queue.push_back({*mesh_file, mh, true});
	return mh;
}

Handle<Mesh> ResourceManager::skinned_mesh_instantiate(Handle<SkinnedMesh> skm)
{
	ZoneScoped;

	auto& data = mesh_storage;
	std::unique_lock<std::mutex> q_lock{data.queue_lock};
	std::unique_lock<std::shared_mutex> sm_lock{data.mesh_meta_lock};
	std::unique_lock<std::shared_mutex> sk_lock{data.sk_mesh_meta_lock};

	SkinnedMesh& sk_mesh = data.sk_meshes[skm];
	
	if(data.gpu_vbuf_head + sk_mesh.vertex_count > data.gpu_vertcap)
	{
		log::error("resource_manager: vertex buffer overflowed");
		return Handle<Mesh>{0};
	}

	if(data.meshes.size() <= data.next_mesh)
		data.meshes.insert(data.meshes.end(), (data.next_mesh + 1) - data.meshes.size(), Mesh{});

	data.meshes[data.next_mesh] = Mesh
	{
		sk_mesh.name,
		sk_mesh.bounds,
		{
			Mesh::LODLevel
			{
				data.gpu_vbuf_head,
				sk_mesh.vertex_count,
				sk_mesh.ib_index_offset,
				sk_mesh.index_count
			}
		},
		1,
		data.gpu_lodbuf_head,
		true,
		false
	};
	

	data.gpu_vbuf_head += sk_mesh.vertex_count;

	auto mh = Handle<Mesh>{data.next_mesh++};
	data.sk_instance_queue.push_back({mh, data.gpu_lodbuf_head++});
	return mh;
}

Handle<Texture> ResourceManager::load_texture(const vfs::path& path)
{
	ZoneScoped;

	auto phash = fnv::hash(path.c_str());
	if(loaded_textures.contains(phash))
		return loaded_textures[phash];

	auto& data = texture_storage;

	std::scoped_lock<std::mutex> lock{data.cpu_lock};
	if(data.textures.size() <= data.next_slot)
	{
		data.texture_metadata.insert(data.texture_metadata.end(), (data.next_slot + 1) - data.texture_metadata.size(), {});
		data.textures.reserve(data.texture_metadata.capacity());
		data.textures.resize(data.texture_metadata.size());
	}

	data.async_queue.push_back({path, data.next_slot});
	Handle<Texture> h{data.next_slot++};

	loaded_textures[phash] = h;
	return h;
}

Handle<Skeleton> ResourceManager::load_skeleton(const vfs::path& path)
{
	ZoneScoped;

	auto phash = fnv::hash(path.c_str());
	if(loaded_skeletons.contains(phash))
		return loaded_skeletons[phash];

	auto pfile = vfs::open(path, vfs::access_readonly);
	if(!pfile.has_value())
	{
		log::error("resource_manager: loading skeleton {} failed: {}", path.string(), vfs::file_open_error(pfile.error()));
		return Handle<Skeleton>{0};
	}

	const auto* pdata = vfs::map<std::byte>(*pfile, vfs::access_readonly);
	const auto* header = reinterpret_cast<const SkeletonFileFormat::Header*>(pdata);

	if(header->magic != SkeletonFileFormat::fmt_magic || header->vmajor != SkeletonFileFormat::fmt_major_version)
	{
		log::error("resource_manager: loading skeleton {} failed: file is invalid", path.string());
		return Handle<Skeleton>{0};
	}

	Skeleton skel;
	skel.name = path.filename().string();
	skel.bone_count = static_cast<uint16_t>(header->bone_count);
	skel.bone_names.resize(header->bone_count);
	skel.bone_transforms.resize(header->bone_count);
	skel.bone_parents.resize(header->bone_count);
	skel.bone_inv_bind_matrices.resize(header->bone_count);

	const auto* string_table = reinterpret_cast<const char*>(pdata + header->name_table_offset);
	const auto* transform_table = reinterpret_cast<const Transform*>(pdata + header->transform_table_offset);
	const auto* parent_table = reinterpret_cast<const uint32_t*>(pdata + header->parent_table_offset);
	const auto* matrix_table = reinterpret_cast<const mat4*>(pdata + header->matrix_table_offset);

	for(uint32_t i = 0; i < header->bone_count; i++)
	{
		skel.bone_names[i] = std::string(string_table);
		string_table += skel.bone_names[i].length() + 1;

		skel.bone_transforms[i] = transform_table[i];
		skel.bone_parents[i] = static_cast<uint16_t>(parent_table[i]);
		skel.bone_inv_bind_matrices[i] = matrix_table[i];
	}

	skeleton_storage.push_back(skel);
	Handle<Skeleton> h{static_cast<uint32_t>(skeleton_storage.size())};
	loaded_skeletons[phash] = h;
	return h;
}

Handle<Animation> ResourceManager::load_animation(const vfs::path& path)
{
	ZoneScoped;

	auto phash = fnv::hash(path.c_str());
	if(loaded_animations.contains(phash))
		return loaded_animations[phash];

	auto pfile = vfs::open(path, vfs::access_readonly);
	if(!pfile.has_value())
	{
		log::error("resource_manager: loading animation {} failed: {}", path.string(), vfs::file_open_error(pfile.error()));
		return Handle<Animation>{0};
	}
	const auto* pdata = vfs::map<std::byte>(*pfile, vfs::access_readonly);
	const auto* header = reinterpret_cast<const AnimationFileFormat::Header*>(pdata);

	if(header->magic != AnimationFileFormat::fmt_magic || header->vmajor != AnimationFileFormat::fmt_major_version)
	{
		log::error("resource_manager: loading animation {} failed: file is invalid", path.string());
		return Handle<Animation>{0};
	}

	Animation anim;
	anim.name = path.filename().string();
	anim.channels.resize(header->channel_count);

	const auto* skeleton_path = reinterpret_cast<const char*>(pdata + header->ref_skeleton_offset);
	Handle<Skeleton> rs = load_skeleton(vfs::path{"anim"} / skeleton_path);
	if(!rs)
		log::warn("resource_manager: animation {} is referencing invalid skeleton {}", path.string(), skeleton_path);
	anim.ref_skeleton = rs;

	const auto* chan_table = reinterpret_cast<const AnimationFileFormat::Channel*>(pdata + header->channel_table_offset);
	for(uint32_t i = 0; i < header->channel_count; i++)
	{
		AnimationChannel& chn = anim.channels[i];
		chn.timestamps.resize(chan_table[i].keyframe_count);
		if(chan_table[i].bone > get_skeleton(anim.ref_skeleton).bone_count || chan_table[i].bone == 0)
		{
			log::warn("resource_manager: animation {} channel {} is referencing invalid bone {} on skeleton {}", path.string(), i, chan_table[i].bone, skeleton_path);
		}

		chn.bone = chan_table[i].bone - 1u;
		chn.path = static_cast<AnimationPath>(chan_table[i].path);
		chn.interp = static_cast<AnimationInterp>(chan_table[i].interp);

		std::memcpy(chn.timestamps.data(), pdata + chan_table[i].timestamp_offset, sizeof(float) * chan_table[i].keyframe_count);

		anim.start_time = std::min(anim.start_time, chn.timestamps[0]);
		anim.end_time = std::max(anim.end_time, chn.timestamps.back());

		auto esize_for_path = [](AnimationPath ap) -> size_t
		{
			switch(ap)
			{
				using enum AnimationPath;
				case Translation:
				case Scale:
					return 3zu;
				case Rotation:
					return 4zu;
				default:
					std::unreachable();
			}
		};

		chn.values.resize(chan_table[i].keyframe_count * esize_for_path(chn.path));
		std::memcpy(chn.values.data(), pdata + chan_table[i].value_offset, esize_for_path(chn.path) * chan_table[i].keyframe_count * sizeof(float));
	}

	animation_storage.push_back(anim);
	Handle<Animation> h{static_cast<uint32_t>(animation_storage.size())};
	loaded_animations[phash] = h;
	return h;
}

void ResourceManager::bind_mesh_vpos(vulkan::CommandBuffer& cmd)
{
	ZoneScoped;
	cmd.bind_vertex_buffers({mesh_storage.gpu_vertex_pos_buffer.get()});
	cmd.bind_index_buffer(mesh_storage.gpu_index_buffer.get());
}

void ResourceManager::bind_mesh_full(vulkan::CommandBuffer& cmd)
{
	ZoneScoped;
	cmd.bind_vertex_buffers
	({
		mesh_storage.gpu_vertex_pos_buffer.get(),
		mesh_storage.gpu_vertex_attr_buffer.get()
	});

	cmd.bind_index_buffer(mesh_storage.gpu_index_buffer.get());
}

MeshStorageBuffers ResourceManager::get_mesh_buffers()
{
	return
	{
		mesh_storage.gpu_vertex_pos_buffer.get(),
		mesh_storage.gpu_vertex_attr_buffer.get(),
		mesh_storage.gpu_index_buffer.get(),
		mesh_storage.gpu_skinned_vertices.get(),
		mesh_storage.gpu_meshlod_buffer.get()
	};
}

vk::DescriptorSet ResourceManager::get_texture_descriptor() const
{
	return texture_storage.dset;
}

vulkan::Buffer* ResourceManager::get_material_buffer(Handle<MaterialTemplate> h)
{
	assert(material_storage.contains(h));
	assert(material_storage[h].tmp.size_class != MaterialTemplate::SizeClass::Implicit);
	return material_storage[h].gpu_material_data.get();
}

std::string& ResourceManager::get_texture_metadata(Handle<Texture> h)
{
	return texture_storage.texture_metadata[h];
}

std::string& ResourceManager::get_material_metadata(Handle64<Material> h)
{
	auto tmp_hash = template_from_material(h);
	assert(material_storage.contains(tmp_hash));
	auto mat_offset = offset_from_material(h);
	return material_storage[tmp_hash].metadata[mat_offset];
}

void ResourceManager::set_material_dirty(Handle64<Material> h, bool dirty)
{
	if(!dirty)
		return;

	auto tmp_hash = template_from_material(h);
	assert(material_storage.contains(tmp_hash));
	auto mat_offset = offset_from_material(h);

	std::scoped_lock<std::mutex> rlock{material_storage[tmp_hash].cpu_rlock};
	material_storage[tmp_hash].dirty[mat_offset / 64] |= (1ull << (mat_offset % 64));
}

std::pair<uint32_t, uint32_t> ResourceManager::process_mesh_queue()
{
	auto& data = mesh_storage;

	uint32_t processed_assets = 0;
	uint32_t processed_instances = 0;

	std::unique_lock<std::mutex> q_lock{data.queue_lock, std::defer_lock};
	if(!q_lock.try_lock())
		return {processed_assets, processed_instances};

	if(data.async_queue.empty() && data.sk_instance_queue.empty())
		return {processed_assets, processed_instances};

	auto aq_size = data.async_queue.size();
	auto iq_size = data.sk_instance_queue.size();

	data.transfer_cmd_vpos.reserve(aq_size);
	data.transfer_cmd_vattr.reserve(aq_size);
	data.transfer_cmd_idx.reserve(aq_size);
	data.transfer_cmd_lod.reserve(aq_size + iq_size);
	data.transfer_cmd_skv.reserve(aq_size);

	std::unique_lock<std::shared_mutex> m_lock{data.mesh_meta_lock};
	std::unique_lock<std::shared_mutex> sk_m_lock{data.sk_mesh_meta_lock};
		
	auto* streambuf = stream_buffer->map<std::byte>();

	for(auto& entry : data.async_queue)
	{
		uint32_t vcount = 0;
		uint32_t icount = 0;

		uint32_t vpos_size = 0;
		uint32_t vattr_size = 0;
		uint32_t idx_size = 0;
		uint32_t lod_size = 0;

		if(entry.skinned)
		{
			SkinnedMesh& m = data.sk_meshes[entry.handle];
			vcount = m.vertex_count;
			icount = m.index_count;

			vpos_size = vcount * sizeof(SkinnedMesh::Vertex);
			idx_size = icount * sizeof(uint32_t);
		}
		else
		{
			Mesh& m = data.meshes[entry.handle];

			for(uint32_t i = 0; i < m.lod_count; i++)
			{
				if(i == 0 || (i > 0 && (m.lods[i].vertex_offset - m.lods[0].vertex_offset) != 0))
				{
					vcount += m.lods[i].vertex_count;
				}
				icount += m.lods[i].index_count;
			}
		
			vpos_size = vcount * sizeof(Mesh::Vertex::pos_type);
			vattr_size = vcount * sizeof(Mesh::Vertex::Attributes);
			idx_size = icount * sizeof(uint32_t);
			lod_size = m.lod_count * sizeof(Mesh::LODLevel);
		}
			
		uint32_t d_size = vpos_size + vattr_size + idx_size + lod_size;
		if(stream_buffer_head + d_size >= stream_buffer_size)
			break;

		const auto* mesh_data = vfs::map<std::byte>(entry.mesh_data, vfs::access_readonly);
		const auto* header = reinterpret_cast<const MeshFormat::Header*>(mesh_data);
		const auto* lod_table = reinterpret_cast<const MeshFormat::MeshLOD*>(mesh_data + header->lod_offset);

		std::memcpy(streambuf + stream_buffer_head, mesh_data + header->vpos_offset, vpos_size);

		uint32_t idx_offset = 0;
		if(entry.skinned)
		{
			SkinnedMesh& m = data.sk_meshes[entry.handle];
			uint32_t skv_offset = static_cast<uint32_t>(m.ssbo_vertex_offset) * sizeof(SkinnedMesh::Vertex);
			idx_offset = static_cast<uint32_t>(m.ib_index_offset) * sizeof(uint32_t);

			data.transfer_cmd_skv.push_back
			({
				.srcOffset = stream_buffer_head,
				.dstOffset = skv_offset,
				.size = vpos_size
			});
			stream_buffer_head += vpos_size;
		}
		else
		{
			Mesh& m = data.meshes[entry.handle];
			idx_offset = m.lods[0].index_offset * sizeof(uint32_t);
			
			uint32_t vpos_offset = static_cast<uint32_t>(m.lods[0].vertex_offset) * sizeof(Mesh::Vertex::pos_type);
			uint32_t vattr_offset = static_cast<uint32_t>(m.lods[0].vertex_offset) * sizeof(Mesh::Vertex::Attributes);
			uint32_t lod_offset = m.lod0_offset * sizeof(Mesh::LODLevel);

			data.transfer_cmd_vpos.push_back
			({
				.srcOffset = stream_buffer_head,
				.dstOffset = vpos_offset,
				.size = vpos_size
			});
			stream_buffer_head += vpos_size;

			std::memcpy(streambuf + stream_buffer_head, mesh_data + header->vattr_offset, vattr_size);
			data.transfer_cmd_vattr.push_back
			({
				.srcOffset = stream_buffer_head,
				.dstOffset = vattr_offset,
				.size = vattr_size
			});
			stream_buffer_head += vattr_size;

			std::memcpy(streambuf + stream_buffer_head, &m.lods[0], lod_size);
			data.transfer_cmd_lod.push_back
			({
				.srcOffset = stream_buffer_head,
				.dstOffset = lod_offset,
				.size = lod_size
			});
			stream_buffer_head += lod_size;
		}

		std::memcpy(streambuf + stream_buffer_head, mesh_data + header->index_offset, idx_size);
		data.transfer_cmd_idx.push_back
		({
			.srcOffset = stream_buffer_head,
	       		.dstOffset = idx_offset,
			.size = idx_size
		});
		stream_buffer_head += idx_size;

		vfs::close(entry.mesh_data);
		processed_assets++;	
	}

	for(auto& entry : data.sk_instance_queue)
	{
		auto& mesh = data.meshes[entry.instance];
		if(stream_buffer_head + sizeof(Mesh::LODLevel) >= stream_buffer_size)
			break;

		std::memcpy(streambuf + stream_buffer_head, &mesh.lods[0], sizeof(Mesh::LODLevel));

		data.transfer_cmd_lod.push_back
		({
			.srcOffset = stream_buffer_head,
			.dstOffset = entry.offset * sizeof(Mesh::LODLevel),
			.size = sizeof(Mesh::LODLevel)
		});
		stream_buffer_head += sizeof(Mesh::LODLevel);
		processed_instances++;
	}

	return {processed_assets, processed_instances};
}

uint32_t ResourceManager::process_tex_queue()
{
	ZoneScoped;

	auto& data = texture_storage;

	std::unique_lock<std::mutex> lock{data.cpu_lock, std::defer_lock};
	if(!lock.try_lock())
		return 0u;

	if(data.async_queue.empty())
		return 0u;

	uint32_t qsize = static_cast<uint32_t>(data.async_queue.size());
	uint32_t i = 0;

	data.load_data.reserve(qsize);
	data.to_update.reserve(qsize);
	data.img_info.reserve(qsize);
	data.init_barriers.reserve(qsize);
	data.release_barriers.reserve(qsize);
	data.acquire_barriers.reserve(qsize);

	for(i = 0; i < qsize; i++)
	{
		auto& entry = data.async_queue[i];

		auto file = vfs::open(entry.path, vfs::access_readonly);
		if(!file.has_value())
		{
			log::error("resource_manager: failed to load texture {}: {}", entry.path.string(), vfs::file_open_error(file.error()));
			continue;
		}

		const auto* ptr = vfs::map<std::byte>(*file, vfs::access_readonly);
		const auto* header = reinterpret_cast<const TextureFileFormat::Header*>(ptr);
		if(header->magic != TextureFileFormat::fmt_magic || header->vmajor != TextureFileFormat::fmt_major_version)
		{
			log::error("resource_manager: failed to load texture {}: invalid file", entry.path.string());
			continue;
		}

		if(header->texformat == TextureFileFormat::TextureFormat::Invalid)
		{
			log::error("resource_manager: failed to load texture {}: invalid format", entry.path.string());
			continue;
		}

		const auto* res_table = reinterpret_cast<const TextureFileFormat::SubresourceDescription*>(ptr + header->subres_desc_offset);
		uint32_t tex_size = 0u;
		uint32_t num_mips = 0;
		uint32_t num_layers = 0;
		for(uint32_t l = 0; l < header->num_subres; l++)
		{
			tex_size += res_table[l].data_size_bytes;
			num_mips = std::max(num_mips, res_table[l].level + 1);
			num_layers = std::max(num_layers, res_table[l].layer + 1);
		}

		assert(num_mips <= 16u);

		if(stream_buffer_head + tex_size >= stream_buffer_size)
			break;

		data.load_data.push_back
		({
			.texture = Handle<Texture>{entry.promised_handle},
			.size = tex_size,
			.offset = stream_buffer_head
		});
		auto& last = data.load_data.back();
		data.to_update.push_back(last.texture);

		last.image = device.create_image
		({
			.width = res_table[0].width,
			.height = res_table[0].height,
			.levels = num_mips,
			.layers = num_layers,
			.format = TextureFileFormat::to_vkformat(header->texformat),
			.usage = vulkan::ImageUsage::ShaderRead,
			.debug_name = entry.path.filename().string()
		});

		vk::ImageSubresourceRange srange
		{
			vk::ImageAspectFlagBits::eColor, 0, num_mips, 0, num_layers
		};

		data.init_barriers.push_back
		({
			.srcStageMask = vk::PipelineStageFlagBits2::eTopOfPipe,
			.dstStageMask = vk::PipelineStageFlagBits2::eTransfer,
			.dstAccessMask = vk::AccessFlagBits2::eTransferWrite,
			.oldLayout = vk::ImageLayout::eUndefined,
			.newLayout = vk::ImageLayout::eTransferDstOptimal,
			.image = last.image->get_handle(),
			.subresourceRange = srange
		});

		data.release_barriers.push_back
		({
			.srcStageMask = vk::PipelineStageFlagBits2::eTransfer,
			.srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
			.dstStageMask = vk::PipelineStageFlagBits2::eAllCommands,
			.oldLayout = vk::ImageLayout::eTransferDstOptimal,
			.newLayout = vk::ImageLayout::eTransferDstOptimal,
			.srcQueueFamilyIndex = device.get_queue_index(vulkan::Queue::Transfer),
			.dstQueueFamilyIndex = device.get_queue_index(vulkan::Queue::Graphics),
			.image = last.image->get_handle(),
			.subresourceRange = srange
		});

		data.acquire_barriers.push_back
		({
			.srcStageMask = vk::PipelineStageFlagBits2::eFragmentShader,
			.dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader,
			.dstAccessMask = vk::AccessFlagBits2::eShaderRead,
			.oldLayout = vk::ImageLayout::eTransferDstOptimal,
			.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
			.srcQueueFamilyIndex = device.get_queue_index(vulkan::Queue::Transfer),
			.dstQueueFamilyIndex = device.get_queue_index(vulkan::Queue::Graphics),
			.image = last.image->get_handle(),
			.subresourceRange = srange
		});

		std::memcpy(stream_buffer->map<std::byte>() + stream_buffer_head, ptr + res_table[0].data_offset, tex_size);
		stream_buffer_head += tex_size;
	}

	data.async_queue.erase(data.async_queue.begin(), data.async_queue.begin() + i);
	return i;
}

void ResourceManager::copy_mesh_data(uint32_t assets, uint32_t instances)
{
	auto& data = mesh_storage;
	auto cmd = device.request_command_buffer(vulkan::Queue::Graphics, "gfx_release_mesh_storage");
	cmd.pipeline_barrier
	({
		{
		.src_stage = vk::PipelineStageFlagBits2::eVertexAttributeInput,
		.src_access = vk::AccessFlagBits2::eVertexAttributeRead,
		.dst_stage = vk::PipelineStageFlagBits2::eAllCommands,
		.src_queue = vulkan::Queue::Graphics,
		.dst_queue = vulkan::Queue::Transfer,
		.buffer = data.gpu_vertex_pos_buffer.get(),
		},
		{
		.src_stage = vk::PipelineStageFlagBits2::eVertexAttributeInput,
		.src_access = vk::AccessFlagBits2::eVertexAttributeRead,
		.dst_stage = vk::PipelineStageFlagBits2::eAllCommands,
		.src_queue = vulkan::Queue::Graphics,
		.dst_queue = vulkan::Queue::Transfer,
		.buffer = data.gpu_vertex_attr_buffer.get(),
		},
		{
		.src_stage = vk::PipelineStageFlagBits2::eIndexInput,
		.src_access = vk::AccessFlagBits2::eIndexRead,
		.dst_stage = vk::PipelineStageFlagBits2::eAllCommands,
		.src_queue = vulkan::Queue::Graphics,
		.dst_queue = vulkan::Queue::Transfer,
		.buffer = data.gpu_index_buffer.get(),
		},
		{
		.src_stage = vk::PipelineStageFlagBits2::eComputeShader,
		.src_access = vk::AccessFlagBits2::eShaderStorageRead,
		.dst_stage = vk::PipelineStageFlagBits2::eAllCommands,
		.src_queue = vulkan::Queue::Graphics,
		.dst_queue = vulkan::Queue::Transfer,
		.buffer = data.gpu_skinned_vertices.get(),
		},
		{
		.src_stage = vk::PipelineStageFlagBits2::eComputeShader,
		.src_access = vk::AccessFlagBits2::eShaderStorageRead,
		.dst_stage = vk::PipelineStageFlagBits2::eAllCommands,
		.src_queue = vulkan::Queue::Graphics,
		.dst_queue = vulkan::Queue::Transfer,
		.buffer = data.gpu_meshlod_buffer.get(),
		}
	});
	auto grtv = device.submit(cmd, vulkan::submit_signal_timeline);

	cmd = device.request_command_buffer(vulkan::Queue::Transfer, "async_mesh_copy");
	cmd.add_wait_semaphore({vulkan::Queue::Graphics, grtv, vk::PipelineStageFlagBits2::eTransfer});
	cmd.pipeline_barrier
	({
		{
		.src_stage = vk::PipelineStageFlagBits2::eTransfer,
		.dst_stage = vk::PipelineStageFlagBits2::eTransfer,
		.dst_access = vk::AccessFlagBits2::eTransferWrite,
		.src_queue = vulkan::Queue::Graphics,
		.dst_queue = vulkan::Queue::Transfer,
		.buffer = data.gpu_vertex_pos_buffer.get(),
		},
		{
		.src_stage = vk::PipelineStageFlagBits2::eTransfer,
		.dst_stage = vk::PipelineStageFlagBits2::eTransfer,
		.dst_access = vk::AccessFlagBits2::eTransferWrite,
		.src_queue = vulkan::Queue::Graphics,
		.dst_queue = vulkan::Queue::Transfer,
		.buffer = data.gpu_vertex_attr_buffer.get(),
		},
		{
		.src_stage = vk::PipelineStageFlagBits2::eTransfer,
		.dst_stage = vk::PipelineStageFlagBits2::eTransfer,
		.dst_access = vk::AccessFlagBits2::eTransferWrite,
		.src_queue = vulkan::Queue::Graphics,
		.dst_queue = vulkan::Queue::Transfer,
		.buffer = data.gpu_index_buffer.get(),
		},
		{
		.src_stage = vk::PipelineStageFlagBits2::eTransfer,
		.dst_stage = vk::PipelineStageFlagBits2::eTransfer,
		.dst_access = vk::AccessFlagBits2::eTransferWrite,
		.src_queue = vulkan::Queue::Graphics,
		.dst_queue = vulkan::Queue::Transfer,
		.buffer = data.gpu_skinned_vertices.get(),
		},
		{
		.src_stage = vk::PipelineStageFlagBits2::eTransfer,
		.dst_stage = vk::PipelineStageFlagBits2::eTransfer,
		.dst_access = vk::AccessFlagBits2::eTransferWrite,
		.src_queue = vulkan::Queue::Graphics,
		.dst_queue = vulkan::Queue::Transfer,
		.buffer = data.gpu_meshlod_buffer.get(),
		}
	});

	if(!data.transfer_cmd_vpos.empty())
		cmd.vk_object().copyBuffer(stream_buffer->handle, data.gpu_vertex_pos_buffer->handle, static_cast<uint32_t>(data.transfer_cmd_vpos.size()), data.transfer_cmd_vpos.data());

	if(!data.transfer_cmd_vattr.empty())
		cmd.vk_object().copyBuffer(stream_buffer->handle, data.gpu_vertex_attr_buffer->handle, static_cast<uint32_t>(data.transfer_cmd_vattr.size()), data.transfer_cmd_vattr.data());

	if(!data.transfer_cmd_idx.empty())
		cmd.vk_object().copyBuffer(stream_buffer->handle, data.gpu_index_buffer->handle, static_cast<uint32_t>(data.transfer_cmd_idx.size()), data.transfer_cmd_idx.data());

	if(!data.transfer_cmd_skv.empty())
		cmd.vk_object().copyBuffer(stream_buffer->handle, data.gpu_skinned_vertices->handle, static_cast<uint32_t>(data.transfer_cmd_skv.size()), data.transfer_cmd_skv.data());

	if(!data.transfer_cmd_lod.empty())
		cmd.vk_object().copyBuffer(stream_buffer->handle, data.gpu_meshlod_buffer->handle, static_cast<uint32_t>(data.transfer_cmd_lod.size()), data.transfer_cmd_lod.data());

	cmd.pipeline_barrier
	({
		{
		.src_stage = vk::PipelineStageFlagBits2::eTransfer,
		.src_access = vk::AccessFlagBits2::eTransferWrite,
		.dst_stage = vk::PipelineStageFlagBits2::eAllCommands,
		.src_queue = vulkan::Queue::Transfer,
		.dst_queue = vulkan::Queue::Graphics,
		.buffer = data.gpu_vertex_pos_buffer.get(),
		},
		{
		.src_stage = vk::PipelineStageFlagBits2::eTransfer,
		.src_access = vk::AccessFlagBits2::eTransferWrite,
		.dst_stage = vk::PipelineStageFlagBits2::eAllCommands,
		.src_queue = vulkan::Queue::Transfer,
		.dst_queue = vulkan::Queue::Graphics,
		.buffer = data.gpu_vertex_attr_buffer.get(),
		},
		{
		.src_stage = vk::PipelineStageFlagBits2::eTransfer,
		.src_access = vk::AccessFlagBits2::eTransferWrite,
		.dst_stage = vk::PipelineStageFlagBits2::eAllCommands,
		.src_queue = vulkan::Queue::Transfer,
		.dst_queue = vulkan::Queue::Graphics,
		.buffer = data.gpu_index_buffer.get(),
		},
		{
		.src_stage = vk::PipelineStageFlagBits2::eTransfer,
		.src_access = vk::AccessFlagBits2::eTransferWrite,
		.dst_stage = vk::PipelineStageFlagBits2::eAllCommands,
		.src_queue = vulkan::Queue::Transfer,
		.dst_queue = vulkan::Queue::Graphics,
		.buffer = data.gpu_skinned_vertices.get(),
		},
		{
		.src_stage = vk::PipelineStageFlagBits2::eTransfer,
		.src_access = vk::AccessFlagBits2::eTransferWrite,
		.dst_stage = vk::PipelineStageFlagBits2::eAllCommands,
		.src_queue = vulkan::Queue::Transfer,
		.dst_queue = vulkan::Queue::Graphics,
		.buffer = data.gpu_meshlod_buffer.get(),
		}
	});
	auto wt = device.submit(cmd, vulkan::submit_signal_timeline);

	cmd = device.request_command_buffer(vulkan::Queue::Graphics, "gfx_acquire_mesh_storage");
	cmd.add_wait_semaphore({vulkan::Queue::Transfer, wt, vk::PipelineStageFlagBits2::eVertexAttributeInput | vk::PipelineStageFlagBits2::eIndexInput | vk::PipelineStageFlagBits2::eComputeShader});
	cmd.pipeline_barrier
	({
		{
		.src_stage = vk::PipelineStageFlagBits2::eVertexAttributeInput,
		.dst_stage = vk::PipelineStageFlagBits2::eVertexAttributeInput,
		.dst_access = vk::AccessFlagBits2::eVertexAttributeRead,
		.src_queue = vulkan::Queue::Transfer,
		.dst_queue = vulkan::Queue::Graphics,
		.buffer = data.gpu_vertex_pos_buffer.get(),
		},
		{
		.src_stage = vk::PipelineStageFlagBits2::eVertexAttributeInput,
		.dst_stage = vk::PipelineStageFlagBits2::eVertexAttributeInput,
		.dst_access = vk::AccessFlagBits2::eVertexAttributeRead,
		.src_queue = vulkan::Queue::Transfer,
		.dst_queue = vulkan::Queue::Graphics,
		.buffer = data.gpu_vertex_attr_buffer.get(),
		},
		{
		.src_stage = vk::PipelineStageFlagBits2::eIndexInput,
		.dst_stage = vk::PipelineStageFlagBits2::eIndexInput,
		.dst_access = vk::AccessFlagBits2::eIndexRead,
		.src_queue = vulkan::Queue::Transfer,
		.dst_queue = vulkan::Queue::Graphics,
		.buffer = data.gpu_index_buffer.get(),
		},
		{
		.src_stage = vk::PipelineStageFlagBits2::eComputeShader,
		.dst_stage = vk::PipelineStageFlagBits2::eComputeShader,
		.dst_access = vk::AccessFlagBits2::eShaderStorageRead,
		.src_queue = vulkan::Queue::Transfer,
		.dst_queue = vulkan::Queue::Graphics,
		.buffer = data.gpu_skinned_vertices.get(),
		},
		{
		.src_stage = vk::PipelineStageFlagBits2::eComputeShader,
		.dst_stage = vk::PipelineStageFlagBits2::eComputeShader,
		.dst_access = vk::AccessFlagBits2::eShaderStorageRead,
		.src_queue = vulkan::Queue::Transfer,
		.dst_queue = vulkan::Queue::Graphics,
		.buffer = data.gpu_meshlod_buffer.get(),
		}
	});
	auto gwt = device.submit(cmd, vulkan::submit_signal_timeline);
	device.wait_timeline(vulkan::Queue::Graphics, gwt);
	
	if(assets)
		log::debug("resource_manager: copied {} meshes", assets);
	
	for(auto i = 0u; i < assets; i++)
	{
		if(data.async_queue[i].skinned)
			data.sk_meshes[data.async_queue[i].handle].in_gpumem = true;
		else
			data.meshes[data.async_queue[i].handle].in_gpumem = true;
	}
	data.async_queue.erase(data.async_queue.begin(), data.async_queue.begin() + assets);

	if(instances)
		log::debug("resource_manager: instantiated {} skinned meshes", instances);

	for(auto i = 0u; i < instances; i++)
	{
		data.meshes[data.sk_instance_queue[i].instance].in_gpumem = true;
	}
	data.sk_instance_queue.erase(data.sk_instance_queue.begin(), data.sk_instance_queue.begin() + instances);

	data.transfer_cmd_vpos.clear();
	data.transfer_cmd_vattr.clear();
	data.transfer_cmd_idx.clear();
	data.transfer_cmd_skv.clear();
	data.transfer_cmd_lod.clear();
}

void ResourceManager::copy_texture_data(uint32_t count)
{
	auto& data = texture_storage;
	auto cmd = device.request_command_buffer(vulkan::Queue::Transfer, "async_texture_copy");
	cmd.vk_object().pipelineBarrier2
	({
		.imageMemoryBarrierCount = count,
		.pImageMemoryBarriers = data.init_barriers.data()
	});

	for(auto& entry : data.load_data)
	{
		auto num_mips = entry.image->get_key().levels;
		auto num_layers = entry.image->get_key().layers;
		for(uint32_t level = 0; level < num_mips; level++)
		{
			auto& subres = entry.image->get_subresource(level, 0);
			data.copy_cmd[level] = 
			{
				.bufferOffset = entry.offset + subres.byte_offset,
				.imageSubresource = {vk::ImageAspectFlagBits::eColor, level, 0, num_layers},
				.imageExtent = {subres.width, subres.height, 1}
			};
		}

		cmd.vk_object().copyBufferToImage(stream_buffer->handle, entry.image->get_handle(), vk::ImageLayout::eTransferDstOptimal, num_mips, data.copy_cmd.data());
	}

	cmd.vk_object().pipelineBarrier2
	({
		.imageMemoryBarrierCount = count,
		.pImageMemoryBarriers = data.release_barriers.data()
	});
	auto ttv = device.submit(cmd, vulkan::submit_signal_timeline);

	cmd = device.request_command_buffer(vulkan::Queue::Graphics, "gfx_texture_acquire");
	cmd.add_wait_semaphore({vulkan::Queue::Transfer, ttv, vk::PipelineStageFlagBits2::eFragmentShader});
	cmd.vk_object().pipelineBarrier2
	({
		.imageMemoryBarrierCount = count,
		.pImageMemoryBarriers = data.acquire_barriers.data()
	});
	auto gtv = device.submit(cmd, vulkan::submit_signal_timeline);
	device.wait_timeline(vulkan::Queue::Graphics, gtv);
	log::debug("resource_manager: uploaded {} textures", count);

	std::unique_lock<std::mutex> lock{data.cpu_lock};
	for(auto& entry : data.load_data)
	{
		data.texture_metadata[entry.texture] = entry.image->get_key().debug_name;
		data.textures[entry.texture] = std::move(entry.image);
	}

	descriptor_update(device, texture_storage, data.to_update);
	data.load_data.clear();
	data.to_update.clear();
	data.init_barriers.clear();
	data.release_barriers.clear();
	data.acquire_barriers.clear();
}

void ResourceManager::copy_material_data()
{
	ZoneScoped;

	auto cmd = device.request_command_buffer(vulkan::Queue::Graphics, "material_copy");

	for(auto& [type, data] : material_storage)
	{
		if(data.tmp.size_class == MaterialTemplate::SizeClass::Implicit)
			continue;
		
		std::scoped_lock<std::mutex> rlock{data.cpu_rlock};
		
		uint64_t dirty_count = 0;
		for(auto dpage : data.dirty)
			dirty_count += __builtin_popcountll(dpage);

		if(dirty_count == 0)
			continue;

		vk::BufferCopy region{};
		if(dirty_count >= (data.size / 3) * 2)
		{
			region.size = data.stride * data.size;
			cmd.vk_object().copyBuffer(data.cpu_material_data->handle, data.gpu_material_data->handle, 1, &region);

			for(auto& dpage : data.dirty)
				dpage = 0;
		}
		else
		{
			for(uint32_t i = 0; i < data.dirty.size(); i++)
			{
				auto& dpage = data.dirty[i];
				while(dpage != 0)
				{
					//FIXME: inefficient but works for now since we only invalidate materials from the editor and no data is being streamed in
					auto tz = __builtin_ctzll(dpage);
					region.size = data.stride;
					region.srcOffset = ((i * 64) + tz) * data.stride; 
					region.dstOffset = ((i * 64) + tz) * data.stride; 
					cmd.vk_object().copyBuffer(data.cpu_material_data->handle, data.gpu_material_data->handle, 1, &region);
					dpage &= (dpage - 1);
				}
			}	
		}
	}

	cmd.memory_barrier
	({
	 	{
		.src_stage = vk::PipelineStageFlagBits2::eTransfer,
		.src_access = vk::AccessFlagBits2::eTransferWrite,
		.dst_stage = vk::PipelineStageFlagBits2::eFragmentShader,
		.dst_access = vk::AccessFlagBits2::eShaderRead
		}
	});
	device.submit(cmd);
}

void ResourceManager::stream_resources()
{
	ZoneScoped;

	if(transfer_running)
		return;

	stream_buffer_head = 0;
	auto [assets, instances] = process_mesh_queue();
	stream_buffer_head = align_up(stream_buffer_head, 16);
	auto textures = process_tex_queue();

	if(stream_buffer_head != 0)
	{
		auto [ss, su] = log::pretty_format_size(stream_buffer_head);
		log::debug("resource_manager: streaming {:.2f}{} resource data", ss, su);

		if(assets || instances)
		{
			transfer_running = true;
			copy_mesh_data(assets, instances);
		}

		if(textures)
		{
			transfer_running = true;
			copy_texture_data(textures);
		}
	}

	copy_material_data();
	transfer_running = false;
}

}
