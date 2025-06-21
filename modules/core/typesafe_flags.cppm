export module lumina.core:typesafe_flags;

import std;

export namespace lumina
{

template <typename T>
struct typesafe_flag_traits
{
	constexpr static bool bitmask_enabled = false;
};

template <typename T>
class typesafe_flags
{
public:
        using storage_type = typename std::underlying_type<T>::type;

        constexpr typesafe_flags() noexcept : storage{0} {}
        constexpr typesafe_flags(T val) noexcept : storage{static_cast<storage_type>(val)} {}
        constexpr explicit typesafe_flags(storage_type val) noexcept : storage{val} {}

        constexpr typesafe_flags(const typesafe_flags& val) noexcept = default;
        constexpr typesafe_flags& operator=(const typesafe_flags& rhs) noexcept = default;

	constexpr typesafe_flags(typesafe_flags&& val) noexcept = default;
	constexpr typesafe_flags& operator=(typesafe_flags&& rhs) noexcept = default;

        constexpr auto operator<=>(const typesafe_flags& rhs) const = default;

        constexpr typesafe_flags operator&(const typesafe_flags& rhs) const noexcept
        {
                return typesafe_flags(storage & rhs.storage);
        }

        constexpr typesafe_flags operator|(const typesafe_flags& rhs) const noexcept
        {
                return typesafe_flags(storage | rhs.storage);
        }

        constexpr typesafe_flags operator^(const typesafe_flags& rhs) const noexcept
        {
                return typesafe_flags(storage ^ rhs.storage);
        }

        constexpr typesafe_flags& operator&=(const typesafe_flags& rhs) noexcept
        {
                storage &= rhs.storage;
                return *this;
        }

        constexpr typesafe_flags& operator|=(const typesafe_flags& rhs) noexcept
        {
                storage |= rhs.storage;
                return *this;
        }

        constexpr typesafe_flags& operator^=(const typesafe_flags& rhs) noexcept
        {
                storage ^= rhs.storage;
                return *this;
        }

        constexpr operator storage_type() const noexcept
        {
                return storage;
        }

        constexpr operator bool() const noexcept
       	{
                return storage > storage_type(0);
        }

        constexpr bool operator !() const noexcept
        {
                return storage <= storage_type(0);
        }
private:
        storage_type storage;
};

template <typename T>
constexpr typesafe_flags<T> operator&(T bit, const typesafe_flags<T>& flags) noexcept
{
        return flags.operator&(bit);
}

template <typename T>
constexpr typesafe_flags<T> operator|(T bit, const typesafe_flags<T>& flags) noexcept
{
        return flags.operator|(bit);
}

template <typename T>
constexpr typesafe_flags<T> operator^(T bit, const typesafe_flags<T>& flags) noexcept
{
        return flags.operator^(bit);
}

template <typename T, typename std::enable_if<typesafe_flag_traits<T>::bitmask_enabled, bool>::type = true>
constexpr typesafe_flags<T> operator&(T lhs, T rhs) noexcept
{
        return typesafe_flags<T>(lhs) & rhs;
}

template <typename T, typename std::enable_if<typesafe_flag_traits<T>::bitmask_enabled, bool>::type = true>
constexpr typesafe_flags<T> operator|(T lhs, T rhs) noexcept
{
        return typesafe_flags<T>(lhs) | rhs;
}

template <typename T, typename std::enable_if<typesafe_flag_traits<T>::bitmask_enabled, bool>::type = true>
constexpr typesafe_flags<T> operator^(T lhs, T rhs) noexcept
{
        return typesafe_flags<T>(lhs) ^ rhs;
}

}
