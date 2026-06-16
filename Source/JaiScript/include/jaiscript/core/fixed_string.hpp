#pragma once

#include <string>
#include <string_view>
#include <algorithm>
#include <cstddef>

namespace jai {

// fixed_string - C++20 NTTP-compatible compile-time string

template<std::size_t N>
struct fixed_string {
    char value[N]{};

    constexpr fixed_string() = default;

    constexpr fixed_string(const char (&str)[N]) noexcept {
        std::copy_n(str, N, value);
    }

    [[nodiscard]] constexpr std::string_view view() const noexcept {
        return {value, N - 1};
    }

    [[nodiscard]] constexpr operator std::string_view() const noexcept {
        return view();
    }

    [[nodiscard]] std::string str() const {
        return std::string(value, N - 1);
    }

    [[nodiscard]] constexpr std::size_t size() const noexcept {
        return N - 1;
    }

    [[nodiscard]] constexpr const char* c_str() const noexcept {
        return value;
    }

    // For use as array member (property.hpp compatibility)
    [[nodiscard]] constexpr const char* data() const noexcept {
        return value;
    }

    constexpr bool operator==(const fixed_string&) const = default;
};

template<std::size_t N>
fixed_string(const char (&)[N]) -> fixed_string<N>;

} // namespace jai
