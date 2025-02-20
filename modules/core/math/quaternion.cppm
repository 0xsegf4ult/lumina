export module lumina.core.math:quaternion;

export import :matrix;
export import :util;
export import :vector;

import std;

export namespace lumina 
{

template <typename T>
struct basic_quat : public Vector<T, 4>
{
public:
	using Vector<T, 4>::Vector;
	constexpr basic_quat() : Vector<T, 4>{T(0.0), T(0.0), T(0.0), T(1.0)} {}
	
	constexpr basic_quat(const Vector<T, 4>& v) : Vector<T, 4>(v) {}

	constexpr auto as_vector() const noexcept
	{
		return static_cast<const Vector<T, 4>&>(*this);
	}

	static constexpr auto normalize(const basic_quat<T>& quat) noexcept
	{
		return quat / quat.as_vector().magnitude();	
	}

	static constexpr auto identity() noexcept
	{
		return basic_quat<T>{Vector<T, 4>::basis(3)};
	}

	template <typename U>
	static constexpr auto from_axis_angle(const Vector<U, 3>& axis, U angle) noexcept
	{
		const auto ha = angle * U(0.5);
		return basic_quat<T>{axis * std::sin(ha), std::cos(ha)};
	}

	template <typename U>
	static constexpr auto from_euler(const Vector<U, 3>& euler) noexcept
	{
		const auto cy = std::cos(T(euler.x) * T(0.5));
		const auto sy = std::sin(T(euler.x) * T(0.5));
		const auto cp = std::cos(T(euler.y) * T(0.5));
		const auto sp = std::sin(T(euler.y) * T(0.5));
		const auto cr = std::cos(T(euler.z) * T(0.5));
		const auto sr = std::sin(T(euler.z) * T(0.5));

		return basic_quat<T>{Vector<T, 4>
		{
			sr * cp * cy - cr * sp * sy,
			cr * sp * cy + sr * cp * sy,
			cr * cp * sy - sr * sp * cy,
			cr * cp * cy + sr * sp * sy
		}};
	}

	static constexpr auto slerp(const basic_quat<T>& begin, const basic_quat<T>& target, T a)
	{
		basic_quat<T> z = target;
		auto dot = Vector<T, 4>::dot(begin, target);
		if(dot < 0.0f)
		{
			z = -target;
			dot = -dot;
		}

		auto threshold = T(0.9995);
		if(dot > threshold)
		{
			return basic_quat<T>
			{
				mix(begin.x, z.x, a),
				mix(begin.y, z.y, a),
				mix(begin.z, z.z, a),
				mix(begin.w, z.w, a)
			};
		}
		else
		{
			T angle = std::acos(dot);
			return basic_quat<T>{(begin * std::sin((T(1) - a) * angle) + z * std::sin(a * angle)) / std::sin(angle)};
		}
	}

	//FIXME: make less explicit?
	static constexpr auto make_mat3(const basic_quat<T>& quat) noexcept
	{
		return Matrix<T, 3, 3>
                {
                        Vector<T, 3>{
                                T(1.0) - T(2.0) * quat.y * quat.y - T(2.0) * quat.z * quat.z,
                                T(2.0) * quat.x * quat.y - T(2.0) * quat.z * quat.w,
                                T(2.0) * quat.x * quat.z + T(2.0) * quat.y * quat.w
                        },
                        Vector<T, 3>{
                                T(2.0) * quat.x * quat.y + T(2.0) * quat.z * quat.w,
                                T(1.0) - T(2.0) * quat.x * quat.x - T(2.0) * quat.z * quat.z,
                                T(2.0) * quat.y * quat.z - T(2.0) * quat.x * quat.w
                        },
                        Vector<T, 3>{
                                T(2.0) * quat.x * quat.z - T(2.0) * quat.y * quat.w,
                                T(2.0) * quat.y * quat.z + T(2.0) * quat.x * quat.w,
                                T(1.0) - T(2.0) * quat.x * quat.x - T(2.0) * quat.y * quat.y
                        }
		};
	}

	static constexpr auto make_mat4(const basic_quat<T>& quat) noexcept
	{
		return Matrix<T, 4, 4>
		{
			Vector<T, 4>{
				T(1.0) - T(2.0) * quat.y * quat.y - T(2.0) * quat.z * quat.z,
				T(2.0) * quat.x * quat.y - T(2.0) * quat.z * quat.w,
				T(2.0) * quat.x * quat.z + T(2.0) * quat.y * quat.w,
				T(0.0)
			},
			Vector<T, 4>{
				T(2.0) * quat.x * quat.y + T(2.0) * quat.z * quat.w,
				T(1.0) - T(2.0) * quat.x * quat.x - T(2.0) * quat.z * quat.z,
				T(2.0) * quat.y * quat.z - T(2.0) * quat.x * quat.w,
				T(0.0)
			},
			Vector<T, 4>{
				T(2.0) * quat.x * quat.z - T(2.0) * quat.y * quat.w,
				T(2.0) * quat.y * quat.z + T(2.0) * quat.x * quat.w,
				T(1.0) - T(2.0) * quat.x * quat.x - T(2.0) * quat.y * quat.y,
				T(0.0)
			},
			Vector<T, 4>{
				T(0.0), T(0.0), T(0.0), T(1.0)
			}
		};
	}
};

template <typename T>
constexpr auto operator~(const basic_quat<T>& q) noexcept
{
	return basic_quat<T>{-q.x, -q.y, -q.z, q.w};
}

template <typename T, typename U>
constexpr bool operator==(const basic_quat<T>& lhs, const basic_quat<U>& rhs) noexcept
{
	return lhs.as_vector() == rhs.as_vector();
}

template <typename T, typename U>
constexpr auto operator+(const basic_quat<T>& lhs, const basic_quat<U>& rhs) noexcept
{
	return basic_quat<decltype(lhs.x + rhs.x)>{lhs.as_vector() + rhs.as_vector()};
}

template <typename T, typename U>
constexpr auto operator-(const basic_quat<T>& lhs, const basic_quat<U>& rhs) noexcept
{
	return basic_quat<decltype(lhs.x + rhs.x)>{lhs.as_vector() - rhs.as_vector()};
}

template <typename T, typename U>
constexpr auto operator*(const basic_quat<T>& lhs, const basic_quat<U>& rhs) noexcept
{
	return basic_quat<decltype(lhs.x + rhs.x)>
	{
		lhs.w * rhs.x + lhs.x * rhs.w + lhs.y * rhs.z - lhs.z * rhs.y,
		lhs.w * rhs.y + lhs.y * rhs.w + lhs.z * rhs.x - lhs.x * rhs.z,
		lhs.w * rhs.z + lhs.z * rhs.w + lhs.x * rhs.y - lhs.y * rhs.x,
		lhs.w * rhs.w - lhs.x * rhs.x - lhs.y * rhs.y - lhs.z * rhs.z
	};
}

using Quaternion = basic_quat<float>;

}

export template <typename T>
struct std::formatter<lumina::basic_quat<T>> : std::formatter<lumina::Vector<T, 4, std::make_index_sequence<4>>>
{
};
