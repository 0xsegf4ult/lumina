export module lumina.core.math:plane;

export import :vector;

export namespace lumina
{
	template <typename T>
	struct basic_plane
	{
		constexpr basic_plane() = default;
		constexpr basic_plane(float _a, float _b, float _c, float _d) noexcept : a{_a}, b{_b}, c{_c}, d{_d} {}
		constexpr basic_plane(const Vector<T, 3>& normal, float _d) noexcept : a{normal.x}, b{normal.y}, c{normal.z}, d{_d} {}
		constexpr basic_plane(const Vector<T, 4>& norm_dist) noexcept : a{norm_dist.x}, b{norm_dist.y}, c{norm_dist.z}, d{norm_dist.w} {}

		static constexpr basic_plane from_point_and_normal(const Vector<T, 3>& p, const Vector<T, 3>& normal) noexcept
		{
			Vector<T, 3> nn = Vector<T, 3>::normalize(normal);
			return basic_plane(nn, Vector<T, 3>::dot(p, nn));
		}

		static constexpr basic_plane from_points(const Vector<T, 3>& p0, const Vector<T, 3>& p1, const Vector<T, 3>& p2) noexcept
		{
			Vector<T, 3> normal = Vector<T, 3>::cross(p1, p2);
			return from_point_and_normal(p0, normal);
		}

		constexpr Vector<T, 3> normal() const noexcept
		{
			return Vector<T, 3>(a, b, c);
		}

		constexpr Vector<T, 3> point() const noexcept
		{
			return normal() * d;
		}

		constexpr basic_plane normalize() noexcept
		{
			const float mag = Vector<T, 3>(a, b, c).magnitude();
			return basic_plane{a / mag, b / mag, c / mag, d / mag};
		}

		constexpr Vector<T, 4> as_vector() const noexcept
		{
			return Vector<T, 4>{a, b, c, d};
		}

		static constexpr basic_plane translate(const basic_plane& plane, const Vector<T, 3>& position) noexcept
		{
			return basic_plane(plane.a, plane.b, plane.c, plane.d + Vector<T, 3>::dot(plane.normal(), position));
		}

		static constexpr float distance(const basic_plane& p, const Vector<T, 3>& v) noexcept
		{
			return Vector<T, 3>::dot(p.normal(), v) - p.d;
		}

		float a, b, c, d;
	};

	using Plane = basic_plane<float>;
}

