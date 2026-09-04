#pragma once

#include <jaiscript/core/checked_result.hpp>
#include <jaiscript/core/runtime_errors.hpp>

#include <cstdint>
#include <string>

namespace jai::detail {

// Arity texts (docs/diagnostics.md family C): "'fib' expects 1 argument, got 2" / "lambda expects
// 1 argument, got 0". {0} = the callee (its name view points into symbolizer storage, so the
// intern is a hit even while frozen), {1} = "N argument(s), got M", composed only while the
// symbolizer is unfrozen; a parallel worker gets the declared count alone from the table below.
// Viability itself is jai::arity_accepts (overload_resolution.hpp).
inline constexpr std::string_view arity_named = "'{0}' expects {1}";
inline constexpr std::string_view arity_named_one = "'{0}' expects {1} argument";
inline constexpr std::string_view arity_named_many = "'{0}' expects {1} arguments";
inline constexpr std::string_view arity_lambda = "lambda expects {0}";
inline constexpr std::string_view arity_lambda_one = "lambda expects {0} argument";
inline constexpr std::string_view arity_lambda_many = "lambda expects {0} arguments";
inline constexpr std::string_view lambda_callee_name = "<lambda>";

// Counts ride the checked_result symbol channel, so their decimal spellings are interned once
// at backend init: error paths never intern (parallel workers run against a frozen symbolizer)
struct arity_count_symbols {
    static constexpr size_t count = 64;
    uint64_t ids[count] = {};
    template <typename Symbolizer>
    void intern(const Symbolizer& symbolizer) {
        for (size_t i = 0; i < count; ++i) { ids[i] = symbolizer.intern(std::to_string(i)); }
    }
    template <typename Symbolizer>
    uint64_t operator()(size_t n, const Symbolizer& symbolizer) const {
        return n < count ? ids[n] : symbolizer.intern(std::to_string(n));
    }
};

inline std::string arity_detail(size_t expected, size_t got) {
    return std::to_string(expected) + (expected == 1 ? " argument, got " : " arguments, got ") + std::to_string(got);
}

template <typename Symbolizer>
inline error_propagator arity_error(std::string_view callee, size_t expected, size_t got,
                                    const arity_count_symbols& symbols, const Symbolizer& symbolizer) {
    const std::error_code ec = make_error_code(runtime_error_code::argument_count_mismatch);
    const bool anonymous = callee.empty() || callee == lambda_callee_name;
    if (!symbolizer.frozen()) {
        const uint64_t detail = symbolizer.intern(arity_detail(expected, got));
        if (anonymous) { return {ec, arity_lambda, detail, 0}; }
        return {ec, arity_named, symbolizer.intern(callee), detail};
    }
    const uint64_t count = symbols(expected, symbolizer);
    if (anonymous) { return {ec, expected == 1 ? arity_lambda_one : arity_lambda_many, count, 0}; }
    return {ec, expected == 1 ? arity_named_one : arity_named_many, symbolizer.intern(callee), count};
}

} // namespace jai::detail
