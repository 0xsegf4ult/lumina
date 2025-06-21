module;

#include <vulkan/vulkan_core.h>
#include <cassert>
#include <tracy/Tracy.hpp>

module lumina.vulkan;

import :device;
import :queues;
import :buffer;
import :image;
import :command_buffer;
import :descriptor;
import :shader;
import :pipeline;
import vulkan_hpp;
import std;

import lumina.core;

using std::size_t, std::uint32_t, std::uint64_t, std::memcpy;

template <typename... T>
struct overloaded : T... { using T::operator()...; };

namespace lumina::vulkan
{

constexpr bool track_resource_lifetime = false;
constexpr size_t sem_wait_timeout = 1000000000;

Device::Device(vk::Device _handle, vk::Instance owner, GPUInfo _gpu, DeviceFeatures _features) : handle{_handle}, instance{owner}, gpu{_gpu}, features{_features}
{
	queues[0].index = gpu.qf_indices.graphics.value();
	queues[1].index = gpu.qf_indices.compute.value_or(queues[0].index);
	queues[2].index = gpu.qf_indices.transfer.value_or(queues[0].index);

        vk::StructureChain<vk::SemaphoreCreateInfo, vk::SemaphoreTypeCreateInfo> sem_chain
        {
                {},
                {
                        .semaphoreType = vk::SemaphoreType::eTimeline,
                        .initialValue = 0
                }
        };

        for(auto& qd : queues)
        {
                qd.handle = handle.getQueue(qd.index, 0);
                qd.semaphore = handle.createSemaphore(sem_chain.get<vk::SemaphoreCreateInfo>());
                set_object_name(qd.semaphore, get_queue_name(static_cast<Queue>(qd.index)).data() + std::string{" timeline"});
                qd.timeline = 0;

		for(auto i = 0u; i < num_ctx; i++)
		{
			std::vector<CommandPool> cpl(std::thread::hardware_concurrency());
			qd.cpools[i].swap(cpl);
			for(auto& cpool : qd.cpools[i])
			{
				cpool.handle = handle.createCommandPool
				({
					.flags = vk::CommandPoolCreateFlagBits::eTransient,
					.queueFamilyIndex = qd.index
				});
			}
		}
        }

        for(auto i = 0u; i < num_ctx; i++)
        {
                wsi_sync[i].acquire = handle.createSemaphore({});
                set_object_name(wsi_sync[i].acquire, "wsi_acquire_f" + std::to_string(i));
                wsi_sync[i].present = handle.createSemaphore({});
                set_object_name(wsi_sync[i].present, "wsi_present_f" + std::to_string(i));
        }

	vk::SamplerCreateInfo sampler_ci
        {
                .magFilter = vk::Filter::eLinear,
                .minFilter = vk::Filter::eLinear,
                .mipmapMode = vk::SamplerMipmapMode::eLinear,
                .addressModeU = vk::SamplerAddressMode::eRepeat,
                .addressModeV = vk::SamplerAddressMode::eRepeat,
                .addressModeW = vk::SamplerAddressMode::eRepeat,
                .anisotropyEnable = true,
                .maxAnisotropy = 4.0f,
                .compareEnable = false,
                .compareOp = vk::CompareOp::eAlways,
                .minLod = 0.0f,
                .maxLod = vk::LodClampNone,
                .unnormalizedCoordinates = false
        };

        sampler_prefabs[0] = handle.createSampler(sampler_ci);

        sampler_ci.anisotropyEnable = false;
        sampler_ci.maxAnisotropy = 0.0f;
        sampler_prefabs[1] = handle.createSampler(sampler_ci);

        sampler_ci.addressModeU = vk::SamplerAddressMode::eClampToEdge;
        sampler_ci.addressModeV = vk::SamplerAddressMode::eClampToEdge;
        sampler_ci.addressModeW = vk::SamplerAddressMode::eClampToEdge;
        sampler_prefabs[2] = handle.createSampler(sampler_ci);
	
	sampler_ci.magFilter = vk::Filter::eNearest;
	sampler_ci.minFilter = vk::Filter::eNearest;
	sampler_ci.mipmapMode = vk::SamplerMipmapMode::eNearest;
	sampler_prefabs[3] = handle.createSampler(sampler_ci);

	sampler_ci.magFilter = vk::Filter::eLinear;
	sampler_ci.minFilter = vk::Filter::eLinear;
	sampler_ci.mipmapMode = vk::SamplerMipmapMode::eLinear;
	sampler_ci.addressModeU = vk::SamplerAddressMode::eClampToBorder;
	sampler_ci.addressModeV = vk::SamplerAddressMode::eClampToBorder;
	sampler_ci.addressModeW = vk::SamplerAddressMode::eClampToBorder;
	sampler_ci.borderColor = vk::BorderColor::eFloatOpaqueWhite;
	sampler_ci.compareEnable = true;
	sampler_ci.compareOp = vk::CompareOp::eLess;
	sampler_prefabs[4] = handle.createSampler(sampler_ci);

	sampler_ci.magFilter = vk::Filter::eNearest;
	sampler_ci.minFilter = vk::Filter::eNearest;
	sampler_ci.mipmapMode = vk::SamplerMipmapMode::eNearest;
	sampler_ci.compareEnable = false;
	sampler_ci.compareOp = vk::CompareOp::eAlways;
	sampler_ci.magFilter = vk::Filter::eLinear;
	sampler_ci.minFilter = vk::Filter::eLinear;
	sampler_prefabs[5] = handle.createSampler(sampler_ci);

	upload_buffer = create_buffer
	({
		.domain = BufferDomain::Host,
		.usage = BufferUsage::StagingBuffer,
		.size = upload_buffer_size,
		.debug_name = "device::upload_buffer"
	});
		
	if constexpr(perf_events_enabled)
	{
		for(uint32_t i = 0; i < 2; ++i)
		{
			perf_events[i].query = handle.createQueryPool
			({
				.queryType = vk::QueryType::eTimestamp,
				.queryCount = 64
			});
			
			handle.resetQueryPool(perf_events[i].query, 0, 64);
		}
	}

}

Device::~Device()
{
	// manually destroy upload_buffer
	upload_buffer->device = nullptr;

	wait_idle();
		
	if constexpr(perf_events_enabled)
	{	
		for(uint32_t i = 0; i < 2; i++)
			handle.destroyQueryPool(perf_events[i].query);
	}
		
	handle.destroyBuffer(upload_buffer->handle);
	handle.freeMemory(upload_buffer->memory);

	for(auto& [key, pipe] : pso_cache.gfx_data)
		handle.destroyPipeline(pipe.pipeline);

	for(auto& [key, pipe] : pso_cache.comp_data)
		handle.destroyPipeline(pipe.pipeline);
	
	for(auto& [key, layout] : pso_cache.layout_data)
		handle.destroyPipelineLayout(layout.handle);
	
	for(auto& [key, dsl] : ds_cache.layout_data)
		handle.destroyDescriptorSetLayout(dsl);

	for(auto& [sh, stage] : shader_cache.data)
		handle.destroyShaderModule(stage.shader_module);

        for(auto& sampler : sampler_prefabs)
                handle.destroySampler(sampler);

	for(auto& wsi : wsi_sync)
	{
                handle.destroySemaphore(wsi.acquire);
                handle.destroySemaphore(wsi.present);
	}

	for(auto& qd : queues)
        {
		for(auto i = 0u; i < num_ctx; i++)
		{
			for(auto& pool : qd.cpools[i])
			{
				handle.destroyCommandPool(pool.handle);
			}
		}
                
		handle.destroySemaphore(qd.semaphore);
        }

        handle.destroy();
}

vk::Device Device::get_handle() const
{
        return handle;
}

vk::PhysicalDevice Device::get_gpu() const
{
        return gpu.handle;
}

DeviceFeatures Device::get_features() const
{
	return features;
}

vk::Queue Device::get_queue(Queue queue) 
{
	assert(queue != Queue::Invalid);
	std::scoped_lock<std::mutex> qlock{queues[static_cast<size_t>(queue)].queue_lock};
	return queues[static_cast<size_t>(queue)].handle;
}

uint32_t Device::get_queue_index(Queue queue) const
{
	if(queue == Queue::Invalid)
		return vk::QueueFamilyIgnored;

	return queues[static_cast<size_t>(queue)].index;
}

std::optional<uint32_t> Device::get_memory_type(uint32_t type, vk::MemoryPropertyFlags flags) const
{
	for(uint32_t i = 0; i < gpu.mem_props.memoryTypeCount; i++)
	{
		if((type & 1) == 1)
		{
			if((gpu.mem_props.memoryTypes[i].propertyFlags & flags) == flags)
				return i;
		}
		type >>= 1;
	}

	return std::nullopt;
}

vk::Sampler Device::get_prefab_sampler(SamplerPrefab sampler) const
{
	return sampler_prefabs[static_cast<size_t>(sampler)];
}	

uint64_t Device::current_frame_number() const
{
	return frame_counter_global;
}

size_t Device::current_frame_index() const
{
	return static_cast<size_t>(frame_counter_global % num_ctx);
}

BufferHandle Device::create_buffer(const BufferKey& key)
{
	ZoneScoped;

	// FIXME: queue indices must be unique, remove duplicates on systems where we alias compute/transfer queues to gfx
	std::array<uint32_t, 3> indices;
	indices[0] = queues[0].index;
	indices[1] = queues[1].index;
	indices[2] = queues[2].index;

	// FIXME: nvidia ignores sharingMode for buffers, check if concurrent affects perf on amd

	vk::BufferUsageFlagBits default_usage_flags = vk::BufferUsageFlagBits::eShaderDeviceAddress;
        vk::Buffer buf = handle.createBuffer
        ({
                .size = key.size,
                .usage = decode_buffer_usage(key.usage) | default_usage_flags,
                .sharingMode = vk::SharingMode::eConcurrent,
		.queueFamilyIndexCount = 3u,
		.pQueueFamilyIndices = indices.data()
        });
        set_object_name(buf, key.debug_name);

        auto mem_req = handle.getBufferMemoryRequirements(buf);
        auto mem_idx = get_memory_type(mem_req.memoryTypeBits, decode_buffer_domain(key.domain));
	if(!mem_idx.has_value())
	{
		log::error("create_buffer: failed to find memory type");
		return nullptr;
	}

	auto alloc_chain = vk::StructureChain<vk::MemoryAllocateInfo, vk::MemoryAllocateFlagsInfo>
	{
		{
			.allocationSize = mem_req.size,
			.memoryTypeIndex = mem_idx.value()
		},
		{
			.flags = vk::MemoryAllocateFlagBits::eDeviceAddress
		}
	};

        vk::DeviceMemory mem = handle.allocateMemory(alloc_chain.get<vk::MemoryAllocateInfo>());

        auto ptr = std::make_unique<Buffer>(this, buf, mem, key.size);

        handle.bindBufferMemory(ptr->handle, ptr->memory, 0);

        if(key.domain != BufferDomain::Device)
        {
                ptr->mapped = handle.mapMemory(ptr->memory, 0, ptr->size);
                if(key.initial_data)
                        memcpy(ptr->mapped, key.initial_data, ptr->size);
        }

	if constexpr(track_resource_lifetime)
	{
		auto [sv, su] = log::pretty_format_size(mem_req.size);
		log::debug("create_buffer: {} size {}{}", key.debug_name, sv, su);
	}

        return BufferHandle{std::move(ptr)};
}

ImageHandle Device::proxy_image(const ImageKey& key, vk::Image object)
{
	ImageHandle img{std::make_unique<Image>(this, key, object, nullptr)};
	img->disown();
	img->disown_memory();
	set_object_name(object, key.debug_name);

	return img;
}

ImageHandle Device::create_image(const ImageKey& key)
{
	ZoneScoped;
	const vk::ImageType type = image_type_from_size(key.width, key.height, key.depth);

	vk::ImageUsageFlagBits default_usage_flags = vk::ImageUsageFlagBits{};

	vk::ImageCreateFlagBits img_flags = vk::ImageCreateFlagBits{};
	if((key.usage == ImageUsage::Cubemap || key.usage == ImageUsage::CubemapRead) && key.layers == 6)
		img_flags = vk::ImageCreateFlagBits::eCubeCompatible;
	
	auto get_sample_count = [](uint32_t samples)
	{
		switch(samples)
		{
		case 1:
			return vk::SampleCountFlagBits::e1;
		case 2:
			return vk::SampleCountFlagBits::e2;
		case 4:
			return vk::SampleCountFlagBits::e4;
		default:
			std::unreachable();
		}
	};

	const vk::Image image = handle.createImage
	({
		.flags = img_flags,
		.imageType = type,
		.format = key.format,
		.extent = {key.width, key.height, key.depth},
		.mipLevels = key.levels,
		.arrayLayers = key.layers,
		.samples = get_sample_count(key.samples),
		.tiling = vk::ImageTiling::eOptimal,
		.usage = decode_image_usage(key.usage) | default_usage_flags,
		.sharingMode = vk::SharingMode::eExclusive,
		.initialLayout = vk::ImageLayout::eUndefined
	});
	set_object_name(image, key.debug_name);
	auto mem_req = handle.getImageMemoryRequirements(image);

	const vk::MemoryPropertyFlags mem_property = vk::MemoryPropertyFlagBits::eDeviceLocal;
	auto mem_type = get_memory_type(mem_req.memoryTypeBits, mem_property);
	if(!mem_type.has_value())
	{
		log::error("create_image: failed to find memory type for image");
		return nullptr;
	}

	const vk::DeviceMemory memory = handle.allocateMemory
	({
		.allocationSize = mem_req.size,
		.memoryTypeIndex = mem_type.value()
	});

	handle.bindImageMemory(image, memory, 0);
	ImageHandle img{std::make_unique<Image>(this, key, image, memory)};
	
	if constexpr(track_resource_lifetime)
	{
		auto [sv, su] = log::pretty_format_size(mem_req.size);
		log::debug("create_image: {} size {}{}", key.debug_name, sv, su);
	}

	if(key.initial_data)
	{
		auto is = size_for_image(key.width, key.height, key.format);
		if(is > upload_buffer_size)
		{
			log::warn("create_image: failed to upload initial_data, data size {}MiB exceeds upload buffer size of {}MiB", static_cast<float>(is) / 1024.0f / 1024.0f, static_cast<float>(upload_buffer_size) / 1024.0f / 1024.0f);
		       	return img;
		}	
		memcpy(upload_buffer->mapped, key.initial_data, is);
		//FIXME: assumes 1 level and layer

		vk::BufferImageCopy copy_region
		{
			.imageSubresource = {get_format_aspect(key.format), 0, 0, 1},
			.imageExtent = {key.width, key.height, key.depth}
		};

		auto cb = request_command_buffer(Queue::Graphics, "create_image_cb");
		{
			cb.pipeline_barrier
			({{
				.src_stage = vk::PipelineStageFlagBits2::eTopOfPipe,
				.dst_stage = vk::PipelineStageFlagBits2::eTransfer,
				.dst_access = vk::AccessFlagBits2::eTransferWrite,
				.src_layout = vk::ImageLayout::eUndefined,
				.dst_layout = vk::ImageLayout::eTransferDstOptimal,
				.image = img.get()
			}});

			cb.vk_object().copyBufferToImage(upload_buffer->handle, image, vk::ImageLayout::eTransferDstOptimal, {copy_region});

			cb.pipeline_barrier
			({{
				.src_stage = vk::PipelineStageFlagBits2::eTransfer,
				.src_access = vk::AccessFlagBits2::eTransferWrite,
				.dst_stage = vk::PipelineStageFlagBits2::eComputeShader | vk::PipelineStageFlagBits2::eFragmentShader,
				.dst_access = vk::AccessFlagBits2::eShaderRead,
				.src_layout = vk::ImageLayout::eTransferDstOptimal,
				.dst_layout = vk::ImageLayout::eShaderReadOnlyOptimal,
				.image = img.get()
			}});

		}
		auto ttv = submit(cb, submit_signal_timeline);
		wait_timeline(Queue::Graphics, ttv);
	}

	return img;
}

ImageViewHandle Device::create_image_view(const ImageViewKey& key)
{
	ZoneScoped;
	const vk::ImageView vh = handle.createImageView
	({
		.image = key.image->get_handle(),
		.viewType = key.view_type,
		.format = key.format,
		.components = 
		{
			vk::ComponentSwizzle::eIdentity,
			vk::ComponentSwizzle::eIdentity,
			vk::ComponentSwizzle::eIdentity,
			vk::ComponentSwizzle::eIdentity
		},
		.subresourceRange = 
		{
			get_format_aspect(key.format),
			key.level, key.levels,
			key.layer, key.layers
		}
	});
	set_object_name(vh, key.debug_name);
	
	return ImageViewHandle{std::make_unique<ImageView>(this, key, vh)};
}

void Device::release_resource(Queue queue, ReleaseRequest&& req)
{
	auto& qd = queues[static_cast<size_t>(queue)];
	req.timeline = qd.frame_tvals[qd.frame_counter.load() % num_ctx];
	qd.released_resources.push_back(req);
}

CommandBuffer Device::request_command_buffer(Queue queue, std::string_view dbg_name)
{
	ZoneScoped;
	auto threadID = job::get_thread_id();
	vk::CommandBuffer cmd;

	auto& qd = queues[static_cast<size_t>(queue)];
	auto fidx = qd.frame_counter.load() % num_ctx;

	auto& cpool = qd.cpools[fidx][threadID];
	std::scoped_lock<std::mutex> r_lock{cpool.lock};
	if(cpool.current < cpool.buffers.size())
		cmd = cpool.buffers[cpool.current++];
	else
	{
		ZoneScopedN("allocate_cmdbuf");
		cmd = handle.allocateCommandBuffers
		({
			.commandPool = cpool.handle,
			.level = vk::CommandBufferLevel::ePrimary,
			.commandBufferCount = 1
		}).back();

		if constexpr(track_resource_lifetime)
			log::debug("allocate cmdbuf {}", dbg_name);

		cpool.buffers.push_back(cmd);
		cpool.current++;
	}

	set_object_name(cmd, std::format("cmd_{}::thread{}", get_queue_name(queue), threadID));
	if(!dbg_name.empty())
		set_object_name(cmd, dbg_name);

	cmd.begin({.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
	cpool.cmd_counter++;
	return {this, cmd, threadID, queue, fidx, dbg_name};
}

void Device::submit(CommandBuffer& cmd)
{
	ZoneScoped;
	assert(cmd.thread == job::get_thread_id());
	cmd.vk_object().end();

	auto& queue = queues[static_cast<size_t>(cmd.queue)];

	auto& pool = queue.cpools[cmd.ctx_index][cmd.thread];
	
	{
		
	std::scoped_lock<std::mutex> r_lock{pool.lock};
	if(pool.cmd_counter < 1)
	{
		log::warn("submit: queue {}[{}] f{} has no active cmdbuffers val {}", get_queue_name(cmd.queue), cmd.thread, cmd.ctx_index, pool.cmd_counter.load());
		log::critical("submit to queue with no allocated cmd buffers");
		std::abort();
	}
	else
	{
		pool.cmd_counter--;
	}
	
	}
	
	{
		std::scoped_lock<std::mutex> qlock{queue.queue_lock};
		queue.submissions.push_back(std::move(cmd));
	}
}

uint64_t Device::submit(CommandBuffer& cmd, submit_signal_timeline_t)
{
	ZoneScoped;
	auto queue = cmd.queue;
	submit(cmd);

	uint64_t val = 0;
	submit_queue(queue, &val);
	return val;
}

void Device::submit_queue(Queue queue, uint64_t* sig_timeline)
{
	std::scoped_lock<std::mutex> qlock{queues[static_cast<size_t>(queue)].queue_lock};
	submit_queue_nolock(queue, sig_timeline);
}

void Device::submit_queue_nolock(Queue queue, uint64_t* sig_timeline)
{
	ZoneScoped;

	auto& qd = queues[static_cast<size_t>(queue)];
	qd.batch_data.clear();
	qd.submit_batches.clear();

	if(qd.submissions.empty())
	{
		if(sig_timeline)
			log::warn("submit_queue: empty queue submit requests timeline signal!?");
		return;
	}

	auto fidx = qd.frame_counter % num_ctx;

	++qd.timeline;
	qd.frame_tvals[fidx] = qd.timeline;

	uint32_t cur_batch = 0;
	qd.batch_data.push_back({});
	qd.submit_batches.push_back({});
	for(auto& cmd : qd.submissions)
	{
		if(cmd.ctx_index != fidx)
			log::warn("submit_queue: command buffer exists across frame boundaries");

		auto wsi_stages = cmd.requires_wsi_sync();
		auto* batch = &qd.batch_data[cur_batch];

		auto wsem = cmd.get_wait_semaphores();
		if(!wsem.empty())
		{
			if(!batch->cmd.empty() || !batch->signal_sem.empty())
			{
				cur_batch++;
				qd.batch_data.push_back({});
				qd.submit_batches.push_back({});
				batch = &qd.batch_data[cur_batch];
			}

			for(auto& ws : wsem)
			{
				batch->wait_sem.push_back
				({
					.semaphore = queues[static_cast<size_t>(ws.wait_queue)].semaphore,
					.value = ws.wait_value,
					.stageMask = ws.wait_stages
				});
			}
		}

		if(!batch->signal_sem.empty())
		{
			cur_batch++;
			qd.batch_data.push_back({});
			qd.submit_batches.push_back({});
			batch = &qd.batch_data[cur_batch];
		}
		
		if(wsi_stages && queue == Queue::Graphics)
		{
			auto& wsi = wsi_sync[frame_counter_global % num_ctx];
			if(!wsi.signaled)
			{
				batch->wait_sem.push_back
				({
					.semaphore = wsi.acquire,
					.value = 0,
					.stageMask = wsi_stages
				});
				batch->signal_sem.push_back
				({
					.semaphore = wsi.present,
					.value = 0,
					.stageMask = vk::PipelineStageFlagBits2::eAllCommands
				});
				wsi.signaled = true;
			}
			else
				log::error("build_submit_batches: WSI already signaled!");
		}

		batch->cmd.push_back
		({
			.commandBuffer = cmd.vk_object()
		});
	}

	qd.batch_data.back().signal_sem.push_back
	({
		.semaphore = qd.semaphore,
		.value = qd.timeline,
		.stageMask = vk::PipelineStageFlagBits2::eAllCommands
	});

	if(sig_timeline)
		*sig_timeline = qd.timeline;

	for(size_t i = 0; i < qd.batch_data.size(); i++)
        {
		if(qd.batch_data[i].cmd.empty())
			log::warn("submit_queue_nolock: submitting empty batch");

		qd.submit_batches[i] =
                {
                        .waitSemaphoreInfoCount = static_cast<uint32_t>(qd.batch_data[i].wait_sem.size()),
                        .pWaitSemaphoreInfos = qd.batch_data[i].wait_sem.data(),
                        .commandBufferInfoCount = static_cast<uint32_t>(qd.batch_data[i].cmd.size()),
                        .pCommandBufferInfos = qd.batch_data[i].cmd.data(),
                        .signalSemaphoreInfoCount = static_cast<uint32_t>(qd.batch_data[i].signal_sem.size()),
                        .pSignalSemaphoreInfos = qd.batch_data[i].signal_sem.data()
                };
        }

	if(qd.submit_batches.empty())
		log::warn("submit_queue_nolock: not submitting any batches!");

	[[maybe_unused]] auto status = qd.handle.submit2(static_cast<uint32_t>(qd.submit_batches.size()), qd.submit_batches.data(), nullptr);

	qd.submissions.clear();
}

vk::Semaphore Device::wsi_signal_acquire()
{
	return wsi_sync[frame_counter_global % num_ctx].acquire;
}

vk::Semaphore Device::wsi_signal_present()
{
	return wsi_sync[frame_counter_global % num_ctx].present;
}

bool Device::wait_timeline(Queue queue, uint64_t val)
{
	ZoneScoped;
	vk::SemaphoreWaitInfo wait;
	wait.semaphoreCount = 1;
	wait.pSemaphores = &queues[static_cast<size_t>(queue)].semaphore;
	wait.pValues = &val;

	auto result = handle.waitSemaphores(wait, sem_wait_timeout);
	if(result == vk::Result::eTimeout)
	{
		uint64_t g_val;
		[[maybe_unused]] auto s1 = handle.getSemaphoreCounterValue(queues[static_cast<size_t>(queue)].semaphore, &g_val);
		log::error("wait_timeline: timed out waiting for signal {:#x}, current is {:#x}", val, g_val);
		return false;
	}

	return true;
}

void Device::destroy_resources(Queue queue, uint64_t timeline)
{
	ZoneScoped;

	auto& qd = queues[static_cast<size_t>(queue)];
	if(timeline == 0)
	{
		auto fidx = qd.frame_counter.load() % num_ctx;
		timeline = qd.frame_tvals[fidx];
	}

	for(auto& req : qd.released_resources)
	{
		if(req.timeline > timeline)
			continue;

		std::visit(overloaded
		{
			[this](vk::Buffer buf){handle.destroyBuffer(buf);},
			[this](vk::Image img){handle.destroyImage(img);},
			[this](vk::ImageView view){handle.destroyImageView(view);},
			[this](vk::DeviceMemory mem){handle.freeMemory(mem);}
		}, req.resource);
	}

	std::erase_if(qd.released_resources, [timeline](const ReleaseRequest& elem)
	{
		return elem.timeline <= timeline;
	});	
}

void Device::wait_idle()
{
	handle.waitIdle();
	
	destroy_resources(Queue::Graphics, 0);
	destroy_resources(Queue::Compute, 0);
	destroy_resources(Queue::Transfer, 0);
}

void Device::advance_timeline(Queue queue)
{
	ZoneScoped;
	
	auto& qd = queues[static_cast<size_t>(queue)];
	std::scoped_lock<std::mutex> qlock{qd.queue_lock};
	submit_queue_nolock(queue);

	auto last_fidx = qd.frame_counter.load() % num_ctx;
	qd.frame_counter++;

	auto fidx = (last_fidx + 1) % num_ctx;

	{
		ZoneScopedN("sema_wait");
		vk::SemaphoreWaitInfo wait;
		wait.semaphoreCount = 1;
		wait.pSemaphores = &qd.semaphore;
		wait.pValues = &qd.frame_tvals[fidx];

		auto result = handle.waitSemaphores(wait, sem_wait_timeout);
		if(result == vk::Result::eTimeout)
		{
			uint64_t val = 0;
			[[maybe_unused]] auto s1 = handle.getSemaphoreCounterValue(qd.semaphore, &val);
			log::warn("gpu timeline wait on queue {} timed out [cpu {:#x}][gpu {:#x}]", get_queue_name(queue), qd.frame_tvals[fidx], val);
			std::abort();
		}
	}
	

	destroy_resources(queue, qd.frame_tvals[fidx]);

	{

	ZoneScopedN("reset_cmd");

	for(uint32_t tid = 0; auto& pool : qd.cpools[fidx])
	{
		std::scoped_lock<std::mutex> r_lock{pool.lock};
		if(pool.cmd_counter == 0)
		{
			pool.current = 0;
			handle.resetCommandPool(pool.handle);
		}
		else
		{
			log::debug("extending cmdpool {}[{}] lifetime, {} unsubmitted", get_queue_name(queue), tid, pool.cmd_counter.load());
		}
		tid++;
	}

	}

}

void Device::begin_frame()
{
	ZoneScoped;

	advance_timeline(Queue::Compute);
	advance_timeline(Queue::Graphics);
	
	frame_counter_global = (frame_counter_global + 1);
	auto fidx = frame_counter_global % num_ctx;

	if constexpr(perf_events_enabled)
	{
		ZoneScopedN("collect_perf_events");

		std::array<uint64_t, 64> timestamps;
	
		auto num_evt = perf_events[fidx].evt_head;
		if(num_evt)
		{
			[[maybe_unused]] auto qp_res = handle.getQueryPoolResults(perf_events[fidx].query, 0, num_evt * 2, num_evt * 2 * sizeof(uint64_t), timestamps.data(), sizeof(uint64_t), vk::QueryResultFlagBits::e64);
			auto period = gpu.props.limits.timestampPeriod; 
			for(uint32_t idx = 0; idx < num_evt; idx++)
				perf_events[fidx].events[idx].time = (timestamps[idx * 2 + 1] - timestamps[idx * 2]) * (period / 1e6);

			evt_count = num_evt;
			std::memcpy(cur_events.data(), perf_events[fidx].events.data(), sizeof(PerfEvent) * num_evt);
		}

		handle.resetQueryPool(perf_events[fidx].query, 0, 64);
		perf_events[fidx].evt_head = 0;
	}
		
	wsi_sync[fidx].signaled = false;
	
	// update memory budget ocassionally
	if(frame_counter_global % 60 == 0)
	{
		ZoneScopedN("update_mem_budget");
		auto chain = gpu.handle.getMemoryProperties2<vk::PhysicalDeviceMemoryProperties2, vk::PhysicalDeviceMemoryBudgetPropertiesEXT>();

		const vk::PhysicalDeviceMemoryProperties& props = chain.get<vk::PhysicalDeviceMemoryProperties2>().memoryProperties;
		const vk::PhysicalDeviceMemoryBudgetPropertiesEXT& budget = chain.get<vk::PhysicalDeviceMemoryBudgetPropertiesEXT>();

		vmem_usage = 0;
		vmem_budget = 0;

		auto get_heap_budget = [this, &props, &budget](vulkan::BufferDomain dom)
		{

			const vk::MemoryPropertyFlags flags = decode_buffer_domain(dom);
			uint32_t index = 0;
			for(uint32_t i = 0; i < props.memoryTypeCount; i++)
				if(props.memoryTypes[i].propertyFlags == flags)
					index = props.memoryTypes[i].heapIndex;

			vmem_usage += budget.heapUsage[index];
			vmem_budget += budget.heapBudget[index];
		};
		get_heap_budget(vulkan::BufferDomain::Device);
		get_heap_budget(vulkan::BufferDomain::DeviceMapped);
	}
};

void Device::end_frame()
{
	ZoneScoped;

	submit_queue(Queue::Compute);
	submit_queue(Queue::Graphics);
}

Shader* Device::try_get_shader(const std::filesystem::path& path)
{
	ZoneScoped;
	const Handle<Shader> shandle{fnv::hash(path.c_str())};
	
	{
		std::shared_lock<std::shared_mutex> lock{shader_cache.lock};
		if(shader_cache.data.contains(shandle)) [[likely]]
			return &shader_cache.data[shandle];
	}

	{
		std::filesystem::path spath = "shaders" / path;
	        spath += std::filesystem::path{".spv"};
		auto result = load_spv(handle, spath);
	       	if(!result.has_value())
		{
			log::warn("shader_cache: failed to load shader {}: {}", path.string(), result.error());
			return nullptr;
		}

		std::unique_lock<std::shared_mutex> lock{shader_cache.lock};
		shader_cache.data.insert_or_assign(shandle, *result);
		return &shader_cache.data[shandle];
	}
}

vk::DescriptorSetLayout Device::get_descriptor_set_layout(const DescriptorSetLayoutKey& key, bool is_push)
{
	ZoneScoped;
	{
		std::shared_lock<std::shared_mutex> lock{ds_cache.layout_lock};
		if(ds_cache.layout_data.contains(key)) [[likely]]
			return ds_cache.layout_data[key];
	}

	{
		auto result = create_descriptor_layout(handle, key, is_push);

		std::unique_lock<std::shared_mutex> lock{ds_cache.layout_lock};
		ds_cache.layout_data[key] = result;
		return ds_cache.layout_data[key];
	}
}

PipelineLayout& Device::get_pipeline_layout(const PipelineLayoutKey& key)
{
	ZoneScoped;
	{
		std::shared_lock<std::shared_mutex> lock{pso_cache.layout_lock};
		if(pso_cache.layout_data.contains(key)) [[likely]]
			return pso_cache.layout_data[key];
	}

	{
		PipelineLayout layout;
		uint32_t num_dsl = 0;
		for(const auto& dslkey : key.dsl_keys)
		{
			if(dslkey.is_empty())
				break;

			// first layout is push descriptor
			layout.ds_layouts[num_dsl] = get_descriptor_set_layout(dslkey, num_dsl == 0 ? true : false);
			num_dsl++;
		}

		layout.handle = handle.createPipelineLayout
		({
		 	.setLayoutCount = num_dsl,
			.pSetLayouts = layout.ds_layouts.data(),
			.pushConstantRangeCount = key.pconst.size ? 1u : 0u,
			.pPushConstantRanges = key.pconst.size ? &key.pconst : nullptr
		});

		std::unique_lock<std::shared_mutex> lock{pso_cache.layout_lock};
		pso_cache.layout_data[key] = layout;
		return pso_cache.layout_data[key];
	}
}

Pipeline* Device::try_get_pipeline(const GraphicsPSOKey& key)
{
	ZoneScoped;
	{
		std::shared_lock<std::shared_mutex> lock{pso_cache.gfx_lock};
		if(pso_cache.gfx_data.contains(key)) [[likely]]
			return &pso_cache.gfx_data[key];
	}

	{
		Pipeline pipe;
		
		std::array<Shader*, max_shader_stages> shaders;
		uint32_t num_shaders = 0;

		for(auto& shader : key.shaders)
		{
			if(shader.empty())
				break;

			const Handle<Shader> shandle{fnv::hash(shader.c_str())};
			auto* sptr = try_get_shader(shader);
			if(!sptr) 
				return nullptr;

			shaders[num_shaders] = sptr;
			pipe.shaders[num_shaders] = shandle;
			num_shaders++;
		}

		auto layout_key = build_pipe_layout({shaders.data(), num_shaders});
		auto& layout = get_pipeline_layout(layout_key);

		auto result = compile_pipeline(handle, layout.handle, {shaders.data(), num_shaders}, key);
		if(!result.has_value())
			return nullptr;

		pipe.layout_key = layout_key;
		pipe.layout = layout;
		pipe.pipeline = *result;

		std::unique_lock<std::shared_mutex> lock{pso_cache.gfx_lock};
		pso_cache.gfx_data[key] = pipe;
		return &pso_cache.gfx_data[key];
	}
}

Pipeline* Device::try_get_pipeline(const ComputePSOKey& key)
{
	ZoneScoped;
	{
		std::shared_lock<std::shared_mutex> lock{pso_cache.comp_lock};
		if(pso_cache.comp_data.contains(key)) [[likely]]
			return &pso_cache.comp_data[key];
	}

	{
		Pipeline pipe;

		const Handle<Shader> shandle{fnv::hash(key.shader.c_str())};
		auto* sptr = try_get_shader(key.shader);
		if(!sptr)
			return nullptr;

		pipe.shaders[0] = shandle;
		auto layout_key = build_pipe_layout({&sptr, 1});
		auto& layout = get_pipeline_layout(layout_key);

		auto result = compile_pipeline(handle, layout.handle, sptr, key);
		if(!result.has_value())
			return nullptr;

		pipe.layout_key = layout_key;
		pipe.layout = layout;
		pipe.pipeline = *result;

		std::unique_lock<std::shared_mutex> lock{pso_cache.comp_lock};
		pso_cache.comp_data[key] = pipe;
		return &pso_cache.comp_data[key];
	}	
}	

void Device::start_perf_event(std::string_view name, CommandBuffer& cmd)
{
	if constexpr(perf_events_enabled)
	{
		auto fidx = frame_counter_global % num_ctx;
		auto head = perf_events[fidx].evt_head;
		perf_events[fidx].events[head].name = name;
		cmd.vk_object().beginDebugUtilsLabelEXT
		({
			.pLabelName = name.data()
		});
		cmd.vk_object().writeTimestamp(vk::PipelineStageFlagBits::eTopOfPipe, perf_events[fidx].query, head * 2);
	}
}

void Device::end_perf_event(CommandBuffer& cmd)
{
	if constexpr(perf_events_enabled)
	{
		auto fidx = frame_counter_global % num_ctx;
		cmd.vk_object().writeTimestamp(vk::PipelineStageFlagBits::eBottomOfPipe, perf_events[fidx].query, perf_events[fidx].evt_head * 2 + 1);
		cmd.vk_object().endDebugUtilsLabelEXT();
		perf_events[fidx].evt_head++;
	}
}

}
