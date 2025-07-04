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

export enum class RenderBucket
{
	Default,
	DoubleSided,
	OpaqueAlphaMask,
	OpaqueAlphaMaskDoubleSided,
	Transparent,
	TransparentDoubleSided
};

export struct RenderBatch
{
	uint32_t first;
	uint32_t count;
	Handle<MaterialTemplate> pipeline;
	RenderBucket bucket;
};

struct ObjectData
{
	mat4 transform;
	vec4 sphere;
	float cull_scale;
	uint32_t material_offset;
	uint32_t lod0_offset;
	uint32_t bucket_lod_count;
};

export struct RenderObject
{
	Handle<Mesh> mesh;
	Handle64<Material> material;
	Transform transform;
	RenderBucket bucket;
};

struct ObjectInstance
{
	Handle<RenderObject> in_object;
	uint32_t out_offset{0};
};

struct ClusterInstance
{
	Handle<RenderObject> object;
	uint32_t cluster_offset;
};

export struct CullingData
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
	bool needs_barrier{true};

	bool frustum_cull{true};
	bool occlusion_cull{true};
	bool cone_cull{true};
	bool coverage_cull{true};
	bool is_ortho{false};
	bool is_shadow{false};
	int forced_lod{-1};
	float lod_base{10.0f};
	float lod_step{1.5f};
	float coverage_threshold{0.005f};

	bool freeze_culling{false};
};

struct GPUCullingData
{
	vec4 frustum_planes[4];
	mat4 viewmat;
	uint32_t frustum_cull{1u};
	uint32_t occlusion_cull{1u};
	uint32_t cone_cull{1u};
	uint32_t coverage_cull{1u};
	float coverage_threshold{0.005f};
	uint32_t is_ortho{0u};
	float rt_w, rt_h;	
	float znear, zfar;
	float p00, p11;
	float pyr_w, pyr_h;
	float lod_base{10.0f};
	float lod_step{1.5f};
	vec4 cam_pos;
	int forced_lod{-1};
};

export struct RenderView;
struct RenderView
{
	CullingData culling_data;

	std::vector<Handle<RenderObject>> insert_queue;
	std::vector<ObjectInstance> instances;

	vulkan::BufferHandle culling_data_cbv;

	vulkan::BufferHandle buckets;
	std::vector<uint32_t> cluster_bucket_offsets;
	std::vector<uint32_t> cluster_bucket_sizes;

	vulkan::BufferHandle instance_buffer;
	vulkan::BufferHandle cluster_instances;
	vulkan::BufferHandle visibility;
	vulkan::BufferHandle visibility_ocl;
	vulkan::BufferHandle commands;

	std::vector<vulkan::BufferHandle> visibility_sums;
	std::array<uint32_t, 16> intermediate_sizes;
	uint32_t instance_out_head = 0;
};

export using MaterialBucketFilter = std::function<RenderBucket(Handle64<Material>)>;

export class GPUScene
{
public:
	GPUScene(vulkan::Device& dev, ResourceManager& rm) : device{dev}, resource_manager{rm}
	{
		streambuf = device.create_buffer
		({
			.domain = vulkan::BufferDomain::Host,
			.usage = vulkan::BufferUsage::StagingBuffer,
			.size = streambuf_size,
			.debug_name = "gpuscene_streambuf"
		});
		
		object_buffer = device.create_buffer
		({
			.domain = vulkan::BufferDomain::Device,
			.usage = vulkan::BufferUsage::StorageBuffer,
			.size = sizeof(ObjectData) * initial_object_capacity,
			.debug_name = "gpuscene_object_buffer"
		});

		spd_globalatomic = device.create_buffer
		({
			.domain = vulkan::BufferDomain::DeviceMapped,
			.usage = vulkan::BufferUsage::StorageBuffer,
			.size = sizeof(uint32_t),
			.debug_name = "gpuscene_spd_atomic"
		});
		*(spd_globalatomic->map<uint32_t>()) = 0u;

		const vk::StructureChain<vk::SamplerCreateInfo, vk::SamplerReductionModeCreateInfo> chain =
		{
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
			.maxLod = vk::LodClampNone,
			.unnormalizedCoordinates = false
			},
			{
			.reductionMode = vk::SamplerReductionMode::eMin
			}
		};
		dp_sampler = device.get_handle().createSampler(chain.get<vk::SamplerCreateInfo>());
	}

	~GPUScene()
	{
		device.get_handle().destroySampler(dp_sampler);
	}

	GPUScene(const GPUScene&) = delete;
	GPUScene(GPUScene&&) = delete;

	GPUScene& operator=(const GPUScene&) = delete;
	GPUScene& operator=(GPUScene&&) = delete;

	Handle<RenderView> register_render_view(CullingData&& desc)
	{
		views.push_back(RenderView{});
		auto& view = views.back();
		view.culling_data = std::move(desc);

		const uint32_t pipe_count = view.culling_data.is_shadow ? 4 : bucket_counter * 6;
		view.culling_data_cbv = device.create_buffer
		({
			.domain = vulkan::BufferDomain::DeviceMapped,
			.usage = vulkan::BufferUsage::UniformBuffer,
			.size = sizeof(GPUCullingData),
			.debug_name = "gpu_culling_data"
		});

		view.buckets = device.create_buffer
		({
			.domain = vulkan::BufferDomain::Device,
			.usage = vulkan::BufferUsage::IndirectBuffer,
			.size = sizeof(uvec2) * pipe_count,
			.debug_name = "gpu_bucket_buffer"
		});
		view.cluster_bucket_offsets.resize(pipe_count);
		view.cluster_bucket_sizes.resize(pipe_count);

		view.instance_buffer = device.create_buffer
		({
			.domain = vulkan::BufferDomain::Device,
			.usage = vulkan::BufferUsage::StorageBuffer,
			.size = sizeof(ObjectInstance) * 4096,
			.debug_name = "renderview_instance_buffer"
		});

		view.cluster_instances = device.create_buffer
		({
			.domain = vulkan::BufferDomain::Device,
			.usage = vulkan::BufferUsage::StorageBuffer,
			.size = sizeof(ClusterInstance) * 65536,
			.debug_name = "renderview_cluster_instance_buffer"
		});

		view.visibility = device.create_buffer
		({
			.domain = vulkan::BufferDomain::Device,
			.usage = vulkan::BufferUsage::StorageBuffer,
			.size = sizeof(uint8_t) * 65536,
			.debug_name = "renderview_visibility_buffer"
		});


		view.visibility_sums.push_back
		(device.create_buffer
		({
			.domain = vulkan::BufferDomain::Device,
			.usage = vulkan::BufferUsage::StorageBuffer,
			.size = sizeof(uint32_t) * 65536,
			.debug_name = "renderview_visibility_indices"
		}));
		
		uint32_t n = (65536 / 512) + 1;
		while(n > 1)
		{
			view.visibility_sums.push_back
			(device.create_buffer
			({
				.domain = vulkan::BufferDomain::Device,
				.usage = vulkan::BufferUsage::StorageBuffer,
				.size = sizeof(uint32_t) * n
			}));
			n = (n / 512) + 1;
		}

		view.visibility_sums.push_back
		(device.create_buffer
		({
			.domain = vulkan::BufferDomain::Device,
			.usage = vulkan::BufferUsage::StorageBuffer,
			.size = sizeof(uint32_t)
		}));

		view.visibility_ocl = device.create_buffer
		({
			.domain = vulkan::BufferDomain::Device,
			.usage = vulkan::BufferUsage::StorageBuffer,
			.size = sizeof(uint32_t) * 65536,
			.debug_name = "renderview_visibility_disocclusion_buffer"
		});

		view.commands = device.create_buffer
		({
			.domain = vulkan::BufferDomain::Device,
			.usage = vulkan::BufferUsage::IndirectBuffer,
			.size = sizeof(vk::DrawIndexedIndirectCommand) * 65536,
			.debug_name = "renderview_command_buffer"
		});		

		return Handle<RenderView>{static_cast<uint32_t>(views.size())};
	}

	void update_render_view(Handle<RenderView> h, Camera* cam = nullptr, vulkan::Image* zb = nullptr)
	{
		auto& view = get_view(h);
		if(cam)
			view.culling_data.camera = cam;

		if(zb)
			view.culling_data.zbuf = zb;
	}

	void register_material_bucket_filter(Handle<MaterialTemplate> tmp, MaterialBucketFilter&& filter)
	{
		mtl_bucket_filters[tmp] = std::move(filter);
		if(!mtl_bucket_offset.contains(tmp))
		{
			mtl_bucket_offset[tmp] = bucket_counter;
			bucket_counter++;
		}
	}

	RenderBucket determine_object_bucket(const RenderObject& obj)
	{
		auto tmp = template_from_material(obj.material);
		assert(mtl_bucket_filters.contains(tmp));

		return mtl_bucket_filters[tmp](obj.material);
	}

	Handle<RenderObject> register_object(RenderObject&& obj, array_proxy<Handle<RenderView>> views)
	{
		obj.bucket = determine_object_bucket(obj);
		render_objects.push_back(std::move(obj));

		Handle<RenderObject> rh{static_cast<uint32_t>(render_objects.size())};

		for(auto handle : views)
		{
			if(!handle)
				continue;
		
			get_view(handle).insert_queue.push_back(rh);
		}

		dirty_objects.emplace_back(rh, false);
		return rh;
	}

	void update_object(Handle<RenderObject> obj, Transform* tf)
	{
		auto& object = get_object(obj);

		if(tf)
			object.transform = *tf;

		dirty_objects.emplace_back(obj, false);
	}

	RenderObject& get_object(Handle<RenderObject> obj)
	{
		assert(obj);
		return render_objects[obj - 1];
	}

	vulkan::Buffer* get_object_buffer()
	{
		return object_buffer.get();
	}

	vulkan::Buffer* get_cluster_instance_buffer(Handle<RenderView> view)
	{
		assert(view);
		return get_view(view).cluster_instances.get();
	}

	void ready_objects()
	{
		ZoneScoped;
		streambuf_head = 0;
		if(!dirty_objects.empty())
		{
			ZoneScopedN("copy_gpu_objects");
			auto cmd = device.request_command_buffer(vulkan::Queue::Graphics, "gpu_scene_ready_objects");
			device.start_perf_event("gpu_scene_ready_objects", cmd);
			for(auto& [handle, processed]: dirty_objects)
			{
				auto& object = get_object(handle);
				auto& t = object.transform;

				const auto& mesh = resource_manager.get_mesh(object.mesh);
				if(!mesh.in_gpumem)
					continue;
			
				processed = true;
				auto tm = object.transform.as_matrix();
				const uint32_t packed_bucket_lcount = (std::to_underlying(object.bucket) << 16) | mesh.lod_count;
				ObjectData data
				{
					tm,
					mesh.sphere,
					std::max(std::max(std::abs(t.scale.x), std::abs(t.scale.y)), std::abs(t.scale.z)),
					static_cast<uint32_t>(object.material & 0xFFFFFFFF),
					mesh.lod0_offset,
					packed_bucket_lcount
				};
				memcpy(streambuf->map<std::byte>() + streambuf_head, &data, sizeof(ObjectData));
				vk::BufferCopy region
				{
					.srcOffset = streambuf_head,
					.dstOffset = (handle - 1) * sizeof(ObjectData),
					.size = sizeof(ObjectData)
				};
				cmd.vk_object().copyBuffer(streambuf->handle, object_buffer->handle, 1, &region);
				streambuf_head += sizeof(ObjectData);
			}
			
			cmd.pipeline_barrier
			({
				{
				.src_stage = vk::PipelineStageFlagBits2::eTransfer,
				.src_access = vk::AccessFlagBits2::eTransferWrite,
				.dst_stage = vk::PipelineStageFlagBits2::eComputeShader | vk::PipelineStageFlagBits2::eVertexShader,
				.dst_access = vk::AccessFlagBits2::eShaderRead,
				.buffer = object_buffer.get()
				}
			});
			device.end_perf_event(cmd);

			auto cwb = device.submit(cmd, vulkan::submit_signal_timeline);
			device.wait_timeline(vulkan::Queue::Graphics, cwb);
			std::erase_if(dirty_objects, [](const std::pair<Handle<RenderObject>, bool>& elem)
			{
				return elem.second;
			});
		}
		streambuf_head = 0;

		auto cmd = device.request_command_buffer(vulkan::Queue::Graphics, "gpu_scene_copy_handles");
		device.start_perf_event("gpu_scene_copy_handles", cmd);
		cmd.memory_barrier
		({
			{
			.src_stage = vk::PipelineStageFlagBits2::eTransfer | vk::PipelineStageFlagBits2::eDrawIndirect | vk::PipelineStageFlagBits2::eComputeShader,
			.src_access = vk::AccessFlagBits2::eTransferWrite | vk::AccessFlagBits2::eIndirectCommandRead | vk::AccessFlagBits2::eShaderWrite,
			.dst_stage = vk::PipelineStageFlagBits2::eTransfer,
			.dst_access = vk::AccessFlagBits2::eTransferWrite,
			}
		});

		for(auto& view : views)
		{
			refresh_view(view);
			const uint32_t pipe_count = view.culling_data.is_shadow ? 4 : bucket_counter * 6;
			if(!view.culling_data.is_shadow)
				cmd.vk_object().fillBuffer(view.visibility_ocl->handle, 0, sizeof(uvec4), 0u);
			
			cmd.vk_object().fillBuffer(view.visibility->handle, 0, 65536u, 0u);	
			cmd.vk_object().fillBuffer(view.cluster_instances->handle, 0u, sizeof(ClusterInstance) * 65536u, 0u);

			auto size = static_cast<uint32_t>(view.instances.size()) * sizeof(ObjectInstance);
			if(!size)
				continue;

			memcpy(streambuf->map<std::byte>() + streambuf_head, view.instances.data(), size);
			vk::BufferCopy region
			{
				.srcOffset = streambuf_head,
				.dstOffset = 0,
				.size = size
			};
			cmd.vk_object().copyBuffer(streambuf->handle, view.instance_buffer->handle, 1, &region);
			streambuf_head += size;

			region.srcOffset = streambuf_head;
			region.size = pipe_count * sizeof(uvec2);

			for(uint32_t i = 0; i < pipe_count; i++)
			{
				uvec2 bucket;
				bucket.x = view.cluster_bucket_offsets[i];
				bucket.y = 0;
				memcpy(streambuf->map<std::byte>() + streambuf_head, &bucket, sizeof(uvec2));
				streambuf_head += sizeof(uvec2);
			}

			cmd.vk_object().copyBuffer(streambuf->handle, view.buckets->handle, 1, &region);
		}

		device.end_perf_event(cmd);
		device.submit(cmd);
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

		for(auto& view : views)
		{
			assert(view.camera);
			auto& cd = view.culling_data;
			if(cd.zbuf)
			{
				auto res = cd.override_res != uvec2{0u} ? cd.override_res : cd.camera->get_res();

				cd.map_width = previous_pow2(res.x);
				cd.map_height = previous_pow2(res.y);

				cd.dp_width = previous_pow2(cd.map_width);
				cd.dp_height = previous_pow2(cd.map_height);
				cd.dp_miplevels = dp_num_mips(cd.dp_width, cd.dp_height);

				if(!cd.depth_pyramid || cd.last_res != res)
				{
					cd.zbuf_remap = device.create_image
					({
						.width = cd.map_width,
						.height = cd.map_height,
						.format = vk::Format::eR32Sfloat,
						.usage = vulkan::ImageUsage::RWCompute,
						.debug_name = std::format("remap::zbuf_{:#x}", reinterpret_cast<std::uint64_t>(cd.zbuf))
					});

					cd.depth_pyramid = device.create_image
					({
						.width = cd.dp_width,
						.height = cd.dp_height,
						.levels = cd.dp_miplevels,
						.format = vk::Format::eR32Sfloat,
						.usage = vulkan::ImageUsage::RWCompute,
						.debug_name = std::format("depth_pyramid::zbuf_{:#x}", reinterpret_cast<std::uint64_t>(cd.zbuf))
					});

					for(auto i = 0u; i < cd.dp_miplevels; i++)
						cd.dp_layer_views[i] = cd.depth_pyramid->get_mip_view(i);
					cd.last_res = res;
					cd.needs_barrier = true;
				}
			}

			if(view.instances.empty())
				continue;

			auto* gcd = view.culling_data_cbv->map<GPUCullingData>();
			gcd->frustum_cull = cd.frustum_cull;
			gcd->occlusion_cull = cd.occlusion_cull;
			gcd->cone_cull = cd.cone_cull;
			gcd->coverage_cull = cd.coverage_cull;
			gcd->coverage_threshold = cd.coverage_threshold;
			gcd->is_ortho = cd.is_ortho;
			gcd->forced_lod = cd.forced_lod;
			gcd->lod_base = cd.lod_base;
			gcd->lod_step = cd.lod_step;

			if(!cd.freeze_culling)
			{

			gcd->viewmat = cd.camera->get_view_matrix();

			const vec2 cplanes = cd.camera->get_clip_planes();
			gcd->znear = cplanes.x;
			gcd->zfar = cplanes.y;

			mat4 proj = cd.camera->get_projection_matrix();
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
				const vec4 frustumX = Plane(projT[3] + projT[0]).normalize().as_vector();
				const vec4 frustumY = Plane(projT[3] + projT[1]).normalize().as_vector();
				gcd->frustum_planes[0] = vec4{frustumX.x, frustumX.z, frustumY.y, frustumY.z};
			}

			gcd->p00 = proj[0][0];
			gcd->p11 = proj[1][1];
			gcd->cam_pos = vec4{cd.camera->get_pos(), 1.0f};
			
			}

			gcd->pyr_w = static_cast<float>(cd.dp_width);
			gcd->pyr_h = static_cast<float>(cd.dp_height);
			auto cres = cd.camera->get_res();
			gcd->rt_w = static_cast<float>(cres.x);
			gcd->rt_h = static_cast<float>(cres.y);		
		}
	}

	void execute_compute_cull(bool is_visbuffer)
	{
		ZoneScoped;
		auto cmd = device.request_command_buffer(vulkan::Queue::Graphics, "gpu_scene_cull");
		device.start_perf_event("gpu_scene_instance_cull", cmd);
		
		cmd.memory_barrier
		({
			{
			.src_stage = vk::PipelineStageFlagBits2::eTransfer,
			.src_access = vk::AccessFlagBits2::eTransferWrite,
			.dst_stage = vk::PipelineStageFlagBits2::eComputeShader,
			.dst_access = vk::AccessFlagBits2::eShaderRead | vk::AccessFlagBits2::eShaderWrite
			}
		});

		cmd.bind_pipeline({"instance_cull.comp"});
	
		for(auto& view : views)
		{
			auto& cd = view.culling_data;
			auto size = view.instances.size();
			if(!size)
				continue;

			cmd.push_descriptor_set
			({
				.uniform_buffers =
				{
					{4, view.culling_data_cbv.get()}
				},
				.storage_buffers =
				{
					{0, object_buffer.get()},
					{1, view.instance_buffer.get()},
					{2, view.cluster_instances.get()},
					{3, resource_manager.get_mesh_buffers().lod}
				}
			});

			cmd.push_constant(&size, sizeof(uint32_t));
			cmd.dispatch((size + 31u) / 32u, 1u, 1u);
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
		
		device.end_perf_event(cmd);

		device.start_perf_event("gpu_scene_cluster_cull", cmd);
		cmd.bind_pipeline({"cluster_cull.comp"});

		for(auto& view : views)
		{
			auto& cd = view.culling_data;
			if(cd.zbuf)
			{
				if(cd.needs_barrier)
				{
					cmd.pipeline_barrier
					({
						{
						.src_stage = vk::PipelineStageFlagBits2::eTopOfPipe,
						.dst_stage = vk::PipelineStageFlagBits2::eComputeShader,
						.dst_access = vk::AccessFlagBits2::eShaderRead,
						.src_layout = vk::ImageLayout::eUndefined,
						.dst_layout = vk::ImageLayout::eGeneral,
						.image = cd.depth_pyramid.get()
						}
					});
					cd.needs_barrier = false;
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
						.image = cd.depth_pyramid.get()
						}
					});
				}
			}

			auto size = view.cluster_bucket_offsets.back() + view.cluster_bucket_sizes.back();
			if(!size)
				continue;

			cmd.push_descriptor_set
			({
				.uniform_buffers = 
				{
					{6, view.culling_data_cbv.get()}
				},
				.storage_buffers =
				{
					{0, object_buffer.get()},
					{1, view.cluster_instances.get()},
					{2, view.visibility.get()},
					{3, view.visibility_ocl.get()},
					{4, view.buckets.get()},
					{5, resource_manager.get_mesh_buffers().cluster},
				}
			});

			if(cd.zbuf)
			{
				cmd.push_descriptor_set
				({
					.sampled_images =
					{
						{
						7,
						cd.depth_pyramid->get_default_view(),
						dp_sampler,
						vk::ImageLayout::eGeneral
						}
					}
				});
			}

			cmd.push_constant(&size, sizeof(uint32_t));
			cmd.dispatch((size + 31u) / 32u, 1u, 1u);

			if(cd.zbuf)
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
					.image = cd.depth_pyramid.get()
					}
				});
			}
		}
		device.end_perf_event(cmd);
		
		cmd.memory_barrier
		({
			{
			.src_stage = vk::PipelineStageFlagBits2::eComputeShader,
			.src_access = vk::AccessFlagBits2::eShaderWrite,
			.dst_stage = vk::PipelineStageFlagBits2::eComputeShader,
			.dst_access = vk::AccessFlagBits2::eShaderRead
			}
		});

		device.start_perf_event("gpu_scene_prefix_scan", cmd);

		for(auto& view : views)
		{
			auto& cd = view.culling_data;
			auto size = view.cluster_bucket_offsets.back() + view.cluster_bucket_sizes.back();
			if(!size)
				continue;

			cmd.bind_pipeline({"prefix_scan_index_u8.comp"});
			
			cmd.push_descriptor_set
			({
				.storage_buffers =
				{
					{0, view.visibility.get()},
					{1, view.visibility_sums[0].get()},
					{2, view.visibility_sums[1].get()}
				}
			});

			view.intermediate_sizes[0] = size;
			cmd.push_constant(&size, sizeof(uint32_t));
			size = (size / 512u) + 1;
			cmd.dispatch(size, 1u, 1u);
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

		for(auto& view : views)
		{
			auto& cd = view.culling_data;
			uint32_t size = (view.intermediate_sizes[0] / 512u) + 1u;

			for(uint32_t i = 1; i < view.visibility_sums.size() - 1; i++)
			{
				cmd.bind_pipeline({"prefix_scan_index.comp"});

				cmd.push_descriptor_set
				({
					.storage_buffers =
					{
						{0, view.visibility_sums[i].get()},
						{1, view.visibility_sums[i].get()},
						{2, view.visibility_sums[i + 1].get()}
					}
				});

				view.intermediate_sizes[i] = size;
				cmd.push_constant(&size, sizeof(uint32_t));
				size = (size / 512u) + 1;
				cmd.dispatch(size, 1u, 1u);

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
						{0, view.visibility_sums[i].get()},
						{1, view.visibility_sums[i - 1].get()}
					}
				});

				cmd.push_constant(&view.intermediate_sizes[i - 1], sizeof(uint32_t));
				cmd.dispatch(view.intermediate_sizes[i], 1u, 1u);

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

		device.end_perf_event(cmd);

		device.start_perf_event("gpu_scene_generate_drawcalls", cmd);

		for(auto& view : views)
		{
			auto size = view.cluster_bucket_offsets.back() + view.cluster_bucket_sizes.back();
			if(!size)
				continue;

			cmd.bind_pipeline({is_visbuffer ? "generate_drawcalls_visbuffer.comp" : "generate_drawcalls.comp"});

			cmd.push_descriptor_set
			({
				.storage_buffers =
				{
					{0, view.cluster_instances.get()},
					{1, view.commands.get()},
					{2, view.visibility_sums[0].get()},
					{3, view.visibility.get()},
					{4, view.buckets.get()},
					{5, object_buffer.get()},
					{6, resource_manager.get_mesh_buffers().cluster}
				}
			});

			cmd.push_constant(&size, sizeof(uint32_t));
			cmd.dispatch((size / 256u) + 1u, 1u, 1u);
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
		device.end_perf_event(cmd);
		
		device.submit(cmd);
	}

	void build_depth_pyramid(vulkan::CommandBuffer& cmd)
	{
		ZoneScoped;
		device.start_perf_event("gpu_scene_buildHZB", cmd);
		cmd.bind_pipeline({"depthreduce.comp"});
		
		for(auto& view : views)
		{
			auto& cd = view.culling_data;
			if(!cd.zbuf || cd.needs_barrier)
				continue;

			cmd.pipeline_barrier
			({
				{
				.src_stage = vk::PipelineStageFlagBits2::eTopOfPipe,
				.dst_stage = vk::PipelineStageFlagBits2::eComputeShader,
				.dst_access = vk::AccessFlagBits2::eShaderWrite,
				.src_layout = vk::ImageLayout::eUndefined,
				.dst_layout = vk::ImageLayout::eGeneral,
				.image = cd.zbuf_remap.get()
				}
			});

			if(!cd.freeze_culling)
			{

			cmd.push_descriptor_set
			({
				.sampled_images =
				{
					{
					0,
					cd.zbuf->get_default_view(),
					dp_sampler,
					vk::ImageLayout::eShaderReadOnlyOptimal
					}
				},
				.storage_images =
				{
					{1, cd.zbuf_remap->get_default_view()}
				}
			});

			vec2 reduce_data{float(cd.map_width), float(cd.map_height)};
			cmd.push_constant(&reduce_data, sizeof(vec2));
			cmd.dispatch(vulkan::group_count(cd.map_width, 8), vulkan::group_count(cd.map_height, 8), 1);

			}

			cmd.pipeline_barrier
			({
				{
				.src_stage = vk::PipelineStageFlagBits2::eComputeShader,
				.src_access = vk::AccessFlagBits2::eShaderWrite,
				.dst_stage = vk::PipelineStageFlagBits2::eComputeShader,
				.dst_access = vk::AccessFlagBits2::eShaderRead,
				.src_layout = vk::ImageLayout::eGeneral,
				.dst_layout = vk::ImageLayout::eGeneral,
				.image = cd.zbuf_remap.get()
				},
				{
				.src_stage = vk::PipelineStageFlagBits2::eComputeShader,
				.dst_stage = vk::PipelineStageFlagBits2::eComputeShader,
				.dst_access = vk::AccessFlagBits2::eShaderRead | vk::AccessFlagBits2::eShaderWrite,
				.src_layout = vk::ImageLayout::eGeneral,
				.dst_layout = vk::ImageLayout::eGeneral,
				.src_queue = vulkan::Queue::Graphics,
				.dst_queue = vulkan::Queue::Compute,
				.image = cd.depth_pyramid.get()
				}
			});
		}
		
		cmd.bind_pipeline({"depth_downsample_singlepass.comp"});

		for(auto& view : views)
		{
			auto& cd = view.culling_data;
			if(!cd.zbuf || cd.needs_barrier)
				continue;

			if(!cd.freeze_culling)
			{

			cmd.push_descriptor_set
			({
				.storage_image_arrays =
				{
					{
					2,
					array_proxy<vulkan::ImageView*>{cd.dp_layer_views.data(), cd.dp_miplevels}
					}
				},
				.separate_images =
				{
					{
					0,
					cd.zbuf_remap->get_default_view()
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

			spd_pcb.inv_size = vec2{1.0f / float(cd.map_width), 1.0f / float(cd.map_height)};
			spd_pcb.mips = cd.dp_miplevels;
			spd_pcb.numWorkGroups = ((cd.map_width - 1) / 64) + 1 * ((cd.map_height - 1) / 64 + 1);
			spd_pcb.min_reduce = true;
			
			cmd.push_constant(&spd_pcb, sizeof(SPDpcb));
			cmd.dispatch((cd.map_width - 1) / 64 + 1, (cd.map_height - 1) / 64 + 1, 1);
			
			}

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
				.image = cd.depth_pyramid.get()
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
		device.end_perf_event(cmd);
	}

	bool render_batch(vulkan::CommandBuffer& cmd, Handle<RenderView> vh, Handle<MaterialTemplate> pipe, RenderBucket bucket)
	{
		RenderView& view = get_view(vh);
		if(!mtl_bucket_offset.contains(pipe))
			return false;
		
		auto bucket_offset = (view.culling_data.is_shadow ? 0u : mtl_bucket_offset[pipe]) + std::to_underlying(bucket);

		if(view.cluster_bucket_sizes[bucket_offset] < 1)
			return false;

		cmd.draw_indexed_indirect_count
		(
			view.commands.get(),
			view.cluster_bucket_offsets[bucket_offset] * sizeof(vk::DrawIndexedIndirectCommand),
			view.buckets.get(),
			bucket_offset * sizeof(uvec2) + sizeof(uint32_t),
			view.cluster_bucket_sizes[bucket_offset],
			sizeof(vk::DrawIndexedIndirectCommand)
		);

		return true;
	}

	void draw_ui()
	{
		int ctr = 0;
		for(auto& view : views)
		{
			ImGui::PushID(ctr++);

			ImGui::Checkbox("Frustum culling", &view.culling_data.frustum_cull);
			if(!view.culling_data.is_shadow)
			{
				ImGui::Checkbox("Occlusion culling", &view.culling_data.occlusion_cull);
				ImGui::Checkbox("Cone culling", &view.culling_data.cone_cull);
			}
			ImGui::Checkbox("Screen coverage culling", &view.culling_data.coverage_cull);
			ImGui::DragFloat("Coverage threshold", &view.culling_data.coverage_threshold, 0.001f, 0.0f, 1.0f);
			ImGui::Checkbox("Freeze culling", &view.culling_data.freeze_culling);

			ImGui::SliderInt("LOD override", &view.culling_data.forced_lod, -1, 4);
			ImGui::DragFloat("LOD base", &view.culling_data.lod_base, 0.01f, 0.0f, 10.0f);
			ImGui::DragFloat("LOD step", &view.culling_data.lod_step, 0.01f, 0.0f, 5.0f);

			ImGui::PopID();
		}
	}
private:
	RenderView& get_view(Handle<RenderView> h)
	{
		assert(h);
		return views[h - 1];
	}

	void refresh_view(RenderView& view)
	{
		if(view.insert_queue.empty())
			return;

		for(auto handle : view.insert_queue)
		{
			assert(handle);
			auto& object = get_object(handle);
			auto tmp = template_from_material(object.material);
			assert(mtl_bucket_offset.contains(tmp));
			auto bucket_offset = (view.culling_data.is_shadow ? 0u : mtl_bucket_offset[tmp]) + std::to_underlying(object.bucket);
			
			view.instances.emplace_back(handle, 0);
			
			auto ccount = resource_manager.get_mesh(object.mesh).lods[0].cluster_count;
			view.cluster_bucket_sizes[bucket_offset] += ccount;
		}

		const uint32_t pipe_count = view.culling_data.is_shadow ? 4 : bucket_counter * 6;
		for(uint32_t i = 0; i < pipe_count; i++)
		{
			view.cluster_bucket_offsets[i] = 0u;
			for(uint32_t j = 0; j < i; j++)
				view.cluster_bucket_offsets[i] += view.cluster_bucket_sizes[j];
		}	

		view.insert_queue.clear();

		view.instance_out_head = 0;
		std::sort(view.instances.begin(), view.instances.end(), [this, &view](const ObjectInstance& lhs, const ObjectInstance& rhs)
		{
			auto& l_object = get_object(lhs.in_object);
			auto& r_object = get_object(rhs.in_object);
			auto lhs_bucket = (view.culling_data.is_shadow ? 0u : mtl_bucket_offset[template_from_material(l_object.material)]) + std::to_underlying(l_object.bucket);
			auto rhs_bucket = (view.culling_data.is_shadow ? 0u : mtl_bucket_offset[template_from_material(r_object.material)]) + std::to_underlying(r_object.bucket);

			if(lhs_bucket < rhs_bucket) { return true; }
			else if(lhs_bucket == rhs_bucket) { return lhs.in_object < rhs.in_object; }
			return false;
		});	

		for(auto& inst : view.instances)
		{
			auto& object = get_object(inst.in_object);
			auto ccount = resource_manager.get_mesh(object.mesh).lods[0].cluster_count;
			inst.out_offset = view.instance_out_head;
			view.instance_out_head += ccount;
		}

	}

	vulkan::Device& device;
	ResourceManager& resource_manager;

	constexpr static uint32_t streambuf_size = 64 * 1024 * 1024;
	constexpr static uint32_t initial_object_capacity = 16384u;

	vulkan::BufferHandle streambuf;
	uint32_t streambuf_head = 0;

	vulkan::BufferHandle object_buffer;
	uint32_t gpu_object_head = 0;
	std::vector<RenderObject> render_objects;
	std::vector<std::pair<Handle<RenderObject>, bool>> dirty_objects;

	std::vector<RenderView> views;
	std::unordered_map<Handle<MaterialTemplate>, MaterialBucketFilter> mtl_bucket_filters;
	
	uint32_t bucket_counter = 0;
	std::unordered_map<Handle<MaterialTemplate>, uint32_t> mtl_bucket_offset;

	vulkan::BufferHandle spd_globalatomic;

	vk::Sampler dp_sampler;
};

}

