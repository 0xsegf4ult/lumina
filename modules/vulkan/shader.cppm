export module lumina.vulkan:shader;

import std;
import vulkan_hpp;
import :descriptor;

import lumina.core;
import lumina.vfs;

using std::uint32_t, std::size_t;

namespace lumina::vulkan
{

export const size_t max_shader_stages = 5;

export struct ShaderFileFormat
{
	constexpr static uint32_t fmt_magic = 0x4c48534c;
	constexpr static uint32_t fmt_major_version = 1u;
	constexpr static uint32_t fmt_minor_version = 0u;

	enum class Stage : uint32_t
	{
		Vertex,
		Fragment,
		Compute
	};

	struct Header
	{
		uint32_t magic{fmt_magic};
		uint32_t vmajor{fmt_major_version};
		uint32_t vminor{fmt_minor_version};
		uint32_t num_dslkeys;
		uint32_t pcb_size;
		vk::ShaderStageFlags pcb_stages;
		uint32_t num_stages;
	};

	struct ShaderStage
	{
		Stage stage;
		uint32_t code_size;
		uint32_t code_offset;
	};
};

export struct Shader
{
	struct ShaderStage
	{
		std::vector<uint32_t> spirv;
		vk::ShaderStageFlagBits pipeline_stage{};
	};
	std::array<ShaderStage, max_shader_stages> stages;

	std::array<DescriptorSetLayoutKey, 4> dsl_keys{};
	vk::PushConstantRange pconst{};
};

export std::expected<Shader, std::string_view> load_shader(vk::Device device, const vfs::path& path)
{
	log::info("shader_cache: compiling shader {}", path.string());
	
	Shader res;

	auto shader_file = vfs::open(path, vfs::access_readonly);
	if(!shader_file.has_value())
		return std::unexpected("file not found");

	const auto* shader_data = vfs::map<std::byte>(*shader_file, vfs::access_readonly);
	const auto* header = reinterpret_cast<const ShaderFileFormat::Header*>(shader_data);
	if(header->magic != ShaderFileFormat::fmt_magic || header->vmajor != ShaderFileFormat::fmt_major_version)
		return std::unexpected("invalid file");

	res.pconst.stageFlags = header->pcb_stages;
	res.pconst.size = header->pcb_size;

	const auto* stages = reinterpret_cast<const ShaderFileFormat::ShaderStage*>(shader_data + sizeof(ShaderFileFormat::Header));
	for(uint32_t i = 0; i < header->num_stages; i++)
	{
		const auto& stage = stages[i];
		switch(stage.stage)
		{
		using enum ShaderFileFormat::Stage;
		case Vertex:
			res.stages[i].pipeline_stage = vk::ShaderStageFlagBits::eVertex;
			break;
		case Fragment:
			res.stages[i].pipeline_stage = vk::ShaderStageFlagBits::eFragment;
			break;
		case Compute:
			res.stages[i].pipeline_stage = vk::ShaderStageFlagBits::eCompute;
			break;
		}

		res.stages[i].spirv.resize(stage.code_size / sizeof(uint32_t));
		std::memcpy(res.stages[i].spirv.data(), shader_data + stage.code_offset, stage.code_size);
	}

	const auto* dsl_keys = reinterpret_cast<const std::byte*>(shader_data + sizeof(ShaderFileFormat::Header) + sizeof(ShaderFileFormat::ShaderStage) * header->num_stages);
	for(uint32_t i = 0; i < header->num_dslkeys; i++)
	{
		std::memcpy(&res.dsl_keys[i], dsl_keys, sizeof(DescriptorSetLayoutKey));
		dsl_keys += sizeof(DescriptorSetLayoutKey);
	}

	return res;
}

}

