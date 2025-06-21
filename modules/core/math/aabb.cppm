export module lumina.core.math:aabb;

export import :vector;
import std;

export namespace lumina
{

template <typename T>
struct basic_aabb
{
	constexpr basic_aabb() noexcept : mins(T(0.0)), maxs(T(0.0)) {}
	constexpr basic_aabb(const Vector<T, 3>& min, const Vector<T, 3>& max) noexcept : mins{min}, maxs{max} {}
	constexpr basic_aabb(const basic_aabb& other) noexcept = default;
	constexpr basic_aabb(basic_aabb&& other) noexcept = default;
	
	basic_aabb<T>& operator=(const basic_aabb& other) noexcept = default;
	basic_aabb<T>& operator=(basic_aabb&& other) noexcept = default;

	basic_aabb<T> operator+(const Vector<T, 3>& other) noexcept
	{
		return basic_aabb<T>
		(
			mins + other,
			maxs + other
		);
	}

	[[nodiscard]] constexpr T area() const noexcept
	{
		Vector<T, 3> d = this->maxs - this->mins;
		return T(2.0) * (d.x * d.y + d.y * d.z + d.z * d.x);
	}

	[[nodiscard]] constexpr T volume() const noexcept
	{
		return std::abs(maxs.x - mins.x) * std::abs(maxs.z - mins.z) * std::abs(maxs.y - mins.y);
	}

	[[nodiscard]] constexpr vec3 get_center() const noexcept
	{
		return 0.5f * (mins + maxs);
	}

	[[nodiscard]] constexpr vec3 get_extents() const noexcept
	{
		return 0.5f * (maxs - mins);
	}

	[[nodiscard]] bool contains(const basic_aabb<T>& other) const noexcept
	{
		return( (mins.x <= other.mins.x) && (maxs.x >= other.maxs.x) &&
			(mins.y <= other.mins.y) && (maxs.y >= other.maxs.y) &&
			(mins.z <= other.mins.z) && (maxs.z >= other.maxs.z));
	}

	[[nodiscard]] static bool check_intersect(const basic_aabb<T>& lhs, const basic_aabb<T>& rhs) noexcept
	{
		return( (lhs.maxs.x > rhs.mins.x) && (lhs.mins.x < rhs.maxs.x) &&
			(lhs.maxs.y > rhs.mins.y) && (lhs.mins.y < rhs.maxs.y) &&
			(lhs.maxs.z > rhs.mins.z) && (lhs.mins.z < rhs.maxs.z));
	}

	[[nodiscard]] static basic_aabb<T> merge(const basic_aabb<T>& lhs, const basic_aabb<T>& rhs) noexcept
	{
		return {Vector<T, 3>::min(lhs.mins, rhs.mins), Vector<T, 3>::max(lhs.maxs, rhs.maxs)};
	}

	Vector<T, 3> mins;
	Vector<T, 3> maxs;
};

template <typename T>
struct aabb_simd4_packed
{
	Vector<T, 4> minX;
	Vector<T, 4> minY;
	Vector<T, 4> minZ;
	Vector<T, 4> maxX;
	Vector<T, 4> maxY;
	Vector<T, 4> maxZ;
};

using AABB = basic_aabb<float>;
using SIMD4AABB = aabb_simd4_packed<float>;

}

export template <typename T>
struct std::formatter<lumina::basic_aabb<T>>
{
	template <class ParseContext>
	constexpr ParseContext::iterator parse(ParseContext& ctx)
	{
		return ctx.begin();
	}

	template <class FmtContext>
	FmtContext::iterator format(const lumina::basic_aabb<T>& v, FmtContext& ctx) const
	{
		return std::format_to(ctx.out(), "aabb[{} - {}]", v.mins, v.maxs);
	}
};
