#pragma once

#ifndef __JAISCRIPT_DETAIL_BUILTIN_METHODS_HPP__
#define __JAISCRIPT_DETAIL_BUILTIN_METHODS_HPP__

#include <jaiscript/core/value.hpp>
#include <functional>
#include <unordered_map>
#include <vector>

namespace jai {

    class string_symbolizer;

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

} // namespace jai

#endif // __JAISCRIPT_DETAIL_BUILTIN_METHODS_HPP__
