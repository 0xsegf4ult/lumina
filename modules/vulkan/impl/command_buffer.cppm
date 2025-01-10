module;

#include <cassert>

export module lumina.vulkan:impl_command_buffer;

import :command_buffer;
import :device;
import :queues;
import :image;
import :pipeline;
import :descriptor;

import vulkan_hpp;
import std;

import lumina.core;

namespace lumina::vulkan
{

void CommandBuffer::memory_barrier(array_proxy<MemoryBarrier> bar)
{
	assert(bar.size() > 0);
	assert(bar.size() <= 8);
	std::array<vk::MemoryBarrier2, 8> mb;

	for(auto i = 0ull; i < bar.size(); i++)
	{
		mb[i].srcStageMask = bar[i].src_stage;
		mb[i].srcAccessMask = bar[i].src_access;
		mb[i].dstStageMask = bar[i].dst_stage;
		mb[i].dstAccessMask = bar[i].dst_access;
	}

	cmd.pipelineBarrier2
	({
		.memoryBarrierCount = static_cast<std::uint32_t>(bar.size()),
		.pMemoryBarriers = mb.data()
	});

	dbg_state = DebugState::Recording;
}

void CommandBuffer::pipeline_barrier(array_proxy<BufferBarrier> bar)
{
	assert(bar.size() > 0);
	assert(bar.size() <= 8);
	std::array<vk::BufferMemoryBarrier2, 8> bb;
	
	for(auto i = 0ull; i < bar.size(); i++)
	{
		assert(bar[i].buffer);

		bb[i].srcStageMask = bar[i].src_stage;
		bb[i].srcAccessMask = bar[i].src_access;
		bb[i].dstStageMask = bar[i].dst_stage;
		bb[i].dstAccessMask = bar[i].dst_access;
		bb[i].srcQueueFamilyIndex = device->get_queue_index(bar[i].src_queue);
		bb[i].dstQueueFamilyIndex = device->get_queue_index(bar[i].dst_queue);
		bb[i].buffer = bar[i].buffer->handle;
		bb[i].offset = bar[i].offset;
		bb[i].size = bar[i].size;
	}

	cmd.pipelineBarrier2
	({
		.bufferMemoryBarrierCount = static_cast<std::uint32_t>(bar.size()),
		.pBufferMemoryBarriers = bb.data()
	});
	dbg_state = DebugState::Recording;
}

void CommandBuffer::pipeline_barrier(array_proxy<ImageBarrier> bar)
{
	assert(bar.size() > 0);
	assert(bar.size() <= 8);
	std::array<vk::ImageMemoryBarrier2, 8> vb;
	for(auto i = 0ull; i < bar.size(); i++)
	{
		assert(bar[i].image);

		vb[i].srcStageMask = bar[i].src_stage;
		vb[i].srcAccessMask = bar[i].src_access;
		vb[i].dstStageMask = bar[i].dst_stage;
		vb[i].dstAccessMask = bar[i].dst_access;
		vb[i].oldLayout = bar[i].src_layout;
		vb[i].newLayout = bar[i].dst_layout;
		vb[i].srcQueueFamilyIndex = device->get_queue_index(bar[i].src_queue);
		vb[i].dstQueueFamilyIndex = device->get_queue_index(bar[i].dst_queue);
		vb[i].image = bar[i].image->get_handle();
		vb[i].subresourceRange = 
		{
			is_depth_format(bar[i].image->get_key().format) ? vk::ImageAspectFlagBits::eDepth : vk::ImageAspectFlagBits::eColor,
			bar[i].subresource.level, bar[i].subresource.levels,
			bar[i].subresource.layer, bar[i].subresource.layers
		};
	}

	cmd.pipelineBarrier2
	({
		.imageMemoryBarrierCount = static_cast<std::uint32_t>(bar.size()),
		.pImageMemoryBarriers = vb.data()
	});
	dbg_state = DebugState::Recording;
}

void CommandBuffer::begin_render_pass(const RenderPassDesc& rp)
{
	std::array<vk::RenderingAttachmentInfo, max_attachments> attachments;
	vk::RenderingAttachmentInfo depth;

	vk::RenderingInfo render_info;
	render_info.renderArea = rp.render_area;
	render_info.layerCount = 1;

	uint32_t att_count = 0;
	bool has_depth = false;

	for(auto& att : rp.attachments)
	{
		if(att_count == max_attachments)
			throw std::logic_error("begin_render_pass: render pass has too many color attachments!");

		if(!att.resource)
			continue;

		bool is_depth = is_depth_format(att.resource->get_key().format);
		has_depth = (render_info.pDepthAttachment != nullptr);
		if(is_depth)
		{	
			if(has_depth)
				throw std::logic_error("begin_render_pass: render pass cannot have multiple depth attachments");

			depth.imageView = att.resource->get_default_view()->get_handle();
			depth.imageLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
			depth.loadOp = att.load_op;
			depth.storeOp = att.store_op;
			depth.clearValue = {.depthStencil={att.clear, 0}};

			render_info.pDepthAttachment = &depth;
			continue;
		}

		auto& new_att = attachments[att_count++];
			
		new_att.imageView = att.resource->get_default_view()->get_handle();
		new_att.imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
		new_att.loadOp = att.load_op;
		new_att.storeOp = att.store_op;
	}

	render_info.colorAttachmentCount = att_count;
	render_info.pColorAttachments = attachments.data();

	cmd.beginRendering(render_info);

	if(rp.auto_scissor)
	{
		set_scissor(0, {rp.render_area.offset, rp.render_area.extent});
	}

	if(rp.auto_viewport == AutoViewportMode::Normal)
	{
		set_viewport(0, 
		{
			static_cast<float>(rp.render_area.offset.x),
			static_cast<float>(rp.render_area.offset.y),
			static_cast<float>(rp.render_area.extent.width),
			static_cast<float>(rp.render_area.extent.height),
			0.0f, 1.0f
		});	
	}
	else if(rp.auto_viewport == AutoViewportMode::Flipped)
	{
		set_viewport(0,
		{
			0.0f,
			static_cast<float>(rp.render_area.extent.height),
			static_cast<float>(rp.render_area.extent.width),
			-static_cast<float>(rp.render_area.extent.height),
			0.0f, 1.0f
		});
	}
	dbg_state = DebugState::Recording;
}

void CommandBuffer::set_scissor(uint32_t offset, vk::Rect2D scissor)
{
	cmd.setScissor(offset, 1, &scissor);
	dbg_state = DebugState::Recording;
}

void CommandBuffer::set_viewport(uint32_t offset, vk::Viewport vp)
{
	cmd.setViewport(offset, 1, &vp);
	dbg_state = DebugState::Recording;
}

void CommandBuffer::end_render_pass()
{
	cmd.endRendering();
	dbg_state = DebugState::Recording;
}

void CommandBuffer::bind_pipeline(const GraphicsPSOKey& key)
{
	bound_pipe = device->try_get_pipeline(key);
	if(!bound_pipe)
		return;

	is_compute_pso = false;
	cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, bound_pipe->pipeline);	
	dbg_state = DebugState::Recording;
}

void CommandBuffer::bind_pipeline(const ComputePSOKey& key)
{
	bound_pipe = device->try_get_pipeline(key);
	if(!bound_pipe)
		return;

	is_compute_pso = true;
	cmd.bindPipeline(vk::PipelineBindPoint::eCompute, bound_pipe->pipeline);
	dbg_state = DebugState::Recording;
}

void CommandBuffer::push_constant(void* value, uint32_t size)
{
	assert(value);
	assert(size);
	assert(bound_pipe);
	cmd.pushConstants(bound_pipe->layout, bound_pipe->pconst.stageFlags, 0, size, value);
	dbg_state = DebugState::Recording;
}	

void CommandBuffer::bind_descriptor_sets(array_proxy<DescriptorSet> sets)
{
	assert(bound_pipe);

	std::array<vk::DescriptorSet, 4> ds;
	uint32_t count = 0;
	uint32_t min_set = 1;

	for(const auto& set : sets)
	{
		ds[count++] = set.set;
		min_set = std::min(min_set, set.bindpoint);
	}

	cmd.bindDescriptorSets(is_compute_pso ? vk::PipelineBindPoint::eCompute : vk::PipelineBindPoint::eGraphics, bound_pipe->layout, min_set, {count, ds.data()}, {});
	dbg_state = DebugState::Recording;
}

void CommandBuffer::push_descriptor_set(const DescriptorSetPush& set)
{
	assert(bound_pipe);
	std::array<vk::DescriptorBufferInfo, 16> buffer_info;
	std::array<vk::DescriptorImageInfo, 16> image_info;
	std::array<vk::WriteDescriptorSet, 16> ds_writes;

	uint32_t num_bufferinfo = 0;
	uint32_t num_imageinfo = 0;
	uint32_t num_writes = 0;

	for(auto& si : set.sampled_images)
	{
		image_info[num_imageinfo] =
		{
			.sampler = si.sampler,
			.imageView = si.view->get_handle(),
			.imageLayout = si.layout
		};

		ds_writes[num_writes++] =
		{
			.dstBinding = si.bindpoint,
			.descriptorCount = 1,
			.descriptorType = vk::DescriptorType::eCombinedImageSampler,
			.pImageInfo = &image_info[num_imageinfo]
		};

		num_imageinfo++;
	}

	for(auto& si : set.storage_images)
	{
		image_info[num_imageinfo] =
		{
			.imageView = si.view->get_handle(),
			.imageLayout = vk::ImageLayout::eGeneral
		};

		ds_writes[num_writes++] = 
		{
			.dstBinding = si.bindpoint,
			.descriptorCount = 1,
			.descriptorType = vk::DescriptorType::eStorageImage,
			.pImageInfo = &image_info[num_imageinfo]
		};

		num_imageinfo++;
	}

	for(auto& ubo : set.uniform_buffers)
	{
		buffer_info[num_bufferinfo] = 
		{
			ubo.buffer->handle,
			ubo.offset,
			ubo.range
		};
		
		ds_writes[num_writes++] = 
		{
			.dstBinding = ubo.bindpoint,
			.descriptorCount = 1,
			.descriptorType = vk::DescriptorType::eUniformBuffer,
			.pBufferInfo = &buffer_info[num_bufferinfo]
		};

		num_bufferinfo++;
	}

	for(auto& ssbo : set.storage_buffers)
	{
		buffer_info[num_bufferinfo] = 
		{
			ssbo.buffer->handle,
			ssbo.offset,
			ssbo.range
		};

		ds_writes[num_writes++] = 
		{
			.dstBinding = ssbo.bindpoint,
			.descriptorCount = 1,
			.descriptorType = vk::DescriptorType::eStorageBuffer,
			.pBufferInfo = &buffer_info[num_bufferinfo]
		};

		num_bufferinfo++;
	}

	cmd.pushDescriptorSetKHR(is_compute_pso ? vk::PipelineBindPoint::eCompute : vk::PipelineBindPoint::eGraphics, bound_pipe->layout, 0, {num_writes, ds_writes.data()});
	dbg_state = DebugState::Recording;
}

void CommandBuffer::bind_vertex_buffers(array_proxy<Buffer*> buffers)
{
	assert(!is_compute_pso);
	assert(bound_pipe);
	
	std::array<vk::Buffer, 2> handles;
	std::array<vk::DeviceSize, 2> offsets{0ull, 0ull};
	uint32_t count = 0;

	for(auto buffer : buffers)
	{
		handles[count++] = buffer->handle;
	}

	cmd.bindVertexBuffers(0, {count, handles.data()}, {count, offsets.data()});
	dbg_state = DebugState::Recording;
}

void CommandBuffer::bind_index_buffer(Buffer* buffer, vk::IndexType type)
{
	assert(buffer);
	assert(!is_compute_pso);
	assert(bound_pipe);
	cmd.bindIndexBuffer(buffer->handle, 0, type);
	dbg_state = DebugState::Recording;
}

void CommandBuffer::draw(uint32_t vertex_count, uint32_t instance_count, uint32_t first_vertex, uint32_t first_instance)
{
	assert(!is_compute_pso);
	assert(bound_pipe);
	cmd.draw(vertex_count, instance_count, first_vertex, first_instance);
	dbg_state = DebugState::Recording;
}

void CommandBuffer::draw_indexed(uint32_t index_count, uint32_t instance_count, uint32_t first_index, int32_t vertex_offset, uint32_t first_instance)
{
	assert(!is_compute_pso);
	assert(bound_pipe);
	cmd.drawIndexed(index_count, instance_count, first_index, vertex_offset, first_instance);
	dbg_state = DebugState::Recording;
}

void CommandBuffer::dispatch(uint32_t group_size_x, uint32_t group_size_y, uint32_t group_size_z)
{
	assert(is_compute_pso);
	assert(bound_pipe);
	cmd.dispatch(group_size_x, group_size_y, group_size_z);
	dbg_state = DebugState::Recording;
}

void CommandBuffer::dispatch(uvec3 group_size)
{
	assert(is_compute_pso);
	assert(bound_pipe);
	cmd.dispatch(group_size.x, group_size.y, group_size.z);
	dbg_state = DebugState::Recording;
}

void CommandBuffer::add_wait_semaphore(WaitSemaphoreInfo&& ws)
{
	wsem = std::move(ws);
}

WaitSemaphoreInfo* CommandBuffer::get_wait_semaphore()
{
	if(wsem.wait_queue == Queue::Invalid)
		return nullptr;

	return &wsem;
}

void CommandBuffer::debug_name(std::string_view name)
{
	device->set_object_name(cmd, name);
	dbg_name = name;
}

}

