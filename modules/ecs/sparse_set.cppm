export module lumina.ecs:sparse_set;

import :entity;
import std;

export namespace lumina::ecs
{

class sparse_set
{
	using dense_storage_type = std::vector<entity>;
	using sparse_storage_type = std::vector<entity::handle_type>;
public:
	using pointer = dense_storage_type::pointer;
	using iterator = dense_storage_type::iterator;

	sparse_set() = default;
	virtual ~sparse_set() = default;
	sparse_set(const sparse_set& other) noexcept = default;
	sparse_set(sparse_set&& other) noexcept : dense{std::move(other.dense)}, sparse{std::move(other.sparse)} {}

	sparse_set& operator=(const sparse_set& other) noexcept = default;

	sparse_set& operator=(sparse_set&& other) noexcept
	{
		dense = std::move(other.dense);
		sparse = std::move(other.sparse);
		return *this;
	}

	iterator begin() noexcept
	{
		return dense.begin();
	}

	iterator end() noexcept
	{
		return dense.end();
	}

	void swap(sparse_set& other) noexcept
	{
		std::swap(sparse, other.sparse);
		std::swap(dense, other.dense);
	}

	[[nodiscard]] constexpr entity::handle_type packed_index(entity index) const
	{
		return sparse[index.as_handle()];
	}

	[[nodiscard]] constexpr entity dense_index(entity::handle_type index) const
	{
		return dense[index];
	}

	constexpr entity& operator[](entity index)
	{
		return dense[sparse[index.as_handle()]];
	}

	[[nodiscard]] virtual bool empty() const noexcept
	{
		return dense.empty();
	}

	[[nodiscard]] virtual std::size_t size() const noexcept
	{
		return dense.size();
	}

	[[nodiscard]] virtual std::size_t capacity() const noexcept
	{
		return dense.capacity();
	}

	[[nodiscard]] virtual pointer data() noexcept
	{
		return dense.data();
	}

	virtual void push_back(const entity value)
	{
		dense.push_back(value);

		if(sparse.size() <= value.as_handle())
			sparse.resize(value.as_handle() + 1);

		sparse[value.as_handle()] = dense.size() - 1;
	}

	virtual void erase(const entity index)
	{
		const entity tmp = dense.back();
		dense[sparse[index.as_handle()]] = tmp;
		sparse[tmp.as_handle()] = sparse[index.as_handle()];

		dense.pop_back();
	}

	virtual bool contains(const entity ent)
	{
		if(!ent.is_valid())
			return false;

		if(ent.as_handle() >= sparse.size())
			return false;

		return this->operator[](ent) == ent;
	}
private:
	dense_storage_type dense;
	sparse_storage_type sparse;
};

}
