#pragma once

uint rng_rotl(uint x, uint k)
{
	return (x << k) | (x >> (32 - k));
}

uint rng_hash(uint seed)
{
	seed = (seed ^ 61) ^ (seed >> 16);
	seed *= 9;
	seed = seed ^ (seed >> 4);
	seed *= 0x27d4eb2d;
	seed = seed ^ (seed >> 15);
	return seed;
}

struct RNGState
{
	uint s0;
	uint s1;

	__init(uint2 id, uint frame)
	{
		s0 = rng_hash((id.x << 16) | id.y);
		s1 = rng_hash(frame);
		next();
	}

	[mutating]
	uint next()
	{
		uint result = s0 * 0x9e3779bb;

		s1 ^= s0;
		s0 = rng_rotl(s0, 26) ^ s1 ^ (s1 << 9);
		s1 = rng_rotl(s1, 13);
	
		return result;
	}

	[mutating]
	float next_float()
	{
		uint u = 0x3f800000 | (next() >> 9);
		return reinterpret<float>(u) - 1.0;
	}
};
