module;

#include <cassert>

export module lumina.vulkan:descriptor;

import vulkan_hpp;
import std;
import xxhash;

import lumina.core;

using std::uint32_t, std::uint16_t, std::uint8_t, std::size_t;

namespace lumina::vulkan
{

export class ImageView;
export class Buffer;

export const size_t max_bindings = 16;
export const size_t max_descriptor_sets = 4; 
export struct DescriptorSetLayoutKey
{
	uint16_t sampled_image_bindings{0};
	uint16_t storage_image_bindings{0};
	uint16_t separate_image_bindings{0};
	uint16_t sampler_bindings{0};
	uint16_t uniform_buffer_bindings{0};
	uint16_t storage_buffer_bindings{0};
	uint16_t vs_bindings{0};
	uint16_t fs_bindings{0};
	uint16_t cs_bindings{0};
	uint16_t variable_bindings{0};

	uint8_t binding_arraysize[16];	

	constexpr bool is_empty() const noexcept
	{
		return sampled_image_bindings == 0 && storage_image_bindings == 0 && separate_image_bindings == 0 && sampler_bindings == 0 && uniform_buffer_bindings == 0 && storage_buffer_bindings == 0;
	}

	bool operator==(const DescriptorSetLayoutKey& other) const noexcept
	{
		return std::memcmp(this, &other, sizeof(DescriptorSetLayoutKey)) == 0;
	}
};

export struct CombinedImageSamplerBinding
{
	uint32_t bindpoint;
	ImageView* view;
	vk::Sampler sampler;
	vk::ImageLayout layout = vk::ImageLayout::eShaderReadOnlyOptimal;
};

export struct BufferBinding
{
	uint32_t bindpoint;
	Buffer* buffer;
	vk::DeviceSize offset = 0;
	vk::DeviceSize range = vk::WholeSize;
};

export struct ImageBinding
{
	uint32_t bindpoint;
	ImageView* view;
	vk::ImageLayout layout = vk::ImageLayout::eGeneral;
};

export struct ImageArrayBinding
{
	uint32_t bindpoint;
	array_proxy<ImageView*> views;
	vk::ImageLayout layout = vk::ImageLayout::eGeneral;
};

export struct SamplerBinding
{
	uint32_t bindpoint;
	vk::Sampler sampler;
};

export struct DescriptorSet
{
	uint32_t bindpoint;
	vk::DescriptorSet set;
};

export struct DescriptorSetPush
{
	array_proxy<CombinedImageSamplerBinding> sampled_images;
	array_proxy<ImageBinding> storage_images;
	array_proxy<ImageArrayBinding> storage_image_arrays;
	array_proxy<ImageBinding> separate_images;
	array_proxy<SamplerBinding> samplers;
	array_proxy<BufferBinding> uniform_buffers;
	array_proxy<BufferBinding> storage_buffers;
};

const std::size_t max_variable_resources = 65536;

export vk::DescriptorSetLayout create_descriptor_layout(vk::Device device, const DescriptorSetLayoutKey& key, bool is_push)
{
	std::vector<vk::DescriptorSetLayoutBinding> bindings;

	bool variable_count_set = false;

	for(uint32_t i = 0; i < max_bindings; i++)
	{
		bool variable_count = false;
		if(key.variable_bindings & (1u << i))
		{
			variable_count_set = true;
			variable_count = true;
		}

		vk::ShaderStageFlags stages;
		if(key.vs_bindings & (1u << i))
			stages |= vk::ShaderStageFlagBits::eVertex;
		if(key.fs_bindings & (1u << i))
			stages |= vk::ShaderStageFlagBits::eFragment;
		if(key.cs_bindings & (1u << i))
			stages |= vk::ShaderStageFlagBits::eCompute;

		if(!stages)
			continue;

		vk::DescriptorType type;
		if(key.sampled_image_bindings & (1u << i))
		{
			type = vk::DescriptorType::eCombinedImageSampler;
		}
		else if(key.storage_image_bindings & (1u << i))
		{
			type = vk::DescriptorType::eStorageImage;
		}
		else if(key.separate_image_bindings & (1u << i))
		{
			type = vk::DescriptorType::eSampledImage;
		}
		else if(key.sampler_bindings & (1u << i))
		{
			type = vk::DescriptorType::eSampler;
		}
		else if(key.uniform_buffer_bindings & (1u << i))
		{
			type = vk::DescriptorType::eUniformBuffer;
		}
		else if(key.storage_buffer_bindings & (1u << i))
		{
			type = vk::DescriptorType::eStorageBuffer;
		}
		else
		{
			std::unreachable();
		}

		bindings.push_back
		({
			i,
			type,
			(variable_count) ? static_cast<uint32_t>(max_variable_resources) : key.binding_arraysize[i],
			stages,
			nullptr
		});
	}

	if(is_push)
		assert(!variable_count_set);

	vk::DescriptorSetLayout dsl;

	vk::DescriptorSetLayoutCreateInfo layout_ci
	{
		.flags = is_push ? vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptorKHR : vk::DescriptorSetLayoutCreateFlagBits{},
		.bindingCount = static_cast<uint32_t>(bindings.size()),
		.pBindings = bindings.data()
	};

	if(variable_count_set)
	{
		layout_ci.flags |= vk::DescriptorSetLayoutCreateFlagBits::eUpdateAfterBindPool;

		std::vector<vk::DescriptorBindingFlags> bflags;
		for([[maybe_unused]]auto& binding : bindings)
		{
			bflags.push_back
			({
				binding.descriptorCount > 1 ? (vk::DescriptorBindingFlagBits::ePartiallyBound | vk::DescriptorBindingFlagBits::eVariableDescriptorCount | vk::DescriptorBindingFlagBits::eUpdateAfterBind) : (vk::DescriptorBindingFlags{})
			});
		}

		vk::StructureChain<vk::DescriptorSetLayoutCreateInfo, vk::DescriptorSetLayoutBindingFlagsCreateInfo> chain =
		{
			layout_ci,
			{
				.bindingCount = static_cast<uint32_t>(bflags.size()),
				.pBindingFlags = bflags.data()
			}
		};

		dsl = device.createDescriptorSetLayout(chain.get<vk::DescriptorSetLayoutCreateInfo>());
	}
	else
	{
		dsl = device.createDescriptorSetLayout(layout_ci);
	}

	return dsl;
}

}

export template<>
struct std::hash<lumina::vulkan::DescriptorSetLayoutKey>
{
	std::size_t operator()(const lumina::vulkan::DescriptorSetLayoutKey& key) const noexcept
	{
		return xxhash::XXH3_64bits(&key, sizeof(key));
	}
};
