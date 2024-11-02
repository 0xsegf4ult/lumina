export module lumina.renderer:resource_format;

import lumina.core.math;
import std;
import vulkan_hpp;

using std::uint16_t, std::uint32_t, std::uint64_t, std::int32_t;

export namespace lumina::render
{

struct TextureFileFormat
{
	constexpr static uint32_t fmt_magic = 0x5845544c;
	constexpr static uint32_t fmt_major_version = 0u;
	constexpr static uint32_t fmt_minor_version = 1u;

	enum class TextureFormat : uint32_t
	{
		Invalid,
		BC4Unorm,
		BC5Unorm,
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
		uint32_t num_mips;
		uint32_t mip_desc_offset;
	};

	struct MipLevelDescription
	{
		uint32_t width;
		uint32_t height;
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

struct SphereBounds
{
	vec3 extents{0.0f};
	vec3 center{0.0f};
	float radius{0.0};
};

struct MeshFormat
{
	constexpr static uint32_t fmt_magic = 0x4c444d4c;
	constexpr static uint32_t fmt_major_version = 0u;
	constexpr static uint32_t fmt_minor_version = 2u;
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
		SphereBounds sphere;
		AABB aabb;
	};

	struct MeshLOD
	{
		int32_t vertex_offset{0};
		uint32_t vertex_count{0u};
		uint32_t index_offset{0u};
		uint32_t index_count{0u};
	};
};

constexpr float signNotZero(float v)
{
	return (v >= 0.0f) ? 1.0f : -1.0f;
}

constexpr vec3 oct_snorm_to_vec3(const Vector<uint16_t, 2>& input)
{
	auto float_cast = [](uint16_t s)
	{
		return std::clamp(static_cast<float>(s) * (1.0f / static_cast<float>((uint64_t(1) << 15) -1 )), -1.0f, 1.0f);
	};

	float fx = float_cast(input.x);
	float fy = float_cast(input.y);

	vec3 v{fx, fy, 1.0f - (std::abs(fx) + std::abs(fy))};

	if(v.z < 0.0f)
	{
		float oldX = v.x;
		v.x = ((1.0f) - std::abs(v.y)) * signNotZero(oldX);
		v.y = ((1.0f) - std::abs(oldX)) * signNotZero(v.y);
	}

	return v;
}

constexpr Vector<uint16_t, 2> vec3_to_oct_snorm(const vec3& input, [[maybe_unused]]bool highp = false)
{
	Vector<uint16_t, 2> projected;

	const float invl1norm = (1.0f) / (std::abs(input.x) + std::abs(input.y) + std::abs(input.z));

	auto floored_snorm = [](float f) -> uint16_t
	{
		return static_cast<uint16_t>(std::round(std::clamp(f, -1.0f, 1.0f) * ((uint64_t(1) << 15) - 1))) + 32767;
	};

	if(input.z < 0.0f)
	{
		projected.x = floored_snorm((1.0f - std::abs(input.y * invl1norm)) * signNotZero(input.x));
		projected.y = floored_snorm((1.0f - std::abs(input.x * invl1norm)) * signNotZero(input.y));
	}
	else
	{
		projected.x = floored_snorm(input.x * invl1norm);
		projected.y = floored_snorm(input.y * invl1norm);
	}

	return projected;
}

constexpr float encode_diamond(const vec2& p)
{
	float x = p.x / (std::abs(p.x) + std::abs(p.y));

	auto sgn = [](float v) -> float
	{
		return (v > 0.0f) - (v < 0.0f);
	};

	float py_sign = sgn(p.y);
	return -py_sign * 0.25f * x + 0.5f + py_sign * 0.25f;
}

constexpr float encode_tangent(const vec3& normal, const vec3& tangent, bool flip)
{
	vec3 t1;
	if(std::abs(normal.y) > std::abs(normal.z))
		t1 = vec3{normal.y, -normal.x, 0.0f};
	else
		t1 = vec3{normal.z, 0.0f, -normal.x};

	t1.normalize();

	vec3 t2 = vec3::cross(t1, normal);
	vec2 packed_tangent = vec2(vec3::dot(tangent, t1), vec3::dot(tangent, t2));
	float diamond = encode_diamond(packed_tangent);

	uint32_t fbits = std::bit_cast<uint32_t>(diamond);
	if(flip)
		fbits |= 1u;
	else
		fbits &= (~1u);

	return std::bit_cast<float>(fbits);
}

template <typename Vtx>
constexpr std::pair<SphereBounds, AABB> calc_mesh_bounds(std::span<Vtx> vertices)
{
	SphereBounds sphere;
	AABB aabb;

	vec3 min{std::numeric_limits<float>::max()};
	vec3 max{std::numeric_limits<float>::lowest()};

	for(const auto& vert : vertices)
	{
		const vec3& v = vert.pos;
		min = vec3::min(min, v);
		max = vec3::max(max, v);
	}
	aabb.mins = min;
	aabb.maxs = max;

	sphere.extents = (max - min) / 2.0f;
	sphere.center = sphere.extents + min;

	float r2 = 0.0f;
	for(const auto& vert : vertices)
	{
		const vec3& v = vert.pos;
		vec3 offset = v - sphere.center;
		r2 = std::max(r2, offset.magnitude_sqr());
	}
	sphere.radius = std::sqrt(r2);
	return std::make_pair(sphere, aabb);
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
