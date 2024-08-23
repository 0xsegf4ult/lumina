export module lumina.core:type_hash;

import :hash;
import std;

using std::size_t;

export namespace lumina
{

template <typename T>
struct type_hash
{
	static constexpr size_t get()
	{
		constexpr auto value = fnv::hash(__PRETTY_FUNCTION__);
		return value;
	}
};

}
