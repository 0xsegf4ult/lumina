export module lumina.vulkan:queues;

import vulkan_hpp;
import std;

export namespace lumina::vulkan
{

constexpr std::size_t num_queues = 3;
enum class Queue
{
	Graphics,
	Compute,
	Transfer,

	Invalid
};

constexpr std::string_view get_queue_name(Queue queue)
{
	switch(queue)
	{
	case Queue::Graphics:
		return "gfx";
	case Queue::Compute:
		return "compute";
	case Queue::Transfer:
		return "transfer";
	default:
		return "unnamed queue";
	}
}

struct QueueFamilyIndices
{
	std::optional<std::uint32_t> graphics;
	std::optional<std::uint32_t> compute;
	std::optional<std::uint32_t> transfer;

	bool supported()
	{
		return graphics.has_value();
	}
};

}
