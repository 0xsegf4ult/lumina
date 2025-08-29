#include "gpu_scene.slang"

StructuredBuffer<ObjectData> objects;
StructuredBuffer<ObjectInstance> instances;
RWStructuredBuffer<ClusterInstance> out_clusters;
StructuredBuffer<MeshLOD> lods;
ConstantBuffer<CullingData> culling_data;

bool is_visible(float4 sphere, float radius)
{
	bool visible = true;
	float3 center = mul(sphere, culling_data.viewmat).xyz;

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

	return visible;
}

[shader("compute")]
[numthreads(32, 1, 1)]
void computeMain(uint DTid : SV_DispatchThreadID, uniform uint instance_count)
{
	if(DTid >= instance_count)
		return;

	ObjectInstance instance = instances[DTid];
	if(instance.input_offset == 0)
		return;

	float4 center = mul(float4(objects[instance.input_offset - 1].sphere.xyz, 1.0), objects[instance.input_offset - 1].transform);
	float radius = objects[instance.input_offset - 1].cull_scale * objects[instance.input_offset - 1].sphere.w;

	float mesh_dist = distance(center.xyz, culling_data.cam_pos.xyz);
	uint lodIndex = uint(max(1.0 + log(mesh_dist / culling_data.lod_base) / log(culling_data.lod_step), 0.0));
	uint lod_count = objects[instance.input_offset - 1].pack_bucket_lcount & 0xFFFF;
	lodIndex = min(culling_data.forced_lod < 0 ? lodIndex : culling_data.forced_lod, lod_count - 1);
	MeshLOD lod = lods[objects[instance.input_offset - 1].lod0_offset + lodIndex];
	
	if(is_visible(center, radius))
	{
		for(uint i = 0; i < lod.cluster_count; i++)
		{	
			out_clusters[instance.output_offset + i].object = instance.input_offset;
			out_clusters[instance.output_offset + i].cluster = lod.cluster_offset + i;
		}
	}
}
