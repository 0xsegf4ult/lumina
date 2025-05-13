export module lumina.vulkan:command_buffer;

import vulkan_hpp;
import std;

import lumina.core;

import :queues;
import :pipeline;
import :image;
import :buffer;
import :descriptor;

using std::uint32_t, std::size_t, std::int32_t;

export namespace lumina::vulkan
{

class Device;

struct MemoryBarrier
{
	vk::PipelineStageFlags2 src_stage;
	vk::AccessFlags2 src_access{};
	vk::PipelineStageFlags2 dst_stage;
	vk::AccessFlags2 dst_access{};
};

struct ImageSubresource
{
	uint32_t level{0};
	uint32_t levels{vk::RemainingMipLevels};
	uint32_t layer{0};
	uint32_t layers{vk::RemainingArrayLayers};
};

struct BufferBarrier
{
	vk::PipelineStageFlags2 src_stage;
	vk::AccessFlags2 src_access{};
	vk::PipelineStageFlags2 dst_stage;
	vk::AccessFlags2 dst_access{};
	Queue src_queue = Queue::Invalid;
	Queue dst_queue = Queue::Invalid;
	Buffer* buffer;
	vk::DeviceSize offset = 0;
	vk::DeviceSize size = vk::WholeSize;
};

struct ImageBarrier
{
	vk::PipelineStageFlags2 src_stage;
	vk::AccessFlags2 src_access{};
	vk::PipelineStageFlags2 dst_stage;
	vk::AccessFlags2 dst_access{};
	vk::ImageLayout src_layout;
	vk::ImageLayout dst_layout;
	Queue src_queue = Queue::Invalid;
	Queue dst_queue = Queue::Invalid;
	Image* image;
	ImageSubresource subresource{};
};

const uint32_t max_attachments = 8;

struct AttachmentDesc
{
	ImageView* resource{nullptr};
	vk::AttachmentLoadOp load_op{vk::AttachmentLoadOp::eDontCare};
	vk::AttachmentStoreOp store_op{vk::AttachmentStoreOp::eStore};
	float clear{0.0f};

	bool operator < (const AttachmentDesc& other) const
	{
		return std::tie(resource, load_op, store_op, clear) < std::tie(other.resource, other.load_op, other.store_op, other.clear);
	}
};

enum class AutoViewportMode
{
	Disabled,
	Normal,
	Flipped
};

struct RenderPassDesc
{
	vk::Rect2D render_area{};
	array_proxy<AttachmentDesc> attachments;
	uint32_t view_mask = 0;
	bool auto_scissor{true};
	AutoViewportMode auto_viewport{AutoViewportMode::Normal};
};

struct WaitSemaphoreInfo
{
	Queue wait_queue{Queue::Invalid};
	uint64_t wait_value;
	vk::PipelineStageFlags2 wait_stages;
};

struct CommandBuffer
{
	CommandBuffer() : device{nullptr}, cmd{nullptr}, thread{0}, queue{0} {}
	CommandBuffer(vulkan::Device* dev, vk::CommandBuffer cb, uint32_t tid, Queue q, size_t ci, std::string_view dn = std::string_view{}) : device{dev}, cmd{cb}, thread{tid}, queue{q}, ctx_index{ci}, dbg_name{dn}
	{
	}

	CommandBuffer(const CommandBuffer&) = delete;
	CommandBuffer(CommandBuffer&& other) noexcept : device{other.device}, cmd{other.cmd}, thread{other.thread}, queue{other.queue}, ctx_index{other.ctx_index}, bound_pipe{other.bound_pipe}, wsi_sync{other.wsi_sync}, wsem{other.wsem}, ws_count{other.ws_count}, dbg_name{other.dbg_name}
	{

	}

	CommandBuffer& operator=(const CommandBuffer&) = delete;
	CommandBuffer& operator=(CommandBuffer&& other)
	{
		device = other.device;
		cmd = other.cmd;
		thread = other.thread;
		queue = other.queue;
		ctx_index = other.ctx_index;
		bound_pipe = other.bound_pipe;
		wsi_sync = other.wsi_sync;
		wsem = other.wsem;
		ws_count = other.ws_count;
		dbg_name = other.dbg_name;
		return *this;
	}

	constexpr vk::CommandBuffer vk_object() const
	{
		return cmd;
	}
	
	constexpr vk::PipelineStageFlags2 requires_wsi_sync() const
	{
		return wsi_sync;
	}

	void explicit_set_wsi_sync(vk::PipelineStageFlagBits2 stages)
	{
		wsi_sync |= stages;
	}

	void memory_barrier(array_proxy<MemoryBarrier> b);
	void pipeline_barrier(array_proxy<ImageBarrier> b);
	void pipeline_barrier(array_proxy<BufferBarrier> b);

	void begin_render_pass(const RenderPassDesc& rp);
	void set_scissor(uint32_t offset, vk::Rect2D scissor);
	void set_viewport(uint32_t offset, vk::Viewport vp);
	void end_render_pass();

	void bind_pipeline(const GraphicsPSOKey& pso);
	void bind_pipeline(const ComputePSOKey& pso);

	void push_constant(void* value, uint32_t size);

	void bind_descriptor_sets(array_proxy<DescriptorSet> sets);
	void push_descriptor_set(const DescriptorSetPush& set);
	void bind_vertex_buffers(array_proxy<Buffer*> buffers);
	void bind_index_buffer(Buffer* buffer, vk::IndexType type = vk::IndexType::eUint32);
	void draw(uint32_t vertex_count, uint32_t instance_count, uint32_t first_vertex, uint32_t first_instance);
	void draw_indexed(uint32_t index_count, uint32_t instance_count, uint32_t first_index, int32_t vertex_offset, uint32_t first_instance);
	void dispatch(uint32_t group_size_x, uint32_t group_size_y, uint32_t group_size_z);
	void dispatch(uvec3 group_size);

	void add_wait_semaphore(WaitSemaphoreInfo&& ws);
	std::span<WaitSemaphoreInfo> get_wait_semaphores();

	void debug_name(std::string_view name);

	vulkan::Device* device;
	vk::CommandBuffer cmd;
	uint32_t thread;
	Queue queue;
	size_t ctx_index;

	vk::PipelineStageFlags2 wsi_sync{};

	Pipeline* bound_pipe{nullptr};
	bool is_compute_pso{false};
	std::array<WaitSemaphoreInfo, 2> wsem;
	uint32_t ws_count = 0;

	std::string_view dbg_name{"cmd_generic"};
};

}
