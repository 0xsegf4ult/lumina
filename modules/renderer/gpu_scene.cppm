module;

#include <cassert>
#include <tracy/Tracy.hpp>

export module lumina.renderer:gpu_scene;

import :camera;
import :resource_storage;
import :resource_manager;
import lumina.vulkan;
import lumina.core;
import imgui;

import std;

using std::uint32_t, std::int32_t, std::uint64_t, std::size_t;

namespace lumina::render
{

export struct MeshPass;
export struct RenderObject;

struct IndirectBatch
{
	Handle<RenderObject> object;
	uint32_t batch;
	uint64_t sort_key;
};

export enum class RenderBucket
{
	Default,
	DoubleSided,
	OpaqueAlphaMask,
	OpaqueAlphaMaskDoubleSided,
};

export struct Multibatch
{
	uint32_t first;
	uint32_t count;
	Handle<MaterialTemplate> pipeline;
	RenderBucket bucket;
};

export struct ViewportData
{
	Camera* camera{nullptr};
	vulkan::Image* zbuf{nullptr};

	vulkan::ImageHandle zbuf_remap{nullptr};
	vulkan::ImageHandle depth_pyramid{nullptr};

	uvec2 override_res{0};
	uvec2 last_res{0};
	
	uint32_t map_width{0};
	uint32_t map_height{0};
	uint32_t dp_width{0};
	uint32_t dp_height{0};
	uint32_t dp_miplevels{0};
	std::array<vulkan::ImageView*, 12> dp_layer_views;
	bool needs_barrier = true;
};

export struct CullingData
{
	bool frustum_cull{true};
	bool occlusion_cull{true};
	bool is_ortho{false};
	int forced_lod{0};
	float lod_base{10.0f};
	float lod_step{1.5f};
	bool compact_drawcalls{true};
	bool sort_drawcalls{false};
};

export struct RenderObject
{
	Handle<Mesh> mesh;
	Handle64<Material> material;
	Transform transform;
	RenderBucket bucket;
	uint32_t custom_key;
};

struct MeshPass
{
	enum class Type
	{
		ForwardOpaque,
		ForwardTransparent,
		Shadowcast
	} type;

	Handle<ViewportData> viewport;
	CullingData cull_data;

	std::vector<Handle<RenderObject>> unbatched_objects;
	std::vector<IndirectBatch> indirect_batches{};
	std::vector<Multibatch> multibatches{};

	size_t multibatch_capacity = 32;
	size_t command_capacity = 4096;

	std::array<uint32_t, 16> intermediate_sizes;

	struct GPUBuffers
	{
		vulkan::BufferHandle multibatch;
		vulkan::BufferHandle indirect;
		vulkan::BufferHandle command;
		vulkan::BufferHandle visibility_main;
		std::vector<vulkan::BufferHandle> visibility;
		vulkan::BufferHandle compact_command;
		vulkan::BufferHandle culldata;
	};

	std::array<GPUBuffers, 2> gpu_buffers;
};

struct GPUIndirectObject
{
	uint32_t lod0_offset;
	uint32_t lod_count;
	uint32_t batch;
	uint32_t object;
};

struct GPUMultibatch
{
	uint32_t first;
	uint32_t count;
	uint32_t draw_count;
	uint32_t unused;
};

struct GPUObjectData
{
	mat4 transform;
	vec4 sphere;
	uint32_t material_offset;
	uint32_t unused0;
	uint32_t unused1;
	uint32_t unused2;
};

struct GPUCullingData
{
	vec4 frustum_planes[4];
	mat4 viewmat;
	int frustum_cull{true};
	int occlusion_cull{true};
	float znear, zfar;
	float p00, p11;
	float pyr_w, pyr_h;
	vec4 cam_pos;
	float lod_base{10.0f};
	float lod_step{1.5f};
	int forced_lod{-1};
	int is_ortho{0};
	uint32_t global_draw_count;
};

export using MaterialBucketFilter = std::function<RenderBucket(Handle64<Material>)>;

}

export namespace lumina::render
{


class GPUScene
{
public:
	GPUScene(vulkan::Device* dev, ResourceManager* rm) : device{dev}, resource_manager{rm}
	{
		gpu_object_buffer = device->create_buffer
		({
			.domain = vulkan::BufferDomain::Device,
			.usage = vulkan::BufferUsage::StorageBuffer,
			.size = sizeof(GPUObjectData) * initial_object_capacity,
			.debug_name = "GPUScene::object_buffer"
		});
	
		streambuf = device->create_buffer
		({
			.domain = vulkan::BufferDomain::Host,
			.usage = vulkan::BufferUsage::StagingBuffer,
			.size = streambuf_size,
			.debug_name = "GPUScene::streambuf"
		});

		spd_globalatomic = device->create_buffer
		({
			.domain = vulkan::BufferDomain::DeviceMapped,
			.usage = vulkan::BufferUsage::StorageBuffer,
			.size = sizeof(uint32_t),
			.debug_name = "GPUScene::spd_atomic"
		});
		*(spd_globalatomic->map<uint32_t>()) = 0u;

		vk::SamplerCreateInfo samplerci
		{
			.magFilter = vk::Filter::eLinear,
			.minFilter = vk::Filter::eLinear,
			.mipmapMode = vk::SamplerMipmapMode::eNearest,
			.addressModeU = vk::SamplerAddressMode::eClampToEdge,
			.addressModeV = vk::SamplerAddressMode::eClampToEdge,
			.addressModeW = vk::SamplerAddressMode::eClampToEdge,
			.anisotropyEnable = false,
			.compareEnable = false,
			.compareOp = vk::CompareOp::eAlways,
			.minLod = 0.0f,
			.maxLod = 1e7f,
			.unnormalizedCoordinates = false
		};

		vk::StructureChain<vk::SamplerCreateInfo, vk::SamplerReductionModeCreateInfo> chain =
		{
			samplerci,
			{
			.reductionMode = vk::SamplerReductionMode::eMin
			}
		};
		dp_sampler = device->get_handle().createSampler(chain.get<vk::SamplerCreateInfo>());

		vk::StructureChain<vk::SamplerCreateInfo, vk::SamplerReductionModeCreateInfo> chain_max =
		{
			samplerci,
			{
			.reductionMode = vk::SamplerReductionMode::eMin
			}
		};

		dp_sampler_max = device->get_handle().createSampler(chain_max.get<vk::SamplerCreateInfo>());
	}

	~GPUScene()
	{
		device->get_handle().destroySampler(dp_sampler);
		device->get_handle().destroySampler(dp_sampler_max);
	}

	Handle<ViewportData> register_viewport(Camera* cam, vulkan::Image* zb, uvec2 res = {0u, 0u})
	{
		viewports.push_back({.camera = cam, .zbuf = zb, .override_res = res});
		return Handle<ViewportData>{static_cast<uint32_t>(viewports.size())};
	}

	void update_viewport(Handle<ViewportData> vp, Camera* cam = nullptr, vulkan::Image* zb = nullptr)
	{
		auto& viewport = viewports[vp - 1];
		if(cam)
			viewport.camera = cam;

		if(zb)
			viewport.zbuf = zb;
	}

	Handle<MeshPass> register_mesh_pass(MeshPass::Type type, Handle<ViewportData> vp, const CullingData& desc)
	{
		passes.push_back(MeshPass{.type = type, .viewport = vp, .cull_data = desc});
		auto& pass = passes.back();

		auto uid = std::to_string(passes.size());

		for(uint32_t i = 0; i < 2; i++)
		{
			pass.gpu_buffers[i].multibatch = device->create_buffer
			({
				.domain = vulkan::BufferDomain::Device,
				.usage = vulkan::BufferUsage::IndirectBuffer,
				.size = sizeof(GPUMultibatch) * pass.multibatch_capacity,
				.debug_name = "gpu_multibatches::" + uid
			});

			pass.gpu_buffers[i].indirect = device->create_buffer
			({
				.domain = vulkan::BufferDomain::Device,
				.usage = vulkan::BufferUsage::StorageBuffer,
				.size = sizeof(GPUIndirectObject) * pass.command_capacity,
				.debug_name = "gpu_indirect::" + uid
			});

			pass.gpu_buffers[i].command = device->create_buffer
			({
				.domain = vulkan::BufferDomain::Device,
				.usage = vulkan::BufferUsage::IndirectBuffer,
				.size = sizeof(vk::DrawIndexedIndirectCommand) * pass.command_capacity,
				.debug_name = "gpu_commands::" + uid
			});

			pass.gpu_buffers[i].visibility_main = device->create_buffer
			({
				.domain = vulkan::BufferDomain::Device,
				.usage = vulkan::BufferUsage::StorageBuffer,
				.size = sizeof(uint32_t) * pass.command_capacity,
				.debug_name = "gpu_visibility::" + uid
			});

			pass.gpu_buffers[i].visibility.push_back(
			device->create_buffer
			({
				.domain = vulkan::BufferDomain::Device,
				.usage = vulkan::BufferUsage::StorageBuffer,
				.size = sizeof(uint32_t) * pass.command_capacity,
				.debug_name = "gpu_visibility::" + uid
			}));

			uint32_t n = (pass.command_capacity / 256) + 1;
			while(n > 1)
			{
				pass.gpu_buffers[i].visibility.push_back(
				device->create_buffer
				({
					.domain = vulkan::BufferDomain::Device,
					.usage = vulkan::BufferUsage::StorageBuffer,
					.size = sizeof(uint32_t) * n,
					.debug_name = "prefix_scan_intermediate::" + uid
				}));
				n = (n / 256) + 1;
			}

			pass.gpu_buffers[i].visibility.push_back(
			device->create_buffer
			({
				.domain = vulkan::BufferDomain::Device,
				.usage = vulkan::BufferUsage::StorageBuffer,
				.size = sizeof(uint32_t),
				.debug_name = "prefix_scan_intermediate::" + uid
			}));

			pass.gpu_buffers[i].compact_command = device->create_buffer
			({
				.domain = vulkan::BufferDomain::Device,
				.usage = vulkan::BufferUsage::IndirectBuffer,
				.size = sizeof(vk::DrawIndexedIndirectCommand) * pass.command_capacity,
				.debug_name = "gpu_commands::" + uid
			});

			pass.gpu_buffers[i].culldata = device->create_buffer
			({
				.domain = vulkan::BufferDomain::DeviceMapped,
				.usage = vulkan::BufferUsage::StorageBuffer,
				.size = sizeof(GPUCullingData),
				.debug_name = "gpu_culldata::" + uid
			});
		}

		return Handle<MeshPass>{static_cast<uint32_t>(passes.size())};
	}

	void register_material_bucket_filter(Handle<MaterialTemplate> tmp, MaterialBucketFilter&& filter)
	{
		mtl_bucket_filters[tmp] = std::move(filter);
	}

	RenderBucket determine_object_bucket(const RenderObject& obj)
	{
		auto tmp = template_from_material(obj.material);
		if(!mtl_bucket_filters.contains(tmp))
			return RenderBucket::Default;

		return mtl_bucket_filters[tmp](obj.material);
	}

	Handle<RenderObject> register_object(RenderObject&& obj, array_proxy<Handle<MeshPass>> passes)
	{
		obj.bucket = determine_object_bucket(obj); 
		renderables.push_back(obj);

		Handle<RenderObject> rh{static_cast<uint32_t>(renderables.size())};

		for(auto handle : passes)
		{
			if(!handle)
				continue;

			get_pass(handle).unbatched_objects.push_back(rh);
		}

		dirty_objects.push_back(std::make_pair(rh, false));
		return rh;
	}

	void combine_pass_objects(Handle<MeshPass> src, Handle<MeshPass> dst)
	{
		auto& spass = get_pass(src);
		auto& dpass = get_pass(dst);

		for(auto& obj : spass.indirect_batches)
			dpass.unbatched_objects.push_back(obj.object);	
	}

	void update_object(Handle<RenderObject> obj, Transform* tf)
	{
		auto& object = get_object(obj);

		if(tf)
			object.transform = *tf;

		dirty_objects.push_back(std::make_pair(obj, false));
	}

	RenderObject& get_object(Handle<RenderObject> obj)
	{
		assert(obj);
		return renderables[obj - 1];
	}

	vulkan::Buffer* get_object_buffer()
	{
		return gpu_object_buffer.get();
	}

	Multibatch* fetch_batch(Handle<MeshPass> mp, Handle<MaterialTemplate> pipe, RenderBucket bucket)
	{
		for(auto& mb : get_pass(mp).multibatches)
		{
			if(mb.pipeline == pipe && mb.bucket == bucket)
				return &mb;
		}

		return nullptr;
	}

	bool render_batch(vulkan::CommandBuffer& cmd, Handle<MeshPass> mp, Handle<MaterialTemplate> pipe, RenderBucket bucket)
	{
		MeshPass& pass = get_pass(mp);

		Multibatch* batch = fetch_batch(mp, pipe, bucket);
		if(!batch)
			return false;

		uint32_t batchID = static_cast<uint32_t>(batch - pass.multibatches.data());

		if(pass.cull_data.compact_drawcalls)
		{
			cmd.draw_indexed_indirect_count
			(
				pass.gpu_buffers[device->current_frame_index()].compact_command.get(),
				batch->first * sizeof(vk::DrawIndexedIndirectCommand),
				pass.gpu_buffers[device->current_frame_index()].multibatch.get(),
				batchID * sizeof(GPUMultibatch) + __builtin_offsetof(GPUMultibatch, draw_count),
				batch->count,
				sizeof(vk::DrawIndexedIndirectCommand)
			);
		}
		else
		{
			cmd.draw_indexed_indirect
			(
				pass.gpu_buffers[device->current_frame_index()].command.get(),
				batch->first * sizeof(vk::DrawIndexedIndirectCommand),
				batch->count,
				sizeof(vk::DrawIndexedIndirectCommand)
			);
		}

		return true;
	}

	void ready_objects()
	{
		ZoneScoped;
		uint32_t streambuf_head = 0;
		if(!dirty_objects.empty())
		{
			ZoneScopedN("copy_gpu_objects");
			auto cb = device->request_command_buffer(vulkan::Queue::Graphics, "gpu_scene::ready_objects");
			device->start_perf_event("gpu_scene::ready_objects", cb);
			{
				for(auto& [handle, processed]: dirty_objects)
				{
					auto& object = get_object(handle);
					auto& t = object.transform;
					
					render::Mesh& mesh = resource_manager->get_mesh(object.mesh);
					if(!mesh.in_gpumem)
					{
						continue;
					}
					processed = true;

					auto tm = object.transform.as_matrix();

					vec4 center = vec4{mesh.bounds.sphere.center, 1.0f} * tm;
					float radius = std::abs(std::max(std::max(t.scale.x, t.scale.y), t.scale.z)) * mesh.bounds.sphere.radius;

					GPUObjectData data
					{
						tm,
						vec4{center.x, center.y, center.z, radius},
						static_cast<uint32_t>(object.material & 0xFFFFFFFF)
					};

					memcpy(streambuf->map<std::byte>() + streambuf_head, &data, sizeof(GPUObjectData));

					vk::BufferCopy region
					{
						.srcOffset = streambuf_head,
						.dstOffset = (handle - 1) * sizeof(GPUObjectData),
						.size = sizeof(GPUObjectData)
					};
					cb.vk_object().copyBuffer(streambuf->handle, gpu_object_buffer->handle, 1, &region);

					streambuf_head += sizeof(GPUObjectData); 
				}

				cb.pipeline_barrier
				({
				 	{
			 		.src_stage = vk::PipelineStageFlagBits2::eTransfer,
			       		.src_access = vk::AccessFlagBits2::eTransferWrite,
					.dst_stage = vk::PipelineStageFlagBits2::eComputeShader | vk::PipelineStageFlagBits2::eVertexShader,
					.dst_access = vk::AccessFlagBits2::eShaderRead,
					.buffer = gpu_object_buffer.get()
					}
				});		
			}
			device->end_perf_event(cb);

			auto cwb = device->submit(cb, vulkan::submit_signal_timeline);
			device->wait_timeline(vulkan::Queue::Graphics, cwb);
			std::erase_if(dirty_objects, [](const std::pair<Handle<RenderObject>, bool>& elem)
			{
				return elem.second;
			});
		}
		streambuf_head = 0;

		auto cb = device->request_command_buffer(vulkan::Queue::Graphics, "gpu_scene::refresh_passes");
		device->start_perf_event("gpu_scene::refresh_passes", cb);

		auto fidx = device->current_frame_index();

		for(auto& p : passes)
		{
			refresh_pass(p);
		
			{
			ZoneScopedN("copy_gpu_batches");

			uint32_t multibuf_size = p.multibatches.size() * sizeof(GPUMultibatch);
			uint32_t indbuf_size = p.indirect_batches.size() * sizeof(GPUIndirectObject);
			uint32_t cmdbuf_size = p.indirect_batches.size() * sizeof(vk::DrawIndexedIndirectCommand);

			if(!p.multibatches.empty())
			{
				for(auto i = 0ull; i < p.multibatches.size(); i++)
				{
					const Multibatch& batch = p.multibatches[i];
					*(streambuf->map<GPUMultibatch>(streambuf_head) + i) = GPUMultibatch{batch.first, batch.count, 0};
				}

				vk::BufferCopy region
				{
					.srcOffset = streambuf_head,
					.dstOffset = 0,
					.size = sizeof(GPUMultibatch) * p.multibatches.size()
				};
				cb.vk_object().copyBuffer(streambuf->handle, p.gpu_buffers[fidx].multibatch->handle, 1, &region);

				streambuf_head += sizeof(GPUMultibatch) * p.multibatches.size();
				for(auto i = 0ull; i < p.indirect_batches.size(); i++)
				{
					const IndirectBatch& batch = p.indirect_batches[i];
					const Mesh& mesh = resource_manager->get_mesh(get_object(batch.object).mesh);

					*(streambuf->map<GPUIndirectObject>(streambuf_head) + i) = GPUIndirectObject
					{
						.lod0_offset = mesh.lod0_offset,
						.lod_count = mesh.lod_count,
						.batch = batch.batch,
						.object = batch.object - 1
					};
				}

				region.srcOffset = streambuf_head;
				region.size = sizeof(GPUIndirectObject) * p.indirect_batches.size();
				cb.vk_object().copyBuffer(streambuf->handle, p.gpu_buffers[fidx].indirect->handle, 1, &region);

				streambuf_head += sizeof(GPUIndirectObject) * p.indirect_batches.size();

				for(auto i = 0ull; i < p.indirect_batches.size(); i++)
				{
					const IndirectBatch& batch = p.indirect_batches[i];
					*(streambuf->map<vk::DrawIndexedIndirectCommand>(streambuf_head) + i) = vk::DrawIndexedIndirectCommand
					{
						.indexCount = 0u,
						.instanceCount = 0u,
						.firstIndex = 0u,
						.vertexOffset = 0,
						.firstInstance = batch.object - 1
					};
				}

				region.srcOffset = streambuf_head;
				region.size = sizeof(vk::DrawIndexedIndirectCommand) * p.indirect_batches.size();
				cb.vk_object().copyBuffer(streambuf->handle, p.gpu_buffers[fidx].command->handle, 1, &region);

				streambuf_head += sizeof(vk::DrawIndexedIndirectCommand) * p.indirect_batches.size();
			}



			}
		}

		cb.memory_barrier
		({
			{
			.src_stage = vk::PipelineStageFlagBits2::eTransfer,
			.src_access = vk::AccessFlagBits2::eTransferWrite,
			.dst_stage = vk::PipelineStageFlagBits2::eComputeShader,
			.dst_access = vk::AccessFlagBits2::eShaderRead | vk::AccessFlagBits2::eShaderWrite
			}
		});

		device->end_perf_event(cb);
		device->submit(cb);
	}

	void ready_culling()
	{
		ZoneScoped;

		auto previous_pow2 = [](uint32_t v) -> uint32_t
		{
			uint32_t res = 1;

			while(res * 2 < v)
				res *= 2;

			return res;
		};

		auto dp_num_mips = [](uint32_t w, uint32_t h) -> uint32_t
		{
			uint32_t result = 1;

			while(w > 2 || h > 2)
			{
				result++;
				w /= 2;
				h /= 2;
			}

			return result;
		};

		for(auto& vp : viewports)
		{
			if(vp.zbuf)
			{
				assert(vp.camera);
				auto res = vp.override_res != uvec2{0u} ? vp.override_res : vp.camera->get_res();

				vp.map_width = previous_pow2(res.x);
				vp.map_height = previous_pow2(res.y);

				vp.dp_width = previous_pow2(vp.map_width);
				vp.dp_height = previous_pow2(vp.map_height);
				vp.dp_miplevels = dp_num_mips(vp.dp_width, vp.dp_height);

				if(!vp.depth_pyramid || vp.last_res != res)
				{
					vp.zbuf_remap = device->create_image
					({
						.width = vp.map_width,
						.height = vp.map_height,
						.format = vk::Format::eR32Sfloat,
						.usage = vulkan::ImageUsage::RWCompute,
						.debug_name = std::format("remap::zbuf_{:#x}", reinterpret_cast<std::uint64_t>(vp.zbuf))
					});

					vp.depth_pyramid = device->create_image
					({
						.width = vp.dp_width,
						.height = vp.dp_height,
						.levels = vp.dp_miplevels,
						.format = vk::Format::eR32Sfloat,
						.usage = vulkan::ImageUsage::RWCompute,
						.debug_name = std::format("depth_pyramid::zbuf_{:#x}", reinterpret_cast<std::uint64_t>(vp.zbuf))
					});

					for(auto i = 0u; i < vp.dp_miplevels; i++)
						vp.dp_layer_views[i] = vp.depth_pyramid->get_mip_view(i);

					vp.last_res = res;

					vp.needs_barrier = true;
				}
			}
		}


		for(auto& pass : passes)
		{
			if(pass.multibatches.empty())
				continue;

			assert(pass.viewport);

			const CullingData& cd = pass.cull_data;
			const ViewportData& vp = viewports[pass.viewport - 1];
			assert(vp.camera);

			auto* gcd = pass.gpu_buffers[device->current_frame_index()].culldata->map<GPUCullingData>();
			gcd->frustum_cull = cd.frustum_cull;
			gcd->occlusion_cull = cd.occlusion_cull;
			gcd->forced_lod = cd.forced_lod;
			gcd->lod_base = cd.lod_base;
			gcd->lod_step = cd.lod_step;
			
			gcd->viewmat = vp.camera->get_view_matrix();
			
			vec2 cplanes = vp.camera->get_clip_planes();
			gcd->znear = cplanes.x;
			gcd->zfar = cplanes.y;

			mat4 proj = vp.camera->get_projection_matrix();
			mat4 projT = mat4::transpose(proj);

			if(cd.is_ortho)
			{
				gcd->frustum_planes[0] = Plane(projT[3] + projT[0]).normalize().as_vector(); // x + w < 0
				gcd->frustum_planes[1] = Plane(projT[3] - projT[0]).normalize().as_vector(); // x - w < 0
													     				gcd->frustum_planes[2] = Plane(projT[3] + projT[1]).normalize().as_vector(); // y + w > 0
																										     				gcd->frustum_planes[3] = Plane(projT[3] - projT[1]).normalize().as_vector(); // y - w < 0
			}
			else
			{
				vec4 frustumX = Plane(projT[3] + projT[0]).normalize().as_vector();
				vec4 frustumY = Plane(projT[3] + projT[1]).normalize().as_vector();
				gcd->frustum_planes[0] = vec4{frustumX.x, frustumX.z, frustumY.y, frustumY.z};
			}

			gcd->p00 = proj[0][0];
			gcd->p11 = proj[1][1];

			gcd->pyr_w = static_cast<float>(vp.dp_width);
			gcd->pyr_h = static_cast<float>(vp.dp_height);

			gcd->cam_pos = vec4{vp.camera->get_pos(), 1.0f};
			gcd->is_ortho = int(cd.is_ortho);
			gcd->global_draw_count = static_cast<uint32_t>(pass.indirect_batches.size());
		}
	}

	void execute_compute_cull()
	{
		ZoneScoped;
		auto cmd = device->request_command_buffer(vulkan::Queue::Graphics, "gpu_scene::cull");
		device->start_perf_event("gpu_scene::cull", cmd);

		cmd.bind_pipeline({"cull.comp"});

		for(auto& vp : viewports)
		{
			if(vp.zbuf)
			{
				if(vp.needs_barrier)
				{
					cmd.pipeline_barrier
					({
						{
						.src_stage = vk::PipelineStageFlagBits2::eTopOfPipe,
						.dst_stage = vk::PipelineStageFlagBits2::eComputeShader,
						.dst_access = vk::AccessFlagBits2::eShaderRead,
						.src_layout = vk::ImageLayout::eUndefined,
						.dst_layout = vk::ImageLayout::eGeneral,
						.image = vp.depth_pyramid.get()
						}
					});
					vp.needs_barrier = false;
				}
				else
				{
					cmd.pipeline_barrier
					({
						{
						.src_stage = vk::PipelineStageFlagBits2::eComputeShader,
						.dst_stage = vk::PipelineStageFlagBits2::eComputeShader,
						.dst_access = vk::AccessFlagBits2::eShaderRead,
						.src_layout = vk::ImageLayout::eGeneral,
						.dst_layout = vk::ImageLayout::eGeneral,
						.src_queue = vulkan::Queue::Compute,
						.dst_queue = vulkan::Queue::Graphics,
						.image = vp.depth_pyramid.get()
						}
					});
				}
			
			}
		}

		for(auto& pass : passes)
		{
			if(pass.multibatches.empty())
				continue;

			auto& vp = viewports[pass.viewport - 1];

			auto& gbuf = pass.gpu_buffers[device->current_frame_index()];

			cmd.push_descriptor_set
			({
				.storage_buffers =
				{
					{0, gbuf.indirect.get()},
					{1, gbuf.command.get()},
					{2, gbuf.multibatch.get()},
					{3, gpu_object_buffer.get()},
					{4, resource_manager->get_mesh_buffers().lod},
					{5, gbuf.culldata.get()},
					{6, gbuf.visibility_main.get()}
				}
			});

			if(vp.zbuf)
			{
				cmd.push_descriptor_set
				({
					.sampled_images =
					{
						{
						7,
						vp.depth_pyramid->get_default_view(),
						pass.cull_data.is_ortho ? dp_sampler_max : dp_sampler,
						vk::ImageLayout::eGeneral
						}
					}
				});
			}

			cmd.dispatch((static_cast<uint32_t>(pass.indirect_batches.size()) / 256u) + 1u, 1u, 1u);
		}

		for(auto& vp : viewports)
		{
			if(vp.zbuf)
			{
				cmd.pipeline_barrier
				({
				 	{
					.src_stage = vk::PipelineStageFlagBits2::eComputeShader,
					.src_access = vk::AccessFlagBits2::eShaderWrite,
					.dst_stage = vk::PipelineStageFlagBits2::eComputeShader,
					.src_layout = vk::ImageLayout::eGeneral,
					.dst_layout = vk::ImageLayout::eGeneral,
					.src_queue = vulkan::Queue::Graphics,
					.dst_queue = vulkan::Queue::Compute,
					.image = vp.depth_pyramid.get()
					}
				});
			}
		}

		cmd.memory_barrier
		({
			{
			.src_stage = vk::PipelineStageFlagBits2::eComputeShader,
			.src_access = vk::AccessFlagBits2::eShaderWrite,
			.dst_stage = vk::PipelineStageFlagBits2::eComputeShader,
			.dst_access = vk::AccessFlagBits2::eShaderRead
			}
		});
		
	
		for(auto& pass : passes)
		{
			if(pass.multibatches.empty())
				continue;
			
			if(!pass.cull_data.compact_drawcalls)
				continue;
			
			auto& gbuf = pass.gpu_buffers[device->current_frame_index()];

			auto psize = static_cast<uint32_t>(pass.indirect_batches.size());
		
			cmd.bind_pipeline({"prefix_scan_index.comp"});
			cmd.push_descriptor_set
			({
				.storage_buffers = 
				{
					{0, gbuf.visibility_main.get()},
					{1, gbuf.visibility[0].get()},
					{2, gbuf.visibility[1].get()}
				}
			});

			pass.intermediate_sizes[0] = psize;
			cmd.push_constant(&psize, sizeof(uint32_t));
			psize = (psize / 256u) + 1;
			cmd.dispatch(psize, 1u, 1u);
				
			cmd.memory_barrier
			({
				{
				.src_stage = vk::PipelineStageFlagBits2::eComputeShader,
				.src_access = vk::AccessFlagBits2::eShaderWrite,
				.dst_stage = vk::PipelineStageFlagBits2::eComputeShader,
				.dst_access = vk::AccessFlagBits2::eShaderRead
				}
			});

			for(uint32_t i = 1; i < gbuf.visibility.size() - 1; i++)
			{
				if(i > 1)
					cmd.bind_pipeline({"prefix_scan_index.comp"});

				cmd.push_descriptor_set
				({
					.storage_buffers =
					{
						{0, gbuf.visibility[i].get()},
						{1, gbuf.visibility[i].get()},
						{2, gbuf.visibility[i + 1].get()}
					}
				});

				pass.intermediate_sizes[i] = psize;
				cmd.push_constant(&psize, sizeof(uint32_t));
				psize = (psize / 256u) + 1;
				cmd.dispatch(psize, 1u, 1u);

				cmd.memory_barrier
				({
					{
					.src_stage = vk::PipelineStageFlagBits2::eComputeShader,
					.src_access = vk::AccessFlagBits2::eShaderWrite,
					.dst_stage = vk::PipelineStageFlagBits2::eComputeShader,
					.dst_access = vk::AccessFlagBits2::eShaderRead
					}
				});

				cmd.bind_pipeline({"prefix_scan_add_partial.comp"});

				cmd.push_descriptor_set
				({
					.storage_buffers =
					{
						{0, gbuf.visibility[i].get()},
						{1, gbuf.visibility[i - 1].get()}
					}
				});

				cmd.push_constant(&pass.intermediate_sizes[i - 1], sizeof(uint32_t));
				cmd.dispatch(pass.intermediate_sizes[i], 1u, 1u);
				
				cmd.memory_barrier
				({
					{
					.src_stage = vk::PipelineStageFlagBits2::eComputeShader,
					.src_access = vk::AccessFlagBits2::eShaderWrite,
					.dst_stage = vk::PipelineStageFlagBits2::eComputeShader,
					.dst_access = vk::AccessFlagBits2::eShaderRead
					}
				});
			}
		}

		cmd.bind_pipeline({"compact_drawcalls.comp"});
		
		for(auto& pass : passes)
		{
			if(pass.multibatches.empty())
				continue;

			if(!pass.cull_data.compact_drawcalls)
				continue;

			auto& gbuf = pass.gpu_buffers[device->current_frame_index()];
			auto psize = static_cast<uint32_t>(pass.indirect_batches.size());

			cmd.push_descriptor_set
			({
				.storage_buffers =
				{
					{0, gbuf.command.get()},
					{1, gbuf.compact_command.get()},
					{2, gbuf.visibility[0].get()},
					{3, gbuf.visibility_main.get()},
					{4, gbuf.multibatch.get()},
					{5, gbuf.indirect.get()}
				}
			});

			cmd.push_constant(&psize, sizeof(uint32_t));
			cmd.dispatch((psize / 256u) + 1u, 1u, 1u);
		}

		cmd.memory_barrier
		({
			{
			.src_stage = vk::PipelineStageFlagBits2::eComputeShader,
			.src_access = vk::AccessFlagBits2::eShaderWrite,
			.dst_stage = vk::PipelineStageFlagBits2::eDrawIndirect,
			.dst_access = vk::AccessFlagBits2::eIndirectCommandRead
			}
		});

		device->end_perf_event(cmd);
		device->submit(cmd);
	}

	void build_depth_pyramid(vulkan::CommandBuffer& cmd)
	{
		ZoneScoped;
		device->start_perf_event("gpu_scene::buildHZB", cmd);
		cmd.bind_pipeline({"depthreduce.comp"});

		for(auto& vp : viewports)
		{
			if(!vp.zbuf || vp.needs_barrier)
				continue;

			cmd.pipeline_barrier
			({
				{
				.src_stage = vk::PipelineStageFlagBits2::eTopOfPipe,
				.dst_stage = vk::PipelineStageFlagBits2::eComputeShader,
				.dst_access = vk::AccessFlagBits2::eShaderWrite,
				.src_layout = vk::ImageLayout::eUndefined,
				.dst_layout = vk::ImageLayout::eGeneral,
				.image = vp.zbuf_remap.get()
				}
			});


			cmd.push_descriptor_set
			({
				.sampled_images =
				{
					{
					0,
					vp.zbuf->get_default_view(),
					dp_sampler,
					vk::ImageLayout::eShaderReadOnlyOptimal
					}
				},
				.storage_images =
				{
					{1, vp.zbuf_remap->get_default_view()}
				}
			});

			vec2 reduce_data{float(vp.map_width), float(vp.map_height)};

			cmd.push_constant(&reduce_data, sizeof(vec2));
			cmd.dispatch(vulkan::group_count(vp.map_width, 32), vulkan::group_count(vp.map_height, 32), 1);
			cmd.pipeline_barrier
			({
			 	{
				.src_stage = vk::PipelineStageFlagBits2::eComputeShader,
				.src_access = vk::AccessFlagBits2::eShaderWrite,
				.dst_stage = vk::PipelineStageFlagBits2::eComputeShader,
				.dst_access = vk::AccessFlagBits2::eShaderRead,
				.src_layout = vk::ImageLayout::eGeneral,
				.dst_layout = vk::ImageLayout::eGeneral,
				.image = vp.zbuf_remap.get()
				},
				{
				.src_stage = vk::PipelineStageFlagBits2::eComputeShader,
				.dst_stage = vk::PipelineStageFlagBits2::eComputeShader,
				.dst_access = vk::AccessFlagBits2::eShaderRead | vk::AccessFlagBits2::eShaderWrite,
				.src_layout = vk::ImageLayout::eGeneral,
				.dst_layout = vk::ImageLayout::eGeneral,
				.src_queue = vulkan::Queue::Graphics,
				.dst_queue = vulkan::Queue::Compute,
				.image = vp.depth_pyramid.get()
				},
			});
		}

		cmd.bind_pipeline({"depth_downsample_singlepass.comp"});

		for(auto& vp : viewports)
		{
			if(!vp.zbuf || vp.needs_barrier)
				continue;

			cmd.push_descriptor_set
			({
				.storage_image_arrays =
				{
					{
					2,
					array_proxy<vulkan::ImageView*>{vp.dp_layer_views.data(), vp.dp_miplevels}
					}
				},
				.separate_images =
				{
					{
					0,
					vp.zbuf_remap->get_default_view(),
					}
				},
				.samplers =
				{
					{
					1,
					dp_sampler
					}
				},
				.storage_buffers =
				{
					{
					3,
					spd_globalatomic.get()
					}
				}
			});

			struct SPDpcb
			{
				vec2 inv_size;
				uint32_t mips;
				uint32_t numWorkGroups;
				bool min_reduce;
			} spd_pcb;

			spd_pcb.inv_size = vec2{1.0f / float(vp.map_width), 1.0f / float(vp.map_height)};
			spd_pcb.mips = vp.dp_miplevels,
			spd_pcb.numWorkGroups = ((vp.map_width - 1) / 64) + 1  * ((vp.map_height - 1) / 64 + 1);
			spd_pcb.min_reduce = true;

			cmd.push_constant(&spd_pcb, sizeof(SPDpcb));

			cmd.dispatch((vp.map_width - 1) / 64 + 1, (vp.map_height - 1) / 64 + 1, 1);

			cmd.pipeline_barrier
			({
				{
				.src_stage = vk::PipelineStageFlagBits2::eComputeShader,
				.src_access = vk::AccessFlagBits2::eShaderWrite,
				.dst_stage = vk::PipelineStageFlagBits2::eComputeShader,
				.src_layout = vk::ImageLayout::eGeneral,
				.dst_layout = vk::ImageLayout::eGeneral,
				.src_queue = vulkan::Queue::Compute,
				.dst_queue = vulkan::Queue::Graphics,
				.image = vp.depth_pyramid.get()
				}
			});

			cmd.pipeline_barrier
			({
				{
				.src_stage = vk::PipelineStageFlagBits2::eComputeShader,
				.src_access = vk::AccessFlagBits2::eShaderWrite,
				.dst_stage = vk::PipelineStageFlagBits2::eComputeShader,
				.dst_access = vk::AccessFlagBits2::eShaderWrite,
				.buffer = spd_globalatomic.get()
				}
			});
		}
		device->end_perf_event(cmd);
	}

	void draw_ui()
	{
		int ctr = 0;
                for(auto& pass : passes)
                {
                        ImGui::PushID(ctr++);

                        auto to_cstring = [](MeshPass::Type type) -> const char*
                        {
                                switch(type)
                                {
                                case MeshPass::Type::ForwardOpaque:
                                        return "forwardOpaque";
                                case MeshPass::Type::ForwardTransparent:
                                        return "forwardTransparent";
                                case MeshPass::Type::Shadowcast:
                                        return "shadowcast";
                                default:
                                        return "generic";
                                }
                        };

                        ImGui::Text("%s: unbatched %zu, cmd %zu, multibatch %zu", to_cstring(pass.type), pass.unbatched_objects.size(), pass.indirect_batches.size(), pass.multibatches.size());
                        ImGui::Checkbox("frustum culling", &pass.cull_data.frustum_cull);
                        ImGui::Checkbox("occlusion culling", &pass.cull_data.occlusion_cull);
			ImGui::Checkbox("drawcall compaction", &pass.cull_data.compact_drawcalls);
			ImGui::Checkbox("sort drawcalls", &pass.cull_data.sort_drawcalls);
                        ImGui::SliderInt("forced lod", &pass.cull_data.forced_lod, -1, 4);
                        ImGui::DragFloat("lod base", &pass.cull_data.lod_base, 0.01f, 0.0f, 10.0f);
                        ImGui::DragFloat("lod step", &pass.cull_data.lod_step, 0.01f, 0.0f, 5.0f);
                        ImGui::PopID();
                }
	}
private:
	MeshPass& get_pass(Handle<MeshPass> mp)
	{
		assert(mp);
		return passes[mp - 1];
	}

	void refresh_pass(MeshPass& pass)
	{
		ZoneScoped;
		std::vector<IndirectBatch> new_batches;
		{
			new_batches.reserve(pass.unbatched_objects.size());
			for(auto handle : pass.unbatched_objects)
			{
				IndirectBatch cmd;
				cmd.object = handle;

				auto& obj = get_object(handle);

				uint64_t meshmat = (obj.material >> 32) << 32 | uint64_t(obj.bucket); 
				//uint64_t meshmat = uint64_t(obj.material) ^ uint64_t(obj.mesh);

				cmd.sort_key = meshmat | (uint64_t(obj.custom_key) << 32);
				new_batches.push_back(cmd);
			}
			pass.unbatched_objects.clear();
		}

		{
			std::sort(new_batches.begin(), new_batches.end(),
			[](const IndirectBatch& lhs, const IndirectBatch& rhs)
			{
				if(lhs.sort_key < rhs.sort_key) { return true; }
				else if(lhs.sort_key == rhs.sort_key) { return lhs.object < rhs.object; }
				else{ return false; }
			});
		}

		{
			if(pass.indirect_batches.size() > 0 && new_batches.size() > 0)
			{
				uint32_t index = static_cast<uint32_t>(pass.indirect_batches.size());
				pass.indirect_batches.reserve(pass.indirect_batches.size() + new_batches.size());
				pass.indirect_batches.insert(pass.indirect_batches.end(), new_batches.begin(), new_batches.end());

				IndirectBatch* begin = pass.indirect_batches.data();
				IndirectBatch* mid = begin + index;
				IndirectBatch* end = begin + pass.indirect_batches.size();

				std::inplace_merge(begin, mid, end,
				[](const IndirectBatch& lhs, const IndirectBatch& rhs)
				{
					if(lhs.sort_key < rhs.sort_key) { return true; }
					else if(lhs.sort_key == rhs.sort_key) { return lhs.object < rhs.object; }
					else{ return false; }
				});
			}
			else if(new_batches.size() > 0)
			{
				pass.indirect_batches = std::move(new_batches);
			}
		}
		
		if(pass.cull_data.sort_drawcalls && viewports[pass.viewport - 1].camera)
		{
			ZoneScopedN("draw_sort");
			std::sort(std::begin(pass.indirect_batches), std::end(pass.indirect_batches), 
			[this, &pass](const IndirectBatch& lhs, const IndirectBatch& rhs)
			{
				if(lhs.sort_key < rhs.sort_key) { return true; }
				else if(lhs.sort_key == rhs.sort_key)
				{
					auto& vp = viewports[pass.viewport - 1];

					float ldist = (get_object(lhs.object).transform.translation - vp.camera->get_pos()).magnitude_sqr();
					float rdist = (get_object(rhs.object).transform.translation - vp.camera->get_pos()).magnitude_sqr();

					if(pass.type != MeshPass::Type::ForwardTransparent)
					{
						if(ldist < rdist)
							return true;
					}
					else
					{
						if(ldist > rdist)
							return true;
					}

					return false;
				}
				else { return false; }
			});
		}

		{
			pass.multibatches.clear();

			if(!pass.indirect_batches.empty())
			{
				Multibatch nb;
				nb.first = 0;
				nb.count = 1;
				nb.pipeline = Handle<MaterialTemplate>{static_cast<uint32_t>(get_object(pass.indirect_batches[0].object).material >> 32)};
				nb.bucket = get_object(pass.indirect_batches[0].object).bucket;

				pass.indirect_batches[0].batch = 0;

				for(uint32_t i = 1; i < pass.indirect_batches.size(); i++)
				{
					IndirectBatch* joinbatch = &pass.indirect_batches[nb.first];
					IndirectBatch* batch = &pass.indirect_batches[i];

					uint32_t jb_pipe = get_object(joinbatch->object).material >> 32;
					uint32_t b_pipe = get_object(batch->object).material >> 32;

					bool same_pipe = (jb_pipe == b_pipe);

					RenderBucket jb_bucket = get_object(joinbatch->object).bucket;
					RenderBucket b_bucket = get_object(batch->object).bucket;

					bool same_bucket = (jb_bucket == b_bucket);

					if(!same_pipe || !same_bucket)
					{
						pass.multibatches.push_back(nb);
						nb.first = i;
						nb.count = 1;
						nb.pipeline = Handle<MaterialTemplate>{b_pipe};
						nb.bucket = b_bucket;
					}
					else
					{
						nb.count++;
					}
					
					batch->batch = static_cast<uint32_t>(pass.multibatches.size());
				}
				pass.multibatches.push_back(nb);
			}
		}
	}

	vulkan::Device* device;
	ResourceManager* resource_manager;

	std::vector<ViewportData> viewports;
	std::vector<MeshPass> passes;
	std::vector<RenderObject> renderables;
	std::unordered_map<Handle<MaterialTemplate>, MaterialBucketFilter> mtl_bucket_filters;
	std::vector<std::pair<Handle<RenderObject>, bool>> dirty_objects;

	constexpr static uint32_t initial_object_capacity = 65536;

	uint32_t gpu_object_head = 0;
	vulkan::BufferHandle gpu_object_buffer;

	constexpr static uint32_t streambuf_size = 64 * 1024 * 1024;
	vulkan::BufferHandle streambuf;

	vulkan::BufferHandle spd_globalatomic;

	vk::Sampler dp_sampler;
	vk::Sampler dp_sampler_max;
};

}
