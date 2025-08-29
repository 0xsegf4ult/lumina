#if defined UBYTE_PASS
RWStructuredBuffer<uint8_t> inputs;
#else
RWStructuredBuffer<uint> inputs;
#endif

RWStructuredBuffer<uint> outputs;
RWStructuredBuffer<uint> partial_sums;

groupshared uint sdata[32];

uint SubgroupID()
{
	return spirv_asm
 	{
		result: $$uint = OpLoad builtin(SubgroupId:uint);
	};
}

uint WavePrefixInclusiveAdd(uint sum)
{
	return spirv_asm
	{
		OpCapability GroupNonUniformArithmetic;
		OpGroupNonUniformIAdd $$uint result Subgroup 1 $sum;
	};
}

[shader("compute")]
[numthreads(512, 1, 1)]
void computeMain(uint Dtid : SV_DispatchThreadID, uint GTid : SV_GroupThreadID, uint Gid : SV_GroupID, uniform uint count)
{
	uint sum = 0;
	if(Dtid < count)
		sum = inputs[Dtid];
	
	sum = WavePrefixInclusiveAdd(sum);

	if(WaveGetLaneIndex() == WaveGetLaneCount() - 1)
		sdata[SubgroupID()] = sum;
	
	GroupMemoryBarrierWithGroupSync();

	if(SubgroupID() == 0)
	{
		uint warpSum = WaveGetLaneIndex() < WaveGetLaneCount() ? sdata[WaveGetLaneIndex()] : 0;
		warpSum = WavePrefixInclusiveAdd(warpSum);
		sdata[WaveGetLaneIndex()] = warpSum;
	}

	GroupMemoryBarrierWithGroupSync();

	uint blockSum = 0;
	if(SubgroupID() > 0)
		blockSum = sdata[SubgroupID() - 1];
	
	sum += blockSum;

	if(Dtid < count)
		outputs[Dtid] = sum;

	if(GTid == 511)
		partial_sums[Gid] = sum;
}
