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

void CommandBuffer::pipeline_barrier(const ImageBarrier& bar)
{
	vk::ImageMemoryBarrier2 vb
	{
		.srcStageMask = bar.src_stage,
		.srcAccessMask = bar.src_access,
		.dstStageMask = bar.dst_stage,
		.dstAccessMask = bar.dst_access,
		.oldLayout = bar.src_layout,
		.newLayout = bar.dst_layout,
		.image = bar.image->get_handle(),
		.subresourceRange = 
		{
			is_depth_format(bar.image->get_key().format) ? vk::ImageAspectFlagBits::eDepth : vk::ImageAspectFlagBits::eColor,
			bar.subresource.level, bar.subresource.levels,
			bar.subresource.layer, bar.subresource.layers
		}
	};

	cmd.pipelineBarrier2
	({
		.imageMemoryBarrierCount = 1u,
		.pImageMemoryBarriers = &vb
	});
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
}

void CommandBuffer::set_scissor(uint32_t offset, vk::Rect2D scissor)
{
	cmd.setScissor(offset, 1, &scissor);
}

void CommandBuffer::set_viewport(uint32_t offset, vk::Viewport vp)
{
	cmd.setViewport(offset, 1, &vp);
}

void CommandBuffer::end_render_pass()
{
	cmd.endRendering();
}

void CommandBuffer::bind_pipeline(const GraphicsPSOKey& key)
{
	bound_pipe = device->try_get_pipeline(key);
	if(!bound_pipe)
		return;

	cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, bound_pipe->pipeline);	
}

void CommandBuffer::bind_pipeline(const ComputePSOKey& key)
{
	bound_pipe = device->try_get_pipeline(key);
	if(!bound_pipe)
		return;

	is_compute_pso = true;
	cmd.bindPipeline(vk::PipelineBindPoint::eCompute, bound_pipe->pipeline);
}

void CommandBuffer::push_constant(void* value, uint32_t size)
{
	cmd.pushConstants(bound_pipe->layout, bound_pipe->pconst.stageFlags, 0, size, value);
}	

void CommandBuffer::bind_descriptor_sets(array_proxy<DescriptorSet> sets)
{
}

void CommandBuffer::bind_descriptor_sets(array_proxy<DescriptorSetPush> sets)
{
	for(auto& set : sets)
	{
		std::vector<vk::DescriptorBufferInfo> buffer_info;
		std::vector<vk::DescriptorImageInfo> image_info;
		std::vector<vk::WriteDescriptorSet> ds_writes;

		for(auto& si : set.bindings.sampled_images)
		{
			image_info.push_back
			({
				.sampler = si.sampler,
				.imageView = si.view->get_handle(),
				.imageLayout = si.layout
			});

			ds_writes.push_back
			({
				.dstBinding = si.bindpoint,
				.descriptorCount = 1,
				.descriptorType = vk::DescriptorType::eCombinedImageSampler,
				.pImageInfo = &image_info.back()
			});
		}

		for(auto& si : set.bindings.storage_images)
		{
			image_info.push_back
			({
				.imageView = si.view->get_handle(),
				.imageLayout = vk::ImageLayout::eGeneral
			});

			ds_writes.push_back
			({
				.dstBinding = si.bindpoint,
				.descriptorCount = 1,
				.descriptorType = vk::DescriptorType::eStorageImage,
				.pImageInfo = &image_info.back()
			});
		}

		for(auto& ubo : set.bindings.uniform_buffers)
		{
			buffer_info.push_back
			({
				ubo.buffer->handle,
				ubo.offset,
				ubo.range
			});
			
			ds_writes.push_back
			({
				.dstBinding = ubo.bindpoint,
				.descriptorCount = 1,
				.descriptorType = vk::DescriptorType::eUniformBuffer,
				.pBufferInfo = &buffer_info.back()
			});
		}

		for(auto& ssbo : set.bindings.storage_buffers)
		{
			buffer_info.push_back
			({
				ssbo.buffer->handle,
				ssbo.offset,
				ssbo.range
			});

			ds_writes.push_back
			({
				.dstBinding = ssbo.bindpoint,
				.descriptorCount = 1,
				.descriptorType = vk::DescriptorType::eStorageBuffer,
				.pBufferInfo = &buffer_info.back()
			});
		}

		cmd.pushDescriptorSetKHR(is_compute_pso ? vk::PipelineBindPoint::eCompute : vk::PipelineBindPoint::eGraphics, bound_pipe->layout, set.bindpoint, {static_cast<uint32_t>(ds_writes.size()), ds_writes.data()});	
	}
}

void CommandBuffer::bind_vertex_buffers(array_proxy<Buffer*> buffers)
{
	//FIXME: multiple buffers
	cmd.bindVertexBuffers(0, {buffers.data()[0]->handle}, {0});
}

void CommandBuffer::bind_index_buffer(Buffer* buffer, vk::IndexType type)
{
	cmd.bindIndexBuffer(buffer->handle, 0, type);
}

void CommandBuffer::draw(uint32_t vertex_count, uint32_t instance_count, uint32_t first_vertex, uint32_t first_instance)
{
	cmd.draw(vertex_count, instance_count, first_vertex, first_instance);
}

void CommandBuffer::draw_indexed(uint32_t index_count, uint32_t instance_count, uint32_t first_index, int32_t vertex_offset, uint32_t first_instance)
{
	cmd.drawIndexed(index_count, instance_count, first_index, vertex_offset, first_instance);
}

}

