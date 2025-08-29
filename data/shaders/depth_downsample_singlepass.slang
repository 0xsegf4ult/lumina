#include "common.slang"

Texture2D inputRT;
SamplerState reduction_sampler;
RWTexture2D outputRT[12];
RWStructuredBuffer<uint> global_atomic;

struct ShaderConstants
{
	float2 inv_size;
	uint mips;
	uint numWorkGroups;
	bool min_reduce;
};

[[vk::push_constant]] uniform ShaderConstants consts;

groupshared float4 smem_data[16][16];
groupshared uint atomic_counter;

float4 reduce_load4(uint2 i0, uint2 i1, uint2 i2, uint2 i3)
{
	float4 v0 = outputRT[5][i0];
	float4 v1 = outputRT[5][i1];
	float4 v2 = outputRT[5][i2];
	float4 v3 = outputRT[5][i3];

	return consts.min_reduce ? min(min(v0, v1), min(v2, v3)) : max(max(v0, v1), max(v2, v3));
}

float4 reduce_load4(uint2 base)
{
	return reduce_load4(
		uint2(base + uint2(0, 0)),
		uint2(base + uint2(0, 1)),
		uint2(base + uint2(1, 0)),
		uint2(base + uint2(1, 1))
	);
}

float4 reduce_quad(float4 v)
{
	float4 v0 = v;
	float4 v1 = QuadReadAcrossX(v);
	float4 v2 = QuadReadAcrossY(v);
	float4 v3 = QuadReadAcrossDiagonal(v);

	return consts.min_reduce ? min(min(v0, v1), min(v2, v3)) : max(max(v0, v1), max(v2, v3));
}

void downsample_mips_0_1(uint x, uint y, uint2 wgID, uint invocation, uint mip)
{
	float4 v[4];

	int2 tex = int2(wgID * 64) + int2(x * 2, y * 2);
	int2 pix = int2(wgID * 32) + int2(x, y);
	v[0] = inputRT.SampleLevel(reduction_sampler, tex * consts.inv_size + consts.inv_size, 0);
	outputRT[0][pix] = v[0];

	tex = int2(wgID * 64) + int2(x * 2 + 32, y * 2);
	pix = int2(wgID * 32) + int2(x + 16, y);
	v[1] = inputRT.SampleLevel(reduction_sampler, tex * consts.inv_size + consts.inv_size, 0);
	outputRT[0][pix] = v[1];
	
	tex = int2(wgID * 64) + int2(x * 2, y * 2 + 32);
	pix = int2(wgID * 32) + int2(x, y + 16);
	v[2] = inputRT.SampleLevel(reduction_sampler, tex * consts.inv_size + consts.inv_size, 0);
	outputRT[0][pix] = v[2];

	tex = int2(wgID * 64) + int2(x * 2 + 32, y * 2 + 32);
	pix = int2(wgID * 32) + int2(x + 16, y + 16);
	v[3] = inputRT.SampleLevel(reduction_sampler, tex * consts.inv_size + consts.inv_size, 0 );
	outputRT[0][pix] = v[3];

	if(mip <= 1)
		return;

	v[0] = reduce_quad(v[0]);
	v[1] = reduce_quad(v[1]);
	v[2] = reduce_quad(v[2]);
	v[3] = reduce_quad(v[3]);

	if(invocation % 4 == 0)
	{
		outputRT[1][int2(wgID * 16) + int2(x / 2, y / 2)] = v[0];
		smem_data[x / 2][y / 2] = v[0];

		outputRT[1][int2(wgID * 16) + int2(x / 2 + 8, y / 2)] = v[1];
		smem_data[x / 2 + 8][y / 2] = v[1];

		outputRT[1][int2(wgID * 16) + int2(x / 2, y / 2 + 8)] = v[2];
		smem_data[x / 2][y / 2 + 8] = v[2];

		outputRT[1][int2(wgID * 16) + int2(x / 2 + 8, y / 2 + 8)] = v[3];
		smem_data[x / 2 + 8][y / 2 + 8] = v[3];
	}
}

void downsample_mip2(uint x, uint y, uint2 wgID, uint invocation, uint mip)
{
	float4 v = smem_data[x][y];
	v = reduce_quad(v);
	
	if(invocation % 4 == 0)
	{
		outputRT[mip][int2(wgID * 8) + int2(x / 2, y / 2)] = v;
		smem_data[x + (y / 2) % 2][y] = v;
	}
}

void downsample_mip3(uint x, uint y, uint2 wgID, uint invocation, uint mip)
{
	if(invocation < 64)
	{
		float4 v = smem_data[x * 2 + y % 2][y * 2];
		v = reduce_quad(v);

		if(invocation % 4 == 0)
		{
			outputRT[mip][int2(wgID * 4) + int2(x / 2, y / 2)] = v;
			smem_data[x * 2 + y / 2][y * 2] = v;
		}
	}
}

void downsample_mip4(uint x, uint y, uint2 wgID, uint invocation, uint mip)
{
	if(invocation < 16)
	{
		float4 v = smem_data[x * 4 + y][y * 4];
		v = reduce_quad(v);

		if(invocation % 4 == 0)
		{
			outputRT[mip][int2(wgID * 2) + int2(x / 2, y / 2)] = v;
			smem_data[x / 2 + y][0] = v;
		}
	}
}

void downsample_mip5(uint2 wgID, uint invocation, uint mip)
{
	if(invocation < 4)
	{
		float4 v = smem_data[invocation][0];
		v = reduce_quad(v);

		if(invocation % 4 == 0)
			outputRT[mip][wgID] = v;
	}
}

void downsample_next_four(uint x, uint y, uint2 wgID, uint invocation, uint baseMip, uint mips)
{
	if(mips <= baseMip)
		return;

	GroupMemoryBarrierWithGroupSync();
	downsample_mip2(x, y, wgID, invocation, baseMip);

	if(mips <= baseMip + 1)
		return;

	GroupMemoryBarrierWithGroupSync();
	downsample_mip3(x, y, wgID, invocation, baseMip + 1);
	
	if(mips <= baseMip + 2)
		return;

	GroupMemoryBarrierWithGroupSync();
	downsample_mip4(x, y, wgID, invocation, baseMip + 2);
	
	if(mips <= baseMip + 3)
		return;

	GroupMemoryBarrierWithGroupSync();
	downsample_mip5(wgID, invocation, baseMip + 3);
}

void downsample_mips_6_7(uint x, uint y, uint mips)
{
	int2 tex = int2(x * 4 + 0, y * 4 + 0);
	int2 pix = int2(x * 2 + 0, y * 2 + 0);

	float4 v0 = reduce_load4(tex);
	outputRT[6][pix] = v0;

	tex = int2(x * 4 + 2, y * 4 + 0);
	pix = int2(x * 2 + 1, y * 2 + 0);
	float4 v1 = reduce_load4(tex);
	outputRT[6][pix] = v1;

	tex = int2(x * 4 + 0, y * 4 + 2);
	pix = int2(x * 2 + 0, y * 2 + 1);
	float4 v2 = reduce_load4(tex);
	outputRT[6][pix] = v2;

	tex = int2(x * 4 + 2, y * 4 + 2);
	pix = int2(x * 2 + 1, y * 2 + 1);
	float4 v3 = reduce_load4(tex);
	outputRT[6][pix] = v3;

	if(consts.mips <= 7)
		return;

	float4 v = consts.min_reduce ? min(min(v0, v1), min(v2, v3)) : max(max(v0, v1), max(v2, v3));
	outputRT[7][int2(x, y)] = v;
	smem_data[x][y] = v;
}

bool wg_exit_check(uint numWorkGroups, uint invocation)
{
	if(invocation == 0)
	{
		InterlockedAdd(global_atomic[0], 1u, atomic_counter);
	}

	GroupMemoryBarrierWithGroupSync();
	return (atomic_counter != (numWorkGroups - 1));
}

[shader("compute")]
[numthreads(256, 1, 1)]
void computeMain(uint groupIndex : SV_GroupIndex, uint2 Gid : SV_GroupID)
{
	uint2 sub_xy = remap_lane_8x8(groupIndex % 64);
	uint x = sub_xy.x + 8 * ((groupIndex >> 6) % 2);
	uint y = sub_xy.y + 8 * (groupIndex >> 7);

	downsample_mips_0_1(x, y, Gid, groupIndex, consts.mips);

	downsample_next_four(x, y, Gid, groupIndex, 2, consts.mips);

	if(consts.mips <= 6)
		return;

	if(wg_exit_check(consts.numWorkGroups, groupIndex))
		return;

	global_atomic[0] = 0;

	downsample_mips_6_7(x, y, consts.mips);

	downsample_next_four(x, y, uint2(0, 0), groupIndex, 8, consts.mips);
}	
