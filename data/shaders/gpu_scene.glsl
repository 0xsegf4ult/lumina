struct GPUIndirectObject
{
	uint lod0_offset;
	uint lod_count;
	uint batch;
	uint object;
};

struct IndirectCommand
{
	uint index_count;
	uint instance_count;
	uint index_offset;
	int vertex_offset;
	uint instance_id;
};

struct Multibatch
{
	uint first;
	uint count;
	uint draw_count;
	uint unused;
};

struct GPUObjectData
{
	mat4 transform;
	vec4 sphere;
	uint material;
	uint unused0;
	uint unused1;
	uint unused2;
};

struct MeshLOD
{
	int vertex_offset;
	uint vertex_count;
	uint index_offset;
	uint index_count;
};
