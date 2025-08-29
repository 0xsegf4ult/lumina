StructuredBuffer<uint> inputs;
RWStructuredBuffer<uint> outputs;

groupshared uint sum;

[shader("compute")]
[numthreads(512, 1, 1)]
void computeMain(uint Gtid : SV_GroupThreadID, uint Dtid : SV_DispatchThreadID, uint Gid : SV_GroupID, uniform uint count)
{
	if(Gid > 0 && Dtid < count)
	{
		sum = 0;
		if(Gtid == 0)
			sum = inputs[Gid - 1];

		GroupMemoryBarrierWithGroupSync();

		outputs[Dtid] += sum;
	}
}
