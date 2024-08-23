export module lumina.core:handle;

import std;
using std::uint32_t, std::size_t;

export namespace lumina
{
	template <typename T, typename tag>
	struct StronglyTyped
	{
		StronglyTyped() = default;

		explicit constexpr StronglyTyped(const T& value) noexcept(std::is_nothrow_copy_constructible<T>::value) : storage(value) {}
		explicit constexpr StronglyTyped(T&& value) noexcept(std::is_nothrow_move_constructible<T>::value) : storage(std::move(value)) {}

		constexpr operator const T&() const
		{
			return storage;
		}

		constexpr operator T&()
		{
			return storage;
		}

		constexpr T get_storage() const
		{
			return storage;
		}
	private:
		T storage;
	};

	template <typename T>
	struct handle_tag;

	template <typename T>
	using Handle = StronglyTyped<uint32_t, handle_tag<T>>;
}

export template <typename T, typename tag>
struct std::hash<lumina::StronglyTyped<T, tag>>
{
	size_t operator()(const lumina::StronglyTyped<T, tag>& input) const noexcept
	{
		std::hash<T> hobj;
		return hobj(input.get_storage());
	}
};

export template <typename T, typename tag>
struct std::formatter<lumina::StronglyTyped<T, tag>> : std::formatter<T>
{
};
