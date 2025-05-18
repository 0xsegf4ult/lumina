export module lumina.renderer:resource_storage;

import lumina.core;
import lumina.vfs;
import lumina.vulkan;
import :animation;
import :resource_format;

using std::uint16_t, std::uint32_t, std::int32_t, std::size_t;

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
        bool in_gpumem{false};
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

struct MeshStorageBuffers
{
	vulkan::Buffer* vertex_pos;
	vulkan::Buffer* vertex_attr;
	vulkan::Buffer* index;
	vulkan::Buffer* skinned_vertex;
	vulkan::Buffer* lod;
};

struct Texture;
struct Material;

struct MaterialMetadata
{
	std::string name;
	bool dirty{false};
};

struct MaterialTemplate
{
        std::string name;
        std::array<bool, 3> passes;

        enum class SizeClass
        {
                Implicit, // do not generate buffers
                Small, // 1k materials
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
		return 0u;
	default:
		std::unreachable();
	}
}

Handle<MaterialTemplate> template_from_material(Handle64<Material> handle)
{
	return Handle<MaterialTemplate>{static_cast<uint32_t>((handle >> 32) & 0xFFFFFFFF)};
}

uint32_t offset_from_material(Handle64<Material> handle)
{
        return static_cast<uint32_t>(handle & 0xFFFFFFFF);
}

Handle64<Material> material_from_handles(Handle<MaterialTemplate> tmp, uint32_t offset)
{
        return Handle64<Material>{(static_cast<uint64_t>(tmp) << 32) | offset};
}

struct MeshStorage
{
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

	std::mutex queue_lock;
	std::vector<AsyncLoadRequest> async_queue;
	std::vector<SkinnedInstanceRequest> sk_instance_queue;

	std::shared_mutex mesh_meta_lock;
	std::vector<Mesh> meshes;
	uint32_t next_mesh = 1;

	std::shared_mutex sk_mesh_meta_lock;
	std::vector<SkinnedMesh> sk_meshes;
	uint32_t next_sk_mesh = 1;
	
	vulkan::BufferHandle gpu_vertex_pos_buffer;
	vulkan::BufferHandle gpu_vertex_attr_buffer;
	vulkan::BufferHandle gpu_index_buffer;
	vulkan::BufferHandle gpu_skinned_vertices;
	vulkan::BufferHandle gpu_meshlod_buffer;
	
	int32_t gpu_vbuf_head = 0;
	int32_t gpu_sk_vbuf_head = 0;
	uint32_t gpu_ibuf_head = 0;
	uint32_t gpu_lodbuf_head = 0;

	constexpr static uint32_t gpu_vertcap = 4194304u;
	constexpr static uint32_t gpu_sk_vertcap = 2097152u;
	constexpr static uint32_t gpu_idxcap = 33554432u;
	constexpr static uint32_t gpu_lodcap = 65536u;

	std::vector<vk::BufferCopy> transfer_cmd_vpos;
	std::vector<vk::BufferCopy> transfer_cmd_vattr;
	std::vector<vk::BufferCopy> transfer_cmd_idx;
	std::vector<vk::BufferCopy> transfer_cmd_lod;
	std::vector<vk::BufferCopy> transfer_cmd_skv;
};

struct TextureLoadData
{
	vulkan::ImageHandle image{};
	Handle<Texture> texture;
	uint32_t size;
	uint32_t offset;
};

struct TextureStorage
{
	std::vector<vulkan::ImageHandle> textures;
	std::vector<std::string> texture_metadata;
	uint32_t next_slot = 0;
	
	struct AsyncLoadRequest
	{
		vfs::path path;
		uint32_t promised_handle;
	};

	std::mutex cpu_lock;
	std::vector<AsyncLoadRequest> async_queue;

	vk::DescriptorSet dset;
	vk::DescriptorPool dpool;
	vk::DescriptorSetLayout dsl;

	std::vector<TextureLoadData> load_data;
	std::vector<Handle<Texture>> to_update;
	std::vector<vk::DescriptorImageInfo> img_info;
	std::vector<vk::BufferImageCopy> copy_cmd;

	std::vector<vk::ImageMemoryBarrier2> init_barriers;
	std::vector<vk::ImageMemoryBarrier2> release_barriers;
	std::vector<vk::ImageMemoryBarrier2> acquire_barriers;
	constexpr static uint32_t max_resources = 65536;
};

struct MTData
{
	MaterialTemplate tmp;

	uint32_t stride;
	uint32_t size;
	uint32_t gpu_size{0u};
	uint32_t capacity;

	std::mutex cpu_rlock;
	std::vector<MaterialMetadata> metadata;

	vulkan::BufferHandle cpu_material_data;
	vulkan::BufferHandle gpu_material_data;
};
using MaterialStorage = std::unordered_map<uint32_t, MTData>;

}
