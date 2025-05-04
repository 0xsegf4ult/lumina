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
	
		#if defined(__clang__) || defined(__GNUC__) || defined(__GNUG__)
		constexpr auto value = fnv::hash(__PRETTY_FUNCTION__);
		#elif defined(_MSC_VER)
		constexpr auto value = fnv::hash(__FUNCSIG__);
		#endif
		return value;
	}
};

}
