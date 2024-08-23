module;

#include <spirv_cross.hpp>

export module spirv_cross;

// minimum for spv reflection
export namespace spirv_cross
{
	using spirv_cross::Compiler;
	using spirv_cross::ShaderResources;
	using spirv_cross::Resource;
	using spv::DecorationBinding;
	using spv::DecorationDescriptorSet;
}
