export module lumina.core.math:util;

import std;

export namespace lumina
{

template <std::floating_point T>
constexpr T to_radians(T degrees)
{
        return degrees * (std::numbers::pi_v<T> / T(180.0));
}

template <typename T, typename L>
constexpr T mix(const T& a, const T& b, const L& lerp)
{
	return a * (1.0f - lerp) + b * lerp;
}

constexpr float fp_epsilon = std::numeric_limits<float>::epsilon();
constexpr float fp_epsilon_sq = fp_epsilon * fp_epsilon;

}
