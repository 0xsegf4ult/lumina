export module lumina.vulkan:shader;

import spirv_cross;
import std;
import vulkan_hpp;
import :descriptor;

import lumina.core;

using std::uint32_t, std::size_t;

namespace lumina::vulkan
{

constexpr bool dump_reflection_info = false;

export struct Shader
{
	vk::ShaderStageFlagBits pipeline_stage{};
	vk::ShaderModule shader_module;

	vk::PushConstantRange pconst{};
	std::array<DescriptorSetLayoutKey, 4> dsl_keys{};
};

vk::ShaderStageFlagBits infer_shader_kind(const std::filesystem::path& path)
{
	const std::string& ext = path.string();
	if(ext.contains(".vert"))
		return vk::ShaderStageFlagBits::eVertex;
	else if(ext.contains(".frag"))
		return vk::ShaderStageFlagBits::eFragment;
	else if(ext.contains(".geom"))
		return vk::ShaderStageFlagBits::eGeometry;
	else if(ext.contains(".comp"))
		return vk::ShaderStageFlagBits::eCompute;

	return vk::ShaderStageFlagBits::eAll;
}

void shader_reflect(Shader& stg, const std::vector<uint32_t>& spirv)
{
	spirv_cross::Compiler spvcomp(spirv);
	spirv_cross::ShaderResources res;

	res = spvcomp.get_shader_resources();
	auto stage = stg.pipeline_stage;

	for(const auto& pcb : res.push_constant_buffers)
	{
		vk::PushConstantRange& vkstruct = stg.pconst;
		auto ranges = spvcomp.get_active_buffer_ranges(pcb.id);

		vkstruct.stageFlags |= stage;
		uint32_t range_acc = vkstruct.size;
		for(auto& range : ranges)
			range_acc += range.range;

		vkstruct.size = std::max(vkstruct.size, range_acc);
	}
	if(stg.pconst.size >= 128)
		log::warn("shader_reflect: push constant size exceeds 128 bytes");

	auto emit_bindings = [&spvcomp, stage](const spirv_cross::Resource& r, DescriptorSetLayoutKey& dsl)
	{
		auto& type = spvcomp.get_type(r.type_id);

		auto bindpoint = spvcomp.get_decoration(r.id, spv::DecorationBinding);
		
		dsl.binding_arraysize[bindpoint] = 1;
		if(!type.array.empty())
		{
			if(type.array[0] == 0)
			{
				if constexpr(dump_reflection_info)
					log::debug("binding {} is variable size", bindpoint);
				dsl.variable_bindings |= (1u << bindpoint);
			}
					
			dsl.binding_arraysize[bindpoint] = type.array[0];
		}

		std::string dbg_s = "";

		if(stage & vk::ShaderStageFlagBits::eVertex)
		{
			dsl.vs_bindings |= (1u << bindpoint);
			if constexpr(dump_reflection_info)
				dbg_s += " VS";
		}
		if(stage & vk::ShaderStageFlagBits::eFragment)
		{
			dsl.fs_bindings |= (1u << bindpoint);
			if constexpr(dump_reflection_info)
				dbg_s += " FS";
		}
		if(stage & vk::ShaderStageFlagBits::eCompute)
		{
			dsl.cs_bindings |= (1u << bindpoint);
			
			if constexpr(dump_reflection_info)
				dbg_s += "CS";
		}

		if constexpr(dump_reflection_info)
			log::debug("binding {} is accessed in {}", bindpoint, dbg_s);

		return bindpoint;
	};

	{
		for(const auto& tex : res.sampled_images)
		{
			auto set = spvcomp.get_decoration(tex.id, spv::DecorationDescriptorSet);
			auto bindpoint = emit_bindings(tex, stg.dsl_keys[set]);
			stg.dsl_keys[set].sampled_image_bindings |= (1u << bindpoint);	
			if constexpr(dump_reflection_info)
				log::debug("binding {} is IMAGE_SAMPLER", bindpoint);
		}

		for(const auto& ubo : res.uniform_buffers)
		{
			auto set = spvcomp.get_decoration(ubo.id, spv::DecorationDescriptorSet);
			auto bindpoint = emit_bindings(ubo, stg.dsl_keys[set]);
			stg.dsl_keys[set].uniform_buffer_bindings |= (1u << bindpoint);
			if constexpr(dump_reflection_info)
				log::debug("binding {} is UNIFORM_BUFFER", bindpoint);
		}

		for(const auto& ssbo : res.storage_buffers)
		{
			auto set = spvcomp.get_decoration(ssbo.id, spv::DecorationDescriptorSet);
			auto bindpoint = emit_bindings(ssbo, stg.dsl_keys[set]);
			stg.dsl_keys[set].storage_buffer_bindings |= (1u << bindpoint);
			if constexpr(dump_reflection_info)
				log::debug("binding {} is STORAGE_BUFFER", bindpoint);
		}

		for(const auto& si : res.storage_images)
		{
			auto set = spvcomp.get_decoration(si.id, spv::DecorationDescriptorSet);
			auto bindpoint = emit_bindings(si, stg.dsl_keys[set]);
			stg.dsl_keys[set].storage_image_bindings |= (1u << bindpoint);
			if constexpr(dump_reflection_info)
				log::debug("binding {} is STORAGE_IMAGE", bindpoint);
		}

		for(const auto& si : res.separate_images)
		{
			auto set = spvcomp.get_decoration(si.id, spv::DecorationDescriptorSet);
			auto bindpoint = emit_bindings(si, stg.dsl_keys[set]);
			stg.dsl_keys[set].separate_image_bindings |= (1u << bindpoint);
			if constexpr(dump_reflection_info)
				log::debug("binding {} is SAMPLED_IMAGE", bindpoint);
		}

		for(const auto& s : res.separate_samplers)
		{
			auto set = spvcomp.get_decoration(s.id, spv::DecorationDescriptorSet);
			auto bindpoint = emit_bindings(s, stg.dsl_keys[set]);
			stg.dsl_keys[set].sampler_bindings |= (1u << bindpoint);
			if constexpr(dump_reflection_info)
				log::debug("binding {} is SAMPLER", bindpoint);
		}
	}
}

export std::expected<Shader, std::string_view> load_spv(vk::Device device, const std::filesystem::path& path)
{
	log::info("shader_cache: compiling shader {}", path.string());
	
	auto stage = infer_shader_kind(path);
	std::ifstream spv_file{path, std::ios::binary | std::ios::ate};
	if(!spv_file.is_open())
	{
		return std::unexpected{"failed to open file"};
	}

	auto code_size = spv_file.tellg();
	spv_file.seekg(0u);
	std::vector<uint32_t> code(static_cast<size_t>(code_size) / sizeof(uint32_t));
	spv_file.read(reinterpret_cast<char*>(code.data()), code_size);

	vk::ShaderModule mod;

	try
	{
		mod = device.createShaderModule
		({
			.codeSize = code.size() * sizeof(uint32_t),
			.pCode = code.data()
		});
	}
	catch(const vk::Error& err)
	{
		return std::unexpected{err.what()};
	}

	Shader stg
	{
		.pipeline_stage = stage,
		.shader_module = mod
	};

	shader_reflect(stg, code);
	return stg;	
}

}

