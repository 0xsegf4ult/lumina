module lumina.vulkan:impl_device;

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

namespace lumina::vulkan
{

constexpr size_t sem_wait_timeout = 1000000000;

Device::Device(vk::Device _handle, GPUInfo _gpu, DeviceFeatures _features) : handle{_handle}, gpu{_gpu}, features{_features}
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
        }

        for(auto i = 0u; i < num_ctx; i++)
        {
                auto& ctx = ectx_data[i];

                for(auto& qd : queues)
                {
                        ctx.cmd_pools[qd.index].resize(std::thread::hardware_concurrency());

                        for(auto& cpool : ctx.cmd_pools[qd.index])
			{
				cpool.handle = handle.createCommandPool
				({
					.flags = vk::CommandPoolCreateFlagBits::eTransient,
					.queueFamilyIndex = qd.index,
				});
			}

                        ctx.timelines[qd.index] = 0;
                }
                ctx.wsi.acquire = handle.createSemaphore({});
                set_object_name(ctx.wsi.acquire, "wsi_acquire_f" + std::to_string(i));
                ctx.wsi.present = handle.createSemaphore({});
                set_object_name(ctx.wsi.present, "wsi_present_f" + std::to_string(i));
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
                .maxLod = 1e7f,
                .unnormalizedCoordinates = false
        };

        sampler_prefabs[0] = handle.createSampler(sampler_ci);

        sampler_ci.anisotropyEnable = false;
        sampler_ci.maxAnisotropy = 0.0f;
        sampler_prefabs[1] = handle.createSampler(sampler_ci);

        sampler_ci.addressModeU = vk::SamplerAddressMode::eClampToBorder;
        sampler_ci.addressModeV = vk::SamplerAddressMode::eClampToBorder;
        sampler_ci.addressModeW = vk::SamplerAddressMode::eClampToBorder;
        sampler_prefabs[2] = handle.createSampler(sampler_ci);

	upload_buffer = create_buffer
	({
		.domain = BufferDomain::Host,
		.usage = BufferUsage::StagingBuffer,
		.size = upload_buffer_size,
		.debug_name = "device::upload_buffer"
	});
}

Device::~Device()
{
	// manually destroy upload_buffer
	upload_buffer->device = nullptr;

	wait_idle();

	handle.destroyBuffer(upload_buffer->handle);
	handle.freeMemory(upload_buffer->memory);

	for(auto& [key, pipe] : pso_cache.gfx_data)
		handle.destroyPipeline(pipe.pipeline);

	for(auto& [key, pipe] : pso_cache.comp_data)
		handle.destroyPipeline(pipe.pipeline);
	
	for(auto& [key, layout] : pso_cache.layout_data)
		handle.destroyPipelineLayout(layout.layout);
	
	for(auto& [key, dsl] : ds_cache.layout_data)
		handle.destroyDescriptorSetLayout(dsl);

	for(auto& [sh, stage] : shader_cache.data)
		handle.destroyShaderModule(stage.shader_module);

        for(auto& sampler : sampler_prefabs)
                handle.destroySampler(sampler);

        for(auto& ctx : ectx_data)
        {
                handle.destroySemaphore(ctx.wsi.acquire);
                handle.destroySemaphore(ctx.wsi.present);

                for(auto& qp : ctx.cmd_pools)
                {
                        for(auto& pool : qp)
                                handle.destroyCommandPool(pool.handle);
                }
        }

        for(auto& qd : queues)
                handle.destroySemaphore(qd.semaphore);

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

vk::Queue Device::get_queue(Queue queue) const
{
	return queues[static_cast<size_t>(queue)].handle;
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

vk::Sampler Device::get_prefab_sampler(SamplerPrefab prefab) const
{
	return sampler_prefabs[static_cast<size_t>(prefab)];
}	

BufferHandle Device::create_buffer(const BufferKey& key)
{
        vk::Buffer buf = handle.createBuffer
        ({
                .size = key.size,
                .usage = decode_buffer_usage(key.usage),
                .sharingMode = vk::SharingMode::eExclusive
        });
        set_object_name(buf, key.debug_name);

        auto mem_req = handle.getBufferMemoryRequirements(buf);
        auto mem_idx = get_memory_type(mem_req.memoryTypeBits, decode_buffer_domain(key.domain));

        vk::DeviceMemory mem = handle.allocateMemory
        ({
                .allocationSize = mem_req.size,
                .memoryTypeIndex = mem_idx.value()
        });

        auto ptr = std::make_unique<Buffer>(this, buf, mem, key.size);

        handle.bindBufferMemory(ptr->handle, ptr->memory, 0);

        if(key.domain != BufferDomain::Device)
        {
                ptr->mapped = handle.mapMemory(ptr->memory, 0, ptr->size);
                if(key.initial_data)
                        memcpy(ptr->mapped, key.initial_data, ptr->size);
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
	vk::ImageType type = image_type_from_size(key.width, key.height, key.depth);

	vk::ImageUsageFlagBits default_usage_flags = vk::ImageUsageFlagBits{};

	vk::Image image = handle.createImage
	({
		.imageType = type,
		.format = key.format,
		.extent = {key.width, key.height, key.depth},
		.mipLevels = key.levels,
		.arrayLayers = key.layers,
		.samples = vk::SampleCountFlagBits::e1,
		.tiling = vk::ImageTiling::eOptimal,
		.usage = decode_image_usage(key.usage) | default_usage_flags,
		.sharingMode = vk::SharingMode::eExclusive,
		.initialLayout = vk::ImageLayout::eUndefined
	});
	set_object_name(image, key.debug_name);
	vk::MemoryRequirements mem_req = handle.getImageMemoryRequirements(image);

	auto mem_type = get_memory_type(mem_req.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
	if(!mem_type.has_value())
	{
		log::error("create_image: failed to find memory type for image");
		return nullptr;
	}

	vk::DeviceMemory memory = handle.allocateMemory
	({
		.allocationSize = mem_req.size,
		.memoryTypeIndex = mem_type.value()
	});

	handle.bindImageMemory(image, memory, 0);
	ImageHandle img{std::make_unique<Image>(this, key, image, memory)};

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
		auto aspect = is_depth_format(key.format) ? vk::ImageAspectFlagBits::eDepth : vk::ImageAspectFlagBits::eColor;

		vk::BufferImageCopy copy_region
		{
			.imageSubresource = {aspect, 0, 0, 1},
			.imageExtent = {key.width, key.height, key.depth}
		};

		auto cb = request_command_buffer();
		{
			cb.pipeline_barrier
			({
				.src_stage = vk::PipelineStageFlagBits2::eTopOfPipe,
				.dst_stage = vk::PipelineStageFlagBits2::eTransfer,
				.dst_access = vk::AccessFlagBits2::eTransferWrite,
				.src_layout = vk::ImageLayout::eUndefined,
				.dst_layout = vk::ImageLayout::eTransferDstOptimal,
				.image = img.get()
			});

			cb.vk_object().copyBufferToImage(upload_buffer->handle, image, vk::ImageLayout::eTransferDstOptimal, {copy_region});

			cb.pipeline_barrier
			({
				.src_stage = vk::PipelineStageFlagBits2::eTransfer,
				.src_access = vk::AccessFlagBits2::eTransferWrite,
				.dst_stage = vk::PipelineStageFlagBits2::eComputeShader | vk::PipelineStageFlagBits2::eFragmentShader,
				.dst_access = vk::AccessFlagBits2::eShaderRead,
				.src_layout = vk::ImageLayout::eTransferDstOptimal,
				.dst_layout = vk::ImageLayout::eShaderReadOnlyOptimal,
				.image = img.get()
			});

		}
		submit(cb);
	}

	return img;
}

ImageViewHandle Device::create_image_view(const ImageViewKey& key)
{
	vk::ImageView vh = handle.createImageView
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
			is_depth_format(key.format) ? vk::ImageAspectFlagBits::eDepth : vk::ImageAspectFlagBits::eColor,
			key.level, key.levels,
			key.layer, key.layers
		}
	});
	set_object_name(vh, key.debug_name);

	return ImageViewHandle{std::make_unique<ImageView>(this, key, vh)};
}

void Device::release_buffer(vk::Buffer buffer)
{
	exec_context().released_buffers.push_back(buffer);
}

void Device::release_memory(vk::DeviceMemory mem)
{
	exec_context().released_mem.push_back(mem);
}

void Device::release_image(vk::Image img)
{
	exec_context().released_images.push_back(img);
}

void Device::release_image_view(vk::ImageView view)
{
	exec_context().released_image_views.push_back(view);
}

CommandBuffer Device::request_command_buffer(Queue queue)
{
	std::scoped_lock<std::mutex> lock{cmd_lock};
	auto threadID = job::get_thread_id();
	vk::CommandBuffer cmd;

	auto& cpool = exec_context().cmd_pools[static_cast<size_t>(queue)][threadID];
	if(cpool.current < cpool.buffers.size())
		cmd = cpool.buffers[cpool.current++];
	else
	{
		cmd = handle.allocateCommandBuffers
		({
			.commandPool = cpool.handle,
			.level = vk::CommandBufferLevel::ePrimary,
			.commandBufferCount = 1
		}).back();

		cpool.buffers.push_back(cmd);
		cpool.current++;
	}

	set_object_name(cmd, "cmd_generic::thread" + std::to_string(threadID));

	vk::CommandBufferBeginInfo bufbegin
	{
		.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit
	};
	cmd.begin(bufbegin);
	cmd_counter++;
	return {this, cmd, threadID, queue, cur_ctx};
}

void Device::submit(CommandBuffer& cmd, Fence* fence)
{
	cmd.vk_object().end();

	std::scoped_lock<std::mutex> lock{cmd_lock};

	auto& queue = queues[static_cast<size_t>(cmd.queue)];
	queue.submissions.push_back(std::move(cmd));

	if(fence)
		submit_queue(cmd.queue, fence);

	cmd_counter--;
	unlock_cond.notify_all();
}

void Device::submit_queue(Queue queue, Fence* fence)
{
	auto& qd = queues[static_cast<size_t>(queue)];
	qd.batch_data.clear();
	qd.submit_batches.clear();

	if(qd.submissions.empty())
	{
		if(fence)
			log::warn("vulkan::Device::submit_queue: empty queue submit requests fence wait!?");
		return;
	}

	++qd.timeline;
	exec_context().timelines[static_cast<size_t>(queue)] = qd.timeline;

	uint32_t cur_batch = 0;
	qd.batch_data.push_back({});
	qd.submit_batches.push_back({});
	for(auto& cmd : qd.submissions)
	{
		auto wsi_stages = cmd.requires_wsi_sync();
		auto batch = &qd.batch_data[cur_batch];

		if(!batch->signal_sem.empty())
		{
			cur_batch++;
			qd.batch_data.push_back({});
			qd.submit_batches.push_back({});
			batch = &qd.batch_data[cur_batch];
		}

		if(wsi_stages)
		{
			if(!exec_context().wsi.signaled)
			{
				batch->wait_sem.push_back
				({
					.semaphore = exec_context().wsi.acquire,
					.value = 0,
					.stageMask = wsi_stages
				});
				batch->signal_sem.push_back
				({
					.semaphore = exec_context().wsi.present,
					.value = 0,
					.stageMask = vk::PipelineStageFlagBits2::eAllCommands
				});
				exec_context().wsi.signaled = true;
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

	if(fence)
	{
		fence->semaphore = qd.semaphore;
		fence->timeline = qd.timeline;
	}

	for(size_t i = 0; i < qd.batch_data.size(); i++)
        {
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

	[[maybe_unused]] auto status = get_queue(queue).submit2(static_cast<uint32_t>(qd.submit_batches.size()), qd.submit_batches.data(), nullptr);

	qd.submissions.clear();
}

vk::Semaphore Device::wsi_signal_acquire()
{
	return exec_context().wsi.acquire;
}

vk::Semaphore Device::wsi_signal_present()
{
	return exec_context().wsi.present;
}

bool Device::wait_for_fence(Fence& fence)
{
	vk::SemaphoreWaitInfo wait;
	wait.semaphoreCount = 1;
	wait.pSemaphores = &fence.semaphore;
	wait.pValues = &fence.timeline;

	auto result = handle.waitSemaphores(wait, sem_wait_timeout);
	if(result == vk::Result::eTimeout)
	{
		log::error("wait_for_fence: timed out");
		return false;
	}
	return true;
}

bool Device::wait_for_fences(std::initializer_list<Fence*> fences)
{
	std::vector<vk::Semaphore> sems;
	std::vector<uint64_t> tv;

	for(auto f : fences)
	{
		sems.push_back(f->semaphore);
		tv.push_back(f->timeline);
	}

	vk::SemaphoreWaitInfo wait;
	wait.semaphoreCount = static_cast<uint32_t>(sems.size());
	wait.pSemaphores = sems.data();
	wait.pValues = tv.data();

	auto result = handle.waitSemaphores(wait, sem_wait_timeout);
	if(result == vk::Result::eTimeout)
	{
		log::error("wait_for_fences: timed out");
		return false;
	}
	return true;
}

void Device::wait_idle()
{
	handle.waitIdle();

	for(auto& ctx : ectx_data)
		context_release_objects(ctx);
}

void Device::end_context()
{
	{
		std::unique_lock<std::mutex> lock{cmd_lock};
		unlock_cond.wait(lock, [this]()
		{
			return cmd_counter == 0;
		});
	}

	submit_queue(Queue::Transfer);
	submit_queue(Queue::Graphics);
	submit_queue(Queue::Compute);
}

void Device::next_context()
{
	end_context();

	std::scoped_lock<std::mutex> lock{cmd_lock};

	cur_ctx = (cur_ctx + 1) % num_ctx;
	ExecutionContext& cur = exec_context();

	{
		vk::SemaphoreWaitInfo wait{};
		vk::Semaphore s[num_queues];
		uint64_t t[num_queues] = {0, 0, 0};
		for(auto i = 0u; i < num_queues; i++)
		{
			if(cur.timelines[i])
			{
				s[wait.semaphoreCount] = queues[i].semaphore;
				t[wait.semaphoreCount] = cur.timelines[i];
				wait.semaphoreCount++;
			}
		}

		if(wait.semaphoreCount)
		{
			wait.pSemaphores = s;
			wait.pValues = t;

			auto result = handle.waitSemaphores(wait, sem_wait_timeout);
			if(result == vk::Result::eTimeout)
			{ 
				uint64_t vals[3];
                                [[maybe_unused]] auto s1 = handle.getSemaphoreCounterValue(queues[0].semaphore, vals);
                                [[maybe_unused]] auto s2 = handle.getSemaphoreCounterValue(queues[1].semaphore, vals + 1);
                                [[maybe_unused]] auto s3 = handle.getSemaphoreCounterValue(queues[2].semaphore, vals + 2);
                                log::warn("gpu_frame_timeline wait [cpu_timelines gfx: {}, compute: {}, transfer: {}][gpu_timelines gfx: {}, compute: {}, transfer: {}] timed out!", cur.timelines[0], cur.timelines[1], cur.timelines[2], vals[0], vals[1], vals[2]);
			}
		}
	}

	cur.wsi.signaled = false;
	context_release_objects(cur);

	auto chain = gpu.handle.getMemoryProperties2<vk::PhysicalDeviceMemoryProperties2, vk::PhysicalDeviceMemoryBudgetPropertiesEXT>();

        const vk::PhysicalDeviceMemoryProperties& props = chain.get<vk::PhysicalDeviceMemoryProperties2>().memoryProperties;
        const vk::PhysicalDeviceMemoryBudgetPropertiesEXT& budget = chain.get<vk::PhysicalDeviceMemoryBudgetPropertiesEXT>();

        {

        vk::MemoryPropertyFlags flags = decode_buffer_domain(vulkan::BufferDomain::Device);
        uint32_t index = 0;
        for(uint32_t i = 0; i < props.memoryTypeCount; i++)
                if(props.memoryTypes[i].propertyFlags == flags)
                        index = props.memoryTypes[i].heapIndex;

        vmem_usage = budget.heapUsage[index];
        vmem_budget = budget.heapBudget[index];

        }
}

void Device::context_release_objects(ExecutionContext& ctx)
{
	for(auto buf : ctx.released_buffers)
		handle.destroyBuffer(buf);

	for(auto view : ctx.released_image_views)
		handle.destroyImageView(view);
	
	for(auto img : ctx.released_images)
		handle.destroyImage(img);

	for(auto mem : ctx.released_mem)
		handle.freeMemory(mem);

	ctx.released_mem.clear();
	ctx.released_images.clear();
	ctx.released_image_views.clear();
	ctx.released_buffers.clear();

	if(cmd_counter)
		log::error("vulkan::Device: cmdpool reset with {} unsubmitted command buffers", cmd_counter);

	for(auto& qp : ctx.cmd_pools)
	{
		for(auto& pool : qp)
		{
			pool.current = 0;
			handle.resetCommandPool(pool.handle);
		}
	}
}

Shader* Device::try_get_shader(const std::filesystem::path& path)
{
	Handle<Shader> shandle{fnv::hash(path.c_str())};
	
	{
		std::shared_lock<std::shared_mutex> lock{shader_cache.lock};
		if(shader_cache.data.contains(shandle)) [[likely]]
			return &shader_cache.data[shandle];
	}

	{
		std::unique_lock<std::shared_mutex> lock{shader_cache.lock, std::defer_lock};
		std::filesystem::path spath = "shaders" / path;
	        spath += std::filesystem::path{".spv"};
		auto result = load_spv(handle, spath);
	       	if(!result.has_value())
		{
			log::warn("shader_cache: failed to load shader {}: {}", path.string(), result.error());
			return nullptr;
		}	
		lock.lock();
		shader_cache.data[shandle] = *result;
		return &shader_cache.data[shandle];
	}
}

vk::DescriptorSetLayout Device::get_descriptor_set_layout(const DescriptorSetLayoutKey& key)
{
	{
		std::shared_lock<std::shared_mutex> lock{ds_cache.layout_lock};
		if(ds_cache.layout_data.contains(key)) [[likely]]
			return ds_cache.layout_data[key];
	}

	{
		std::unique_lock<std::shared_mutex> lock{ds_cache.layout_lock, std::defer_lock};

		auto result = create_descriptor_layout(handle, key);

		lock.lock();
		ds_cache.layout_data[key] = result;
		return ds_cache.layout_data[key];
	}
}

PipelineLayout& Device::get_pipeline_layout(const PipelineLayoutKey& key)
{
	{
		std::shared_lock<std::shared_mutex> lock{pso_cache.layout_lock};
		if(pso_cache.layout_data.contains(key)) [[likely]]
			return pso_cache.layout_data[key];
	}

	{
		std::unique_lock<std::shared_mutex> lock{pso_cache.layout_lock, std::defer_lock};

		PipelineLayout layout;
		uint32_t num_dsl = 0;
		for(auto& dslkey : key.dsl_keys)
		{
			if(dslkey.is_empty())
				break;

			layout.ds_layouts[num_dsl++] = get_descriptor_set_layout(dslkey);
		}

		log::debug("creating pipeline layout with {} descriptor sets, pconst size {}", num_dsl, key.pconst.size);
		vk::PipelineLayoutCreateInfo layoutci
		{
		 	.setLayoutCount = num_dsl,
			.pSetLayouts = layout.ds_layouts.data(),
			.pushConstantRangeCount = key.pconst.size ? 1u : 0u,
			.pPushConstantRanges = key.pconst.size ? &key.pconst : nullptr
		};

		auto result = handle.createPipelineLayout(layoutci);
		layout.layout = result;
		lock.lock();
		pso_cache.layout_data[key] = layout;;
		return pso_cache.layout_data[key];
	}
}

Pipeline* Device::try_get_pipeline(const GraphicsPSOKey& key)
{
	{
		std::shared_lock<std::shared_mutex> lock{pso_cache.gfx_lock};
		if(pso_cache.gfx_data.contains(key)) [[likely]]
			return &pso_cache.gfx_data[key];
	}

	{
		std::unique_lock<std::shared_mutex> lock{pso_cache.gfx_lock, std::defer_lock};
		Pipeline pipe;
		
		std::array<Shader*, max_shader_stages> shaders;
		uint32_t num_shaders = 0;

		for(auto& shader : key.shaders)
		{
			if(shader.empty())
				break;

			Handle<Shader> shandle{fnv::hash(shader.c_str())};
			Shader* sptr = try_get_shader(shader);
			if(!sptr) 
				return nullptr;

			shaders[num_shaders] = sptr;
			pipe.shaders[num_shaders] = shandle;
			num_shaders++;
		}

		auto layout_key = build_pipe_layout({shaders.data(), num_shaders});
		auto& layout = get_pipeline_layout(layout_key);

		auto result = compile_pipeline(handle, layout.layout, {shaders.data(), num_shaders}, key);
		if(!result.has_value())
			return nullptr;

		pipe.layout = layout.layout;
		pipe.ds_layouts = layout.ds_layouts;
		pipe.pipeline = *result;
		pipe.pconst = layout_key.pconst;

		lock.lock();
		pso_cache.gfx_data[key] = pipe;
		return &pso_cache.gfx_data[key];
	}
}

Pipeline* Device::try_get_pipeline(const ComputePSOKey& key)
{
	{
		std::shared_lock<std::shared_mutex> lock{pso_cache.comp_lock};
		if(pso_cache.comp_data.contains(key)) [[likely]]
			return &pso_cache.comp_data[key];
	}

	{
		std::unique_lock<std::shared_mutex> lock{pso_cache.comp_lock, std::defer_lock};
		Pipeline pipe;

		Handle<Shader> shandle{fnv::hash(key.shader.c_str())};
		Shader* sptr = try_get_shader(key.shader);
		if(!sptr)
			return nullptr;

		pipe.shaders[0] = shandle;
		auto layout_key = build_pipe_layout({&sptr, 1});
		auto& layout = get_pipeline_layout(layout_key);

		auto result = compile_pipeline(handle, layout.layout, sptr, key);
		if(!result.has_value())
			return nullptr;

		pipe.layout = layout.layout;
		pipe.ds_layouts = layout.ds_layouts;
		pipe.pipeline = *result;
		pipe.pconst = layout_key.pconst;

		lock.lock();
		pso_cache.comp_data[key] = pipe;
		return &pso_cache.comp_data[key];
	}	
}	

}
