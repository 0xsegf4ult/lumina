module;

#include <cassert>
#include <tracy/Tracy.hpp>

export module lumina.renderer:gpu_scene;

import :mesh_registry;
import :material_registry;
import :camera;
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

export struct Multibatch
{
	uint32_t first;
	uint32_t count;
	Handle<MaterialTemplate> pipeline;
};

export struct ViewportData
{
	Camera* camera{nullptr};
	vulkan::Image* zbuf{nullptr};
	vulkan::ImageHandle depth_pyramid{nullptr};

	uvec2 override_res{0};
	uvec2 last_res{0};
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
};

export struct RenderObject
{
	Handle<Mesh> mesh;
	Handle64<Material> material;
	Transform transform;
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
	std::vector<IndirectBatch> padded_indirect_batches{};
	std::vector<Multibatch> multibatches{};

	size_t multibatch_capacity = 32;
	size_t command_capacity = 65536;

	struct GPUBuffers
	{
		vulkan::BufferHandle multibatch;
		vulkan::BufferHandle indirect;
		vulkan::BufferHandle command;
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
	vec4 frustum_planes;
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
	uint32_t global_draw_count;
};

struct GPUCullingDataOrtho
{
	vec4 frustum_planes[4];
	mat4 viewmat;
	vec4 cam_pos;
	int frustum_cull{true};
	int occlusion_cull{true};
	float znear, zfar;
	float p00, p11;
	float pyr_w, pyr_h;
	float lod_base{10.0f};
	float lod_step{1.5f};
	int forced_lod{-1};
	uint32_t global_draw_count;
};

}

uint32_t align_up(uint32_t val, uint32_t size)
{
	auto mod{val % size};
	val -= mod;
	return mod == 0 ? val : val + size;
}

export namespace lumina::render
{


class GPUScene
{
public:
	GPUScene(vulkan::Device* dev, MeshRegistry* mr) : device{dev}, mesh_registry{mr}
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
				.debug_name = "gpu::multibatches::" + uid
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

			pass.gpu_buffers[i].culldata = device->create_buffer
			({
				.domain = vulkan::BufferDomain::DeviceMapped,
				.usage = vulkan::BufferUsage::StorageBuffer,
				.size = desc.is_ortho ? sizeof(GPUCullingDataOrtho) : sizeof(GPUCullingData),
				.debug_name = "gpu_culldata::" + uid
			});
		}

		return Handle<MeshPass>{static_cast<uint32_t>(passes.size())};
	}

	Handle<RenderObject> register_object(RenderObject&& obj, array_proxy<Handle<MeshPass>> passes)
	{
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

	Multibatch* fetch_batch(Handle<MeshPass> mp, Handle<MaterialTemplate> pipe)
	{
		for(auto& mb : get_pass(mp).multibatches)
		{
			if(mb.pipeline == pipe)
				return &mb;
		}

		return nullptr;
	}

	bool render_batch(vulkan::CommandBuffer& cmd, Handle<MeshPass> mp, Handle<MaterialTemplate> pipe)
	{
		MeshPass& pass = get_pass(mp);

		Multibatch* batch = fetch_batch(mp, pipe);
		if(!batch)
			return false;

		uint32_t batchID = static_cast<uint32_t>(batch - pass.multibatches.data());

		cmd.vk_object().drawIndexedIndirectCount
		(
			pass.gpu_buffers[cmd.ctx_index].command->handle,
			batch->first * sizeof(vk::DrawIndexedIndirectCommand),
			pass.gpu_buffers[cmd.ctx_index].multibatch->handle,
			batchID * sizeof(GPUMultibatch) + __builtin_offsetof(GPUMultibatch, draw_count),
			batch->count,
			sizeof(vk::DrawIndexedIndirectCommand)
		);

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
			cb.debug_name("gpu_scene::ready_objects");
			{
				for(auto& [handle, processed]: dirty_objects)
				{
					auto& object = get_object(handle);
					auto& t = object.transform;
					
					render::Mesh& mesh = mesh_registry->get(object.mesh);
					if(!mesh.in_gpumem)
					{
						continue;
					}
					processed = true;

					vec4 center = vec4{mesh.bounds.sphere.center, 1.0f} * t.as_matrix();
					float radius = std::abs(std::max(std::max(t.scale.x, t.scale.y), t.scale.z)) * mesh.bounds.sphere.radius;

					GPUObjectData data
					{
						object.transform.as_matrix(),
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
			log::debug("copying {} objects to gpu", dirty_objects.size());
			std::erase_if(dirty_objects, [](const std::pair<Handle<RenderObject>, bool>& elem)
			{
				return elem.second;
			});
			log::debug("remaining {} objects", dirty_objects.size());
		}
		streambuf_head = 0;

		auto cb = device->request_command_buffer(vulkan::Queue::Graphics, "gpu_scene::refresh_passes");
		cb.debug_name("gpu_scene::refresh_passes");
		device->start_perf_event("gpu_scene::refresh_passes", cb);

		auto fidx = device->current_frame_index();

		for(auto& p : passes)
		{
			refresh_pass(p);
		
			{
			ZoneScopedN("copy_gpu_batches");

			uint32_t multibuf_size = p.multibatches.size() * sizeof(GPUMultibatch);
			uint32_t indbuf_size = p.padded_indirect_batches.size() * sizeof(GPUIndirectObject);
			
			if(!p.multibatches.empty())
			{
				for(auto i = 0ull; i < p.multibatches.size(); i++)
				{
					const Multibatch& batch = p.multibatches[i];
					*(streambuf->map<GPUMultibatch>(streambuf_head) + i) = GPUMultibatch{batch.first, batch.count, 0, 0};
				}
				
				vk::BufferCopy region
				{
					.srcOffset = streambuf_head,
					.dstOffset = 0,
					.size = sizeof(GPUMultibatch) * p.multibatches.size()
				};
				cb.vk_object().copyBuffer(streambuf->handle, p.gpu_buffers[fidx].multibatch->handle, 1, &region);

				streambuf_head += sizeof(GPUMultibatch) * p.multibatches.size();	
				for(auto i = 0ull; i < p.padded_indirect_batches.size(); i++)
				{
					const IndirectBatch& batch = p.padded_indirect_batches[i];
					if(batch.object == 0)
						continue;

					const Mesh& mesh = mesh_registry->get(get_object(batch.object).mesh);
					
					*(streambuf->map<GPUIndirectObject>(streambuf_head) + i) = GPUIndirectObject
					{
						.lod0_offset = mesh.lod0_offset,
						.lod_count = mesh.lod_count,
						.batch = batch.batch,
						.object = batch.object - 1
					};
				}

				region.srcOffset = streambuf_head;
				region.size = sizeof(GPUIndirectObject) * p.padded_indirect_batches.size();
				cb.vk_object().copyBuffer(streambuf->handle, p.gpu_buffers[fidx].indirect->handle, 1, &region);

				streambuf_head += sizeof(GPUIndirectObject) * p.padded_indirect_batches.size();
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
			
				vp.dp_width = res.x / 2;
				vp.dp_height = res.y / 2;	
				vp.dp_miplevels = dp_num_mips(vp.dp_width, vp.dp_height);

				if(!vp.depth_pyramid || vp.last_res != res)
				{
					log::debug("recreating dpyr");
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

			if(cd.is_ortho)
			{
				auto* gcd = pass.gpu_buffers[device->current_frame_index()].culldata->map<GPUCullingDataOrtho>();
				gcd->frustum_cull = cd.frustum_cull;
				gcd->occlusion_cull = cd.occlusion_cull;
				gcd->forced_lod = cd.forced_lod;
				gcd->lod_base = cd.lod_base;
				gcd->lod_step = cd.lod_step;
				
				gcd->viewmat = vp.camera->get_view_matrix();
				vec2 cplanes = vp.camera->get_clip_planes();
				// set frustum planes
				mat4 proj = vp.camera->get_projection_matrix();
				mat4 projT = mat4::transpose(proj);
				gcd->frustum_planes[0] = Plane(projT[3] + projT[0]).normalize().as_vector(); // x + w < 0
				gcd->frustum_planes[1] = Plane(projT[3] - projT[0]).normalize().as_vector(); // x - w < 0
				gcd->frustum_planes[2] = Plane(projT[3] + projT[1]).normalize().as_vector(); // y + w > 0
				gcd->frustum_planes[3] = Plane(projT[3] - projT[1]).normalize().as_vector(); // y - w < 0
				gcd->znear = cplanes.x;
				gcd->zfar = cplanes.y;
				gcd->p00 = proj[0][0];
				gcd->p11 = proj[1][1];

				gcd->pyr_w = static_cast<float>(vp.dp_width);
				gcd->pyr_h = static_cast<float>(vp.dp_height);

				gcd->cam_pos = vec4{vp.camera->get_pos(), 1.0f};
				gcd->global_draw_count = static_cast<uint32_t>(pass.padded_indirect_batches.size());
			}
			else
			{
				auto* gcd = pass.gpu_buffers[device->current_frame_index()].culldata->map<GPUCullingData>();
				gcd->frustum_cull = cd.frustum_cull;
				gcd->occlusion_cull = cd.occlusion_cull;
				gcd->forced_lod = cd.forced_lod;
				gcd->lod_base = cd.lod_base;
				gcd->lod_step = cd.lod_step;

				vec2 cplanes = vp.camera->get_clip_planes();
				gcd->znear = cplanes.x;
				gcd->zfar = cplanes.y;

				gcd->viewmat = vp.camera->get_view_matrix();
				
				mat4 proj = vp.camera->get_projection_matrix();
				mat4 projT = mat4::transpose(proj);
				vec4 frustumX = Plane(projT[3] + projT[0]).normalize().as_vector();
				vec4 frustumY = Plane(projT[3] + projT[1]).normalize().as_vector();

				gcd->frustum_planes = vec4{frustumX.x, frustumX.z, frustumY.y, frustumY.z};

				gcd->p00 = proj[0][0];
				gcd->p11 = proj[1][1];

				gcd->pyr_w = static_cast<float>(vp.dp_width);
				gcd->pyr_h = static_cast<float>(vp.dp_height);

				gcd->cam_pos = vec4{vp.camera->get_pos(), 1.0f};

				gcd->global_draw_count = static_cast<uint32_t>(pass.padded_indirect_batches.size());
			}
		}
	}

	void execute_compute_cull()
	{
		ZoneScoped;
		auto cb = device->request_command_buffer(vulkan::Queue::Graphics, "gpu_scene::cull");
		cb.debug_name("gpu_scene::cull");
		device->start_perf_event("gpu_scene::cull", cb);
		//FIXME: sort by type and bind pipelines less
		for(auto& pass : passes)
		{
			if(pass.multibatches.empty())
			{
				continue;
			}

			if(pass.cull_data.is_ortho)
				cb.bind_pipeline({"cull_ortho.comp"});
			else
				cb.bind_pipeline({"cull.comp"});

			auto& vp = viewports[pass.viewport - 1];
			if(vp.zbuf && vp.needs_barrier)
			{
				cb.pipeline_barrier
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

			auto& gbuf = pass.gpu_buffers[cb.ctx_index];

			cb.push_descriptor_set
			({
				.storage_buffers = 
	 			{
					{0, gbuf.indirect.get()},
					{1, gbuf.command.get()},
					{2, gbuf.multibatch.get()},
					{3, gpu_object_buffer.get()},
					{4, mesh_registry->get_lodbuffer()},
					{5, gbuf.culldata.get()}
				},
			});

			if(vp.zbuf)
			{

				cb.push_descriptor_set
				({
					.sampled_images =
					{
						{6, vp.depth_pyramid->get_default_view(), pass.cull_data.is_ortho ? dp_sampler_max : dp_sampler, vk::ImageLayout::eGeneral}
					}
				});
			}

			cb.dispatch((static_cast<uint32_t>(pass.padded_indirect_batches.size()) / 32u) + 1u, 1u, 1u);
		}
		cb.memory_barrier
		({
			{
			.src_stage = vk::PipelineStageFlagBits2::eComputeShader,
			.src_access = vk::AccessFlagBits2::eShaderWrite,
			.dst_stage = vk::PipelineStageFlagBits2::eDrawIndirect,
			.dst_access = vk::AccessFlagBits2::eIndirectCommandRead
			}
		});
		device->end_perf_event(cb);
		device->submit(cb);
	}

	void build_depth_pyramid()
	{
		ZoneScoped;
		auto cb = device->request_command_buffer(vulkan::Queue::Graphics, "gpu_scene::buildHZB");
		device->start_perf_event("gpu_scene::buildHZB", cb);
		cb.debug_name("gpu_scene::depthreduce");
		cb.bind_pipeline({"depth_downsample_singlepass.comp"});

		for(auto& vp : viewports)
		{
			if(!vp.zbuf || vp.needs_barrier)
				continue;

			cb.pipeline_barrier
			({
			 	{
				.src_stage = vk::PipelineStageFlagBits2::eLateFragmentTests,
				.src_access = vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
				.dst_stage = vk::PipelineStageFlagBits2::eComputeShader,
				.dst_access = vk::AccessFlagBits2::eShaderRead,
				.src_layout = vk::ImageLayout::eDepthStencilAttachmentOptimal,
				.dst_layout = vk::ImageLayout::eShaderReadOnlyOptimal,
				.image = vp.zbuf
				},
				{
				.src_stage = vk::PipelineStageFlagBits2::eComputeShader,
				.src_access = vk::AccessFlagBits2::eShaderWrite | vk::AccessFlagBits2::eShaderRead,
				.dst_stage = vk::PipelineStageFlagBits2::eComputeShader,
				.dst_access = vk::AccessFlagBits2::eShaderRead | vk::AccessFlagBits2::eShaderWrite,
				.src_layout = vk::ImageLayout::eGeneral,
				.dst_layout = vk::ImageLayout::eGeneral,
				.image = vp.depth_pyramid.get()
				}
			});

			cb.push_descriptor_set
			({
				.storage_image_arrays =
				{
					{
					2,
					array_proxy<vulkan::ImageView*>{vp.dp_layer_views.cbegin(), vp.dp_miplevels}
					}
				},
				.separate_images = 
				{
					{
					0,
					vp.zbuf->get_default_view(),
					vk::ImageLayout::eShaderReadOnlyOptimal
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
				
			auto res = vp.override_res != uvec2{0u} ? vp.override_res : vp.camera->get_res();

			spd_pcb.inv_size = vec2{1.0f / float(res.x), 1.0f / float(res.y)};
			spd_pcb.mips = vp.dp_miplevels,
			spd_pcb.numWorkGroups = ((res.x - 1) / 64) + 1  * ((res.y - 1) / 64 + 1);
			spd_pcb.min_reduce = true;
			
			cb.push_constant(&spd_pcb, sizeof(SPDpcb));

			cb.dispatch((res.x - 1) / 64 + 1, (res.y - 1) / 64 + 1, 1);
			
			cb.pipeline_barrier
			({
				{
				.src_stage = vk::PipelineStageFlagBits2::eComputeShader,
				.dst_stage = vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
				.dst_access = vk::AccessFlagBits2::eDepthStencilAttachmentRead | vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
				.src_layout = vk::ImageLayout::eShaderReadOnlyOptimal,
				.dst_layout = vk::ImageLayout::eDepthStencilAttachmentOptimal,
				.image = vp.zbuf
				}
			});

			cb.pipeline_barrier
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
		device->end_perf_event(cb);
		device->submit(cb);
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

				uint64_t meshmat = uint64_t(obj.material >> 32) ^ uint64_t(obj.mesh);

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

		{
			pass.multibatches.clear();

			if(!pass.indirect_batches.empty())
			{
				Multibatch nb;
				nb.first = 0;
				nb.count = 1;
				nb.pipeline = Handle<MaterialTemplate>{static_cast<uint32_t>(get_object(pass.indirect_batches[0].object).material >> 32)};

				uint32_t bctr = 0;
				pass.indirect_batches[0].batch = bctr;

				for(uint32_t i = 1; i < pass.indirect_batches.size(); i++)
				{
					IndirectBatch* joinbatch = &pass.indirect_batches[nb.first];
					IndirectBatch* batch = &pass.indirect_batches[i];

					uint32_t jb_pipe = get_object(joinbatch->object).material >> 32;
					uint32_t b_pipe = get_object(batch->object).material >> 32;

					bool same_pipe = (jb_pipe == b_pipe);

					if(!same_pipe)
					{
						bctr++;
						pass.multibatches.push_back(nb);
						nb.first = i;
						nb.count = 1;
						nb.pipeline = Handle<MaterialTemplate>{b_pipe};
					}
					else
					{
						nb.count++;
					}
					
					batch->batch = static_cast<uint32_t>(bctr);
				}
				pass.multibatches.push_back(nb);

				pass.padded_indirect_batches.clear();
				const Multibatch& last = pass.multibatches.back(); 

				// start of each multibatch needs to be aligned with subgroup size
				// since we write multibatch draw_count for an entire wave from the first thread
				// and we need to avoid diverging multibatch id within a wave

				uint32_t count = 0;
				for(auto& mb : pass.multibatches)
				{
					for(uint32_t i = mb.first; i < mb.first + mb.count; i++)
						pass.padded_indirect_batches.push_back(pass.indirect_batches[i]);

					for(uint32_t i = mb.first + mb.count; i < align_up(mb.first + mb.count, 32); i++)
						pass.padded_indirect_batches.push_back({});
				
					mb.first = align_up(count, 32);
					count += mb.count;
				}
			}
		}
	}

	vulkan::Device* device;
	MeshRegistry* mesh_registry;

	std::vector<ViewportData> viewports;
	std::vector<MeshPass> passes;
	std::vector<RenderObject> renderables;
	std::vector<std::pair<Handle<RenderObject>, bool>> dirty_objects;

	constexpr static uint32_t initial_object_capacity = 65536;

	uint32_t gpu_object_head = 0;
	vulkan::BufferHandle gpu_object_buffer;

	constexpr static uint32_t streambuf_size = 32 * 1024 * 1024;
	vulkan::BufferHandle streambuf;

	vulkan::BufferHandle spd_globalatomic;

	vk::Sampler dp_sampler;
	vk::Sampler dp_sampler_max;
};

}
