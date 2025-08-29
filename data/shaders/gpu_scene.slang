#pragma once

struct ObjectData
{
	float4x4 transform;
	float4 sphere;
	float cull_scale;
	uint material;
	uint lod0_offset;
	uint pack_bucket_lcount;
};

struct ObjectInstance
{
	uint input_offset;
	uint output_offset;
};

struct ClusterInstance
{
	uint object;
	uint cluster;
};

struct IndirectCommand
{
	uint index_count;
	uint instance_count;
	uint index_offset;
	int vertex_offset;
	uint instance_id;
};

struct DrawBucket
{
	uint offset;
	uint count;
};

struct MeshLOD
{
	uint cluster_offset;
	uint cluster_count;
};

struct MeshCluster
{
	int vertex_offset;
	uint vertex_count;
	uint index_offset;
	uint index_count;
	
	float4 sphere;
	float4 cone;
};

struct CullingData
{
	float4 frustum_planes[4];
	float4x4 viewmat;
	uint frustum_cull;
	uint occlusion_cull;
	uint cone_cull;
	uint coverage_cull;
	float coverage_threshold;
	uint is_ortho;
	float rt_w;
	float rt_h;
	float znear;
	float zfar;
	float p00;
	float p11;
	float pyramid_w;
	float pyramid_h;
	float lod_base;
	float lod_step;
	float4 cam_pos;
	int forced_lod;
};
