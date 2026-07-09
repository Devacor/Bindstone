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
    };

    // ONE implementation shared by every backend (defined in interpreter.cpp)
    void init_builtin_method_registries(string_symbolizer* symbolizer, builtin_method_registries& out);

    namespace detail {
        // Emits ONE warning per class-defined method (local, not inherited) that shadows a
        // shared_ptr handle builtin (k_shadowable_handle_method_names). Called at class-
        // definition time by BOTH backends after register_script_class - always-on
        // (independent of static checking), parity-by-construction. Defined in interpreter.cpp.
        void warn_shadowed_handle_builtins(engine& eng, const class_definition& def);
    }

} // namespace jai

#endif // __JAISCRIPT_DETAIL_BUILTIN_METHODS_HPP__
