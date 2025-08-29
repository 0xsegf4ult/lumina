#include "gpu_scene.slang"

//FIXME: COMPILES WRONG USE GLSL_PACKER ON CLUSTER_CULL.COMP

StructuredBuffer<ObjectData> objects;
StructuredBuffer<ClusterInstance> instances;
RWStructuredBuffer<uint8_t> visibility;
RWStructuredBuffer<DrawBucket> buckets;
StructuredBuffer<MeshCluster> clusters;
ConstantBuffer<CullingData> culling_data;
RWStructuredBuffer<uint4> ocl_dispatch;
RWStructuredBuffer<uint> occlusion;
Sampler2D<float> depthPyramid;

bool projectSphereFZ(float3 c, float r, float znear, float p00, float p11, out float4 aabb)
{
	if(c.z < r + znear)
	{
		aabb = float4(0.0);
		return false;
	}

	float2 cxz = c.xz;
	float2 vx = float2(sqrt(dot(cxz, cxz) - r * r), r);
	float2 minx = mul(cxz, float2x2(vx.x, vx.y, -vx.y, vx.x));
	float2 maxx = mul(cxz, float2x2(vx.x , -vx.y, vx.y, vx.x));

	float2 cyz = c.yz;
	float2 vy = float2(sqrt(dot(cyz, cyz) - r * r), r);
	float2 miny = mul(cyz, float2x2(vy.x, vy.y, -vy.y, vy.x));
	float2 maxy = mul(cyz, float2x2(vy.x, -vy.y, vy.y, vy.x));

	aabb = float4(minx.x / minx.y * p00, miny.x / miny.y * p11, maxx.x / maxx.y * p00, maxy.x / maxy.y * p11);

	return true;
}

bool projectSphere(float3 c, float r, float znear, float p00, float p11, out float4 aabb)
{
        if(-c.z < r + znear)
	{
		aabb = float4(0.0);
                return false;
	}

        float2 cxz = -c.xz;
        float2 vx = float2(sqrt(dot(cxz, cxz) - r * r), r);
        float2 minx = mul(cxz, float2x2(vx.x, vx.y, -vx.y, vx.x));
        float2 maxx = mul(cxz, float2x2(vx.x , -vx.y, vx.y, vx.x));

        float2 cyz = -c.yz;
        float2 vy = float2(sqrt(dot(cyz, cyz) - r * r), r);
        float2 miny = mul(cyz, float2x2(vy.x, vy.y, -vy.y, vy.x));
        float2 maxy = mul(cyz, float2x2(vy.x, -vy.y, vy.y, vy.x));

        aabb = float4(minx.x / minx.y * p00, miny.x / miny.y * p11, maxx.x / maxx.y * p00, maxy.x / maxy.y * p11);

        return true;
}

bool is_visible(float4 sphere, float radius, float3 cone, float cone_cutoff, out bool oculled)
{
	bool visible = true;	

	float3 center = mul(sphere, culling_data.viewmat).xyz;
	float3 cone_axis = mul(cone, float3x3(culling_data.viewmat));

	if(culling_data.frustum_cull == 1)
	{
		if(culling_data.is_ortho == 1)
		{
			for(uint i = 0; i < 4; i++)
				visible = visible && dot(culling_data.frustum_planes[i], float4(center, 1.0)) > -radius;
		}
		else
		{
			visible = visible && center.z * culling_data.frustum_planes[0].y - abs(center.x) * culling_data.frustum_planes[0].x > -radius;
			visible = visible && center.z * culling_data.frustum_planes[0].w - abs(center.y) * culling_data.frustum_planes[0].z > -radius;
			visible = visible && -center.z + radius > culling_data.znear && -center.z - radius < culling_data.zfar;
		}
	}

	if(visible && culling_data.cone_cull == 1)
	{
		if(culling_data.is_ortho == 1)
		{
			visible = visible && dot(center.xyz, cone_axis) < cone_cutoff;
		}
		else
		{
			visible = visible && dot(center, cone_axis) < cone_cutoff * length(center.xyz) + radius;
		}
	}

	float4 aabb;
	bool sproj = false;

	if(visible)
	{
		if(culling_data.is_ortho == 1)
			sproj = projectSphereFZ(center, radius, culling_data.znear, culling_data.p00, culling_data.p11, aabb);
		else
			sproj = projectSphere(center, radius, culling_data.znear, culling_data.p00, culling_data.p11, aabb);

		oculled = !sproj;
	}

	if(sproj && visible && culling_data.coverage_cull == 1)
	{
		visible = visible && max(abs(aabb.z - aabb.x), abs(aabb.w - aabb.y)) >= culling_data.coverage_threshold;
	}

	if(sproj && visible && culling_data.occlusion_cull == 1)
	{
		const float4 aabbUV = (culling_data.is_ortho == 1) ? 0.5 + 0.5 * aabb : 0.5 - 0.5 * aabb;
		const float width = (aabbUV.z - aabbUV.x) * culling_data.pyramid_w;
		const float height = (aabbUV.w - aabbUV.y) * culling_data.pyramid_h;

		const float level = floor(log2(max(width, height)));
		const float depth = depthPyramid.SampleLevel((aabbUV.xy + aabbUV.zw) * 0.5, level);
		const float depthSphere = culling_data.znear / (-center.z + radius);

		visible = visible && depthSphere >= depth;
		oculled = !visible;
	}

	return visible;
}

[shader("compute")]
[numthreads(32, 1, 1)]
void computeMain(uint DTid : SV_DispatchThreadID, uniform uint cluster_count)
{
	if(DTid >= cluster_count)
		return;

	ClusterInstance instance = instances[DTid];
	if(instance.object == 0)
		return;

	MeshCluster cluster = clusters[instance.cluster];
	float4 center = mul(float4(cluster.sphere.xyz, 1.0), objects[instance.object - 1].transform);	
	float radius = objects[instance.object - 1].cull_scale * cluster.sphere.w;
	float3 cone_axis = mul(cluster.cone.xyz, float3x3(objects[instance.object - 1].transform));	

	bool oculled = false;
	
	if(is_visible(center, radius, cone_axis, cluster.cone.w, oculled))
	{
		uint bucket = objects[instance.object - 1].pack_bucket_lcount >> 16;
		InterlockedAdd(buckets[bucket].count, 1u);
		visibility[DTid] = 1;
	}
	
	if(culling_data.occlusion_cull == 1 && oculled)
	{
		uint oclOffset;
		InterlockedAdd(ocl_dispatch[0].w, 1u, oclOffset);
		occlusion[oclOffset] = DTid;
		if(oclOffset == 0)
		{
			ocl_dispatch[0].y = 1u;
			ocl_dispatch[0].z = 1u;
		}

		if(oclOffset % 32 == 0)
			InterlockedAdd(ocl_dispatch[0].x, 1u);
	}
} 
