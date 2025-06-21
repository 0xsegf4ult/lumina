module;

#include <cassert>
#include <tracy/Tracy.hpp>

export module lumina.physics.collision:gjk;

import :shape;
import lumina.core;
import std;

using std::uint8_t, std::uint32_t;

namespace lumina::physics
{

// Christer Ericson - Real-Time Collision Detection - Chapters 3.4, 5.1

vec2 line_segment_to_barycentric(const vec3& a, const vec3& b)
{
	ZoneScoped;
	const vec3 ab = b - a;
	const float lensq = ab.magnitude_sqr();

	if(lensq > fp_epsilon)
	{
		const float v = vec3::dot(-a, ab) / lensq;
		return {1.0f - v, v};
	}

	if(a.magnitude_sqr() < b.magnitude_sqr())
		return {1.0f, 0.0f};
			
	return {0.0f, 1.0f};
}

vec3 triangle_to_barycentric(const vec3& a, const vec3& b, const vec3& c)
{
	ZoneScoped;
	const vec3 v0 = b - a;
	const vec3 v1 = c - a;
	const vec3 v2 = c - b;

	// always use the shortest edge for calculating barycentric coordinates
	// in case of degenerate triangles, calculate barycentrics for longest edge (line segment)

	const float bctri_eps = 1e-12f;

	const float d00 = v0.magnitude_sqr();
	const float d11 = v1.magnitude_sqr();
	const float d22 = v2.magnitude_sqr();
	
	if(d00 <= d22)
	{
		const float d01 = vec3::dot(v0, v1);
		const float denom = d00 * d11 - d01 * d01;
		if(denom < bctri_eps)
		{
			if(d00 > d11)
			{
				const vec2 bcoord = line_segment_to_barycentric(a, b);
				return {bcoord.x, bcoord.y, 0.0f};
			}

			const vec2 bcoord = line_segment_to_barycentric(a, c);
			return {bcoord.x, 0.0f, bcoord.y};
		}

		const float a0 = vec3::dot(a, v0);
		const float a1 = vec3::dot(a, v1);
		const float v = (d01 * a1 - d11 * a0) / denom;
		const float w = (d01 * a0 - d00 * a1) / denom;
		return {1.0f - v - w, v, w};
	}

	const float d12 = vec3::dot(v1, v2);
	const float denom = d11 * d22 - d12 * d12;
	if(denom < bctri_eps)
	{
		if(d11 > d22)
		{
			const vec2 bcoord = line_segment_to_barycentric(a, c);
			return {bcoord.x, 0.0f, bcoord.y};
		}

		const vec2 bcoord = line_segment_to_barycentric(b, c);
		return {0.0f, bcoord.x, bcoord.y};
	}

	const float c1 = vec3::dot(c, v1);
	const float c2 = vec3::dot(c, v2);
	const float u = (d22 * c1 - d12 * c2) / denom;
	const float v = (d11 * c2 - d12 * c1) / denom;
	return {u, v, 1.0f - u - v};
}

namespace SimplexVertex
{
	constexpr uint8_t A = (1 << 0);
	constexpr uint8_t B = (1 << 1);
	constexpr uint8_t C = (1 << 2);
	constexpr uint8_t D = (1 << 3);
}

struct SimplexSolution
{
	vec3 point;
	bool converge;
	uint8_t usable_vertices;
};

SimplexSolution simplex_solve2(const vec3& a, const vec3& b)
{
	ZoneScoped;
	const vec2 bcoord = line_segment_to_barycentric(a, b);
	if(bcoord.x <= 0.0f)
		return {b, true, SimplexVertex::B};
	if(bcoord.y <= 0.0f)
		return {a, true, SimplexVertex::A};
		
	return {(bcoord.x * a) + (bcoord.y * b), true, SimplexVertex::A | SimplexVertex::B};
}

SimplexSolution simplex_solve3(const vec3& v_a, const vec3& b, const vec3& v_c)
{
	ZoneScoped;

	// p = 0

	bool swap = false;
	{
		const vec3 ac = v_c - v_a;
		const vec3 bc = v_c - b;
		swap = vec3::dot(bc, bc) < vec3::dot(ac, ac);
	}

	const vec3 a = swap ? v_c : v_a;
	const vec3 c = swap ? v_a : v_c;

	const vec3 ab = b - a;
	const vec3 ac = c - a;
	const vec3 ap = -a;

	const vec3 n = vec3::cross(ab, ac);

	const float nrm_degen_eps = 1e-10f;
	if(n.magnitude_sqr() < nrm_degen_eps)
	{
		float best_dist_sq = v_c.magnitude_sqr();
		uint8_t closest_f = SimplexVertex::C;
		vec3 closest_p = v_c;
		
		auto check_point = [&best_dist_sq, &closest_f, &closest_p](const vec3& p, uint8_t fset)
		{
			const float lsq = p.magnitude_sqr();
			if(lsq < best_dist_sq)
			{
		
				best_dist_sq = lsq;
				closest_f = fset;
				closest_p = p;
			}
		};

		check_point(v_a, SimplexVertex::A);
		check_point(b, SimplexVertex::B);

		auto check_line = [&check_point, &best_dist_sq](const vec3& l, const vec3& p, uint8_t fset)
		{
			const float lsq = l.magnitude_sqr();
			if(lsq > fp_epsilon_sq)
			{
				const float v = std::clamp(vec3::dot(-p, l) / lsq, 0.0f, 1.0f);
				const vec3 q = p + v * l;
				check_point(q, fset);
			}
		};
		
		const vec3 bc = v_c - b;
		check_line(ac, a, SimplexVertex::A | SimplexVertex::C);
		check_line(bc, b, SimplexVertex::B | SimplexVertex::C);
		
		const vec3 v_ab = b - v_a;	
		check_line(v_ab, v_a, SimplexVertex::A | SimplexVertex::B);

		return {closest_p, true, closest_f};
	}

	// region A
	const float d1 = vec3::dot(ab, ap);
	const float d2 = vec3::dot(ac, ap);
	if(d1 <= 0.0f && d2 <= 0.0f)
		return {a, true, swap ? SimplexVertex::C : SimplexVertex::A};

	// region B
	const vec3 bp = -b;
	const float d3 = vec3::dot(ab, bp);
	const float d4 = vec3::dot(ac, bp);
	if(d3 >= 0.0f && d4 <= d3)
		return {b, true, SimplexVertex::B};

	// region AB
	if(d1 * d4 <= d3 * d2 && d1 >= 0.0f && d3 <= 0.0f)
	{
		const float v = d1 / (d1 - d3);
		return {a + (v * ab), true, static_cast<uint8_t>(swap ? (SimplexVertex::C | SimplexVertex::B) : (SimplexVertex::A | SimplexVertex::B))};
	}

	// region C
	const vec3 cp = -c;
	const float d5 = vec3::dot(ab, cp);
	const float d6 = vec3::dot(ac, cp);
	if(d6 >= 0.0f && d5 <= d6)
		return {c, true, swap ? SimplexVertex::A : SimplexVertex::C};

	// region AC
	if(d5 * d2 <= d1 * d6 && d2 >= 0.0f && d6 <= 0.0f)
	{
		const float w = d2 / (d2 - d6);
		return {a + (w * ac), true, SimplexVertex::A | SimplexVertex::C};
	}

	// region BC
	if(d3 * d6 <= d5 * d4 && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f)
	{
		const float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
		return {b + (w * (c - b)), true, static_cast<uint8_t>(swap ? (SimplexVertex::B | SimplexVertex::A) : (SimplexVertex::B | SimplexVertex::C))};
	}

	return {n * vec3::dot(a + b + c, n) / (3.0f * n.magnitude_sqr()), true, SimplexVertex::A | SimplexVertex::B | SimplexVertex::C};
}

bool point_outside_plane(const vec3& p, const vec3& r, const vec3& a, const vec3& b, const vec3& c)
{
	ZoneScoped;

	const vec3 nrm = vec3::cross(b - a, c - a);
	const float signp = vec3::dot(p - a, nrm);
	const float signd = vec3::dot(r - a, nrm);
	return signp * signd < -fp_epsilon;
}

uint8_t remap_vertices(uint8_t v, uint8_t a, uint8_t b, uint8_t c)
{
	return ((((1 & v) != 0) << a) | (((2 & v) != 0) << b) | (((4 & v) != 0) << c));
}

SimplexSolution simplex_solve4(const vec3& a, const vec3& b, const vec3& c, const vec3& d)
{
	ZoneScoped;
	
	uint8_t usable = 0b1111;
	vec3 closest_pt = vec3{0.0f};
	float best_sqdist = std::numeric_limits<float>::max();

	if(point_outside_plane(vec3{0.0f}, d, a, b, c))
	{
		auto [q, cv, u] = simplex_solve3(a, b, c);
		const float sqdist = q.magnitude_sqr();
		if(sqdist < best_sqdist)
		{
			best_sqdist = sqdist;
			closest_pt = q;
			usable = u;
		}
	}

	if(point_outside_plane(vec3{0.0f}, b, a, c, d))
	{
		auto [q, cv, u] = simplex_solve3(a, c, d);
		const float sqdist = q.magnitude_sqr();
		if(sqdist < best_sqdist)
		{
			best_sqdist = sqdist;
			closest_pt = q;
			usable = remap_vertices(u, 0, 2, 3);
		}
	}

	if(point_outside_plane(vec3{0.0f}, c, a, d, b))
	{
		auto [q, cv, u] = simplex_solve3(a, b, d);
		const float sqdist = q.magnitude_sqr();
		if(sqdist < best_sqdist)
		{
			best_sqdist = sqdist;
			closest_pt = q;
			usable = remap_vertices(u, 0, 1, 3);
		}
	}

	if(point_outside_plane(vec3{0.0f}, a, b, d, c))
	{
		auto [q, cv, u] = simplex_solve3(b, c, d);
		const float sqdist = q.magnitude_sqr();
		if(sqdist < best_sqdist)
		{
			closest_pt = q;
			usable = remap_vertices(u, 1, 2, 3);
		}
	}

	return {closest_pt, true, usable};
}

struct Simplex
{
	vec3 va[4];
	vec3 vb[4];
	vec3 vm[4];
	uint32_t num = 0;
	float len_sq = std::numeric_limits<float>::max();
};

SimplexSolution simplex_solve(Simplex& simplex)
{
	ZoneScoped;
	
	SimplexSolution res;

	switch(simplex.num)
	{
	case 1:
		res.point = simplex.vm[0];
		res.usable_vertices = SimplexVertex::A;
		break;
	case 2:
		res = simplex_solve2(simplex.vm[0], simplex.vm[1]);
		break;
	case 3:
		res = simplex_solve3(simplex.vm[0], simplex.vm[1], simplex.vm[2]);
		break;
	case 4:
		res = simplex_solve4(simplex.vm[0], simplex.vm[1], simplex.vm[2], simplex.vm[3]);
		break;
	default:
		std::unreachable();
	}

	const float new_len_sq = res.point.magnitude_sqr();
	if(new_len_sq < simplex.len_sq)
	{
		simplex.len_sq = new_len_sq;
		return {res.point, true, res.usable_vertices};
	}

	return {res.point, false, res.usable_vertices};
}

void simplex_reduce(Simplex& simplex, uint8_t usable)
{
	uint32_t new_num = 0;
	for(uint32_t i = 0; i < simplex.num; i++)
	{
		if((usable & (1 << i)) != 0)
		{
			simplex.va[new_num] = simplex.va[i];
			simplex.vb[new_num] = simplex.vb[i];
			simplex.vm[new_num] = simplex.vm[i];
			
			new_num++;
		}
	}

	simplex.num = new_num;
}

export struct gjkResult
{
	float distance;
	vec3 separating_axis;
	vec3 point_a;
	vec3 point_b;
};

export struct gjkConfiguration
{
	const CShape& shape_a;
	const CShape& shape_b;
	Transform transform_a;
	Transform transform_b;
	float tolerance = 1e-4f;
	float max_dist_sq = 512.0f;
	vec3 saxis_guess = vec3{1.0f, 0.0f, 0.0f};
};

vec3 get_transformed_support(const CShape& shape, const mat4& transform, const vec3& search)
{
	const vec3 s1 = search * mat3::transpose(transform.demote<3>());
	return (vec4{shape.get_support(s1), 1.0f} * transform).demote<3>();
}

export gjkResult gjk_get_distance(gjkConfiguration& cfg)
{
	ZoneScoped;

	const float tolerance_sq = cfg.tolerance * cfg.tolerance;
	float last_sa_len_sq = std::numeric_limits<float>::max();

	Simplex simplex;
	vec3 sa = cfg.saxis_guess;

	const mat4 transform_2_to_1 = cfg.transform_b.as_matrix() * cfg.transform_a.as_inverse_translation_rotation();

	for(;;)
	{
		// shape A is centered at the origin
		// shape B is moved into the space of shape A
		const vec3 v0 = cfg.shape_a.get_support(sa);	
		const vec3 v1 = get_transformed_support(cfg.shape_b, transform_2_to_1, -sa);

		const vec3 m = v0 - v1;
		const float dot = vec3::dot(sa, m);

		if(dot < 0.0f && dot * dot > simplex.len_sq * cfg.max_dist_sq)
			return {std::numeric_limits<float>::max(), vec3{0.0f}, vec3{0.0f}, vec3{0.0f}};

		simplex.va[simplex.num] = v0;
		simplex.vb[simplex.num] = v1;
		simplex.vm[simplex.num] = m;
		simplex.num += 1;

		auto [point, converge, usable] = simplex_solve(simplex);
		if(!converge)
		{
			simplex.num -= 1;
			break;
		}
		sa = point;

		if(usable == 0b1111)
		{
			simplex.len_sq = 0.0f;
			sa = vec3{0.0f};
			break;
		}

		simplex_reduce(simplex, usable);

		if(simplex.len_sq <= tolerance_sq)
		{
			simplex.len_sq = 0.0f;
			sa = vec3{0.0f};	
			break;
		}
		
		float max_vm_len_sq = simplex.vm[0].magnitude_sqr();
		for(uint32_t i = 1; i < simplex.num; i++)
			max_vm_len_sq = std::max(max_vm_len_sq, simplex.vm[i].magnitude_sqr());

		if(simplex.len_sq <= fp_epsilon * max_vm_len_sq)
		{
			simplex.len_sq = 0.0f;
			sa = vec3{0.0f};
			break;
		}

		sa = -sa;

		if(last_sa_len_sq - simplex.len_sq <= fp_epsilon * last_sa_len_sq)
			break;

		last_sa_len_sq = simplex.len_sq;
	}

	switch(simplex.num)
	{
	case 1:
		return {simplex.len_sq, sa, simplex.va[0], simplex.vb[0]};
	case 2:
	{
		const vec2 bc = line_segment_to_barycentric(simplex.vm[0], simplex.vm[1]);
		return {simplex.len_sq, sa, bc.x * simplex.va[0] + bc.y * simplex.va[1], bc.x * simplex.vb[0] + bc.y * simplex.vb[1]};
	}
	case 3:
	{
		const vec3 bc = triangle_to_barycentric(simplex.vm[0], simplex.vm[1], simplex.vm[2]);
		return {simplex.len_sq, sa, bc.x * simplex.va[0] + bc.y * simplex.va[1] + bc.z * simplex.va[2], bc.x * simplex.vb[0] + bc.y * simplex.vb[1] + bc.z * simplex.vb[2]};
	}
	case 4:
	{
		const vec3 bc = triangle_to_barycentric(simplex.vm[0], simplex.vm[1], simplex.vm[2]);
		return {last_sa_len_sq, sa, bc.x * simplex.va[0] + bc.y * simplex.va[1] + bc.z * simplex.va[2], bc.x * simplex.vb[0] + bc.y * simplex.vb[1] + bc.z * simplex.vb[2]};
	}
	default:
		return {simplex.len_sq, sa, vec3{0.0f}, vec3{0.0f}};
	}
}

export struct gjkCastConfiguration
{
	const CShape& shape_a;
	const CShape& shape_b;
	Transform transform_a;
	Transform transform_b;
	vec3 direction;
	float fraction = 1.0f;
	float tolerance = 1e-4f;
};

export struct gjkCastResult
{
	float fraction;
	vec3 separating_axis;
	vec3 point_a;
	vec3 point_b;
};

export gjkCastResult gjk_cast_shape(gjkCastConfiguration& cfg)
{
	ZoneScoped;
	
	float tolerance_sq = cfg.tolerance * cfg.tolerance;
	const float convex_radius_ab = cfg.shape_a.get_convex_radius() + cfg.shape_b.get_convex_radius();

	Simplex simplex;

	vec3 x{0.0f};

	const auto tm_a = cfg.transform_a.as_matrix();
	const auto tm_b = cfg.transform_b.as_matrix();

	vec3 v = x - (get_transformed_support(cfg.shape_b, tm_b, vec3{0.0f}) - get_transformed_support(cfg.shape_a, tm_a, vec3{0.0f})); 

	vec3 last_v = vec3{0.0f};
	float lambda = 0.0f;
	const float fraction = cfg.fraction;

	bool allow_restart = false;

	for(;;)
	{
		const vec3 v0 = get_transformed_support(cfg.shape_a, tm_a, -v);
		const vec3 v1 = get_transformed_support(cfg.shape_b, tm_b, v);
		const vec3 vm = x - (v1 - v0);

		const float vdotw = vec3::dot(v, vm) - (convex_radius_ab * v.magnitude());
		if(vdotw > 0.0f)
		{
			const float vdotr = vec3::dot(v, cfg.direction);
			if(vdotr >= 0.0f)
				return {1.0f, vec3{0.0f}, vec3{0.0f}, vec3{0.0f}};

			const float delta = vdotw / vdotr;
			const float last_lambda = lambda;
			lambda -= delta;

			if(last_lambda == lambda)
				break;

			if(lambda >= fraction)
				return {fraction, vec3{0.0f}, vec3{0.0f}, vec3{0.0f}};

			x = lambda * cfg.direction;
			simplex.len_sq = std::numeric_limits<float>::max();
			tolerance_sq = (cfg.tolerance + convex_radius_ab) * (cfg.tolerance + convex_radius_ab);

			allow_restart = true;
		}

		simplex.va[simplex.num] = v0;
		simplex.vb[simplex.num] = v1;
		simplex.num++;

		for(uint32_t i = 0; i < simplex.num; i++)
			simplex.vm[i] = x - (simplex.vb[i] - simplex.va[i]);

		auto [point, converge, usable] = simplex_solve(simplex);
		if(!converge)
		{
			if(!allow_restart)
				break;

			allow_restart = false;
			simplex.va[0] = v0;
			simplex.vb[0] = v1;
			simplex.num = 1;
			v = x - v1;
			simplex.len_sq = std::numeric_limits<float>::max();
			continue;
		}
		else if(usable == 0b1111)
		{
			assert(simplex.len_sq == 0.0f);
			break;
		}
		v = point;
		

		simplex_reduce(simplex, usable);

		if(simplex.len_sq <= tolerance_sq)
			break;

		last_v = v;
	}

	for(uint32_t i = 0; i < simplex.num; i++)
		simplex.vm[i] = x - (simplex.vb[i] - simplex.va[i]);

	const vec3 norm_v = vec3::normalize(v);
	const vec3 cr_a = cfg.shape_a.get_convex_radius() * norm_v;
	const vec3 cr_b = cfg.shape_b.get_convex_radius() * norm_v;

	vec3 p_a{0.0f};
	vec3 p_b{0.0f};
	vec3 saxis;

	switch(simplex.num)
	{
	case 1:
	{
		p_b = simplex.vb[0] + cr_b;
		p_a = lambda > 0.0f ? p_b : simplex.va[0] - cr_a;
		break;
	}
	case 2:
	{
		const vec2 bcoord = line_segment_to_barycentric(simplex.vm[0], simplex.vm[1]);
		p_b = bcoord.x * simplex.vb[0] + bcoord.y * simplex.vb[1] + cr_b;
		p_a = lambda > 0.0f ? p_b : bcoord.x * simplex.va[0] + bcoord.y * simplex.va[1] - cr_a;
		break;
	}
	case 3:
	case 4:
	{
		const vec3 bcoord = triangle_to_barycentric(simplex.vm[0], simplex.vm[1], simplex.vm[2]);
		p_b = bcoord.x * simplex.vb[0] + bcoord.y * simplex.vb[1] + bcoord.z * simplex.vb[2] + cr_b;
		p_a = lambda > 0.0f ? p_b : bcoord.x * simplex.va[0] + bcoord.y * simplex.va[1] + bcoord.z * simplex.va[2] - cr_a;
		break;
	}
	default:
		std::unreachable();
	}

	saxis = convex_radius_ab > 0.0f ? -1.0f * v : -1.0f * last_v;
	return {lambda, saxis, p_a, p_b};
}

}


