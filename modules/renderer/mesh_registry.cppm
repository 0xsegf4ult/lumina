module;

#include <cassert>
#include <tracy/Tracy.hpp>

export module lumina.renderer:mesh_registry;

import std;
import lumina.core;
import lumina.vfs;
import lumina.vulkan;
import :resource_format;

using std::uint16_t, std::uint32_t, std::int32_t, std::size_t;

namespace lumina::render
{

}

export namespace lumina::render
{

struct MeshBounds
{
	SphereBounds sphere{};
	AABB aabb{};
};

struct Mesh
{
	using Vertex = StaticVertexFormat;
	using Bounds = MeshBounds;

	std::string name{};
	Bounds bounds{};

	using LODLevel = MeshFormat::MeshLOD;
	std::array<LODLevel, MeshFormat::max_lod_count> lods{};

	uint32_t lod_count{0};
	uint32_t lod0_offset{0};
	bool dynamic_instance{false};
	bool in_gpumem = false;
};

struct SkinnedMesh
{
	using Vertex = SkinnedVertexFormat;
	using Bounds = MeshBounds;

	std::string name{};
	Bounds bounds{};

	int32_t ssbo_vertex_offset{0};
	uint32_t vertex_count{0u};

	uint32_t ib_index_offset{0u};
	uint32_t index_count{0u};

	bool in_gpumem = false;
};

constexpr vulkan::VertexDescription static_mesh_pos_stream
{{
	{{vk::Format::eR32G32B32Sfloat}}
}};

constexpr vulkan::VertexDescription static_mesh_vertex_description
{{
	{{vk::Format::eR32G32B32Sfloat}},
	{{vk::Format::eR32Sfloat, vk::Format::eR32G32Sfloat, vk::Format::eR32Uint}}
}};

class MeshRegistry
{
public:
	MeshRegistry(vulkan::Device* dev) : device{dev}
	{
		gpu_vertex_pos_buffer = device->create_buffer
		({
			.domain = vulkan::BufferDomain::Device,
       			.usage = vulkan::BufferUsage::VertexBuffer,
			.size = sizeof(Mesh::Vertex::pos_type) * gpu_vertcap,
			.debug_name = "mesh_registry::vertex_pos"
		});

		gpu_vertex_attr_buffer = device->create_buffer
		({
			.domain = vulkan::BufferDomain::Device,
       			.usage = vulkan::BufferUsage::VertexBuffer,
			.size = sizeof(Mesh::Vertex::Attributes) * gpu_vertcap,
			.debug_name = "mesh_registry::vertex_attr"
		});

		gpu_index_buffer = device->create_buffer
		({
			.domain = vulkan::BufferDomain::Device,
			.usage = vulkan::BufferUsage::IndexBuffer,
			.size = sizeof(uint32_t) * gpu_idxcap,
			.debug_name = "mesh_registry::index"
		});

		gpu_skinned_vertices = device->create_buffer
		({
			.domain = vulkan::BufferDomain::Device,
			.usage = vulkan::BufferUsage::StorageBuffer,
			.size = sizeof(SkinnedMesh::Vertex) * gpu_sk_vertcap,
			.debug_name = "mesh_registry::vertex_skinned"
		});

		gpu_meshlod_buffer = device->create_buffer
		({
			.domain = vulkan::BufferDomain::Device,
			.usage = vulkan::BufferUsage::StorageBuffer,
			.size = sizeof(Mesh::LODLevel) * gpu_lodcap,
			.debug_name = "mesh_registry::lod_levels"
		});

		upload_buffer = device->create_buffer
		({
			.domain = vulkan::BufferDomain::Host,
			.usage = vulkan::BufferUsage::StagingBuffer,
			.size = upload_buffer_size,
			.debug_name = "mesh_registry::upload_buffer"
		});

		meshes.push_back(Mesh{"mesh::null", {}, 0, 0, false, false});
		sk_meshes.push_back(SkinnedMesh{"skinned_mesh::null", {}, 0, 0, 0, 0, false});
	}

	MeshRegistry(const MeshRegistry&) = delete;
	MeshRegistry(MeshRegistry&&) = delete;

	MeshRegistry& operator=(const MeshRegistry&) = delete;
	MeshRegistry& operator=(MeshRegistry&&) = delete;

	Mesh& get(Handle<Mesh> h)
	{
		std::scoped_lock<std::shared_mutex> lock{mesh_meta_lock};
		return meshes[h];
	}

	SkinnedMesh& get(Handle<SkinnedMesh> h)
	{
		std::scoped_lock<std::shared_mutex> lock{sk_mesh_meta_lock};
		return sk_meshes[h];
	}

	Handle<Mesh> skinned_mesh_instantiate(Handle<SkinnedMesh> skm)
	{
		std::unique_lock<std::mutex> q_lock{queue_lock};
		std::unique_lock<std::shared_mutex> sm_lock{mesh_meta_lock};
		std::scoped_lock<std::shared_mutex> sk_lock{sk_mesh_meta_lock};

		SkinnedMesh& sk_mesh = sk_meshes[skm];

		//log::debug("reserved dynamic mesh at v{} i{} l{}", gpu_vbuf_head, sk_mesh.ib_index_offset, gpu_lodbuf_head);

		if(meshes.size() <= next_mesh)
			meshes.insert(meshes.end(), (next_mesh + 1) - meshes.size(), Mesh{});

		meshes[next_mesh] = Mesh{sk_mesh.name, sk_mesh.bounds, {Mesh::LODLevel{gpu_vbuf_head, sk_mesh.vertex_count, sk_mesh.ib_index_offset, sk_mesh.index_count}}, 1, gpu_lodbuf_head, true, false};
		if(gpu_vbuf_head + sk_mesh.vertex_count > gpu_vertcap)
			throw std::runtime_error("mesh_registry: GPU buffer overflow");
		
		gpu_vbuf_head += sk_mesh.vertex_count;

		auto mh = Handle<Mesh>{next_mesh++};
		sk_instance_queue.push_back({mh, gpu_lodbuf_head++});
		return mh;
	}

	Handle<Mesh> enqueue_mesh_load(const vfs::path& path)
	{
		ZoneScoped;
		auto mesh_data = vfs::open_unscoped(path, vfs::access_readonly);	
		if(!mesh_data.has_value())
		{
			log::error("mesh_registry: loading mesh {} failed, {}", path.string(), vfs::file_open_error(mesh_data.error()));
			return Handle<Mesh>{0};
		}

		std::unique_lock<std::shared_mutex> m_lock{mesh_meta_lock};	
		if(meshes.size() <= next_mesh)
			meshes.insert(meshes.end(), (next_mesh + 1) - meshes.size(), Mesh{});
		
		int32_t vertex_offset = gpu_vbuf_head;
		uint32_t index_offset = gpu_ibuf_head;
		uint32_t lod0_offset = gpu_lodbuf_head;
	
		//log::debug("reserved mesh {} at v{} i{} l {}", path.string(), vertex_offset, index_offset, lod0_offset);

		const auto* header = vfs::map<MeshFormat::Header>(*mesh_data, vfs::access_readonly);
		bool is_skinned = (header->vert_format == MeshFormat::VertexFormat::Skinned);
		if(header->magic != MeshFormat::fmt_magic || header->vmajor != MeshFormat::fmt_major_version || is_skinned)
		{
			log::error("mesh_registry: loading mesh {} failed, invalid file", path.string());
			return Handle<Mesh>{0};
		}

		Mesh l_mesh
		{
			.name = path.filename(),
			.bounds = {header->sphere, header->aabb},
			.lod_count = header->num_lods,
			.lod0_offset = lod0_offset
		};

		uint32_t vcount = 0;
		uint32_t icount = 0;
		const auto* lod_table = reinterpret_cast<const MeshFormat::MeshLOD*>(reinterpret_cast<const std::byte*>(header) + header->lod_offset);
		for(uint32_t i = 0; i < l_mesh.lod_count; i++)
		{
			l_mesh.lods[i] = {vertex_offset + lod_table[i].vertex_offset, lod_table[i].vertex_count, index_offset + lod_table[i].index_offset, lod_table[i].index_count};
			if(i == 0 || (i > 0 && lod_table[i].vertex_offset != 0))
				vcount += lod_table[i].vertex_count;
		
			icount += lod_table[i].index_count;
		}

		gpu_vbuf_head += vcount;
		gpu_ibuf_head += icount;
		gpu_lodbuf_head += l_mesh.lod_count;
		meshes[next_mesh] = l_mesh;
		Handle<Mesh> mh{next_mesh++};
		m_lock.unlock();
		
		
		std::scoped_lock<std::mutex> q_lock{queue_lock};
		async_queue.push_back({*mesh_data, mh, false});
		return mh;
	}

	Handle<SkinnedMesh> enqueue_skinned_mesh_load(const vfs::path& path)
	{
		ZoneScoped;
		auto mesh_data = vfs::open_unscoped(path, vfs::access_readonly);

		std::unique_lock<std::shared_mutex> m_lock{sk_mesh_meta_lock};
		if(sk_meshes.size() <= next_sk_mesh)
		       sk_meshes.insert(sk_meshes.end(), (next_sk_mesh + 1) - sk_meshes.size(), SkinnedMesh{});

		int32_t vertex_offset = gpu_sk_vbuf_head;
		uint32_t index_offset = gpu_ibuf_head;

		log::debug("reserved mesh {} at skv{} i{}", path.string(), vertex_offset, index_offset);

		const auto* header = vfs::map<MeshFormat::Header>(*mesh_data, vfs::access_readonly);
		bool is_skinned = (header->vert_format == MeshFormat::VertexFormat::Skinned);
		if(header->magic != MeshFormat::fmt_magic || header->vmajor != MeshFormat::fmt_major_version || !is_skinned)
		{
			log::error("mesh_registry: loading skinned mesh {} failed, invalid file", path.string());
			return Handle<SkinnedMesh>{0};
		}	

		const auto* lod_table = reinterpret_cast<const MeshFormat::MeshLOD*>(reinterpret_cast<const std::byte*>(header) + header->lod_offset);

		SkinnedMesh l_mesh
		{
			.name = path.filename(),
			.bounds = {header->sphere, header->aabb},
			.ssbo_vertex_offset = vertex_offset + static_cast<int32_t>(lod_table[0].vertex_offset), 
			.vertex_count = lod_table[0].vertex_count,
			.ib_index_offset = index_offset + lod_table[0].index_offset,
			.index_count = lod_table[0].index_count,
		};

		gpu_sk_vbuf_head += lod_table[0].vertex_count;
		gpu_ibuf_head += lod_table[0].index_count;
		sk_meshes[next_sk_mesh] = std::move(l_mesh);
		Handle<SkinnedMesh> mh{next_sk_mesh++};
		m_lock.unlock();

		std::scoped_lock<std::mutex> q_lock{queue_lock};
		async_queue.push_back({*mesh_data, mh, true});
		return mh;
	}

	void async_queue_drain()
	{
		ZoneScoped;
		size_t processed_elements = 0;
		size_t processed_instances = 0;
		{

		std::unique_lock<std::mutex> q_lock{queue_lock, std::defer_lock};
		if(!q_lock.try_lock())
			return;

		if(transfer_running)
			return;
		
		if(async_queue.empty() && sk_instance_queue.empty())
			return;
		
		transfer_cmd_vpos.reserve(async_queue.size());
		transfer_cmd_vattr.reserve(async_queue.size());
		transfer_cmd_idx.reserve(async_queue.size());
		transfer_cmd_lod.reserve(async_queue.size() + sk_instance_queue.size());
		transfer_cmd_skv.reserve(async_queue.size());
	
		std::unique_lock<std::shared_mutex> m_lock{mesh_meta_lock};
		std::unique_lock<std::shared_mutex> sk_m_lock {sk_mesh_meta_lock};

		uint32_t upload_buffer_head = 0;
		for(auto& entry : async_queue)
		{
			if(entry.skinned)
			{
				SkinnedMesh& m = sk_meshes[entry.handle];
				
				uint32_t vcount = m.vertex_count;
				uint32_t icount = m.index_count;

				uint32_t sk_vdata_size = vcount * sizeof(SkinnedMesh::Vertex);
				uint32_t idx_data_size = icount * sizeof(uint32_t);				
				uint32_t d_size = sk_vdata_size + idx_data_size;

				bool gpu_overflow = false;
				if(gpu_sk_vbuf_head + vcount > gpu_sk_vertcap)
				{
					log::error("mesh_registry: skinned vertex ssbo overflow needs {}v max {}v", vcount, gpu_sk_vertcap);
					gpu_overflow = true;
				}

				if(gpu_ibuf_head + icount > gpu_idxcap)
				{
					log::error("mesh_registry: index buffer overflow needs {}i max {}i", icount, gpu_idxcap);
					gpu_overflow = true;
				}

				if(gpu_overflow)
					throw std::runtime_error("mesh_registry: GPU buffer overflow");

				if(upload_buffer_head + d_size >= upload_buffer_size)
				       break;

				const auto* data = vfs::map<std::byte>(entry.mesh_data, vfs::access_readonly);
				const auto* header = reinterpret_cast<const MeshFormat::Header*>(data);

				uint32_t skv_offset = static_cast<uint32_t>(m.ssbo_vertex_offset) * sizeof(SkinnedMesh::Vertex);
				uint32_t idx_offset = static_cast<uint32_t>(m.ib_index_offset) * sizeof(uint32_t);	

				std::memcpy(upload_buffer->map<std::byte>() + upload_buffer_head, data + header->vpos_offset, sk_vdata_size);

				transfer_cmd_skv.push_back
				({
					.srcOffset = upload_buffer_head,
					.dstOffset = skv_offset,
					.size = sk_vdata_size
				});

				upload_buffer_head += sk_vdata_size;

				std::memcpy(upload_buffer->map<std::byte>() + upload_buffer_head, data + header->index_offset, idx_data_size);
				transfer_cmd_idx.push_back
				({
					.srcOffset = upload_buffer_head,
					.dstOffset = idx_offset,
					.size = idx_data_size
				});

				upload_buffer_head += idx_data_size;
			}
			else
			{
				uint32_t vcount = 0;
				uint32_t icount = 0;
		
				Mesh& m = meshes[entry.handle];	

				for(uint32_t i = 0; i < m.lod_count; i++)
				{
					if(i == 0 || (i > 0 && (m.lods[i].vertex_offset - m.lods[0].vertex_offset) != 0))
					{
						vcount += m.lods[i].vertex_count;
					}
					icount += m.lods[i].index_count;	
				}

				uint32_t vpos_data_size = vcount * sizeof(Mesh::Vertex::pos_type);
				uint32_t vattr_data_size = vcount * sizeof(Mesh::Vertex::Attributes);
				uint32_t idx_data_size = icount * sizeof(uint32_t);
				uint32_t lod_data_size = m.lod_count * sizeof(Mesh::LODLevel);

				uint32_t d_size = vpos_data_size + vattr_data_size + idx_data_size + lod_data_size;

				bool gpu_overflow = false;
				if(gpu_vbuf_head + vcount > gpu_vertcap)
				{	
					log::error("mesh_registry: vertex buffer overflow needs {}v max {}v", vcount, gpu_vertcap); 
					gpu_overflow = true;
				}

				if(gpu_ibuf_head + icount > gpu_idxcap)
				{
					log::error("mesh_registry: index buffer overflow needs {}i max {}i", icount, gpu_idxcap);
					gpu_overflow = true;
				}

				if(gpu_overflow)
				{
					throw std::runtime_error("mesh_registry: GPU buffer overflow");
				}

				if(upload_buffer_head + d_size >= upload_buffer_size)
					break;
			
				const auto* data = vfs::map<std::byte>(entry.mesh_data, vfs::access_readonly);
				const auto* header = reinterpret_cast<const MeshFormat::Header*>(data);	
				const auto* lod_table = reinterpret_cast<const MeshFormat::MeshLOD*>(data + header->lod_offset);

				uint32_t vpos_offset = static_cast<uint32_t>(m.lods[0].vertex_offset) * sizeof(Mesh::Vertex::pos_type);
				uint32_t vattr_offset = static_cast<uint32_t>(m.lods[0].vertex_offset) * sizeof(Mesh::Vertex::Attributes);
				uint32_t idx_offset = m.lods[0].index_offset * sizeof(uint32_t);
				uint32_t lod_offset = m.lod0_offset * sizeof(Mesh::LODLevel); 


				std::memcpy(upload_buffer->map<std::byte>() + upload_buffer_head, data + header->vpos_offset, vpos_data_size); 

				transfer_cmd_vpos.push_back
				({
					.srcOffset = upload_buffer_head,
					.dstOffset = vpos_offset,
					.size = vpos_data_size
				});

				upload_buffer_head += vpos_data_size;

				std::memcpy(upload_buffer->map<std::byte>() + upload_buffer_head, data + header->vattr_offset, vattr_data_size);

				transfer_cmd_vattr.push_back
				({
					.srcOffset = upload_buffer_head,
					.dstOffset = vattr_offset,
					.size = vattr_data_size
				});

				upload_buffer_head += vattr_data_size;

				std::memcpy(upload_buffer->map<std::byte>() + upload_buffer_head, data + header->index_offset, idx_data_size);

				transfer_cmd_idx.push_back
				({
					.srcOffset = upload_buffer_head,
					.dstOffset = idx_offset,
					.size = idx_data_size
				});

				upload_buffer_head += idx_data_size;
				
				std::memcpy(upload_buffer->map<std::byte>() + upload_buffer_head, &m.lods[0], lod_data_size);
				
				transfer_cmd_lod.push_back
				({
					.srcOffset = upload_buffer_head,
					.dstOffset = lod_offset,
					.size = lod_data_size
				});

				upload_buffer_head += lod_data_size;
			}
			processed_elements++;
		}
		for(auto& entry : sk_instance_queue)
		{
			auto& mesh = meshes[entry.instance];
			if(upload_buffer_head + sizeof(Mesh::LODLevel) >= upload_buffer_size)
			       break;

			std::memcpy(upload_buffer->map<std::byte>() + upload_buffer_head, &mesh.lods[0], sizeof(Mesh::LODLevel));

			//log::debug("copying skinned lod to {}", entry.offset);
			transfer_cmd_lod.push_back
			({
				.srcOffset = upload_buffer_head,
		       		.dstOffset = entry.offset * sizeof(Mesh::LODLevel),
				.size = sizeof(Mesh::LODLevel)
			});

			upload_buffer_head += sizeof(Mesh::LODLevel);	
			processed_instances++;	
		}

		if(!processed_elements && !processed_instances)
		{
			log::warn("mesh_registry: failed to drain async queue");
			return;
		}
		
		log::debug("mesh_registry: uploading {:.2f} kB", static_cast<float>(upload_buffer_head) / 1024.0f);
		transfer_running = true;

		}

		log::debug("mesh_registry: start async cb");
		auto cb = device->request_command_buffer(vulkan::Queue::Transfer, "mesh_registry_copy");
		{
			cb.vk_object().copyBuffer(upload_buffer->handle, gpu_vertex_pos_buffer->handle, static_cast<uint32_t>(transfer_cmd_vpos.size()), transfer_cmd_vpos.data());
			cb.vk_object().copyBuffer(upload_buffer->handle, gpu_vertex_attr_buffer->handle, static_cast<uint32_t>(transfer_cmd_vattr.size()), transfer_cmd_vattr.data());
			cb.vk_object().copyBuffer(upload_buffer->handle, gpu_index_buffer->handle, static_cast<uint32_t>(transfer_cmd_idx.size()), transfer_cmd_idx.data());
			cb.vk_object().copyBuffer(upload_buffer->handle, gpu_skinned_vertices->handle, static_cast<uint32_t>(transfer_cmd_skv.size()), transfer_cmd_skv.data()); 
			cb.vk_object().copyBuffer(upload_buffer->handle, gpu_meshlod_buffer->handle, static_cast<uint32_t>(transfer_cmd_lod.size()), transfer_cmd_lod.data());
			cb.pipeline_barrier
			({
			 	{
			 	.src_stage = vk::PipelineStageFlagBits2::eTransfer,
				.src_access = vk::AccessFlagBits2::eTransferWrite,
				.dst_stage = vk::PipelineStageFlagBits2::eAllCommands,
				.src_queue = vulkan::Queue::Transfer,
				.dst_queue = vulkan::Queue::Graphics,
				.buffer = gpu_vertex_pos_buffer.get(),
				},
				{
				.src_stage = vk::PipelineStageFlagBits2::eTransfer,
				.src_access = vk::AccessFlagBits2::eTransferWrite,
				.dst_stage = vk::PipelineStageFlagBits2::eAllCommands,
				.src_queue = vulkan::Queue::Transfer,
				.dst_queue = vulkan::Queue::Graphics,
				.buffer = gpu_vertex_attr_buffer.get(),
				},
				{
				.src_stage = vk::PipelineStageFlagBits2::eTransfer,
				.src_access = vk::AccessFlagBits2::eTransferWrite,
				.dst_stage = vk::PipelineStageFlagBits2::eAllCommands,
				.src_queue = vulkan::Queue::Transfer,
				.dst_queue = vulkan::Queue::Graphics,
				.buffer = gpu_index_buffer.get(),
				},
				{
				.src_stage = vk::PipelineStageFlagBits2::eTransfer,
				.src_access = vk::AccessFlagBits2::eTransferWrite,
				.dst_stage = vk::PipelineStageFlagBits2::eAllCommands,
				.src_queue = vulkan::Queue::Transfer,
				.dst_queue = vulkan::Queue::Graphics,
				.buffer = gpu_skinned_vertices.get(),
				},
				{
				.src_stage = vk::PipelineStageFlagBits2::eTransfer,
				.src_access = vk::AccessFlagBits2::eTransferWrite,
				.dst_stage = vk::PipelineStageFlagBits2::eAllCommands,
				.src_queue = vulkan::Queue::Transfer,
				.dst_queue = vulkan::Queue::Graphics,
				.buffer = gpu_meshlod_buffer.get(),
				}
			});
		}
		log::debug("mesh_registry: submit async transfer queue");
		auto wt = device->submit(cb, vulkan::submit_signal_timeline);
	
		log::debug("mesh_registry: handover to gfx queue");
		auto gcb = device->request_command_buffer(vulkan::Queue::Graphics, "gfx_mesh_acb_wait");
		gcb.debug_name("gfx_mesh_acb_wait");
	        {
			gcb.add_wait_semaphore({vulkan::Queue::Transfer, wt, vk::PipelineStageFlagBits2::eVertexAttributeInput | vk::PipelineStageFlagBits2::eIndexInput | vk::PipelineStageFlagBits2::eComputeShader});
			log::debug("emit transfer->gfx barrier");
			gcb.pipeline_barrier
			({
		 		{
		       		.src_stage = vk::PipelineStageFlagBits2::eTopOfPipe,
				.dst_stage = vk::PipelineStageFlagBits2::eVertexAttributeInput,
				.dst_access = vk::AccessFlagBits2::eVertexAttributeRead,
				.src_queue = vulkan::Queue::Transfer,
				.dst_queue = vulkan::Queue::Graphics,
				.buffer	= gpu_vertex_pos_buffer.get(),
				},
				{
		       		.src_stage = vk::PipelineStageFlagBits2::eTopOfPipe,
				.dst_stage = vk::PipelineStageFlagBits2::eVertexAttributeInput,
				.dst_access = vk::AccessFlagBits2::eVertexAttributeRead,	
				.src_queue = vulkan::Queue::Transfer,
				.dst_queue = vulkan::Queue::Graphics,
				.buffer = gpu_vertex_attr_buffer.get(),
				},
				{
		       		.src_stage = vk::PipelineStageFlagBits2::eTopOfPipe,
				.dst_stage = vk::PipelineStageFlagBits2::eIndexInput,
				.dst_access = vk::AccessFlagBits2::eIndexRead,
				.src_queue = vulkan::Queue::Transfer,
				.dst_queue = vulkan::Queue::Graphics,
				.buffer = gpu_index_buffer.get(),
				},
				{
				.src_stage = vk::PipelineStageFlagBits2::eTopOfPipe,
				.dst_stage = vk::PipelineStageFlagBits2::eComputeShader,
				.dst_access = vk::AccessFlagBits2::eShaderStorageRead,
				.src_queue = vulkan::Queue::Transfer,
				.dst_queue = vulkan::Queue::Graphics,
				.buffer = gpu_skinned_vertices.get(),
				},
				{
		       		.src_stage = vk::PipelineStageFlagBits2::eTopOfPipe,
				.dst_stage = vk::PipelineStageFlagBits2::eComputeShader,
				.dst_access = vk::AccessFlagBits2::eShaderStorageRead,
				.src_queue = vulkan::Queue::Transfer,
				.dst_queue = vulkan::Queue::Graphics,
				.buffer = gpu_meshlod_buffer.get(),
				}
			});
		}
		
		log::debug("mesh_registry: pre gfx queue submit");
		assert(gcb.dbg_state == vulkan::CommandBuffer::DebugState::Recording);
		auto gwt = device->submit(gcb, vulkan::submit_signal_timeline);
		log::debug("mesh_registry: post gfx queue submit");

		// wait for transfer queue completion	
		device->wait_timeline(vulkan::Queue::Graphics, gwt);
		log::debug("mesh_registry: mesh transfer complete");	
		for(size_t i = 0; i < processed_elements; i++)
		{
			if(async_queue[i].skinned)
				sk_meshes[async_queue[i].handle].in_gpumem = true;
			else
				meshes[async_queue[i].handle].in_gpumem = true;

			vfs::close(async_queue[i].mesh_data);
		}
		async_queue.erase(std::begin(async_queue), std::begin(async_queue) + processed_elements); 
		for(size_t i = 0; i < processed_instances; i++)
		{
			meshes[sk_instance_queue[i].instance].in_gpumem = true;
		}
		sk_instance_queue.erase(std::begin(sk_instance_queue), std::begin(sk_instance_queue) + processed_instances);

		transfer_cmd_vpos.clear();
		transfer_cmd_vattr.clear();
		transfer_cmd_idx.clear();
		transfer_cmd_skv.clear();
		transfer_cmd_lod.clear();
		transfer_running = false;
	}

	void bind_vpos(vulkan::CommandBuffer& cmd)
	{
		cmd.bind_vertex_buffers({gpu_vertex_pos_buffer.get()});
		cmd.bind_index_buffer(gpu_index_buffer.get());
	}

	void bind(vulkan::CommandBuffer& cmd)
	{
		cmd.bind_vertex_buffers({gpu_vertex_pos_buffer.get(), gpu_vertex_attr_buffer.get()});
		cmd.bind_index_buffer(gpu_index_buffer.get());
	}
	
	vulkan::Buffer* get_vpos_buffer()
	{
		return gpu_vertex_pos_buffer.get();
	}	
	
	vulkan::Buffer* get_vattr_buffer()
	{
		return gpu_vertex_attr_buffer.get();
	}	
	
	vulkan::Buffer* get_index_buffer()
	{
		return gpu_index_buffer.get();
	}	

	vulkan::Buffer* get_skinned_vert_buffer()
	{
		return gpu_skinned_vertices.get();
	}	

	vulkan::Buffer* get_lodbuffer()
	{
		return gpu_meshlod_buffer.get();
	}	
private:
	vulkan::Device* device;

	struct AsyncLoadRequest
	{
		Handle<vfs::File> mesh_data;
		uint32_t handle;
		bool skinned;
	};

	struct SkinnedInstanceRequest
	{
		Handle<Mesh> instance;
		uint32_t offset;
	};
	
	//FIXME: lockless queue?
	std::atomic<bool> transfer_running{false};
	std::mutex queue_lock;
	std::vector<AsyncLoadRequest> async_queue;
	std::vector<SkinnedInstanceRequest> sk_instance_queue;

	std::vector<vk::BufferCopy> transfer_cmd_vpos;
	std::vector<vk::BufferCopy> transfer_cmd_vattr;
	std::vector<vk::BufferCopy> transfer_cmd_idx;
	std::vector<vk::BufferCopy> transfer_cmd_lod;
	std::vector<vk::BufferCopy> transfer_cmd_skv;

	std::shared_mutex mesh_meta_lock;
	uint32_t next_mesh = 1;
	std::vector<Mesh> meshes;

	std::shared_mutex sk_mesh_meta_lock;
	uint32_t next_sk_mesh = 1;
	std::vector<SkinnedMesh> sk_meshes;

	constexpr static uint32_t gpu_vertcap = 4194304u;
	constexpr static uint32_t gpu_sk_vertcap = 2097152u;
	constexpr static uint32_t gpu_idxcap = 33554432u;
	constexpr static uint32_t gpu_lodcap = 65536u;

	int32_t gpu_vbuf_head = 0;
	int32_t gpu_sk_vbuf_head = 0;
	uint32_t gpu_ibuf_head = 0;
	uint32_t gpu_lodbuf_head = 0;

	vulkan::BufferHandle gpu_vertex_pos_buffer;
	vulkan::BufferHandle gpu_vertex_attr_buffer;
	vulkan::BufferHandle gpu_index_buffer;
	vulkan::BufferHandle gpu_skinned_vertices;
	vulkan::BufferHandle gpu_meshlod_buffer;

	vulkan::BufferHandle upload_buffer;
	constexpr static uint32_t upload_buffer_size = 32 * 1024 * 1024;
};

}
