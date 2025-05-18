export module lumina.core;
export import lumina.core.config;
export import lumina.core.log;
export import lumina.core.math;
export import lumina.core.job;
export import :hash;
export import :handle;
export import :object_pool;
export import :type_hash;
export import :type_traits;
export import :refcounted;
export import :typesafe_flags;
export import :array_proxy;
export import :iterable_proxy;

import std;

export namespace lumina
{

template <std::unsigned_integral T>
constexpr T align_up(T val, std::size_t size)
{
	auto mod{static_cast<T>(val % size)};
	val -= mod;
	return static_cast<T>(mod == T{0} ? val : val + size);
}

template <std::unsigned_integral T>
constexpr T align_down(T val, std::size_t size)
{
	return static_cast<T>(val - val % size);
}

}
