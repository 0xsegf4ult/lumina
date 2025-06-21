export module lumina.physics.collision:capsule;

import :shape;
import lumina.core;
import std;

export namespace lumina::physics
{

struct CapsuleShapeDescription : public CShapeDescription
{
	float radius;
	float height;
};

class CapsuleShape final : public CShape
{
public:
	CapsuleShape() : CShape(CShapeType::Capsule) {}
	
	static RefCounted<CShape> create(const CapsuleShapeDescription& desc)
	{
		auto* col = new CapsuleShape();
		col->bounds.mins = vec3{-desc.radius, -desc.radius - (desc.height * 0.5f), -desc.radius};
		col->bounds.maxs = vec3{desc.radius, desc.radius + (desc.height * 0.5f), desc.radius};
		col->radius = desc.radius;
		col->height = desc.height;
		col->density = desc.density;

		const float r2 = desc.radius * desc.radius;
		const float cm = desc.density * desc.height * r2 * std::numbers::pi_v<float>;
		const float hm = (2.0f / 3.0f) * std::numbers::pi_v<float> * r2 * desc.radius * desc.density;
		col->mass = cm + (2.0f * hm);

		const float h2 = desc.height * desc.height;
		const float inertia_y = (cm * r2 * 0.5f) + ((2.0f * hm) * (2.0f * r2 / 5.0f));
		const float inertia_xz = (cm * (h2 / 12.0f + r2 / 4.0f)) + ((2.0f * hm) * ((2.0f * r2 / 5.0f) + (h2 / 4.0f) + (3.0f * desc.height * desc.radius / 8.0f)));

		col->inertia_tensor = mat3
		{
			vec3::basis(0) * inertia_xz,
			vec3::basis(1) * inertia_y,
			vec3::basis(2) * inertia_xz
		};

		return RefCounted<CShape>{static_cast<CShape*>(col)};
	}

	// treat capsule as line segment to optimize GJK

	vec3 get_support(const vec3& dir) const override
	{
		const float hh = height * 0.5f;
		return vec3{0.0f, (dir.y > 0.0f) ? hh : -hh, 0.0f};
	}

	float get_convex_radius() const override
	{
		return radius;
	}

	constexpr float get_radius() const noexcept { return radius; }
	constexpr float get_height() const noexcept { return height; }
private:
	float radius;
	float height;
};

}

