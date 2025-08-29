#include "gpu_scene.slang"

StructuredBuffer<ClusterInstance> instances;
RWStructuredBuffer<IndirectCommand> output_commands;
StructuredBuffer<uint> indices;
StructuredBuffer<uint8_t> visibility;
RWStructuredBuffer<DrawBucket> buckets;
StructuredBuffer<ObjectData> objects;
StructuredBuffer<MeshCluster> clusters;

[shader("compute")]
[numthreads(256, 1, 1)]
void computeMain(uint DTid : SV_DispatchThreadID, uniform uint count)
{
	if(DTid >= count)
		return;

	if(visibility[DTid] == 0)
		return;

	ClusterInstance instance = instances[DTid];
	uint bucket = objects[instance.object - 1].pack_bucket_lcount >> 16;
	uint bOffset = 0;
	if(bucket > 0)
	{
		uint dsum = 0;
		for(uint i = 0; i < bucket; i++)
			dsum += buckets[i].count;

		bOffset = buckets[bucket].offset - dsum;
	}

	MeshCluster cluster = clusters[instance.cluster];

	IndirectCommand cmd;
	cmd.index_count = cluster.index_count;
	cmd.instance_count = 1u;
	cmd.index_offset = cluster.index_offset;
	cmd.vertex_offset = cluster.vertex_offset;
	cmd.instance_id = instance.object - 1;

	output_commands[indices[DTid] + bOffset - 1] = cmd;
}
