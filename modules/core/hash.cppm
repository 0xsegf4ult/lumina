export module lumina.core:hash;

import std;

using std::size_t, std::uint32_t, std::uint64_t;

namespace lumina::fnv
{
	constexpr uint32_t prime = 0x1000193u;
	constexpr uint32_t basis = 0x811C9DC5u;

	constexpr size_t strlen_nonull(const char* str)
	{
		size_t out = 0;
		while(str[++out] != '\0');

		return out;
	}
}

export namespace lumina::fnv
{
	constexpr uint32_t hash(const char* str)
	{
		uint32_t out = basis;
		size_t len = strlen_nonull(str);

		for(size_t i = 0; i < len; i++)
			out = (out ^ static_cast<uint32_t>(str[i])) * prime;

		return out;
	}

	constexpr uint32_t operator""_fnv(const char* str)
	{
		return fnv::hash(str);
	}
}

export namespace lumina
{
	constexpr uint64_t hash_combine(const uint64_t seed, const uint64_t hash)
	{
		return seed ^ (hash + 0x9e3779b9 + (seed << 6) + (seed >> 2));
	}
}
