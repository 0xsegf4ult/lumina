export module lumina.ecs:pool;

import :entity;
import :sparse_set;

import lumina.core;
import std;

export namespace lumina::ecs
{

template <typename T>
class component_pool : public sparse_set
{
	using component_storage = std::vector<T>;
public:
	using base_type = sparse_set;
	using iterator = component_storage::iterator;
	using const_iterator = component_storage::const_iterator;

	iterator begin() noexcept
	{
		return data.begin();
	}

	iterator end() noexcept
	{
		return data.end();
	}

	constexpr const_iterator cbegin() const noexcept
	{
		return data.cbegin();
	}

	constexpr const_iterator cend() const noexcept
	{
		return data.cend();
	}

	struct pool_pair_iterator
	{
		using value_type = decltype(std::tuple_cat(std::make_tuple(*std::declval<base_type::iterator>()), std::forward_as_tuple(*std::declval<iterator>())));
		using reference = value_type;

		constexpr pool_pair_iterator() : it{} {}
		constexpr pool_pair_iterator(base_type::iterator ent, iterator cmp) : it{ent, cmp} {}
		constexpr pool_pair_iterator(const pool_pair_iterator& other) : it{other.it} {}

		constexpr reference operator*() noexcept
		{
			return {*std::get<base_type::iterator>(it), *std::get<iterator>(it)};
		}

		constexpr pool_pair_iterator& operator++() noexcept
		{
			++std::get<base_type>(it);
			++std::get<iterator>(it);
			return *this;
		}

		constexpr pool_pair_iterator operator++(int) noexcept
		{
			pool_pair_iterator orig = *this;
			++(*this);
			return orig;
		}

		constexpr bool operator==(const pool_pair_iterator& rhs) noexcept
		{
			return std::get<0>(it) == std::get<0>(rhs.it);
		}

		constexpr bool operator!=(const pool_pair_iterator& rhs) noexcept
		{
			return !(*this == rhs);
		}
	private:
		std::tuple<base_type::iterator, iterator> it;
	};

	iterable_proxy<pool_pair_iterator> pair_iterator() noexcept
	{
		return {pool_pair_iterator{base_type::begin(), begin()}, pool_pair_iterator{base_type::end(), end()}};
	}

	T& get(entity ent)
	{
		entity::handle_type idx = base_type::packed_index(ent);
		return data[idx];
	}

	std::tuple<T&> get_as_tuple(entity ent)
	{
		return std::forward_as_tuple(get(ent));
	}

	template <typename... Args>
	T* emplace(const entity ent, Args... args)
	{
		base_type::push_back(ent);
		return &data.emplace_back(std::forward<Args>(args)...);
	}

	void push_back(entity ent, T comp)
	{
		base_type::push_back(ent);
		data.push_back(comp);
	}

	void erase(entity ent)
	{
		auto packed = base_type::operator[](ent);
		base_type::erase(ent);

		data.erase(data.begin() + packed.as_handle());
	}
private:
	component_storage data;
};

template <typename T>
using type_storage = component_pool<T>;

}
