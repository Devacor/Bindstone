#pragma once

#ifndef __JAISCRIPT_DETAIL_TYPE_CHECKER_HPP__
#define __JAISCRIPT_DETAIL_TYPE_CHECKER_HPP__

// Static type checking (docs/static_checking.md), strict by default. Backend-neutral: walks the
// parsed AST both backends share, once per unique source (amortized like parsing).
// This header stays light — engine.hpp includes it for the mode/diagnostics API; the
// walker lives in source/implementation/type_checker.cpp.

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace jai {

    class engine;
    class declaration;
    class class_decl;
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
        // Raised inside a [[strict_mode]] region: gates the load under engine warn mode too.
        bool strict_region = false;
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
        bool has_strict_errors() const {
            for (const auto& d : diagnostics) {
                if (d.strict_region && d.severity == check_diagnostic::level::error) { return true; }
            }
            return false;
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

    // Stamp counters from one proof pass (engine::last_proof_stats): nodes that received a
    // proof, by kind. Independent of diagnostics; all zero after a clearing pass.
    struct proof_stats {
        uint32_t stores = 0;    // assignment_expr::static_store_proof
        uint32_t decls = 0;     // variable_decl::static_init_proof
        uint32_t args = 0;      // call_expr::static_arg_proofs entries == proof_same_type
        uint32_t returns = 0;   // return_stmt::static_return_proof
        uint32_t members = 0;   // member_expr / identifier_expr field proofs
        uint32_t calls = 0;     // call_expr nodes with a proven callee body
    };

    namespace detail {

        inline constexpr size_t k_check_diagnostic_cap = 100;

        // Program closure classes (engine.cpp, docs/static_checking.md "Programs"): every
        // top-level class of every closure unit by name, the declaring node kept alive here so
        // the checker can model it exactly as it models the unit's own classes. A re-gather
        // replaces an entry (newest declaration wins); a unit's own declaration always wins
        // over the table.
        using program_class_map = std::unordered_map<std::string, std::shared_ptr<class_decl>>;

        // One checker walk does two independent jobs: diagnostics (off -> the report stays
        // empty) and the proof pass (stamp = derive and write proofs onto the AST, clear =
        // reset every proof field, leave = never touch them, e.g. the per-call instance-
        // variable check that must not disturb the cached entry's stamps).
        struct check_options {
            bool diagnostics = true;
            enum class proofs_t : uint8_t { leave, stamp, clear };
            proofs_t proofs = proofs_t::stamp;
            proof_stats* stats = nullptr;   // filled by stamp/clear passes when given
            // Program closure names (engine.cpp, docs/static_checking.md "Programs"): every
            // top-level name of every unit reachable through literal import/include, treated
            // exactly like extra_globals.
            const std::unordered_set<std::string>* program_names = nullptr;
            // Program closure classes: consulted when the unit's own class table misses, ahead
            // of the engine's registrations. Diagnostics only until the class is registered -
            // a member typed through the table alone never seeds a proof.
            const program_class_map* program_classes = nullptr;
        };

        // Every top-level name `decls` declares (functions, variables, externs, destructured
        // names, classes, enums and their values, namespaces with every `::` prefix, namespace
        // members flattened) - the unit's contribution to a program closure.
        void collect_unit_names(const std::vector<declaration_ptr>& decls, std::unordered_set<std::string>& out);
        // Every top-level class `decls` declares (namespace members flattened), newest wins.
        void collect_unit_classes(const std::vector<declaration_ptr>& decls, program_class_map& out);

        // AST-level static check against `eng`'s current registration surface.
        // extra_globals: names the host defines for this execution (instance variables).
        // Diagnostics come back location-sorted and deduplicated, capped at
        // k_check_diagnostic_cap. Mode-independent — callers decide what to do with it.
        check_report run_static_check(engine& eng, const std::vector<declaration_ptr>& decls,
                                      const std::vector<std::string>* extra_globals = nullptr,
                                      check_options opts = {});

    } // namespace detail

} // namespace jai

#endif // __JAISCRIPT_DETAIL_TYPE_CHECKER_HPP__
