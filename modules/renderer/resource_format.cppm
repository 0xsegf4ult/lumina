export module lumina.renderer:resource_format;

import lumina.core.log;
import lumina.core.math;
import std;
import vulkan_hpp;

using std::uint16_t, std::uint32_t, std::uint64_t, std::int32_t;

export namespace lumina::render
{

struct TextureFileFormat
{
	constexpr static uint32_t fmt_magic = 0x5845544c;
	constexpr static uint32_t fmt_major_version = 2u;
	constexpr static uint32_t fmt_minor_version = 1u;

	enum class TextureFormat : uint32_t
	{
		Invalid,
		BC4Unorm,
		BC5Unorm,
		BC6HUfloat,
		BC7Unorm,
		BC7SRGB
	};

	constexpr static TextureFormat from_vkformat(vk::Format fmt)
	{
		switch(fmt)
		{
		case vk::Format::eBc4UnormBlock:
			return TextureFormat::BC4Unorm;
		case vk::Format::eBc5UnormBlock:
			return TextureFormat::BC5Unorm;
		case vk::Format::eBc6HUfloatBlock:
			return TextureFormat::BC6HUfloat;
		case vk::Format::eBc7UnormBlock:
			return TextureFormat::BC7Unorm;
		case vk::Format::eBc7SrgbBlock:
			return TextureFormat::BC7SRGB;
		default:
			return TextureFormat::Invalid;
		}
	}

	constexpr static vk::Format to_vkformat(TextureFormat fmt)
	{
		switch(fmt)
		{
		case TextureFormat::BC4Unorm:
			return vk::Format::eBc4UnormBlock;
		case TextureFormat::BC5Unorm:
			return vk::Format::eBc5UnormBlock;
		case TextureFormat::BC6HUfloat:
			return vk::Format::eBc6HUfloatBlock;
		case TextureFormat::BC7Unorm:
			return vk::Format::eBc7UnormBlock;
		case TextureFormat::BC7SRGB:
			return vk::Format::eBc7SrgbBlock;
		default:
			return {};
		}
	}

	struct Header
	{
		uint32_t magic{fmt_magic};
		uint32_t vmajor{fmt_major_version};
		uint32_t vminor{fmt_minor_version};
		TextureFormat texformat;
		uint32_t num_subres;
		uint32_t subres_desc_offset;
	};

	struct SubresourceDescription
	{
		uint32_t width;
		uint32_t height;
		uint32_t level;
		uint32_t layer;
		uint32_t data_offset;
		uint32_t data_size_bytes;
	};
};

struct StaticVertexFormat
{
	using pos_type = vec3;
	pos_type pos;

	struct Attributes
	{
		float enc_tangent;
		vec2 uv{0.0f};
		Vector<uint16_t, 2> oct_normal;
	} attr;
};
static_assert(sizeof(StaticVertexFormat) == 28u);
static_assert(sizeof(StaticVertexFormat::Attributes) == 16u);

struct SkinnedVertexFormat
{
	vec3 pos;
	float enc_tangent;
	vec2 uv{0.0f};
	Vector<uint16_t, 2> oct_normal;
	uint32_t joints;
	vec4 weights;
};
static_assert(sizeof(SkinnedVertexFormat) == 48u);

struct MeshFormat
{
	constexpr static uint32_t fmt_magic = 0x4c444d4c;
	constexpr static uint32_t fmt_major_version = 1u;
	constexpr static uint32_t fmt_minor_version = 1u;
	constexpr static uint32_t max_lod_count = 5u;

	enum class VertexFormat : uint32_t
	{
		Static,
		Skinned
	};

	struct Header
	{
		uint32_t magic{fmt_magic};
		uint32_t vmajor{fmt_major_version};
		uint32_t vminor{fmt_minor_version};
		VertexFormat vert_format;
		uint32_t vpos_offset;
		uint32_t vattr_offset;
		uint32_t index_offset;
		uint32_t lod_offset;
		uint32_t num_lods;
		uint32_t cluster_offset;
		vec4 sphere;
	};

	struct MeshLOD
	{
		uint32_t cluster_offset{0u};
		uint32_t cluster_count{0u};
	};

	struct Cluster
	{
		int32_t vertex_offset{0};
		uint32_t vertex_count{0u};
		uint32_t index_offset{0u};
		uint32_t index_count{0u};

		vec4 sphere;
		vec4 cone;
	};
};

constexpr float signNotZero(float v)
{
	return (v >= 0.0f) ? 1.0f : -1.0f;
}

vec3 oct_snorm_to_vec3(const Vector<uint16_t, 2>& input)
{
	auto float_cast = [](uint16_t s)
	{
		return std::clamp(static_cast<float>(s) * (1.0f / static_cast<float>((uint64_t(1) << 15) -1 )), -1.0f, 1.0f);
	};

	const float fx = float_cast(input.x);
	const float fy = float_cast(input.y);

	vec3 v{fx, fy, 1.0f - (std::abs(fx) + std::abs(fy))};

	if(v.z < 0.0f)
	{
		float oldX = v.x;
		v.x = ((1.0f) - std::abs(v.y)) * signNotZero(oldX);
		v.y = ((1.0f) - std::abs(oldX)) * signNotZero(v.y);
	}

	return v;
}

Vector<uint16_t, 2> vec3_to_oct_snorm(const vec3& input, [[maybe_unused]]bool highp = false)
{
	Vector<uint16_t, 2> projected;

	const float invl1norm = (1.0f) / (std::abs(input.x) + std::abs(input.y) + std::abs(input.z));
	
	auto packSnorm16 = [](float f) -> uint16_t
	{
		return static_cast<uint16_t>(std::round(std::clamp(f, -1.0f, 1.0f) * 32767.0f));
	};

	if(input.z < 0.0f)
	{
		projected.x = packSnorm16((1.0f - std::abs(input.y * invl1norm)) * signNotZero(input.x));
		projected.y = packSnorm16((1.0f - std::abs(input.x * invl1norm)) * signNotZero(input.y));
	}
	else
	{
		projected.x = packSnorm16(input.x * invl1norm);
		projected.y = packSnorm16(input.y * invl1norm);
	}

	return projected;
}

float encode_diamond(const vec2& p)
{
	const float x = p.x / (std::abs(p.x) + std::abs(p.y));

	auto sgn = [](float v) -> float
	{
		return (v > 0.0f) - (v < 0.0f);
	};

	const float py_sign = sgn(p.y);
	return -py_sign * 0.25f * x + 0.5f + py_sign * 0.25f;
}

float encode_tangent(const vec3& normal, const vec3& tangent, bool flip)
{
	vec3 t1;
	if(std::abs(normal.y) > std::abs(normal.z))
		t1 = vec3{normal.y, -normal.x, 0.0f};
	else
		t1 = vec3{normal.z, 0.0f, -normal.x};

	const vec3 nt1 = vec3::normalize(t1);

	const vec3 t2 = vec3::cross(nt1, normal);
	const vec2 packed_tangent = vec2(vec3::dot(tangent, nt1), vec3::dot(tangent, t2));
	const float diamond = encode_diamond(packed_tangent);

	uint32_t fbits = std::bit_cast<uint32_t>(diamond);
	if(flip)
		fbits |= 1u;
	else
		fbits &= (~1u);

	float res = std::bit_cast<float>(fbits);
	if(std::isnan(res))
	{
		log::warn("encode_tangent: tangent vector {} encoded as NaN!!!", tangent);
		res = 0.0f;
	}

	return res;
}

struct SkeletonFileFormat
{
	constexpr static uint32_t fmt_magic = 0x4c4b534c;
	constexpr static uint32_t fmt_major_version = 0u;
	constexpr static uint32_t fmt_minor_version = 1u;
	
	struct Header
	{
		uint32_t magic{fmt_magic};
		uint32_t vmajor{fmt_major_version};
		uint32_t vminor{fmt_minor_version};
		uint32_t bone_count;
		uint32_t name_table_offset;
		uint32_t transform_table_offset;
		uint32_t parent_table_offset;
		uint32_t matrix_table_offset;
	};
};

struct AnimationFileFormat
{
	constexpr static uint32_t fmt_magic = 0x4d4e414c;
	constexpr static uint32_t fmt_major_version = 0u;
	constexpr static uint32_t fmt_minor_version = 1u;

	struct Header
	{
		uint32_t magic{fmt_magic};
		uint32_t vmajor{fmt_major_version};
		uint32_t vminor{fmt_minor_version};
		uint32_t channel_count;
		uint32_t ref_skeleton_offset;
		uint32_t channel_table_offset;
	};

	struct Channel
	{
		uint32_t keyframe_count;
		uint32_t timestamp_offset;
		uint32_t value_offset;
		uint32_t bone;
		uint32_t path;
		uint32_t interp;
	};
};

}
