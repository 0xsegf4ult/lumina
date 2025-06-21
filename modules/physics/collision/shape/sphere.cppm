export module lumina.physics.collision:sphere;

import :shape;
import lumina.core;
import std;

export namespace lumina::physics
{

struct SphereShapeDescription : public CShapeDescription
{
	float radius;
};

class SphereShape final : public CShape
{
public:
	SphereShape() : CShape(CShapeType::Sphere) {}
	
	static RefCounted<CShape> create(const SphereShapeDescription& desc)
	{
		auto* col = new SphereShape();
		col->bounds.mins = vec3{-desc.radius};
		col->bounds.maxs = vec3{desc.radius};
		col->radius = desc.radius;
		col->density = desc.density;

		const float r2 = desc.radius * desc.radius;
		const float volume = (4.0f / 3.0f) * r2 * desc.radius * std::numbers::pi_v<float>;
		const float mass = volume * desc.density;
		col->mass = mass;

		float inertia = (2.0f / 5.0f) * mass * r2;
		col->inertia_tensor = mat3
		{
			vec3::basis(0) * inertia,
			vec3::basis(1) * inertia,
			vec3::basis(2) * inertia
		};

		return RefCounted<CShape>{static_cast<CShape*>(col)};
	}

	// treat sphere as point to optimize GJK
	
	vec3 get_support([[maybe_unused]]const vec3& dir) const override
	{
		return vec3{0.0f};
	}

	float get_convex_radius() const override
	{
		return radius;
	}

	constexpr float get_radius() const noexcept { return radius; }
private:
	float radius;
};

}

