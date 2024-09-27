module;

#include <cassert>

export module lumina.ecs:realm;

import :entity;
import :pool;
import :view;

import lumina.core;
import std;

export namespace lumina::ecs
{

using hash_type = std::uint32_t;

class Realm
{
public:
	Realm() = default;

	Realm(const Realm&) = delete;
	Realm& operator=(const Realm&) = delete;

	Realm(Realm&&) = delete;
	Realm& operator=(Realm&&) = delete;

	ecs::entity spawn()
	{
		if(next_entity >= 0xFFFFFF - 1 || next_entity >= max_entities)
			throw std::runtime_error("ecs: out of entity handles");

		if(entities_to_recycle.empty())
		{
			next_entity++;
			return ecs::entity{next_entity};
		}

		ecs::entity recycled = entities_to_recycle.back();
		entities_to_recycle.pop_back();
		return recycled;
	}

	void kill(const ecs::entity ent)
	{
		assert(ent.is_valid());
		entities_to_recycle.push_back(ecs::entity(ent.as_handle(), ent.as_version() + 1));
	}

	template <typename T, typename... Args>
	T* emplace(const ecs::entity ent, Args... args)
	{
		assert(ent.is_valid());
		type_storage<T>* c_pool = ensure_pool<T>();
		return c_pool->emplace(ent, std::forward<Args>(args)...);
	}

	template <typename T>
	T& get(const ecs::entity ent)
	{
		assert(ent.is_valid());
		type_storage<T>* c_pool = ensure_pool<T>();
		return c_pool->get(ent);
	}

	template <typename T>
	T* try_get(const ecs::entity ent)
	{
		if(!contains<T>(ent))
			return nullptr;

		return &get<T>(ent);
	}

	template <typename... T>
	ecs::view<type_storage<T>...> view()
	{
		return {ensure_pool<T>()...};
	}

	template <typename T>
	bool contains(const ecs::entity ent)
	{
		type_storage<T>* c_pool = ensure_pool<T>();
		return c_pool->contains(ent);
	}
private:
	template <typename T>
	type_storage<T>* ensure_pool()
	{
		hash_type type = type_hash<T>::get();
		if(!c_pools.contains(type))
			c_pools[type] = std::make_shared<component_pool<T>>();

		return reinterpret_cast<type_storage<T>*>(c_pools[type].get());
	}

	static constexpr std::size_t max_entities = 65536;

	ecs::entity::handle_type next_entity = 0;
	std::vector<ecs::entity> entities_to_recycle;
	std::unordered_map<hash_type, std::shared_ptr<component_pool<ecs::entity>::base_type>> c_pools;
};

}
