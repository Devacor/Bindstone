#pragma once

#ifndef __JAISCRIPT_CORE_OVERLOAD_HPP__
#define __JAISCRIPT_CORE_OVERLOAD_HPP__

namespace jai {

namespace detail {
	template<typename... Args>
	struct overload_selector {
		template<typename R, typename C>
		consteval auto operator()(R (C::*member)(Args...)) const noexcept { return member; }
		template<typename R, typename C>
		consteval auto operator()(R (C::*member)(Args...) const) const noexcept { return member; }
		template<typename R>
		consteval auto operator()(R (*function)(Args...)) const noexcept { return function; }
	};

	template<typename... Args>
	struct const_overload_selector {
		template<typename R, typename C>
		consteval auto operator()(R (C::*member)(Args...) const) const noexcept { return member; }
	};
}

// jai::overload<const std::string&>(&Button::text) is the overload of an overloaded name taking exactly those parameters
template<typename... Args>
inline constexpr detail::overload_selector<Args...> overload{};

// The const one, where a const and a non-const overload take the same parameters
template<typename... Args>
inline constexpr detail::const_overload_selector<Args...> const_overload{};

} // namespace jai

#endif // __JAISCRIPT_CORE_OVERLOAD_HPP__
