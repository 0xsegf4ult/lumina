export module lumina.physics.collision:convex_hull;

import :shape;
import lumina.core;
import std;

export namespace lumina::physics
{

struct BoxShapeDescription : public CShapeDescription
{
	vec3 edges;
};

struct CHullShapeDescription : public CShapeDescription
{
	std::span<vec3> vertices;
};

struct halfedge
{
	int vertex;
	int face;
	int twin;
	int next;
};

class CHullShape final : public CShape
{
public:
	CHullShape() : CShape(CShapeType::ConvexHull) {}
	
	static RefCounted<CShape> create(const BoxShapeDescription& desc)
	{
		float xe = desc.edges.x * 0.5f;
		float ye = desc.edges.y * 0.5f;
		float ze = desc.edges.z * 0.5f;

		CHullShape* col = new CHullShape();
		col->bounds.mins = vec3{-xe, -ye, -ze};
		col->bounds.maxs = vec3{xe, ye, ze};
		col->density = desc.density;
		const float volume = desc.edges.x * desc.edges.y * desc.edges.z;
		const float mass = volume * desc.density;
		col->mass = mass;

		col->vertices[0] = vec3{-xe, ye, ze};
		col->vertices[1] = vec3{xe, -ye, ze};
		col->vertices[2] = vec3{-xe, -ye, ze};
		col->vertices[3] = vec3{xe, ye, ze};
		col->vertices[4] = vec3{-xe, -ye, -ze};
		col->vertices[5] = vec3{xe, -ye, -ze};
		col->vertices[6] = vec3{-xe, ye, -ze};
		col->vertices[7] = vec3{xe, ye, -ze};

		// might be wrong, check with world vectors
		 
		// top
		col->planes[0] = Plane(0.0f, -1.0f, 0.0f, ye);
		// front
		col->planes[1] = Plane(0.0f, 0.0f, -1.0f, ze);
		// left
		col->planes[2] = Plane(-1.0f, 0.0f, 0.0f, xe);
		// right
		col->planes[3] = Plane(1.0f, 0.0f, 0.0f, xe);
		// bottom
		col->planes[4] = Plane(0.0f, 1.0f, 0.0f, ye);
		// back
		col->planes[5] = Plane(0.0f, 0.0f, 1.0f, ze);

		col->plane_to_edge[0] = 8;
		col->plane_to_edge[1] = 21;
		col->plane_to_edge[2] = 1;
		col->plane_to_edge[3] = 17;
		col->plane_to_edge[4] = 0;
		col->plane_to_edge[5] = 7;

		// bottom
		col->half_edges[0] = halfedge{0, 4, 1, 2};
		col->half_edges[2] = halfedge{6, 4, 3, 4};
		col->half_edges[4] = halfedge{7, 4, 5, 6};
		col->half_edges[6] = halfedge{3, 4, 7, 0};

		// top
		col->half_edges[8] = halfedge{2, 0, 9, 10};
		col->half_edges[10] = halfedge{1, 0, 11, 12};
		col->half_edges[12] = halfedge{5, 0, 13, 14};
		col->half_edges[14] = halfedge{4, 0, 15, 8};

		// back
		col->half_edges[7] = {0, 5, 6, 16};
		col->half_edges[16] = {3, 5, 17, 9};
		col->half_edges[9] = {1, 5, 8, 18};
		col->half_edges[18] = {2, 5, 19, 7};

		// left
		col->half_edges[1] = halfedge{6, 2, 0, 19};
		col->half_edges[19] = halfedge{0, 2, 18, 15};
		col->half_edges[15] = halfedge{2, 2, 14, 20};
		col->half_edges[20] = halfedge{4, 2, 21, 1};

		const float ex2 = desc.edges.x * desc.edges.x;
		const float ey2 = desc.edges.y * desc.edges.y;
		const float ez2 = desc.edges.z * desc.edges.z;

		const float x = mass / 12.0f * (ey2 + ez2);
		const float y = mass / 12.0f * (ex2 + ez2);
		const float z = mass / 12.0f * (ex2 + ey2);

		col->inertia_tensor = mat3
		{
			vec3::basis(0) * x,
			vec3::basis(1) * y,
			vec3::basis(2) * z
		};

		return RefCounted<CShape>{static_cast<CShape*>(col)};
	}

	static RefCounted<CShape> create(const CHullShapeDescription& desc)
	{
		CHullShape* col = new CHullShape();
		col->density = desc.density;

		log::error("physics: unimplemented arbitrary convex hull shape!");

		return RefCounted<CShape>{static_cast<CShape*>(col)};
	}

	virtual vec3 get_support(const vec3& dir) const override
	{
		std::uint32_t index{0u};
		float maxproj = vec3::dot(dir, vertices[index]);
		for(std::uint32_t i = 1; i < std::uint32_t(vertices.size()); i++)
		{
			float proj = vec3::dot(dir, vertices[i]);
			if(proj > maxproj)
			{
				index = i;
				maxproj = proj;
			}
		}

		return vertices[index];
	}

	virtual float get_convex_radius() const override
	{
		return 0.0f;
	}

	std::span<const vec3> get_vertices() const
	{
		return {&vertices[0], 8};
	}

	std::span<const Plane> get_planes() const
	{
		return {&planes[0], 6};
	}

	std::span<const halfedge> get_edges() const
	{
		return {&half_edges[0], 24};
	}
private:
	std::array<vec3, 8> vertices;
	std::array<Plane, 6> planes;
	std::array<halfedge, 24> half_edges;
	std::array<std::uint32_t, 6> plane_to_edge;
};

}

