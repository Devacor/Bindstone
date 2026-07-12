#pragma once

#ifndef __JAISCRIPT_STDLIB_STRINGS_HPP__
#define __JAISCRIPT_STDLIB_STRINGS_HPP__

#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/script_namespace.hpp>

#include <cctype>
#include <string>
#include <vector>

namespace jai {
namespace stdlib {

    // string:: namespace — NON-MUTATING transforms. The method forms
    // (s.to_lower() / s.to_upper()) mutate the receiver in place; these take the
    // string as an argument and return a new one, leaving the argument untouched.
    // ASCII byte semantics (chars are bytes 0..255 under the promotion rules).
    inline void register_string_functions(engine& eng_ref) {
        auto& ns = eng_ref.script_namespaces()[eng_ref.symbolize("string")];
        if (!ns) { ns = std::make_shared<script_namespace_data>(); }
        engine* eng = &eng_ref;

        ns->variables[eng_ref.symbolize("to_lower")] = script_value::make_function(
            [eng](const std::vector<script_value>& args) -> checked_result<script_value> {
                if (args.size() != 1) {
                    return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch),
                        "string::to_lower expects exactly 1 argument");
                }
                const script_value& arg = args[0].deref();
                if (!arg.is_string()) {
                    return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch),
                        "string::to_lower expects a string argument");
                }
                std::string out = arg.as<std::string>();
                for (auto& c : out) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
                return script_value(std::move(out), eng);
            }, &eng_ref);

        ns->variables[eng_ref.symbolize("to_upper")] = script_value::make_function(
            [eng](const std::vector<script_value>& args) -> checked_result<script_value> {
                if (args.size() != 1) {
                    return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch),
                        "string::to_upper expects exactly 1 argument");
                }
                const script_value& arg = args[0].deref();
                if (!arg.is_string()) {
                    return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch),
                        "string::to_upper expects a string argument");
                }
                std::string out = arg.as<std::string>();
                for (auto& c : out) { c = static_cast<char>(std::toupper(static_cast<unsigned char>(c))); }
                return script_value(std::move(out), eng);
            }, &eng_ref);
    }

} // namespace stdlib
} // namespace jai

#endif // __JAISCRIPT_STDLIB_STRINGS_HPP__
