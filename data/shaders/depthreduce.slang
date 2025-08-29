#include "common.slang"

Sampler2D<float> inputRT;
RWTexture2D<float> outputRT;

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint2 Gid : SV_GroupID, uint groupIndex : SV_GroupIndex, uniform float2 size)
{
	const uint2 invocation = remap_lane_8x8(groupIndex);
	const uint2 pixel = Gid * 8 + invocation;
	const float depth = inputRT.SampleLevel((pixel + 0.5) / size, 0);
	
	outputRT[pixel] = depth;
}
