#pragma once

float signNotZero(float k)
{
	return k >= 0.0 ? 1.0 : -1.0;
}

float2 signNotZero(float2 v)
{
	return float2(signNotZero(v.x), signNotZero(v.y));
}

uint float3_to_oct(float3 v)
{
	float invl1norm = rcp(abs(v.x) + abs(v.y) + abs(v.z));
	float2 proj;

	if(v.z < 0.0)
	{
		proj.x = (1.0 - abs(v.y * invl1norm)) * signNotZero(v.x);
		proj.y = (1.0 - abs(v.x * invl1norm)) * signNotZero(v.y);
	}
	else
	{
		proj.x = v.x * invl1norm;
		proj.y = v.y * invl1norm;
	}

	return packSnorm2x16(proj);
}

float3 oct_to_float3(uint p)
{
	float2 inp = unpackSnorm2x16ToFloat(p);

	float3 vn = float3(inp.xy, 1.0 - abs(inp.x) - abs(inp.y));
	if(vn.z < 0.0)
		vn.xy = (1.0 - abs(inp.yx)) * signNotZero(inp.xy);

	return normalize(vn);
}

float2 decode_diamond(float p)
{
	float2 v;

	float p_sign = sign(p - 0.5);
	v.x = -p_sign * 4.0 * p + 1.0 + p_sign * 2.0;
	v.y = p_sign * (1.0 - abs(v.x));

	return normalize(v);
}

float4 decode_tangent(float3 normal, float p)
{
	float4 outv = float4(0.0, 0.0, 0.0, 1.0);
	uint bits = reinterpret<uint>(p);
	if((bits & 1) == 1)
		outv.w = -1.0;

	float3 t1;
	if(abs(normal.y) > abs(normal.z))
		t1 = float3(normal.y, -normal.x, 0.0);
	else
		t1 = float3(normal.z, 0.0, -normal.x);

	t1 = normalize(t1);

	float3 t2 = cross(t1, normal);
	float2 packed_tangent = decode_diamond(p);
	outv.xyz = packed_tangent.x * t1 + packed_tangent.y * t2;
	return outv;
}

float encode_diamond(float2 p)
{
	float x = p.x / (abs(p.x) + abs(p.y));
	
	float py_sign = sign(p.y);
	return -py_sign * 0.25 * x + 0.5 + py_sign * 0.25;
}

float encode_tangent(float3 normal, float3 tangent, bool flip)
{
	float3 t1;
	if(abs(normal.y) > abs(normal.z))
		t1 = float3(normal.y, -normal.x, 0.0);
	else
		t1 = float3(normal.z, 0.0, -normal.x);

	t1 = normalize(t1);

	float3 t2 = cross(t1, normal);
	float2 packed_tangent = float2(dot(tangent, t1), dot(tangent, t2));
	float diamond = encode_diamond(packed_tangent);

	uint fbits = reinterpret<uint>(diamond);
	if(flip)
		fbits |= 1;
	else
		fbits &= (~1);
	
	return reinterpret<float>(fbits);
}
