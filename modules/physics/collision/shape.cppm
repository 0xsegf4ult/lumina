export module lumina.physics.collision:shape;

import lumina.core;

export namespace lumina::physics
{

enum class CShapeType
{
	Sphere,
	Capsule,
	ConvexHull,
	Mesh,
	Heightfield,
	Compound
};

struct CShapeDescription
{
	float density = 1.0f;
};

class CShape : public RefCountEnabled<CShape>
{
public:
	CShape() = default;
	CShape(CShapeType t) : type{t} {}

	virtual ~CShape()
	{

	}

	virtual vec3 get_support(const vec3& dir) const = 0;
	virtual float get_convex_radius() const = 0;

	constexpr CShapeType get_type() const noexcept { return type; }
	constexpr AABB get_bounds() const noexcept { return bounds; }
	constexpr float get_density() const noexcept { return density; }
	constexpr float get_mass() const noexcept { return mass; }
	constexpr mat3 get_inertia_tensor() const noexcept { return inertia_tensor; }
protected:
	CShapeType type;
	AABB bounds;
	float density{1.0f};
	float mass{1.0f};
	mat3 inertia_tensor;
};

}

