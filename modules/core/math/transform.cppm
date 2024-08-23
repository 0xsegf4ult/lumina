export module lumina.core.math:transform;

export import :vector;
export import :matrix;
export import :quaternion;

import std;

export namespace lumina 
{

template <typename T>
struct basic_transform
{
	constexpr basic_transform() noexcept : translation{T(0.0)}, rotation{basic_quat<T>::identity()}, scale{T(1.0)} {}
	constexpr basic_transform(Vector<T, 3> _t, basic_quat<T> _r, Vector<T, 3> _s) noexcept : translation{_t}, rotation{_r}, scale{_s} {}
		
	constexpr basic_transform<T>& translate(const Vector<T, 3>& pos) noexcept
	{
		translation += pos;
		return *this;
	}

	constexpr basic_transform<T>& set_scale(const Vector<T, 3>& v) noexcept
	{
		scale = v;
		return *this;
	}
	Matrix<T, 4, 4> as_matrix() const noexcept
	{
		return Matrix<T, 4, 4>::make_scale(scale) * Matrix<T, 4, 4>::transpose(basic_quat<float>::make_mat4(rotation)) * Matrix<T, 4, 4>::make_translation(translation);
	}
	
	Vector<T, 3> translation{0.0f};
	basic_quat<float> rotation{0.0f, 0.0f, 0.0f, 1.0f};
	Vector<T, 3> scale{1.0f};
};

template <typename T, typename U>
constexpr auto operator *(const basic_transform<T>& t1, const basic_transform<U>& t2) noexcept
{
	basic_transform<T> xform;
	xform.translation = t1.translation + t2.translation;
	xform.rotation = t1.rotation * t2.rotation; 
	xform.scale = Vector<T, 3>::scalar_mul(t1.scale, t2.scale);
	return xform;
}

template <typename T>
constexpr std::tuple<Vector<T, 3>, basic_quat<T>, Vector<T, 3>> decompose(Matrix<T, 4, 4> mat) noexcept
{
	const Vector<T, 4> t4 = mat.row(3u);
	const Vector<T, 3> translation = {t4.x, t4.y, t4.z};

	const Vector<T, 3> scale = {mat.row(0u).magnitude(), mat.row(1u).magnitude(), mat.row(2u).magnitude()};

	mat[0][0] /= scale.x;
	mat[0][1] /= scale.y;
	mat[0][2] /= scale.z;
	mat[1][0] /= scale.x;
	mat[1][1] /= scale.y;
	mat[1][2] /= scale.z;
	mat[2][0] /= scale.x;
	mat[2][1] /= scale.y;
	mat[2][2] /= scale.z;

	mat[0][3] = 0.0f;
	mat[1][3] = 0.0f;
	mat[2][3] = 0.0f;

	basic_quat<T> rotation;

	const auto trace = mat[0][0] + mat[1][1] + mat[2][2];
	float root = 0.0f;

	if(trace > 0.0f)
	{
		root = std::sqrt(trace + 1.0f);
		rotation.w = 0.5f * root;
		root = 0.5f / root;
		rotation.x = (mat[2][1] - mat[1][2]) * root;
		rotation.y = (mat[0][2] - mat[2][0]) * root;
		rotation.z = (mat[1][0] - mat[0][1]) * root;
	}
	else
	{
		static size_t next[3] = {1, 2, 0};
		size_t i = 0;

		if(mat[1][1] > mat[0][0])
			i = 1;

		if(mat[2][2] > mat[i][i])
			i = 2;

		const auto j = next[i];
		const auto k = next[j];

		root = std::sqrt(mat[i][i] - mat[j][j] - mat[k][k] + 1.0f);
		
		rotation[i] = 0.5f * root;
		root = 0.5f / root;
		rotation.w = (mat[k][j] - mat[j][k]) * root;
		rotation[j] = (mat[j][i] + mat[i][j]) * root;
		rotation[k] = (mat[k][i] + mat[i][k]) * root;
	}
	
	return {translation, rotation, scale};
}

using Transform = basic_transform<float>;

}

export template <typename T>
struct std::formatter<lumina::basic_transform<T>>
{
	template <class ParseContext>
	constexpr ParseContext::iterator parse(ParseContext& ctx)
	{
		return ctx.begin();
	}

	template <class FmtContext>
	FmtContext::iterator format(const lumina::basic_transform<T>& t, FmtContext& ctx) const
	{
		std::format_to(ctx.out(), "\n[\n");
		std::format_to(ctx.out(), "{}\n{}\n{}\n]", t.translation, t.rotation, t.scale);
		return ctx.out();
	}
};


