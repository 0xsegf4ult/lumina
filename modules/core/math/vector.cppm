export module lumina.core.math:vector;

import std;

using std::size_t, std::uint32_t, std::int32_t;

namespace lumina
{

template <typename T, size_t N>
struct VectorStorage
{	
	constexpr VectorStorage() noexcept = default;
	constexpr VectorStorage(const VectorStorage&) noexcept = default;
	constexpr VectorStorage& operator=(const VectorStorage&) noexcept = default;
	constexpr VectorStorage(VectorStorage&&) noexcept = default;
	constexpr VectorStorage& operator=(VectorStorage&&) noexcept = default;

	template <typename... Us, typename = std::enable_if_t<(sizeof...(Us) == N - 2)>>
	constexpr VectorStorage(const T& _x, const T& _y, Us&&... us) noexcept : data{_x, _y, us...} {}

	T data[N];

};

template <typename T>
struct VectorStorage<T, 2>
{
	constexpr VectorStorage() noexcept = default;
	constexpr VectorStorage(const VectorStorage&) noexcept = default;
	constexpr VectorStorage& operator=(const VectorStorage&) noexcept = default;
	constexpr VectorStorage(VectorStorage&&) noexcept = default;
	constexpr VectorStorage& operator=(VectorStorage&&) noexcept = default;
	constexpr VectorStorage(const T& _x, const T& _y) noexcept : data{_x, _y} {}

	// apparently this is UB, dont care works fine on clang	  
	union
	{
		struct { T x, y; };
		struct { T r, g; };
		T data[2];
	};
};

template <typename T>
struct VectorStorage<T, 3>
{
	constexpr VectorStorage() noexcept = default;
	constexpr VectorStorage(const VectorStorage&) noexcept = default;
	constexpr VectorStorage& operator=(const VectorStorage&) noexcept = default;
	constexpr VectorStorage(VectorStorage&&) noexcept = default;
	constexpr VectorStorage& operator=(VectorStorage&&) noexcept = default;
	constexpr VectorStorage(const T& _x, const T& _y, const T& _z) noexcept : data{_x, _y, _z} {}
	
	union
	{
		struct { T x, y, z; };
		struct { T r, g, b; };
		T data[3];
	};
};

template <typename T>
struct VectorStorage<T, 4>
{
	constexpr VectorStorage() noexcept = default;
	constexpr VectorStorage(const VectorStorage&) noexcept = default;
	constexpr VectorStorage& operator=(const VectorStorage&) noexcept = default;
	constexpr VectorStorage(VectorStorage&&) noexcept = default;
	constexpr VectorStorage& operator=(VectorStorage&&) noexcept = default;
	constexpr VectorStorage(const T& _x, const T& _y, const T& _z, const T& _w) noexcept : data{_x, _y, _z, _w} {}
	
	union
	{
		struct { T x, y, z, w; };
		struct { T r, g, b, a; };
		T data[4];
	};
};

}

export namespace lumina
{

template <typename T, size_t N, typename IDX = std::make_index_sequence<N>>
struct Vector : public VectorStorage<T, N>
{
	constexpr Vector() noexcept = default;
	constexpr Vector(const Vector&) noexcept = default;
	constexpr Vector& operator=(const Vector&) noexcept = default;
	constexpr Vector(Vector&&) noexcept = default;
	constexpr Vector& operator=(Vector&&) noexcept = default;
	
	template <typename... Us, typename = std::enable_if_t<(sizeof...(Us) == N - 2)>>
	constexpr Vector(const T& _x, const T& _y, Us&&... us) noexcept : VectorStorage<T, N>{_x, _y, us...} {}
	
	template <size_t... Is>
	constexpr Vector(const T& _x, std::index_sequence<Is...>) noexcept
	{
		((this->data[Is] = _x), ...);
	}

	explicit constexpr Vector(const T& _x) noexcept : Vector(_x, IDX{}) {}
	
	constexpr Vector& operator=(const T& _x) noexcept
	{
		Vector(_x, IDX{});
		return *this;
	}

	template <typename U, size_t... Is>
	constexpr explicit Vector(const Vector<U, N, std::index_sequence<Is...>>& v) noexcept 
	{
		((this->data[Is] = T(v.data[Is])), ...);
	}

	template <size_t... Is>
	constexpr explicit Vector(const Vector<T, N - 1, std::index_sequence<Is...>>&v, const T& s) noexcept
	{
		((this->data[Is] = v.data[Is]), ...);
		this->data[N - 1] = s;	
	}

	T& operator[](size_t i) noexcept
	{
		return this->data[i];
	}

	constexpr const T& operator[](size_t i) const noexcept
	{
		return this->data[i];
	}

	constexpr T magnitude() const noexcept
	{
		return std::sqrt(dot(*this, *this));
	}

	constexpr T magnitude_sqr() const noexcept
	{
		return dot(*this, *this);
	}

	constexpr Vector normalize() const noexcept
	{
		T mag = magnitude();

		// we ARE looking for an exact match to zero here
		// shut up the compiler complaining about fp comparison
		#if defined(__clang__)
		#pragma clang diagnostic push
		#pragma clang diagnostic ignored "-Wfloat-equal"
		#endif
		return (mag != T(0.0)) ? (*this / mag) : Vector<T, N>{T(0.0)};
		#if defined(__clang__)
		#pragma clang diagnostic pop
		#endif
	}

	template <typename U, size_t... Is>
	Vector& operator+=(const Vector<U, N, std::index_sequence<Is...>>& v) noexcept
	{
		((this->data[Is] += v.data[Is]), ...);
		return *this;
	}

	template <typename U, size_t... Is>
	Vector& operator-=(const Vector<U, N, std::index_sequence<Is...>>& v) noexcept
	{
		((this->data[Is] -= v.data[Is]), ...);
		return *this;
	}

	template <typename U>
	Vector& operator*=(U scalar) noexcept
	{
		[this]<size_t... Is>(U s, std::index_sequence<Is...>)
		{
			((this->data[Is] *= s), ...);
		}(scalar, std::make_index_sequence<N>{});
		return *this;
	}

	template <typename U>
	Vector& operator/=(U scalar)
	{
		[this]<size_t... Is>(U s, std::index_sequence<Is...>)
		{
			((this->data[Is] /= s), ...);
		}(scalar, std::make_index_sequence<N>{});
		return *this;
	}

	template <typename U, size_t... Is>
	static constexpr auto dot(const Vector<T, N, std::index_sequence<Is...>>& lhs, const Vector<U, N, std::index_sequence<Is...>>& rhs) noexcept
       	{
		return ((lhs.data[Is] * rhs.data[Is]) + ...);
	}

	template <typename U>
	static constexpr auto cross(const Vector<T, 3>& lhs, const Vector<U, 3>& rhs) noexcept
	{
		return Vector<decltype(lhs[0] + rhs[0]), 3>
		{
			(lhs.y * rhs.z) - (lhs.z * rhs.y),
			(lhs.z * rhs.x) - (lhs.x * rhs.z),
			(lhs.x * rhs.y) - (lhs.y * rhs.x)
		};
	}

	template <typename U, size_t... Is>
	static constexpr auto scalar_mul(const Vector<T, N, std::index_sequence<Is...>>& lhs, const Vector<U, N, std::index_sequence<Is...>>& rhs) noexcept
	{
		Vector<decltype(lhs[0] + rhs[0]), N> res;
		((res.data[Is] = lhs.data[Is] * rhs.data[Is]), ...);
		return res;
	}

	template <typename U, size_t... Is>
	static constexpr auto min(const Vector<T, N, std::index_sequence<Is...>>& lhs, const Vector<U, N, std::index_sequence<Is...>>& rhs) noexcept
	{
		return Vector<std::decay_t<decltype(lhs[0] + rhs[0])>, N>{std::min(lhs.data[Is], rhs.data[Is])...};
	}

	template <typename U, size_t... Is>
	static constexpr auto max(const Vector<T, N, std::index_sequence<Is...>>& lhs, const Vector<U, N, std::index_sequence<Is...>>& rhs) noexcept
	{
		return Vector<std::decay_t<decltype(lhs[0] + rhs[0])>, N>{std::max(lhs.data[Is], rhs.data[Is])...};
	}

	template <size_t... Is>
	static constexpr auto abs(const Vector<T, N, std::index_sequence<Is...>>& v) noexcept
	{
		return Vector<T, N>{std::abs(v.data[Is])...};
	}

	template <size_t... Is>
	static constexpr auto round(const Vector<T, N, std::index_sequence<Is...>>& v) noexcept
	{
		return Vector<T, N>{std::round(v.data[Is])...};
	}

	template <typename U, typename V, size_t... Is>
	static constexpr auto clamp(const Vector<T, N, std::index_sequence<Is...>>& v0,
			            const Vector<U, N, std::index_sequence<Is...>>& min,
				    const Vector<V, N, std::index_sequence<Is...>>& max) noexcept
	{
		return Vector<decltype(v0[0] + min[0] + max[0]), N>{std::clamp(v0.data[Is], min.data[Is], max.data[Is])...};
	}

	static constexpr auto basis(size_t num) noexcept
	{
		return impl_basis(num, std::make_index_sequence<N>{});
	}

	static void compute_basis(const Vector<T, 3>& _a, Vector<T, 3>& _b, Vector<T, 3>& _c)
	{
		//https://box2d.org/posts/2014/02/computing-a-basis/
		if(std::abs(_a.x) >= T(0.57735))
			_b = Vector<T, 3>{_a.y, -_a.x, T(0.0)};
		else
			_b = Vector<T, 3>{T(0.0), _a.z, -_a.y};

		_b = _b.normalize();
		_c = Vector<T, 3>::cross(_a, _b);
	}
private:
	template <size_t... Is>
	static constexpr auto impl_basis(size_t i, std::index_sequence<Is...>) noexcept
	{
		return Vector<T, N>{T(i == Is)...};
	}
};

template <typename T, size_t N, size_t... Is>
constexpr auto operator+(const Vector<T, N, std::index_sequence<Is...>>& v) noexcept
{
	return v;
}

template <typename T, size_t N, size_t... Is>
constexpr auto operator-(const Vector<T, N, std::index_sequence<Is...>>& v) noexcept
{
	return Vector<T, N>{-v.data[Is]...};
}

template <typename T, typename U, size_t N, size_t... Is>
constexpr auto operator+(const Vector<T, N, std::index_sequence<Is...>>& lhs, const Vector<U, N, std::index_sequence<Is...>>& rhs) noexcept
{
	return Vector<T, N>{lhs.data[Is] + rhs.data[Is]...};
}

template <typename T, typename U, size_t N, size_t... Is>
constexpr auto operator-(const Vector<T, N, std::index_sequence<Is...>>& lhs, const Vector<U, N, std::index_sequence<Is...>>& rhs) noexcept
{
	return Vector<T, N>{lhs.data[Is] - rhs.data[Is]...};
}

template <typename T, typename U, size_t N, size_t... Is>
constexpr auto operator*(const Vector<T, N, std::index_sequence<Is...>>& v, U s) noexcept
{
	return Vector<T, N>{v.data[Is] * T(s)...};
}

template <typename T, typename U, size_t N, size_t... Is>
constexpr auto operator*(U s, const Vector<T, N, std::index_sequence<Is...>>& v) noexcept
{
	return Vector<T, N>{v.data[Is] * T(s)...};
}

template <typename T, typename U, size_t N, size_t... Is>
constexpr auto operator*(const Vector<T, N, std::index_sequence<Is...>>& lhs, const Vector<U, N, std::index_sequence<Is...>>& rhs) noexcept
{
	return Vector<T, N>{lhs.data[Is] * rhs.data[Is]...};
}

template <typename T, typename U, size_t N, size_t... Is>
constexpr auto operator/(const Vector<T, N, std::index_sequence<Is...>>& v, U s)
{
	return Vector<T, N>{v.data[Is] / T(s)...};
}

template <typename T, typename U, size_t N, size_t... Is>
constexpr bool operator==(const Vector<T, N, std::index_sequence<Is...>>& lhs, const Vector<U, N, std::index_sequence<Is...>>& rhs) noexcept
{
	return ((lhs.data[Is] == rhs.data[Is]) && ...);
}

using vec2 = Vector<float, 2>;
using vec3 = Vector<float, 3>;
using vec4 = Vector<float, 4>;
using ivec2 = Vector<int32_t, 2>;
using ivec3 = Vector<int32_t, 3>;
using ivec4 = Vector<int32_t, 4>;
using uvec2 = Vector<uint32_t, 2>;
using uvec3 = Vector<uint32_t, 3>;
using uvec4 = Vector<uint32_t, 4>;
using bvec2 = Vector<std::byte, 2>;
using bvec3 = Vector<std::byte, 3>;
using bvec4 = Vector<std::byte, 4>;

constexpr const vec3 vector_world_up{0.0f, 1.0f, 0.0f};
constexpr const vec3 vector_world_forward{0.0f, 0.0f, -1.0f};
constexpr const vec3 vector_world_right{1.0f, 0.0f, 0.0f};

}

export template <typename T, size_t N, size_t... Is>
struct std::formatter<lumina::Vector<T, N, std::index_sequence<Is...>>>
{
	template <class ParseContext>
	constexpr ParseContext::iterator parse(ParseContext& ctx)
	{
		auto it = ctx.begin();
		if(it == ctx.end())
			return it;

		if(*it == 'v')
		{
			pr_size = true;
			++it;
		}

		if(it != ctx.end() && *it != '}')
			throw std::format_error("invalid format arguments for lumina::Vector");

		return it;
	}

	template <class FmtContext>
	FmtContext::iterator format(const lumina::Vector<T, N, std::index_sequence<Is...>>& v, FmtContext& ctx) const
	{
		if(pr_size)
			std::format_to(ctx.out(), "v{}", N);	

		std::format_to(ctx.out(), "[");
		for(size_t i = 0; i < N; i++)
			std::format_to(ctx.out(), "{}{}", v.data[i], (i < N - 1) ? ", " : "");
		std::format_to(ctx.out(), "]");

		return ctx.out();
	}

	bool pr_size = false;
};

