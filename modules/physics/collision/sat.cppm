module;

#include <cassert>

export module lumina.physics.collision:sat;

import :convex_hull;
import lumina.core;
import std;

using std::uint32_t, std::size_t;

namespace lumina::physics
{

export struct satQueryConfiguration
{
	const CHullShape& hull_a;
	const CHullShape& hull_b;
	Transform transform_a;
	Transform transform_b;
};

struct face_query
{
	float separation;
	size_t plane_index;
};

face_query query_faces(const satQueryConfiguration& cfg)
{
	float max_sep = std::numeric_limits<float>::lowest();
	size_t max_ind = 0;

	mat4 transform_1_to_2 = cfg.transform_a.as_matrix() * cfg.transform_b.as_inverse_translation_rotation();

	log::debug("xform: {}", transform_1_to_2[3].demote<3>());

	for(size_t i = 0; i < cfg.hull_a.get_planes().size(); i++)
	{
		Plane bplane = cfg.hull_a.get_planes()[i];

		vec3 nrm = (bplane.normal() * transform_1_to_2.demote<3>()).normalize();
		Plane plane{nrm, bplane.d + vec3::dot(nrm, transform_1_to_2[3].demote<3>())};
		vec3 support = cfg.hull_b.get_support(-plane.normal());

		float separation = Plane::distance(plane, support);

		log::debug("sep: {}", separation);
		if(separation > max_sep)
		{
			max_sep = separation;
			max_ind = i;
		}
	}

	return {max_sep, max_ind};
}

struct edge_query
{
	float separation;
	size_t edge_index_a;
	size_t edge_index_b;
};

bool is_minkowski_face(const vec3& a, const vec3& b, const vec3& BxA, const vec3& c, const vec3& d, const vec3& DxC)
{
	float cba = vec3::dot(c, BxA);
	float dba = vec3::dot(d, BxA);
	float adc = vec3::dot(a, DxC);
	float bdc = vec3::dot(b, DxC);

	return cba * dba < 0.0f && adc * bdc < 0.0f && cba * bdc > 0.0f;
}

float edge_project(const vec3& p1, const vec3& e1, const vec3& p2, const vec3& e2, const vec3& c1)
{
	vec3 search = vec3::cross(e1, e2);

	const float kTol = 0.005f;

	float len = search.magnitude();
	if(len < kTol * std::sqrt(e1.magnitude_sqr() * e2.magnitude_sqr()))
		return std::numeric_limits<float>::lowest();

	log::debug("edge_project len: {}", len);
	vec3 n = len > 0.0f ? search / len : vec3{0.0f};
	if(vec3::dot(n, p1 - c1) < 0.0f)
		n *= -1.0f;

	return vec3::dot(n, p2 - p1);
}

edge_query query_edges(const satQueryConfiguration& cfg)
{
	size_t max_ind_a = 0;
	size_t max_ind_b = 0;

	float max_sep = std::numeric_limits<float>::lowest();

	log::debug("ta: {}\ntb: {}", cfg.transform_a.as_matrix(), cfg.transform_b.as_inverse_translation_rotation());

	mat4 transform_1_to_2 = cfg.transform_a.as_matrix() * cfg.transform_b.as_inverse_translation_rotation();

	log::debug("ft: {}", transform_1_to_2);

	vec3 c1 = (vec4{0.0f, 0.0f, 0.0f, 1.0f} * transform_1_to_2).demote<3>();
	log::debug("hull centroid: {}", c1);

	for(size_t i = 0; i < cfg.hull_a.get_edges().size(); i += 2)
	{
		const halfedge& edge1 = cfg.hull_a.get_edges()[i];
		const halfedge& twin1 = cfg.hull_a.get_edges()[i + 1];

		vec3 p1 = (vec4{cfg.hull_a.get_vertices()[edge1.vertex], 1.0f} * transform_1_to_2).demote<3>();
		vec3 q1 = (vec4{cfg.hull_a.get_vertices()[twin1.vertex], 1.0f} * transform_1_to_2).demote<3>();
		vec3 e1 = q1 - p1;

		vec3 u1 = cfg.hull_a.get_planes()[edge1.face].normal() * transform_1_to_2.demote<3>();
		vec3 v1 = cfg.hull_a.get_planes()[twin1.face].normal() * transform_1_to_2.demote<3>();

		for(size_t j = 0; j < cfg.hull_b.get_edges().size(); j += 2)
		{
			const halfedge& edge2 = cfg.hull_b.get_edges()[j];
			const halfedge& twin2 = cfg.hull_b.get_edges()[j + 1];

			vec3 p2 = cfg.hull_b.get_vertices()[edge2.vertex];
			vec3 q2 = cfg.hull_b.get_vertices()[twin2.vertex];
			vec3 e2 = q2 - p2;

			vec3 u2 = cfg.hull_b.get_planes()[edge2.face].normal();
			vec3 v2 = cfg.hull_b.get_planes()[twin2.face].normal();

			if(is_minkowski_face(u1, v1, -e1, -u2, -v2, -e2))
			{
				float sep = edge_project(p1, e1, p2, e2, c1);
				log::debug("esep: {}", sep);
				if(sep > max_sep)
				{
					max_ind_a = i;
					max_ind_b = j;
					max_sep = sep;
				}	
			}
		}
	}

	return {max_sep, max_ind_a, max_ind_b};
}

constexpr float edgeRelTolerance = 0.90f;
constexpr float faceRelTolerance = 0.98f;
constexpr float absTolerance = 0.0025f;

export struct satQueryResult
{
	size_t ref_plane;
	size_t inc_plane;

	float penetration;
	bool flip_order = false;
	bool is_face = true;
};


export std::optional<satQueryResult> satQuery(const satQueryConfiguration& cfg)
{
	assert(cfg.hull_a.get_type() == CShapeType::ConvexHull);
	assert(cfg.hull_b.get_type() == CShapeType::ConvexHull);
	
	face_query fq0 = query_faces(cfg);
	if(fq0.separation > 0.0f)
		return std::nullopt;

	face_query fq1 = query_faces({cfg.hull_b, cfg.hull_a, cfg.transform_b, cfg.transform_a});
	if(fq1.separation > 0.0f)
		return std::nullopt;

	edge_query eq0 = query_edges(cfg);
	if(eq0.separation > 0.0f)
		return std::nullopt;

	if(eq0.separation > edgeRelTolerance * std::max(fq0.separation, fq1.separation) + absTolerance)
	{
		return satQueryResult{eq0.edge_index_a, eq0.edge_index_b, eq0.separation, false, false};
	}
	else
	{
		if(fq1.separation > faceRelTolerance * fq0.separation + absTolerance)
			return satQueryResult{fq1.plane_index, fq0.plane_index, std::min(fq0.separation, fq1.separation), true};
		else
			return satQueryResult{fq0.plane_index, fq1.plane_index, std::min(fq0.separation, fq1.separation), false};
	}
}

}
