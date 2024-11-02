float signNotZero(float k)
{
	return k >= 0.0 ? 1.0 : -1.0;
}

vec2 signNotZero(vec2 v)
{
	return vec2(signNotZero(v.x), signNotZero(v.y));
}

uint packSnorm16(float f)
{
	return floatBitsToUint(round(clamp(f, -1.0f, 1.0f)) * 32767);
}

uint vec3_to_oct(vec3 v)
{
	float invl1norm = (1.0f) / (abs(v.x) + abs(v.y) + abs(v.z));

	uint x = 0;
	uint y = 0;

	if(v.z < 0.0f)
	{
		x = packSnorm16((1.0f - abs(v.y * invl1norm)) * signNotZero(v.x));
		y = packSnorm16((1.0f - abs(v.x * invl1norm)) * signNotZero(v.y));
	}
	else
	{
		x = packSnorm16(v.x * invl1norm);
		y = packSnorm16(v.y * invl1norm);
	}

	uint result = y | x << uint(16);	

	return result;
}

float unpackSnorm16(float f)
{
	return clamp((float(f) / float(32767)) - 1.0, -1.0, 1.0);
}

vec3 oct_to_vec3(uint p)
{
	uint one = uint(1);
	uint u = p >> uint(16);
	uint v = p & ((one << uint(16)) - one);

	vec2 inp = vec2(unpackSnorm16(float(v)), unpackSnorm16(float(u)));	

	vec3 vn = vec3(inp.xy, 1.0 - abs(inp.x) - abs(inp.y));
	if(vn.z < 0.0)
		vn.xy = (1.0 - abs(inp.yx)) * signNotZero(inp.xy);

	return normalize(vn);
}

vec2 decode_diamond(float p)
{
	vec2 v;

	float p_sign = sign(p - 0.5f);
	v.x = -p_sign * 4.0f * p + 1.0f + p_sign * 2.0f;
	v.y = p_sign * (1.0f - abs(v.x));

	return normalize(v);
}

vec4 decode_tangent(vec3 normal, float p)
{
	vec4 outv = vec4(0.0f, 0.0f, 0.0f, 1.0f);
	uint bits = floatBitsToUint(p);
	if((bits & 1) == 1)
		outv.w = -1.0f;

	vec3 t1;
	if(abs(normal.y) > abs(normal.z))
		t1 = vec3(normal.y, -normal.x, 0.0f);
	else
		t1 = vec3(normal.z, 0.0f, -normal.x);

	t1 = normalize(t1);

	vec3 t2 = cross(t1, normal);
	vec2 packed_tangent = decode_diamond(p);
	outv.xyz = packed_tangent.x * t1 + packed_tangent.y * t2;
	return outv;
}

float encode_diamond(vec2 p)
{
	float x = p.x / (abs(p.x) + abs(p.y));

	float py_sign = sign(p.y);
	return -py_sign * 0.25f * x + 0.5f + py_sign * 0.25f;
}

float encode_tangent(vec3 normal, vec3 tangent, bool flip)
{
	vec3 t1;
	if(abs(normal.y) > abs(normal.z))
		t1 = vec3(normal.y, -normal.x, 0.0f);
	else
		t1 = vec3(normal.z, 0.0f, -normal.x);

	t1 = normalize(t1);

	vec3 t2 = cross(t1, normal);
	vec2 packed_tangent = vec2(dot(tangent, t1), dot(tangent, t2));
	float diamond = encode_diamond(packed_tangent);

	uint fbits = floatBitsToUint(diamond);
	if(flip)
		fbits |= 1;
	else
		fbits &= (~1);

	return uintBitsToFloat(fbits);
}
