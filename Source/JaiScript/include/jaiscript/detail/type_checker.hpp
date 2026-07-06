#pragma once

#ifndef __JAISCRIPT_DETAIL_TYPE_CHECKER_HPP__
#define __JAISCRIPT_DETAIL_TYPE_CHECKER_HPP__

// Opt-in static type checking (docs/static_checking.md). Backend-neutral: walks the
// parsed AST both backends share, once per unique source (amortized like parsing).
// This header stays light — engine.hpp includes it for the mode/diagnostics API; the
// walker lives in source/implementation/type_checker.cpp.

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace jai {

    class engine;
    class declaration;
    using declaration_ptr = std::shared_ptr<declaration>;

    enum class check_mode : uint8_t {
        off,     // zero behavior change, zero cost (one branch at parse-cache entry)
        warn,    // diagnostics collected + retrievable; execution proceeds
        strict   // any error-severity diagnostic throws static_check_error before anything runs
    };

    struct check_diagnostic {
        enum class level : uint8_t { warning, error };
        level severity = level::error;
        size_t line = 0;
        size_t column = 0;
        std::string message;
        // Related source point (e.g. the declaration a bad assignment violates); 0 = none.
        size_t related_line = 0;
        size_t related_column = 0;
    };

    struct check_report {
        std::vector<check_diagnostic> diagnostics;
        // True when the diagnostic cap (detail::k_check_diagnostic_cap) was hit.
        bool capped = false;

        bool has_errors() const {
            for (const auto& d : diagnostics) {
                if (d.severity == check_diagnostic::level::error) { return true; }
            }
            return false;
        }
        size_t error_count() const {
            size_t n = 0;
            for (const auto& d : diagnostics) {
                if (d.severity == check_diagnostic::level::error) { ++n; }
            }
            return n;
        }

        // Compiler-style listing, one diagnostic per entry:
        //   script:12:5: error: cannot assign 'string' to 'x' declared 'int' (declared at 8:3)
        //       int x = "hello";
        //           ^
        // The offending source line + caret are included when `source` is provided
        // (a jaibite loaded from bytes has no source; the location prefix still prints).
        std::string format(std::string_view source = {}, std::string_view script_name = "script") const;
    };

    // Strict-mode failure: thrown by execute()/jaibite creation/load before any code
    // runs. what() carries the full accumulated, location-sorted, deduplicated listing.
    class static_check_error : public std::runtime_error {
    public:
        explicit static_check_error(const std::string& message) : std::runtime_error(message) {}
    };

    namespace detail {

        inline constexpr size_t k_check_diagnostic_cap = 100;

        // AST-level static check against `eng`'s current registration surface.
        // extra_globals: names the host defines for this execution (instance variables).
        // Diagnostics come back location-sorted and deduplicated, capped at
        // k_check_diagnostic_cap. Mode-independent — callers decide what to do with it.
        check_report run_static_check(engine& eng, const std::vector<declaration_ptr>& decls,
                                      const std::vector<std::string>* extra_globals = nullptr);

    } // namespace detail

} // namespace jai

#endif // __JAISCRIPT_DETAIL_TYPE_CHECKER_HPP__
