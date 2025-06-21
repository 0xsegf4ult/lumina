module;

#include <cassert>

module lumina.vulkan;

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

void CommandBuffer::memory_barrier(array_proxy<MemoryBarrier> bar) const noexcept
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
}

void CommandBuffer::pipeline_barrier(array_proxy<BufferBarrier> bar) const noexcept
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
}

void CommandBuffer::pipeline_barrier(array_proxy<ImageBarrier> bar) const noexcept
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
			get_format_aspect(bar[i].image->get_key().format),
			bar[i].subresource.level, bar[i].subresource.levels,
			bar[i].subresource.layer, bar[i].subresource.layers
		};
	}

	cmd.pipelineBarrier2
	({
		.imageMemoryBarrierCount = static_cast<std::uint32_t>(bar.size()),
		.pImageMemoryBarriers = vb.data()
	});
}

void CommandBuffer::begin_render_pass(const RenderPassDesc& rp) const noexcept
{
	std::array<vk::RenderingAttachmentInfo, max_attachments> attachments;
	vk::RenderingAttachmentInfo depth;
	vk::RenderingAttachmentInfo stencil;

	vk::RenderingInfo render_info;
	render_info.renderArea = rp.render_area;
	render_info.layerCount = rp.view_mask == 0 ? 1 : 0;
	render_info.viewMask = rp.view_mask;

	uint32_t att_count = 0;
	bool has_depth = false;
	bool has_stencil = false;

	for(const auto& att : rp.attachments)
	{
		assert(att_count < max_attachments && "render pass has too many color attachments!");

		if(!att.resource)
			continue;

		const bool is_depth = is_depth_format(att.resource->get_key().format);
		has_depth = (render_info.pDepthAttachment != nullptr);
		has_stencil = (render_info.pStencilAttachment != nullptr);
		if(is_depth)
		{	
			assert(!has_depth && "render pass cannot have multiple depth attachments");
			assert(!has_stencil && "render pass cannot have seperate depth and stencil attachments");

			depth.imageView = att.resource->get_handle();
			depth.imageLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
			depth.loadOp = att.load_op;
			depth.storeOp = att.store_op;
			depth.clearValue = {.depthStencil={att.clear, 0}};
		
			if(att.resolve)
			{
				depth.resolveMode = vk::ResolveModeFlagBits::eAverage;
				depth.resolveImageView = att.resolve->get_handle();
				depth.resolveImageLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
			}

			render_info.pDepthAttachment = &depth;
			continue;
		}

		const bool is_stencil = is_stencil_format(att.resource->get_key().format);
		if(is_stencil)
		{
			assert(!has_stencil && "render pass cannot have multiple stencil attachments");
			assert(!has_depth && "render pass cannot have seperate depth and stencil attachments");

			stencil.imageView = att.resource->get_handle();
			stencil.imageLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
			stencil.loadOp = att.load_op;
			stencil.storeOp = att.store_op;
			stencil.clearValue = {.depthStencil={0.0f, static_cast<uint32_t>(att.clear)}};
	
			assert(!att.resolve && "resolve unsupported for stencil attachments");	

			render_info.pStencilAttachment = &stencil;
			continue;
		}

		auto& new_att = attachments[att_count++];
			
		new_att.imageView = att.resource->get_handle();
		new_att.imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
		new_att.loadOp = att.load_op;
		new_att.storeOp = att.store_op;

		if(att.resolve)
		{
			new_att.resolveMode = vk::ResolveModeFlagBits::eAverage;
			new_att.resolveImageView = att.resolve->get_handle();
			new_att.resolveImageLayout = vk::ImageLayout::eColorAttachmentOptimal;
		}
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
}

void CommandBuffer::set_scissor(uint32_t offset, vk::Rect2D scissor) const noexcept
{
	cmd.setScissor(offset, 1, &scissor);
}

void CommandBuffer::set_viewport(uint32_t offset, vk::Viewport vp) const noexcept
{
	cmd.setViewport(offset, 1, &vp);
}

void CommandBuffer::end_render_pass() const noexcept
{
	cmd.endRendering();
}

void CommandBuffer::bind_pipeline(const GraphicsPSOKey& key) noexcept
{
	bound_pipe = device->try_get_pipeline(key);
	if(!bound_pipe)
		return;

	is_compute_pso = false;
	cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, bound_pipe->pipeline);
	cmd.setCullMode(key.primitive.cullmode);	
}

void CommandBuffer::bind_pipeline(const ComputePSOKey& key) noexcept
{
	bound_pipe = device->try_get_pipeline(key);
	if(!bound_pipe)
		return;

	is_compute_pso = true;
	cmd.bindPipeline(vk::PipelineBindPoint::eCompute, bound_pipe->pipeline);
}

void CommandBuffer::push_constant(const void* value, uint32_t size) const noexcept
{
	assert(value);
	assert(size);
	assert(bound_pipe);
	cmd.pushConstants(bound_pipe->layout.handle, bound_pipe->layout_key.pconst.stageFlags, 0, size, value);
}	

void CommandBuffer::bind_descriptor_sets(array_proxy<DescriptorSet> sets) const noexcept
{
	assert(bound_pipe);

	std::array<vk::DescriptorSet, 4> ds;
	uint32_t count = 0;
	uint32_t min_set = 1;

	for(const auto& set : sets)
	{
		assert(set.bindpoint > 0 && "bindpoint 0 is reserved for push descriptor");
		ds[count++] = set.set;
		min_set = std::min(min_set, set.bindpoint);
	}

	cmd.bindDescriptorSets(is_compute_pso ? vk::PipelineBindPoint::eCompute : vk::PipelineBindPoint::eGraphics, bound_pipe->layout.handle, min_set, {count, ds.data()}, {});
}

void CommandBuffer::push_descriptor_set(const DescriptorSetPush& set) const noexcept
{
	assert(bound_pipe);
	std::array<vk::DescriptorBufferInfo, 16> buffer_info;
	std::array<vk::DescriptorImageInfo, 16> image_info;
	std::array<vk::WriteDescriptorSet, 16> ds_writes;

	uint32_t num_bufferinfo = 0;
	uint32_t num_imageinfo = 0;
	uint32_t num_writes = 0;

	for(const auto& si : set.sampled_images)
	{
		assert(num_imageinfo < 16);
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

	for(const auto& si : set.storage_images)
	{
		assert(num_imageinfo < 16);
		image_info[num_imageinfo] =
		{
			.imageView = si.view->get_handle(),
			.imageLayout = si.layout
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

	for(const auto& sa : set.storage_image_arrays)
	{
		auto binding_size = bound_pipe->layout_key.dsl_keys[0].binding_arraysize[sa.bindpoint];
		assert(num_imageinfo + binding_size < 16);
		for(auto i = 0u; i < sa.views.size(); i++)
		{
			image_info[num_imageinfo + i] =
			{
				.imageView = sa.views[i]->get_handle(),
				.imageLayout = sa.layout
			};
		}

		for(auto i = sa.views.size(); i < binding_size; i++)
		{
			image_info[num_imageinfo + i] =
			{
				.imageView = nullptr
			};
		}

		ds_writes[num_writes++] =
		{
			.dstBinding = sa.bindpoint,
			.descriptorCount = binding_size,
			.descriptorType = vk::DescriptorType::eStorageImage,
			.pImageInfo = &image_info[num_imageinfo]
		};

		num_imageinfo += binding_size;
	}

	for(const auto& si : set.separate_images)
	{
		assert(num_imageinfo < 16);
		image_info[num_imageinfo] =
		{
			.imageView = si.view->get_handle(),
			.imageLayout = si.layout
		};

		ds_writes[num_writes++] =
		{
			.dstBinding = si.bindpoint,
			.descriptorCount = 1,
			.descriptorType = vk::DescriptorType::eSampledImage,
			.pImageInfo = &image_info[num_imageinfo]
		};

		num_imageinfo++;
	}

	for(const auto& s : set.samplers)
	{
		assert(num_imageinfo < 16);
		image_info[num_imageinfo] =
		{
			.sampler = s.sampler
		};

		ds_writes[num_writes++] =
		{
			.dstBinding = s.bindpoint,
			.descriptorCount = 1,
			.descriptorType = vk::DescriptorType::eSampler,
			.pImageInfo = &image_info[num_imageinfo]
		};

		num_imageinfo++;
	}

	for(const auto& ubo : set.uniform_buffers)
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

	for(const auto& ssbo : set.storage_buffers)
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

	cmd.pushDescriptorSetKHR(is_compute_pso ? vk::PipelineBindPoint::eCompute : vk::PipelineBindPoint::eGraphics, bound_pipe->layout.handle, 0, {num_writes, ds_writes.data()});
}

void CommandBuffer::bind_vertex_buffers(array_proxy<Buffer*> buffers) const noexcept
{
	assert(!is_compute_pso);
	assert(bound_pipe);
	
	std::array<vk::Buffer, 2> handles;
	std::array<vk::DeviceSize, 2> offsets{0ull, 0ull};
	uint32_t count = 0;

	for(auto* buffer : buffers)
	{
		handles[count++] = buffer->handle;
	}

	cmd.bindVertexBuffers(0, {count, handles.data()}, {count, offsets.data()});
}

void CommandBuffer::bind_index_buffer(Buffer* buffer, vk::IndexType type) const noexcept
{
	assert(buffer);
	assert(!is_compute_pso);
	assert(bound_pipe);
	cmd.bindIndexBuffer(buffer->handle, 0, type);
}

void CommandBuffer::draw(uint32_t vertex_count, uint32_t instance_count, uint32_t first_vertex, uint32_t first_instance) const noexcept
{
	assert(!is_compute_pso);
	assert(bound_pipe);
	cmd.draw(vertex_count, instance_count, first_vertex, first_instance);
}

void CommandBuffer::draw_indirect(Buffer* buffer, vk::DeviceSize offset, uint32_t draw_count, uint32_t stride) const noexcept
{
	assert(!is_compute_pso);
	assert(bound_pipe);
	assert(buffer);
	assert(stride);

	cmd.drawIndirect(buffer->handle, offset, draw_count, stride);
}

void CommandBuffer::draw_indexed(uint32_t index_count, uint32_t instance_count, uint32_t first_index, int32_t vertex_offset, uint32_t first_instance) const noexcept
{
	assert(!is_compute_pso);
	assert(bound_pipe);
	cmd.drawIndexed(index_count, instance_count, first_index, vertex_offset, first_instance);
}
	
void CommandBuffer::draw_indexed_indirect(Buffer* buffer, vk::DeviceSize offset, uint32_t draw_count, uint32_t stride) const noexcept
{
	assert(!is_compute_pso);
	assert(bound_pipe);
	assert(buffer);
	assert(stride);

	cmd.drawIndexedIndirect(buffer->handle, offset, draw_count, stride); 
}

void CommandBuffer::draw_indexed_indirect_count(Buffer* buffer, vk::DeviceSize offset, Buffer* count_buffer, vk::DeviceSize count_offset, uint32_t max_draw_count, uint32_t stride) const noexcept
{
	assert(!is_compute_pso);
	assert(bound_pipe);
	assert(buffer);
	assert(count_buffer);
	assert(stride);

	cmd.drawIndexedIndirectCount(buffer->handle, offset, count_buffer->handle, count_offset, max_draw_count, stride);
}

void CommandBuffer::dispatch(uint32_t group_size_x, uint32_t group_size_y, uint32_t group_size_z) const noexcept
{
	assert(is_compute_pso);
	assert(bound_pipe);
	cmd.dispatch(group_size_x, group_size_y, group_size_z);
}

void CommandBuffer::dispatch(uvec3 group_size) const noexcept
{
	assert(is_compute_pso);
	assert(bound_pipe);
	cmd.dispatch(group_size.x, group_size.y, group_size.z);
}

void CommandBuffer::dispatch_indirect(Buffer* buffer, vk::DeviceSize offset) const noexcept
{
	assert(is_compute_pso);
	assert(bound_pipe);
	assert(buffer);

	cmd.dispatchIndirect(buffer->handle, offset);
}

void CommandBuffer::add_wait_semaphore(WaitSemaphoreInfo&& ws) noexcept
{
	assert(ws_count < 2);
	wsem[ws_count] = ws;
	ws_count++;
}

std::span<WaitSemaphoreInfo> CommandBuffer::get_wait_semaphores() noexcept
{
	return {wsem.begin(), ws_count};
}

void CommandBuffer::debug_name(std::string_view name)
{
	device->set_object_name(cmd, name);
	dbg_name = name;
}

}

