export module lumina.core:type_traits;
import std;

export namespace lumina
{

template <typename, typename>
struct is_applicable: std::false_type{};

template <typename Func, template<typename...> class Tuple, typename... Args>
struct is_applicable<Func, Tuple<Args...>> : std::is_invocable<Func, Args...> {};

template <typename Func, template<typename...> class Tuple, typename... Args>
struct is_applicable<Func, const Tuple<Args...>> : std::is_invocable<Func, Args...> {};

template <typename Func, typename Args>
inline constexpr bool is_applicable_v = is_applicable<Func, Args>::value;

}
