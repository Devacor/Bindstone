#pragma once

#ifndef __JAISCRIPT_DETAIL_BUILTIN_METHODS_HPP__
#define __JAISCRIPT_DETAIL_BUILTIN_METHODS_HPP__

#include <jaiscript/core/value.hpp>
#include <functional>
#include <unordered_map>
#include <vector>
#include <array>
#include <string_view>

namespace jai {

    class string_symbolizer;
    class class_definition;

    // shared_ptr HANDLE builtin method names that a user class method now SHADOWS and
    // WINS over (Dev ruling 2026-07). same_as is deliberately EXCLUDED: it is a UNIVERSAL
    // identity primitive available on every value (and excluded from the fast method-call
    // gate), not a per-class-overridable handle method. weak_ptr's lock/expired never
    // collide here - they dispatch on weak_ptr VALUES, which are not user class instances.
    inline constexpr std::array<std::string_view, 4> k_shadowable_handle_method_names = {
        std::string_view{"reset"}, std::string_view{"use_count"},
        std::string_view{"unique"}, std::string_view{"cpp_ref_count"}
    };

    // Backend-neutral invocation context for the builtin container/string/pointer methods
    struct builtin_method_context {
        engine* engine_ref = nullptr;
        string_symbolizer* symbolizer = nullptr;

        engine* get_engine() const { return engine_ref; }
        string_symbolizer* get_string_symbolizer() const { return symbolizer; }

        script_value make_value() const { return script_value(std::monostate{}, engine_ref); }
        script_value make_value(script_int v) const { return script_value(v, engine_ref); }
        script_value make_value(script_float v) const { return script_value(v, engine_ref); }
        script_value make_value(script_bool v) const { return script_value(v, engine_ref); }
        script_value make_value(script_char v) const { return script_value(v, engine_ref); }
        script_value make_value(const script_string& v) const { return script_value(v, engine_ref); }
        script_value make_value(script_string&& v) const { return script_value(std::move(v), engine_ref); }
    };

    using builtin_method = std::function<checked_result<script_value>(const builtin_method_context&, script_value&, const std::vector<script_value>&)>;

    struct builtin_method_registries {
        std::unordered_map<uint64_t, builtin_method> array_methods;
        std::unordered_map<uint64_t, builtin_method> map_methods;
        std::unordered_map<uint64_t, builtin_method> string_methods;
        std::unordered_map<uint64_t, builtin_method> weak_ptr_methods;
        std::unordered_map<uint64_t, builtin_method> shared_ptr_methods;

        // Dense dispatch tables, NAME-SORTED so indices are identical for every engine
        // of the same binary (symbol ids are engine-local; names are not) — call sites
        // bake indices into chunks that worker backends read through their own
        // registries. The name→index maps are consulted at COMPILE time only; dispatch
        // is one indexed load. Pointers into the maps are node-stable after init.
        static constexpr uint16_t k_no_builtin = 0xFFFF;
        std::vector<const builtin_method*> array_by_index;
        std::vector<const builtin_method*> map_by_index;
        std::vector<const builtin_method*> string_by_index;
        std::unordered_map<uint64_t, uint16_t> array_index;
        std::unordered_map<uint64_t, uint16_t> map_index;
        std::unordered_map<uint64_t, uint16_t> string_index;
    };

    // ONE implementation shared by every backend (defined in interpreter.cpp)
    void init_builtin_method_registries(string_symbolizer* symbolizer, builtin_method_registries& out);

    namespace detail {
        // Emits ONE warning per class-defined method (local, not inherited) that shadows a
        // shared_ptr handle builtin (k_shadowable_handle_method_names). Called at class-
        // definition time by BOTH backends after register_script_class - always-on
        // (independent of static checking), parity-by-construction. Defined in interpreter.cpp.
        void warn_shadowed_handle_builtins(engine& eng, const class_definition& def);

        // Builtin args are BY-VALUE. A subscript read in argument position mints an
        // owner-pinned element/map-entry reference (transient_read.hpp leaves call args
        // unmarked so ref params can bind), and script/native parameter binding derefs
        // it - but builtins receive the raw args vector, where a reference-typed key
        // never matches map.find (operator<=> doesn't deref) and unchecked_as_string
        // misreads the holder. Normalize at EVERY builtin dispatch boundary, one shared
        // spelling for both backends, so builtins see what a hoisted temp would.
        inline void deref_builtin_args_in_place(std::vector<script_value>& args) {
            for (auto& arg : args) {
                if (arg.is_reference()) [[unlikely]] {
                    script_value resolved(arg.deref());
                    arg = std::move(resolved);
                }
            }
        }
        inline const std::vector<script_value>& deref_builtin_args(
                const std::vector<script_value>& args, std::vector<script_value>& scratch) {
            bool needs_resolve = false;
            for (const auto& arg : args) {
                if (arg.is_reference()) { needs_resolve = true; break; }
            }
            if (!needs_resolve) [[likely]] {
                return args;
            }
            scratch.reserve(args.size());
            for (const auto& arg : args) {
                scratch.emplace_back(arg.is_reference() ? script_value(arg.deref()) : arg);
            }
            return scratch;
        }
    }

} // namespace jai

#endif // __JAISCRIPT_DETAIL_BUILTIN_METHODS_HPP__
