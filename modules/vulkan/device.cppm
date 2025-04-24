export module lumina.vulkan:device;

import vulkan_hpp;
import std;

import :command_buffer;
import :queues;
import :buffer;
import :image;
import :shader;
import :pipeline;
import :descriptor;

import lumina.core;

using std::size_t, std::uint32_t, std::uint64_t;

namespace lumina::vulkan
{

struct CommandPool
{
	vk::CommandPool handle;
	std::vector<vk::CommandBuffer> buffers{};
	size_t current{0u};
	std::atomic<uint32_t> cmd_counter = 0;
	std::mutex lock;
};

}

export namespace lumina::vulkan
{

constexpr uint32_t group_count(uint32_t threads, uint32_t localsize)
{
	return (threads + localsize - 1) / localsize;
}

struct submit_signal_timeline_t {};
constexpr submit_signal_timeline_t submit_signal_timeline;

struct GPUInfo
{
	vk::PhysicalDevice handle;
	QueueFamilyIndices qf_indices;
	vk::PhysicalDeviceMemoryProperties mem_props;
	vk::PhysicalDeviceProperties props;
};

enum class SamplerPrefab
{
	TextureAnisotropic,
	Texture,
	TextureClamp,
	Shadowmap,
	Cubemap
};

struct DeviceFeatures
{
	bool has_swapchain_maintenance_ext{false};
};

struct ReleaseRequest
{
	std::variant<vk::Buffer, vk::Image, vk::ImageView, vk::DeviceMemory> resource;
	uint64_t timeline;
};

struct PerfEvent
{
	std::string_view name;
	double time;
};

constexpr bool perf_events_enabled = true;

class Device
{
public:
	Device(vk::Device _handle, vk::Instance owner, GPUInfo _gpu, DeviceFeatures _features);
	~Device();
	
	Device(const Device&) = delete;
	Device& operator=(const Device&) = delete;

	Device(Device&&) = delete;
	Device& operator=(Device&&) = delete;

	vk::Device get_handle() const;
	vk::PhysicalDevice get_gpu() const;
	DeviceFeatures get_features() const;
	std::optional<uint32_t> get_memory_type(uint32_t type, vk::MemoryPropertyFlags flags) const;
	vk::Queue get_queue(Queue queue);
	uint32_t get_queue_index(Queue queue) const;	
	vk::Sampler get_prefab_sampler(SamplerPrefab sampler) const;
	uint64_t current_frame_index(Queue queue = Queue::Graphics) const;	

	constexpr std::string_view get_name() const
	{
		return {gpu.props.deviceName};
	}

	constexpr std::pair<vk::DeviceSize, vk::DeviceSize> get_memory_budget() const
	{
		return std::make_pair(vmem_usage, vmem_budget);
	}

	template <typename T>
	void set_object_name(const T& obj_handle, std::string_view name)
	{
		handle.setDebugUtilsObjectNameEXT
                ({
                        .objectType = obj_handle.objectType,
                        .objectHandle = reinterpret_cast<uint64_t>(static_cast<typename T::CType>(obj_handle)),
                        .pObjectName = name.data()
                });
	}

	BufferHandle create_buffer(const BufferKey& key);
	ImageHandle proxy_image(const ImageKey& key, vk::Image object);
	ImageHandle create_image(const ImageKey& key);	
	ImageViewHandle create_image_view(const ImageViewKey& key);

	CommandBuffer request_command_buffer(Queue queue = Queue::Graphics, std::string_view dbg_name = std::string_view{});
	void submit(CommandBuffer& cmd);
	uint64_t submit(CommandBuffer& cmd, submit_signal_timeline_t);
	
	void submit_queue(Queue queue, uint64_t* sig_timeline = nullptr);
	void submit_queue_nolock(Queue queue, uint64_t* sig_timeline = nullptr);

	vk::Semaphore wsi_signal_acquire();
	vk::Semaphore wsi_signal_present();
	
	bool wait_timeline(Queue queue, uint64_t value);

	void wait_idle();

	void advance_timeline(Queue queue);
	
	Pipeline* try_get_pipeline(const GraphicsPSOKey& key);
	Pipeline* try_get_pipeline(const ComputePSOKey& key);
	vk::DescriptorSetLayout get_descriptor_set_layout(const DescriptorSetLayoutKey& key, bool is_push);

	void release_resource(Queue queue, ReleaseRequest&& req);
	void destroy_resources(Queue queue);

	void start_perf_event(std::string_view name, CommandBuffer& cmd);
	void end_perf_event(CommandBuffer& cmd);

	std::span<PerfEvent> get_perf_events()
	{
		std::size_t count = 0;
		if constexpr(perf_events_enabled)
		{
			count = perf_events[pe_read].evt_head;
		}

		return {perf_events[pe_read].events.data(), count};
	}
private:
	vk::Device handle;
	vk::Instance instance;
	GPUInfo gpu;
	DeviceFeatures features;

	constexpr static size_t num_ctx = 2;

	struct SubmitBatch
	{
		std::vector<vk::SemaphoreSubmitInfo> wait_sem;
		std::vector<vk::SemaphoreSubmitInfo> signal_sem;
		std::vector<vk::CommandBufferSubmitInfo> cmd;
	};

	struct QueueData
	{
		vk::Queue handle;
		uint32_t index;
		
		std::mutex queue_lock;

		vk::Semaphore semaphore;
		uint64_t timeline{0u};
		std::atomic<uint64_t> frame_counter{0u};

		std::array<uint64_t, num_ctx> frame_tvals{0};
		std::array<std::vector<CommandPool>, num_ctx> cpools;

		std::vector<CommandBuffer> submissions;
		std::vector<SubmitBatch> batch_data;
		std::vector<vk::SubmitInfo2> submit_batches;

		std::vector<ReleaseRequest> released_resources;
	};
	std::array<QueueData, num_queues> queues;


	struct PerfEvents
	{
		std::array<PerfEvent, 64> events;
		uint32_t evt_head = 0;
		vk::QueryPool gfx_qp;
	};

	uint32_t pe_read = 0;
	uint32_t pe_write = 1;
	std::array<PerfEvents, num_ctx> perf_events;

	struct WSISync
	{
		vk::Semaphore acquire;
		vk::Semaphore present;
		bool signaled = false;
	};
	std::array<WSISync, num_ctx> wsi_sync;
	uint64_t wsi_timeline;

	vk::DeviceSize vmem_usage{0};
	vk::DeviceSize vmem_budget{0};

	std::array<vk::Sampler, 5> sampler_prefabs;

	struct ShaderCache
	{
		std::unordered_map<Handle<Shader>, Shader> data;
		std::shared_mutex lock;
	} shader_cache;
	Shader* try_get_shader(const std::filesystem::path& path);

	struct DescriptorCache
	{
		std::unordered_map<DescriptorSetLayoutKey, vk::DescriptorSetLayout> layout_data;
		std::shared_mutex layout_lock;
	} ds_cache;

	struct PipelineCache
	{
		std::unordered_map<PipelineLayoutKey, PipelineLayout> layout_data;
		std::shared_mutex layout_lock;

		std::unordered_map<ComputePSOKey, Pipeline> comp_data;
		std::shared_mutex comp_lock;

		std::unordered_map<GraphicsPSOKey, Pipeline> gfx_data;
		std::shared_mutex gfx_lock;
	} pso_cache;

	PipelineLayout& get_pipeline_layout(const PipelineLayoutKey& key);

	BufferHandle upload_buffer;
	constexpr static uint32_t upload_buffer_size = 16 * 1024 * 1024;

};

}
