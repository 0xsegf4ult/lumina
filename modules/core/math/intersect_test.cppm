module;

#include <immintrin.h>

export module lumina.core.math:intersect_test;

export import :vector;
export import :aabb;

import std;
using std::memcpy;

export namespace lumina
{

vec4 simd_min(const vec4& lhs, const vec4& rhs)
{
	vec4 res;
	__m128 l, r;
	memcpy(&l, &lhs[0], 16);
	memcpy(&r, &rhs[0], 16);
	__m128 vres = _mm_min_ps(l, r);
	memcpy(&res[0], &vres, 16);
	return res;
}

vec4 simd_max(const vec4& lhs, const vec4& rhs)
{
	vec4 res;
	__m128 l, r;
	memcpy(&l, &lhs[0], 16);
	memcpy(&r, &rhs[0], 16);
	__m128 vres = _mm_max_ps(l, r);
	memcpy(&res[0], &vres, 16);
	return res;
}

float simdlike_min(float lhs, float rhs)
{
	return (lhs < rhs) ? lhs : rhs;
}

float simdlike_max(float lhs, float rhs)
{
	return (lhs > rhs) ? lhs : rhs;
}

inline float ray_test_aabb(vec3 origin, vec3 inv_dir, AABB box)
{
	float tmin{0.0f};
	float tmax{std::numeric_limits<float>::infinity()};
	float t{std::numeric_limits<float>::infinity()};

	for(uint32_t d = 0; d < 3; d++)
	{
		float t1 = (box.mins[d] - origin[d]) * inv_dir[d];
		float t2 = (box.maxs[d] - origin[d]) * inv_dir[d];

		tmin = simdlike_min(simdlike_max(t1, tmin), simdlike_max(t2, tmin));
	       	tmax = simdlike_max(simdlike_min(t1, tmax), simdlike_min(t2, tmax));
	}

	return tmin <= tmax ? tmin : t;	
}

inline vec4 ray_test_aabb_simd4(const vec3& origin, const vec3& inv_dir, const SIMD4AABB& boxes)
{
	vec4 tmin{0.0f};
	vec4 tmax{std::numeric_limits<float>::infinity()};

	const vec4 originX{origin.x};
	const vec4 originY{origin.y};
	const vec4 originZ{origin.z};

	const vec4 invdX{inv_dir.x};
	const vec4 invdY{inv_dir.y};
	const vec4 invdZ{inv_dir.z};

	vec4 t1x = vec4::scalar_mul(boxes.minX - originX, invdX);
	vec4 t1y = vec4::scalar_mul(boxes.minY - originY, invdY);
	vec4 t1z = vec4::scalar_mul(boxes.minZ - originZ, invdZ);
	vec4 t2x = vec4::scalar_mul(boxes.maxX - originX, invdX);
	vec4 t2y = vec4::scalar_mul(boxes.maxY - originY, invdY);
	vec4 t2z = vec4::scalar_mul(boxes.maxZ - originZ, invdZ);
	
	tmin = simd_min(simd_max(t1x, tmin), simd_max(t2x, tmin));
	tmin = simd_min(simd_max(t1y, tmin), simd_max(t2y, tmin));
	tmin = simd_min(simd_max(t1z, tmin), simd_max(t2z, tmin));
	tmax = simd_max(simd_min(t1x, tmax), simd_min(t2x, tmax));
	tmax = simd_max(simd_min(t1y, tmax), simd_min(t2y, tmax));
	tmax = simd_max(simd_min(t1z, tmax), simd_min(t2z, tmax));

	__m128 r_tmin, r_tmax;
	memcpy(&r_tmin, &tmin[0], 16);
	memcpy(&r_tmax, &tmax[0], 16);
	__m128 mask = _mm_cmp_ps(r_tmin, r_tmax, _CMP_LE_OQ);
	__m128 r_t = _mm_set1_ps(std::numeric_limits<float>::infinity());
	r_t = _mm_blendv_ps(r_t, r_tmin, mask);
	vec4 t;
	memcpy(&t[0], &r_t, 16);

	return t;
}

inline uvec4 aabb_test_aabb_simd4(const AABB& aabb, const SIMD4AABB& boxes)
{
	__m128 lhsminx = _mm_set1_ps(aabb.mins.x);
	__m128 lhsminy = _mm_set1_ps(aabb.mins.y);
	__m128 lhsminz = _mm_set1_ps(aabb.mins.z);
	__m128 lhsmaxx = _mm_set1_ps(aabb.maxs.x);
	__m128 lhsmaxy = _mm_set1_ps(aabb.maxs.y);
	__m128 lhsmaxz = _mm_set1_ps(aabb.maxs.z);

	__m128 rhsminx;
	memcpy(&rhsminx, &boxes.minX[0], 16);
	__m128 rhsminy;
	memcpy(&rhsminy, &boxes.minY[0], 16);
	__m128 rhsminz;
	memcpy(&rhsminz, &boxes.minZ[0], 16);
	__m128 rhsmaxx;
	memcpy(&rhsmaxx, &boxes.maxX[0], 16);
	__m128 rhsmaxy;
	memcpy(&rhsmaxy, &boxes.maxY[0], 16);
	__m128 rhsmaxz;
	memcpy(&rhsmaxz, &boxes.maxZ[0], 16);

	__m128i intersectx = _mm_and_si128(_mm_castps_si128(_mm_cmpgt_ps(lhsmaxx, rhsminx)), _mm_castps_si128(_mm_cmplt_ps(lhsminx, rhsmaxx)));
	__m128i intersecty = _mm_and_si128(_mm_castps_si128(_mm_cmpgt_ps(lhsmaxy, rhsminy)), _mm_castps_si128(_mm_cmplt_ps(lhsminy, rhsmaxy)));
	__m128i intersectz = _mm_and_si128(_mm_castps_si128(_mm_cmpgt_ps(lhsmaxz, rhsminz)), _mm_castps_si128(_mm_cmplt_ps(lhsminz, rhsmaxz)));

	__m128i r_res = _mm_and_si128(_mm_and_si128(intersectx, intersecty), intersectz);
	uvec4 res;
	memcpy(&res[0], &r_res, 16);
	return res;
}

}
