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
};

struct ExecutionContext
{
	std::array<std::vector<CommandPool>, num_queues> cmd_pools;
	std::array<uint64_t, num_queues> timelines;

	struct WSISync
	{
		vk::Semaphore acquire;
		vk::Semaphore present;
		bool signaled = false;
	} wsi;

	std::vector<vk::DeviceMemory> released_mem;
	std::vector<vk::Image> released_images;
	std::vector<vk::ImageView> released_image_views;
	std::vector<vk::Pipeline> released_pipelines;
	std::vector<vk::PipelineLayout> released_pipeline_layouts;
	std::vector<vk::Buffer> released_buffers;
};

}

export namespace lumina::vulkan
{

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
	TextureClamp
};

struct DeviceFeatures
{
	bool has_swapchain_maintenance_ext{false};
};

struct Fence
{
	Fence() : semaphore{nullptr}, timeline{0u} {}
	Fence(vk::Semaphore sem, uint64_t t) : semaphore{sem}, timeline{t} {}

	vk::Semaphore semaphore;
	uint64_t timeline;
};

class Device
{
public:
	Device(vk::Device _handle, GPUInfo _gpu, DeviceFeatures _features);
	~Device();
	
	Device(const Device&) = delete;
	Device& operator=(const Device&) = delete;

	Device(Device&&) = delete;
	Device& operator=(Device&&) = delete;

	vk::Device get_handle() const;
	vk::PhysicalDevice get_gpu() const;
	DeviceFeatures get_features() const;
	std::optional<uint32_t> get_memory_type(uint32_t type, vk::MemoryPropertyFlags flags) const;
	vk::Queue get_queue(Queue queue) const;
	vk::Sampler get_prefab_sampler(SamplerPrefab sampler) const;

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

	void release_buffer(vk::Buffer buffer);
	void release_image(vk::Image image);
	void release_image_view(vk::ImageView view);
	void release_memory(vk::DeviceMemory mem);

	CommandBuffer request_command_buffer(Queue queue = Queue::Graphics);
	void submit(CommandBuffer& cmd, Fence* fence = nullptr);
	
	vk::Semaphore wsi_signal_acquire();
	vk::Semaphore wsi_signal_present();
	
	bool wait_for_fence(Fence& f);
	bool wait_for_fences(std::initializer_list<Fence*> fv);

	void wait_idle();
	void end_context();
	void next_context();

	Pipeline* try_get_pipeline(const GraphicsPSOKey& key);
	Pipeline* try_get_pipeline(const ComputePSOKey& key);
private:
	ExecutionContext& exec_context()
	{
		return ectx_data[cur_ctx];
	}

	void submit_queue(Queue queue, Fence* fence = nullptr);
	void context_release_objects(ExecutionContext& ctx);

	vk::Device handle;
	GPUInfo gpu;
	DeviceFeatures features;

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

		vk::Semaphore semaphore;
		uint64_t timeline{0u};

		std::vector<CommandBuffer> submissions;
		std::vector<SubmitBatch> batch_data;
		std::vector<vk::SubmitInfo2> submit_batches;
	};
	std::array<QueueData, num_queues> queues;

	std::mutex cmd_lock;
	std::condition_variable unlock_cond;
	uint32_t cmd_counter{0u};

	vk::DeviceSize vmem_usage{0};
	vk::DeviceSize vmem_budget{0};

	size_t cur_ctx = 0;
	static constexpr size_t num_ctx = 2;
	std::array<ExecutionContext, num_ctx> ectx_data;

	std::array<vk::Sampler, 3> sampler_prefabs;

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
	vk::DescriptorSetLayout get_descriptor_set_layout(const DescriptorSetLayoutKey& key);

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
