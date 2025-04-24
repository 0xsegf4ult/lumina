export module lumina.vulkan:pipeline;

import :descriptor;
import :shader;
import vulkan_hpp;
import std;
import lumina.core;
import xxhash;

using std::uint32_t, std::size_t;

namespace lumina::vulkan
{

export const size_t max_vertex_attributes = 8;
export const size_t max_vertex_bindings = 2;
export const size_t max_color_attachments = 8;
export const size_t max_shader_stages = 5;

export struct PrimitiveState
{
	vk::PolygonMode polymode{vk::PolygonMode::eFill};
	vk::CullModeFlags cullmode{vk::CullModeFlagBits::eNone};
	vk::PrimitiveTopology topology{vk::PrimitiveTopology::eTriangleList};
	uint32_t patch_ctrl = 0;
};

export constexpr PrimitiveState tri_fill_nocull
{
	vk::PolygonMode::eFill,
	vk::CullModeFlagBits::eNone,
	vk::PrimitiveTopology::eTriangleList,
	0
};

export constexpr PrimitiveState tri_fill_backcull
{
	vk::PolygonMode::eFill,
	vk::CullModeFlagBits::eBack,
	vk::PrimitiveTopology::eTriangleList,
	0
};

export constexpr PrimitiveState tri_wireframe
{
	vk::PolygonMode::eLine,
	vk::CullModeFlagBits::eNone,
	vk::PrimitiveTopology::eTriangleList,
	0
};

export using VertexDescription = std::array<std::array<vk::Format, max_vertex_attributes>, max_vertex_bindings>;

constexpr uint32_t vk_format_size(vk::Format fmt)
{
	switch(fmt)
	{
	case vk::Format::eR32G32B32A32Sfloat:
		return 16;
	case vk::Format::eR32G32B32Sfloat:
		return 12;
	case vk::Format::eR32G32Sfloat:
		return 8;
	case vk::Format::eR8G8B8A8Unorm:
	case vk::Format::eR8G8B8A8Srgb:
	case vk::Format::eR32Uint:
	case vk::Format::eR32Sint:
	case vk::Format::eR32Sfloat:
		return 4;
	default:
		return 0;
	}
}

export enum class DepthMode
{
	Disabled,
	ReverseZ,
	Shadowcast,
	Equal,
	Less,
	Greater
};

constexpr vk::CompareOp depth_mode_compare_op(DepthMode mode)
{
	switch(mode)
	{
	case DepthMode::Disabled:
	default:
		return vk::CompareOp::eAlways;
	case DepthMode::Equal:
		return vk::CompareOp::eEqual;
	case DepthMode::Shadowcast:
	case DepthMode::Less:
		return vk::CompareOp::eLess;
	case DepthMode::ReverseZ:
	case DepthMode::Greater:
		return vk::CompareOp::eGreater;
	}
}

export enum class BlendMode
{
	Disabled,
	AlphaBlend
};

constexpr vk::PipelineColorBlendAttachmentState decode_blend_mode(BlendMode mode)
{
	vk::PipelineColorBlendAttachmentState att{};

	switch(mode)
	{
	case BlendMode::Disabled:
	default:
		att.blendEnable = false;
		att.colorWriteMask = vk::ColorComponentFlagBits::eA | vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB;
		break;
	case BlendMode::AlphaBlend:
		att.blendEnable = true;
		att.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
		att.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
		att.colorBlendOp = vk::BlendOp::eAdd;
		att.srcAlphaBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
		att.dstAlphaBlendFactor = vk::BlendFactor::eZero;
		att.alphaBlendOp = vk::BlendOp::eAdd;
		att.colorWriteMask = vk::ColorComponentFlagBits::eA | vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB;
		break;
	}

	return att;
}

export struct GraphicsPSOKey
{
	PrimitiveState primitive{};
	VertexDescription vert_desc{};
	DepthMode depth_mode;
	std::array<BlendMode, max_color_attachments> blend_modes{};
	struct AttachmentFormats
	{
		std::array<vk::Format, max_color_attachments> color{};
		vk::Format depth = vk::Format::eUndefined;
	} att_formats;
	uint32_t view_mask = 0u;
	std::array<std::filesystem::path, max_shader_stages> shaders{};

	bool operator==(const GraphicsPSOKey& other) const noexcept
	{
		if(std::memcmp(&primitive, &other.primitive, sizeof(PrimitiveState)) != 0)
			return false;

		if(std::memcmp(&vert_desc, &other.vert_desc, sizeof(VertexDescription)) != 0)
			return false;
		
		if(std::memcmp(&blend_modes, &other.blend_modes, sizeof(blend_modes)) != 0)
			return false;

		if(std::memcmp(&att_formats, &other.att_formats, sizeof(AttachmentFormats)) != 0)
			return false;

		for(uint32_t i = 0; i < max_shader_stages; i++)
			if(shaders[i] != other.shaders[i])
				return false;

		return depth_mode == other.depth_mode && view_mask == other.view_mask;
	}
};

export struct ComputePSOKey
{
	std::filesystem::path shader{};

	bool operator==(const ComputePSOKey& other) const noexcept
	{
		return shader == other.shader;
	}
};

export struct PipelineLayoutKey
{
	std::array<DescriptorSetLayoutKey, max_descriptor_sets> dsl_keys{};	
	vk::PushConstantRange pconst{};

	bool operator==(const PipelineLayoutKey& other) const noexcept
	{
		for(uint32_t i = 0; i < max_descriptor_sets; i++)
			if(dsl_keys[i] != other.dsl_keys[i])
				return false;

		return std::memcmp(&pconst, &other.pconst, sizeof(vk::PushConstantRange)) == 0;
	}
};

export struct PipelineLayout
{
	vk::PipelineLayout layout;
	std::array<vk::DescriptorSetLayout, max_descriptor_sets> ds_layouts;
};

export PipelineLayoutKey build_pipe_layout(std::span<Shader*> shaders)
{
	PipelineLayoutKey layout;

	for(size_t i = 0; i < 4; i++)
	{
		for(auto shader : shaders)
		{
			if(!shader || shader->dsl_keys[i].is_empty())
				continue;
			
			auto& src_dsl = shader->dsl_keys[i];
			auto& dst_dsl = layout.dsl_keys[i];

			dst_dsl.sampled_image_bindings |= src_dsl.sampled_image_bindings;
			dst_dsl.storage_image_bindings |= src_dsl.storage_image_bindings;
			dst_dsl.uniform_buffer_bindings |= src_dsl.uniform_buffer_bindings;
			dst_dsl.storage_buffer_bindings |= src_dsl.storage_buffer_bindings;
			dst_dsl.vs_bindings |= src_dsl.vs_bindings;
			dst_dsl.fs_bindings |= src_dsl.fs_bindings;
			dst_dsl.cs_bindings |= src_dsl.cs_bindings;
			dst_dsl.variable_bindings |= src_dsl.variable_bindings;
		}		
	}

	layout.pconst = shaders[0]->pconst;
	for(auto shader : shaders)
	{
		if(!shader)
			break;

		layout.pconst.stageFlags |= shader->pconst.stageFlags;
		layout.pconst.size = std::max(layout.pconst.size, shader->pconst.size);
	}

	return layout;
}

export struct Pipeline
{
	vk::Pipeline pipeline;
	vk::PipelineLayout layout;
	std::array<vk::DescriptorSetLayout, max_descriptor_sets> ds_layouts;
	vk::PushConstantRange pconst;
	std::array<Handle<Shader>, max_shader_stages> shaders{};
};

export std::expected<vk::Pipeline, bool> compile_pipeline(vk::Device device, vk::PipelineLayout layout, std::span<Shader*> shaders, const GraphicsPSOKey& key)
{
	std::array<vk::PipelineShaderStageCreateInfo, max_shader_stages> stages;
	uint32_t num_stages = 0;
	for(auto& shader: shaders)
	{
		if(!shader || shader->pipeline_stage == vk::ShaderStageFlagBits{0})
			break;

		stages[num_stages].stage = shader->pipeline_stage;
		stages[num_stages].module = shader->shader_module;
		stages[num_stages].pName = "main";
		num_stages++;
	}

	uint32_t num_color_attachments = 0;
	for(const auto& att : key.att_formats.color)
	{
		if(att != vk::Format::eUndefined)
			num_color_attachments++;
		else
			break;
	}

	vk::PipelineRenderingCreateInfo dynamic_rendering
	{
		.viewMask = key.view_mask,
		.colorAttachmentCount = num_color_attachments,
		.pColorAttachmentFormats = key.att_formats.color.data(),
		.depthAttachmentFormat = key.att_formats.depth
	};

	std::array<vk::VertexInputBindingDescription, max_vertex_bindings> vbindings;
	uint32_t num_vertex_bindings = 0;

	std::array<vk::VertexInputAttributeDescription, max_vertex_attributes * max_vertex_bindings> vattr;
	uint32_t num_vertex_attributes = 0;


	uint32_t g_att = 0;
	uint32_t g_att_offset = 0;
	for(auto& binding: key.vert_desc)
	{
		uint32_t offset = 0;
		uint32_t att = 0;
		for(att = 0; auto& attribute : binding)
		{
			auto size = vk_format_size(attribute);
			if(!size)
				break;

			vattr[num_vertex_attributes++] = vk::VertexInputAttributeDescription
			{
				att + g_att, num_vertex_bindings, attribute, offset
			};

			offset += size;
			att++;
			
		}
		g_att += att;
		g_att_offset += offset;

		if(!offset)
			break;

		vbindings[num_vertex_bindings] = vk::VertexInputBindingDescription
		{
			num_vertex_bindings, offset, vk::VertexInputRate::eVertex
		};

		num_vertex_bindings++;
	}

	vk::PipelineVertexInputStateCreateInfo vtxinput
	{
		.vertexBindingDescriptionCount = num_vertex_bindings,
		.pVertexBindingDescriptions = vbindings.data(),
		.vertexAttributeDescriptionCount = num_vertex_attributes,
		.pVertexAttributeDescriptions = vattr.data()
	};
	
	vk::PipelineInputAssemblyStateCreateInfo input_asm
	{
		.topology = key.primitive.topology,
		.primitiveRestartEnable = false
	};

	vk::PipelineTessellationStateCreateInfo tessellation
	{
		.patchControlPoints = key.primitive.patch_ctrl
	};

	vk::PipelineViewportStateCreateInfo viewport{.viewportCount = 1, .scissorCount = 1};

	vk::PipelineRasterizationStateCreateInfo rasterization
	{
		.depthClampEnable = (key.depth_mode == DepthMode::Shadowcast) ? true : false,
		.rasterizerDiscardEnable = false,
		.polygonMode = key.primitive.polymode,
		.cullMode = key.primitive.cullmode,
		.frontFace = vk::FrontFace::eCounterClockwise,
		.depthBiasEnable = false,
		.lineWidth = 1.0f
	};

	vk::PipelineMultisampleStateCreateInfo multisample
	{
		.rasterizationSamples = vk::SampleCountFlagBits::e1,
		.sampleShadingEnable = false
	};

	vk::PipelineDepthStencilStateCreateInfo depthstencil
	{
		.depthTestEnable = (key.att_formats.depth != vk::Format::eUndefined) ? true : false,
		.depthWriteEnable = (key.att_formats.depth != vk::Format::eUndefined && key.depth_mode != DepthMode::Equal) ? true : false,
		.depthCompareOp = (key.att_formats.depth != vk::Format::eUndefined) ? depth_mode_compare_op(key.depth_mode) : vk::CompareOp::eAlways,
		.depthBoundsTestEnable = false,
		.stencilTestEnable = false
	};

	std::array<vk::PipelineColorBlendAttachmentState, max_color_attachments> blend_att;
	for(uint32_t i = 0; i < num_color_attachments; i++)
		blend_att[i] = decode_blend_mode(key.blend_modes[i]);

	vk::PipelineColorBlendStateCreateInfo blend
	{
		.logicOpEnable = false,
		.attachmentCount = num_color_attachments,
		.pAttachments = blend_att.data()
	};

	std::array<vk::DynamicState, 3> dsenables
	{
		vk::DynamicState::eViewport,
		vk::DynamicState::eScissor
	};
	uint32_t num_dstates = 2;

	vk::PipelineDynamicStateCreateInfo dynamic_state
	{
		.dynamicStateCount = num_dstates,
		.pDynamicStates = dsenables.data()
	};

	vk::GraphicsPipelineCreateInfo pipelineci
	{
		.pNext = &dynamic_rendering,
		.stageCount = num_stages,
		.pStages = stages.data(),
		.pVertexInputState = &vtxinput,
		.pInputAssemblyState = &input_asm,
		.pTessellationState = &tessellation,
		.pViewportState = &viewport,
		.pRasterizationState = &rasterization,
		.pMultisampleState = &multisample,
		.pDepthStencilState = &depthstencil,
		.pColorBlendState = &blend,
		.pDynamicState = &dynamic_state,
		.layout = layout
	};

	auto [result, pso] = device.createGraphicsPipeline(nullptr, pipelineci);
	return pso;
}

export std::expected<vk::Pipeline, bool> compile_pipeline(vk::Device device, vk::PipelineLayout layout, Shader* shader, const ComputePSOKey& key) 
{
	vk::PipelineShaderStageCreateInfo sstage{};	
	sstage.stage = shader->pipeline_stage;
	sstage.module = shader->shader_module;
	sstage.pName = "main";

	auto [result, pso] = device.createComputePipeline(nullptr,
	{
		.stage = sstage,
		.layout = layout
	});
	return pso;
}	

}

export template<>
struct std::hash<lumina::vulkan::PipelineLayoutKey>
{
	std::size_t operator()(const lumina::vulkan::PipelineLayoutKey& key) const noexcept
	{
		return xxhash::XXH3_64bits(&key, sizeof(key));
	}
};

export template<>
struct std::hash<lumina::vulkan::GraphicsPSOKey>
{
	std::size_t operator()(const lumina::vulkan::GraphicsPSOKey& key) const noexcept
	{
		auto hv = std::filesystem::hash_value(key.shaders[0]);
		for(uint32_t i = 1; i < lumina::vulkan::max_shader_stages; i++)
		{
			if(key.shaders[i].empty())
				break;

			hv = lumina::hash_combine(hv, std::filesystem::hash_value(key.shaders[i]));
		}

		return lumina::hash_combine(hv, xxhash::XXH3_64bits(&key, sizeof(key) - (sizeof(std::filesystem::path) * lumina::vulkan::max_shader_stages)));
	}
};

export template<>
struct std::hash<lumina::vulkan::ComputePSOKey>
{
	std::size_t operator()(const lumina::vulkan::ComputePSOKey& key) const noexcept
	{
		return std::filesystem::hash_value(key.shader);
	}
};
