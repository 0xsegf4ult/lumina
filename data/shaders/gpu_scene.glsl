#ifndef GPU_SCENE_HEADER
#define GPU_SCENE_HEADER

struct ObjectInstance
{
	mat4 transform;
	vec4 sphere;
	float cull_scale;
	uint material;
	uint lod0_offset;
	uint pack_bucket_lcount;
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

	vec4 sphere;
	vec4 cone;
};

#endif
