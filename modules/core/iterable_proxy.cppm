export module lumina.core:iterable_proxy;

import std;

export namespace lumina
{

template <typename It>
struct iterable_proxy
{
	using iterator = It;
	using value_type = typename std::iterator_traits<It>::value_type;

	constexpr iterable_proxy() noexcept(std::is_nothrow_default_constructible_v<iterator>) : first{}, last{} {}
	constexpr iterable_proxy(iterator f, iterator l) noexcept(std::is_nothrow_move_constructible_v<iterator>) : first{std::move(f)}, last{std::move(l)} {}
	
	constexpr iterator begin() noexcept
	{
		return first;
	}

	constexpr iterator end() noexcept
	{
		return last;
	}

	constexpr iterator cbegin() const noexcept
	{
		return first;
	}

	constexpr iterator cend() const noexcept
	{
		return last;
	}
private:
	iterator first;
	iterator last;
};

}
