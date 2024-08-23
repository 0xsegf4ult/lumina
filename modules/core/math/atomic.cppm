export module lumina.core.math:atomic;

import std;

export namespace lumina
{

template <typename T>
bool atomic_min(std::atomic<T>& atomic, const T val)
{
	T cur = atomic.load(std::memory_order_relaxed);
	while(cur > val)
	{
		if(atomic.compare_exchange_weak(cur, val, std::memory_order_seq_cst))
			return true;
	}

	return false;
}

template <typename T>
bool atomic_max(std::atomic<T>& atomic, const T val)
{
	T cur = atomic.load(std::memory_order_relaxed);
	while(cur < val)
	{
		if(atomic.compare_exchange_weak(cur, val, std::memory_order_seq_cst))
			return true;
	}

	return false;
}

}
