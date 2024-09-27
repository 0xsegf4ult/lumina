export module lumina.ecs:view;

import :entity;
import :pool;

import lumina.core;
import std;

export namespace lumina::ecs
{

template <typename T, typename... Other>
class view
{
public:
	using common_type = std::common_type_t<typename T::base_type, typename Other::base_type...>;

	view(T* fv, Other*... values) noexcept : pools{fv, values...}, v{}
	{
		select_pool();
	}

	template <typename Fn>
	void for_each(Fn func) const
	{
		if(v)
			find_pool_for_each(func, std::index_sequence_for<T, Other...>{});
	}
private:
	void select_pool() noexcept
	{
		v = std::get<0>(pools);
		std::apply([this](auto*, auto*... other)
		{
			((this->v = (other->size() < this->v->size() ) ? other : this->v), ...);
		}, pools);
	}

	template <std::size_t... Is>
	[[nodiscard]] auto get_as_tuple(const ecs::entity ent, std::index_sequence<Is...>) const noexcept
	{
		return std::tuple_cat(std::get<Is>(pools)->get_as_tuple(ent)...);
	}

	template <std::size_t... Is>
	[[nodiscard]] decltype(auto) get_as_tuple(const ecs::entity ent) const
	{
		if constexpr(sizeof...(Is) == 0)
			return get_as_tuple(ent, std::index_sequence_for<T, Other...>{});
		else if constexpr(sizeof...(Is) == 1)
			return (std::get<Is>(pools)->get_as_tuple(ent), ...);
		else
			return std::tuple_cat(std::get<Is>(pools)->get_as_tuple(ent)...);
	}

	template <std::size_t CPool, std::size_t OPool, typename... Args>
	[[nodiscard]] auto access_pool(std::tuple<entity, Args...>& current) const
	{
		if constexpr(CPool == OPool)
			return std::forward_as_tuple(std::get<Args>(current)...);
		else
			return std::get<OPool>(pools)->get_as_tuple(std::get<0>(current));
	}

	template <std::size_t CurIdx, typename Fn, std::size_t... Is>
	void internal_for_each(Fn& func, std::index_sequence<Is...>) const
	{
		for(auto pair : (std::get<CurIdx>(pools)->pair_iterator()))
		{
			if(const auto entity = std::get<0>(pair); ((CurIdx == Is || std::get<Is>(pools)->contains(entity)) && ...))
			{
				if constexpr(is_applicable_v<Fn, decltype(std::tuple_cat(std::tuple<ecs::entity>{}, std::declval<view>().get_as_tuple({})))>)
					std::apply(func, std::tuple_cat(std::make_tuple(entity), access_pool<CurIdx, Is>(pair)...));
				else
					std::apply(func, std::tuple_cat(access_pool<CurIdx, Is>(pair)...));
			}
		}
	}

	template <typename Fn, std::size_t... Is>
	void find_pool_for_each(Fn& func, std::index_sequence<Is...> idx) const
	{
		((std::get<Is>(pools) == v ? internal_for_each<Is>(func, idx) : void()), ...);
	}

	std::tuple<T*, Other*...> pools;
	common_type* v;
};

template <typename T>
class view<T>
{
public:
	using iterator = T::iterator;

	view(T* value) noexcept : v{value}
	{
	}

	[[nodiscard]] explicit operator bool() const noexcept
	{
		return (v != nullptr);
	}

	[[nodiscard]] iterator begin() const noexcept
	{
		return v ? v->begin() : iterator{};
	}

	[[nodiscard]] iterator end() const noexcept
	{
		return v ? v->end() : iterator{};
	}

	template <typename Fn>
	void for_each(Fn func) const
	{
		if(v)
		{
			if constexpr(std::is_invocable_v<Fn, decltype(*v->begin())>)
			{
				for(auto&& component : *v)
					func(component);
			}
			else
			{
				for(auto& component: *v)
					func();
			}
		}
	}
private:
	T* v;
};

}
