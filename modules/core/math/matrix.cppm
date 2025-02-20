export module lumina.core.math:matrix;

export import :vector;
import std;

export namespace lumina 
{

template <typename T, size_t M, size_t N>
struct Matrix : private Vector<Vector<T, N>, M>
{
public:
	using Vector<Vector<T, N>, M>::Vector;
	constexpr Matrix(const Vector<Vector<T, N>, M>& v) : Vector<Vector<T, N>, M>(v) {}

	auto& operator[](size_t i) noexcept { return this->data[i]; }
	constexpr const auto& operator[](size_t i) const noexcept { return this->data[i]; }

	auto& row(size_t i) noexcept { return this->data[i]; }
	constexpr const auto& row(size_t i) const noexcept { return this->data[i]; }

	constexpr auto column(size_t i) const noexcept
	{
		return get_column_vector(i, std::make_index_sequence<M>{});
	}

	constexpr auto& as_vectors() const noexcept
	{
		return static_cast<const Vector<Vector<T, N>, M>&>(*this);
	}

	auto& as_vectors() noexcept
	{
		return static_cast<Vector<Vector<T, N>, M>&>(*this);
	}

	template <typename U>
	auto& operator+=(const Matrix<U, M, N>& m) noexcept
	{
		as_vectors() += m.as_vectors();
		return *this;
	}

	template <typename U>
	auto& operator-=(const Matrix<U, M, N>& m) noexcept
	{
		as_vectors() -= m.as_vectors();
		return *this;
	}

	template <typename U>
	auto& operator*=(const Matrix<U, M, N>& m) noexcept
	{
		as_vectors() *= m.as_vectors();
		return *this;
	}

	template <typename U>
	auto& operator/=(const Matrix<U, M, N>& m)
	{
		as_vectors() /= m.as_vectors();
		return *this;
	}

	static constexpr auto transpose(const Matrix<T, M, N>& m) noexcept
	{
		return transpose(m, std::make_index_sequence<N>{});
	}

	static constexpr auto identity() noexcept
	{
		return identity(std::make_index_sequence<M>{});
	}

	static constexpr auto scalar_mul(Matrix<T, M, N> mat, T s) noexcept
	{
		for(int i = 0; i < M; i++)
			for(int j = 0; j < N; j++)
				mat[i][j] *= s;

		return mat;
	}

	static constexpr auto make_translation(const Vector<T, 3>& tv) noexcept
	{
		return Matrix<T, 4, 4>
		{
			Vector<T, 4>::basis(0), Vector<T, 4>::basis(1), Vector<T, 4>::basis(2),
			Vector<T, 4>{tv, T(1.0)}
		};
	}

	static constexpr auto make_rotX(T ang) noexcept
	{
		return Matrix<T, 4, 4>
		{
			Vector<T, 4>::basis(0),
			Vector<T, 4>{T(0), std::cos(ang), std::sin(ang), T(0)},
			Vector<T, 4>{T(0), -std::sin(ang), std::cos(ang), T(0)},
			Vector<T, 4>::basis(3)
		};
	}

	static constexpr auto make_rotY(T ang) noexcept
	{
		return Matrix<T, 4, 4>
		{
			Vector<T, 4>{std::cos(ang), T(0), -std::sin(ang), T(0)},
			Vector<T, 4>::basis(1),
			Vector<T, 4>{std::sin(ang), T(0), std::cos(ang), T(0)},
			Vector<T, 4>::basis(3)
		};
	}

	//FIXME: do gauss-jordan elimination not this garbage
	// only used for inertia tensor right now

	static constexpr auto inverse(const Matrix<T, 3, 3>& mat) noexcept
	{
		T det = mat[0][0] * (mat[1][1] * mat[2][2] - mat[2][1] * mat[1][2]) -
			mat[0][1] * (mat[1][0] * mat[2][2] - mat[1][2] * mat[2][0]) +
			mat[0][2] * (mat[1][0] * mat[2][1] - mat[1][1] * mat[2][0]);

		T invd = T(1.0) / det;

		return Matrix<T, 3, 3>
		{
			Vector<T, 3>
			{
				(mat[1][1] * mat[2][2] - mat[2][1] * mat[1][2]),
				(mat[0][2] * mat[2][1] - mat[0][1] * mat[2][2]),
				(mat[0][1] * mat[1][2] - mat[0][2] * mat[1][1]),
			} * invd,
			Vector<T, 3>
			{
				(mat[1][2] * mat[2][0] - mat[1][0] * mat[2][2]),
				(mat[0][0] * mat[2][2] - mat[0][2] * mat[2][0]),
				(mat[1][0] * mat[0][2] - mat[0][0] * mat[1][2])
			} * invd,
			Vector<T, 3>
			{
				(mat[1][0] * mat[2][1] - mat[2][0] * mat[1][1]),
				(mat[2][0] * mat[0][1] - mat[0][0] * mat[2][1]),
				(mat[0][0] - mat[1][1] - mat[1][0] * mat[0][1])
			} * invd
		};
	}

	static constexpr auto inverse(const Matrix<T, 4, 4>& m) noexcept
	{
		Matrix<T, 4, 4> result;

		Vector<T, 4> even{T(1.0), T(-1.0), T(1.0), T(-1.0)};
		Vector<T, 4> odd{T(-1.0), T(1.0), T(-1.0), T(1.0)};
		Vector<T, 4> even_pair{T(1.0), T(-1.0), T(-1.0), T(1.0)};
		Vector<T, 4> odd_pair{T(-1.0f), T(1.0), T(1.0), T(-1.0)};

		const Vector<T, 4>& r0 = m[0];
		const Vector<T, 4>& r1 = m[1];
		const Vector<T, 4>& r2 = m[2];
		const Vector<T, 4>& r3 = m[3];

		Vector<T, 4> r0_wwwz{r0.w, r0.w, r0.w, r0.z};
	       	Vector<T, 4> r0_yxxx{r0.y, r0.x, r0.x, r0.x};
		Vector<T, 4> r0_zzyy{r0.z, r0.z, r0.y, r0.y};
		Vector<T, 4> r1_wwwz{r1.w, r1.w, r1.w, r1.z};
	       	Vector<T, 4> r1_yxxx{r1.y, r1.x, r1.x, r1.x};
		Vector<T, 4> r1_zzyy{r1.z, r1.z, r1.y, r1.y};
		Vector<T, 4> r2_wwwz{r2.w, r2.w, r2.w, r2.z};
	       	Vector<T, 4> r2_yxxx{r2.y, r2.x, r2.x, r2.x};
		Vector<T, 4> r2_zzyy{r2.z, r2.z, r2.y, r2.y};
		Vector<T, 4> r3_wwwz{r3.w, r3.w, r3.w, r3.z};
	       	Vector<T, 4> r3_yxxx{r3.y, r3.x, r3.x, r3.x};
		Vector<T, 4> r3_zzyy{r3.z, r3.z, r3.y, r3.y};

		Vector<T, 4> r0_wwwz_r1_yxxx = r0_wwwz * r1_yxxx;
		Vector<T, 4> r0_wwwz_r1_zzyy = r0_wwwz * r1_zzyy;
		Vector<T, 4> r0_yxxx_r1_wwwz = r0_yxxx * r1_wwwz;
		Vector<T, 4> r0_yxxx_r1_zzyy = r0_yxxx * r1_zzyy;
		Vector<T, 4> r0_zzyy_r1_wwwz = r0_zzyy * r1_wwwz;
		Vector<T, 4> r0_zzyy_r1_yxxx = r0_zzyy * r1_yxxx;

		Vector<T, 4> r2_wwwz_r3_yxxx = r2_wwwz * r3_yxxx;
		Vector<T, 4> r2_wwwz_r3_zzyy = r2_wwwz * r3_zzyy;
		Vector<T, 4> r2_yxxx_r3_wwwz = r2_yxxx * r3_wwwz;
		Vector<T, 4> r2_yxxx_r3_zzyy = r2_yxxx * r3_zzyy;
		Vector<T, 4> r2_zzyy_r3_wwwz = r2_zzyy * r3_wwwz;
		Vector<T, 4> r2_zzyy_r3_yxxx = r2_zzyy * r3_yxxx;

		Vector<T, 4> c0 = odd * 
		(
			r1_wwwz * r2_zzyy_r3_yxxx - 
			r1_zzyy * r2_wwwz_r3_yxxx -
			r1_wwwz * r2_yxxx_r3_zzyy +
			r1_yxxx * r2_wwwz_r3_zzyy +
			r1_zzyy * r2_yxxx_r3_wwwz -
			r1_yxxx * r2_zzyy_r3_wwwz
		);

		Vector<T, 4> c1 = even *
		(
			r0_wwwz * r2_zzyy_r3_yxxx -
			r0_zzyy * r2_wwwz_r3_yxxx -
			r0_wwwz * r2_yxxx_r3_zzyy +
			r0_yxxx * r2_wwwz_r3_zzyy +
		       	r0_zzyy * r2_yxxx_r3_wwwz - 
			r0_yxxx * r2_zzyy_r3_wwwz
		);

		Vector<T, 4> c2 = odd *
		(
			r0_wwwz_r1_zzyy * r3_yxxx -
			r0_zzyy_r1_wwwz * r3_yxxx -
			r0_wwwz_r1_yxxx * r3_zzyy +
			r0_yxxx_r1_wwwz * r3_zzyy +
			r0_zzyy_r1_yxxx * r3_wwwz - 
			r0_yxxx_r1_zzyy * r3_wwwz
		);

		Vector<T, 4> c3 = even *
		(
			r0_wwwz_r1_zzyy * r2_yxxx -
			r0_zzyy_r1_wwwz * r2_yxxx -
			r0_wwwz_r1_yxxx * r2_zzyy +
			r0_yxxx_r1_wwwz * r2_zzyy +
			r0_zzyy_r1_yxxx * r2_wwwz -
			r0_yxxx_r1_zzyy * r2_wwwz
		);

		result[0] = {c0[0], c1[0], c2[0], c3[0]};
		result[1] = {c0[1], c1[1], c2[1], c3[1]};
		result[2] = {c0[2], c1[2], c2[2], c3[2]};
		result[3] = {c0[3], c1[3], c2[3], c3[3]};

		Vector<T, 4> r2_zwzw{r2.z, r2.w, r2.z, r2.w};
		Vector<T, 4> r0_yyxx{r0.y, r0.y, r0.x, r0.x};
		Vector<T, 4> r1_wwxy{r1.w, r1.w, r1.x, r1.y};
		Vector<T, 4> r2_xyzz{r2.x, r2.y, r2.z, r2.z};
		Vector<T, 4> r3_wwww{r3.w, r3.w, r3.w, r3.w};
		Vector<T, 4> r1_zzxy{r1.z, r1.z, r1.x, r1.y};
		Vector<T, 4> r0_yxyx{r0.y, r0.x, r0.y, r0.x};
		Vector<T, 4> r3_xxyy{r3.x, r3.x, r3.y, r3.y};
		Vector<T, 4> r1_wzwz{r1.w, r1.z, r1.w, r1.z};
		Vector<T, 4> r2_xyww{r2.x, r2.y, r2.w, r2.w};
		Vector<T, 4> r3_zzzz{r3.z, r3.z, r3.z, r3.z};

		Vector<T, 3> r2_yxz{r2.y, r2.x, r2.z};
		Vector<T, 3> r3_xzy{r3.x, r3.z, r3.y};
		Vector<T, 3> r2_xzy{r2.x, r2.z, r2.y};
		Vector<T, 3> r3_yxz{r3.y, r3.x, r3.z};
		Vector<T, 3> r2_yxw{r2.y, r2.x, r2.w};
		Vector<T, 3> r1_zyx{r2.z, r2.y, r2.x};
		Vector<T, 3> r3_yxw{r3.y, r3.x, r3.w};
		Vector<T, 3> r2_xwy{r2.x, r2.w, r2.y};
		Vector<T, 3> r3_xwy{r3.x, r3.w, r3.y};
		Vector<T, 3> r1_wyx{r1.w, r1.y, r1.x};
		
		T r0_w = r0.w;
		T r0_z = r0.z;

		T det = Vector<T, 4>::dot(even_pair, r0_yyxx * r1_wzwz * r2_zwzw * r3_xxyy) +
			Vector<T, 4>::dot(odd_pair, r0_yxyx * r1_wwxy * r2_xyww * r3_zzzz) +
			Vector<T, 4>::dot(even_pair, r0_yxyx * r1_zzxy * r2_xyzz * r3_wwww) +
			(r0_w * Vector<T, 3>::dot(r1_zyx, r2_yxz * r3_xzy - r2_xzy * r3_yxz)) +
			(r0_z * Vector<T, 3>::dot(r1_wyx, r2_xwy * r3_yxw - r2_yxw * r3_xwy));

		T invdet = T(1.0) / det;
		
		result[0] *= invdet;
		result[1] *= invdet;
		result[2] *= invdet;
		result[3] *= invdet;

		return result;
	}

	static constexpr auto make_scale(const Vector<T, 3>& sv) noexcept
	{
		return Matrix<T, 4, 4>
		{
			Vector<T, 4>::basis(0) * sv.x,
			Vector<T, 4>::basis(1) * sv.y,
			Vector<T, 4>::basis(2) * sv.z,
			Vector<T, 4>::basis(3)
		};
	}

	static constexpr auto make_ortho(float left, float right, float bottom, float top, float near, float far) noexcept
	{
		const float wp = right + left;
		const float wn = right - left;
		const float hp = bottom + top;
		const float hn = bottom - top;
		const float f = 1.0f / (near - far);

		return Matrix<T, 4, 4>
		{
			Vector<T, 4>::basis(0) * 2.0f / wn,
			Vector<T, 4>::basis(1) * 2.0f / hn,
			Vector<T, 4>::basis(2) * f,
			Vector<T, 4>{-wp / wn, -hp / hn, near * f, 1.0f}
		};
	}

	template <size_t Num>
	constexpr auto demote()
	{
		static_assert(N == M);
		static_assert(Num < N);

		return [this]<size_t... Is>(std::index_sequence<Is...>)
		{
			return Matrix<T, Num, Num>
			{
				this->data[Is].template demote<Num>()...
			};
		}(std::make_index_sequence<Num>{});
	}
private:
	template <size_t... Is>
	constexpr auto get_column_vector(size_t i, std::index_sequence<Is...>) const noexcept
	{
		return Vector<T, M>{this->data[Is][i]...};
	}

	template <size_t... Is>
	static constexpr auto transpose(const Matrix<T, M, N>& m, std::index_sequence<Is...>) noexcept
	{
		return Matrix<T, N, M>{m.column(Is)...};
	}

	template <size_t... Is>
	static constexpr auto identity(std::index_sequence<Is...>) noexcept
	{
		return Matrix<T, sizeof...(Is), N>{Vector<T,N>::basis(Is)...};
	}
};

template <typename T, typename U, size_t M, size_t N>
constexpr auto operator==(const Matrix<T, M, N>& lhs, const Matrix<U, M, N>& rhs) noexcept
{
	return lhs.as_vectors() == rhs.as_vectors();
}

template <typename T, size_t M, size_t N>
constexpr auto operator+(const Matrix<T, M, N>& m) noexcept
{
	return m;
}

template <typename T, size_t M, size_t N>
constexpr auto operator-(const Matrix<T, M, N>& m) noexcept
{
	return Matrix<T, M, N>{-m.as_vector()};
}

template <typename T, typename U, size_t M, size_t N>
constexpr auto operator+(const Matrix<T, M, N>& lhs, const Matrix<U, M, N>& rhs) noexcept
{
	return Matrix<T, M, N>{lhs.as_vectors() + rhs.as_vectors()};
}

template <typename T, typename U, size_t M, size_t N>
constexpr auto operator-(const Matrix<T, M, N>& lhs, const Matrix<U, M, N>& rhs) noexcept
{
	return Matrix<T, M, N>{lhs.as_vectors() - rhs.as_vectors()};
}

template <typename T, size_t M, size_t N>
constexpr auto operator*(const Matrix<T, M, N>& m, T s) noexcept
{
	return Matrix<T, M, N>{m.as_vectors() * s};
}

template <typename T, size_t M, size_t N>
constexpr auto operator*(T s, const Matrix<T, M, N>& m) noexcept
{
	return m * s;
}

template <typename T, size_t M, size_t N, size_t... Is>
constexpr auto operator*(const Vector<T, M, std::index_sequence<Is...>>& v, const Matrix<T, M, N>& m) noexcept
{
	return Vector<T, N>{(Vector<T, M>::dot(v, m.column(Is)))...};
}

template <typename T, typename U, size_t M, size_t N, size_t P>
inline auto operator*(const Matrix<T, M, N>& lhs, const Matrix<U, N, P>& rhs) noexcept
{
	Matrix<T, M, P> res;
	for(size_t r = 0; r < M; r++)
		res[r] = lhs[r] * rhs;

	return res;
}

template <typename T, size_t M, size_t N>
std::ostream& operator<<(std::ostream& os, const Matrix<T, M, N>& m)
{
	for(size_t i = 0; i < M; i++)
		os << m[i];

	return os;
}


using mat3 = Matrix<float, 3, 3>;
using mat4 = Matrix<float, 4, 4>;

}

export template <typename T, size_t M, size_t N>
struct std::formatter<lumina::Matrix<T, M, N>>
{
	template <class ParseContext>
	constexpr ParseContext::iterator parse(ParseContext& ctx)
	{
		return ctx.begin();
	}

	template <class FmtContext>
	FmtContext::iterator format(const lumina::Matrix<T, M, N>& m, FmtContext& ctx) const
	{
		std::format_to(ctx.out(), "\n[\n");
		for(auto i = 0; i < M; i++)
		{
			std::format_to(ctx.out(), "\t{}\n", m[i]);	
		}
		std::format_to(ctx.out(), "]");

		return ctx.out();
	}
};

