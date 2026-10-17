#include <jaiscript/stdlib/io.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/value.hpp>
#include <jaiscript/core/dynamic_binder.hpp>
#include <jaiscript/detail/format_spec.hpp>
#include <iostream>
#include <sstream>
#include <format>
#include <memory>
#include <cctype>

namespace jai {
namespace stdlib {

    inline bool is_all_digits(std::string_view s) {
        for (char c : s) {
            if (c < '0' || c > '9') return false;
        }
        return !s.empty();
    }

    // A placeholder is {}, {n}, {:spec}, or {n:spec} — spec is the shared subset
    // in detail/format_spec.hpp (same mini-language as template-string `${x:spec}`).
    inline bool has_format_placeholders(const std::string& str) {
        size_t pos = 0;
        while (pos < str.length()) {
            size_t brace = str.find('{', pos);
            if (brace == std::string::npos) break;

            if (brace + 1 < str.length() && str[brace + 1] == '{') {
                pos = brace + 2;
                continue;
            }

            size_t close = str.find('}', brace);
            if (close != std::string::npos) {
                std::string content = str.substr(brace + 1, close - brace - 1);

                if (content.empty()) {
                    return true;
                }

                size_t colon = content.find(':');
                std::string_view index_part = std::string_view(content).substr(0, colon == std::string::npos ? content.size() : colon);
                bool index_ok = index_part.empty() || is_all_digits(index_part);
                if (index_ok) {
                    if (colon == std::string::npos) {
                        if (!index_part.empty()) {
                            return true;
                        }
                    } else {
                        detail::format_spec spec;
                        if (detail::parse_format_spec(std::string_view(content).substr(colon + 1), spec)) {
                            return true;
                        }
                    }
                }
            }

            pos = brace + 1;
        }
        return false;
    }
    
    inline std::string process_format_string(const std::string& format_str,
                                           const std::vector<script_value>& args,
                                           size_t arg_offset = 0) {
        std::string result;
        size_t pos = 0;
        size_t next_arg = arg_offset;
        
        while (pos < format_str.length()) {
            // Look for both { and } to handle escaping
            size_t next_open = format_str.find('{', pos);
            size_t next_close = format_str.find('}', pos);
            
            // Handle }} escaping
            if (next_close != std::string::npos &&
                (next_open == std::string::npos || next_close < next_open)) {
                result += format_str.substr(pos, next_close - pos);
                if (next_close + 1 < format_str.length() && format_str[next_close + 1] == '}') {
                    // }} -> }
                    result += '}';
                    pos = next_close + 2;
                    continue;
                } else {
                    result += '}';
                    pos = next_close + 1;
                    continue;
                }
            }

            if (next_open == std::string::npos) {
                result += format_str.substr(pos);
                break;
            }
            
            result += format_str.substr(pos, next_open - pos);

            if (next_open + 1 < format_str.length() && format_str[next_open + 1] == '{') {
                result += '{';
                pos = next_open + 2;
                continue;
            }
            
            size_t close_brace = format_str.find('}', next_open);
            if (close_brace == std::string::npos) {
                // Invalid format string, append rest as-is
                result += format_str.substr(next_open);
                break;
            }
            
            std::string spec = format_str.substr(next_open + 1, close_brace - next_open - 1);

            if (spec.empty()) {
                // {} - use next argument
                if (next_arg < args.size()) {
                    result += args[next_arg].to_string();
                    next_arg++;
                }
            } else if (size_t colon = spec.find(':'); colon != std::string::npos) {
                // {:spec} / {n:spec} - format through the shared spec formatter
                // (identical to template-string `${x:spec}`). An invalid spec keeps
                // the braces literal, matching the invalid-index leniency below;
                // a type-vs-spec mismatch throws (catchable, both backends).
                std::string index_part = spec.substr(0, colon);
                std::string fmt_part = spec.substr(colon + 1);
                detail::format_spec parsed;
                bool index_ok = index_part.empty() || is_all_digits(index_part);
                if (index_ok && detail::parse_format_spec(fmt_part, parsed)) {
                    size_t arg_index;
                    if (index_part.empty()) {
                        arg_index = next_arg;
                        next_arg++;
                    } else {
                        arg_index = std::stoull(index_part) + arg_offset;
                    }
                    if (arg_index < args.size()) {
                        result += detail::format_value_with_spec(args[arg_index], parsed, fmt_part);
                    }
                } else {
                    result += "{" + spec + "}";
                }
            } else {
                // {n} - use positional argument
                try {
                    size_t arg_index = std::stoull(spec) + arg_offset;
                    if (arg_index < args.size()) {
                        result += args[arg_index].to_string();
                    }
                } catch (...) {
                    // Invalid index, ignore
                    result += "{" + spec + "}";
                }
            }

            pos = close_brace + 1;
        }
        
        return result;
    }
    
    // Special types for controlling print behavior
    struct skip_newline_t {};
    struct skip_flush_t {};
    
    inline const skip_newline_t skip_newline{};
    inline const skip_flush_t skip_flush{};

    // THE object row, shared by type_of and reflect::type_name (stdlib/reflection.hpp):
    // the instance's own class first, then the value tag, then the bare kind - a value
    // tagged with a base class still answers the class it actually holds.
    std::string object_type_name(const script_value& d) {
        try {
            auto instance = d.as<std::shared_ptr<class_instance>>();
            if (instance) {
                if (auto def = instance->get_class_definition()) {
                    return def->get_name();
                }
            }
        } catch (...) {}
        if (auto ti = d.get_type_info()) {
            if (!ti->type_name.empty() && ti->type_name != "any") { return ti->type_name; }
        }
        return "object";
    }

    void register_io_functions(engine& eng_ref) {
        engine* eng = &eng_ref;
        dynamic_binder<skip_newline_t>(eng_ref, "skip_newline_t").build();
        dynamic_binder<skip_flush_t>(eng_ref, "skip_flush_t").build();
        
        auto skip_newline_ptr = std::make_shared<skip_newline_t>();
        auto skip_flush_ptr = std::make_shared<skip_flush_t>();

        eng_ref.add_global("skip_newline", eng_ref.make_object(skip_newline_ptr));
        eng_ref.add_global("skip_flush", eng_ref.make_object(skip_flush_ptr));
        // Usage:
        //   print()                           - Prints just a newline
        //   print(value)                      - Prints value followed by newline
        //   print(format, args...)            - If format contains {}, uses format-style
        //   print(arg1, arg2, ...)            - If no {} in first arg, prints all args sequentially
        //   print(..., skip_newline)          - Skips the newline at end
        //   print(..., skip_flush)            - Skips both newline and flush
        //
        // Format string placeholders (when {} is found in first argument):
        //   {}     - Sequential placeholder, replaced with next argument
        //   {n}    - Positional placeholder, replaced with nth argument (0-based)
        //   {{     - Escaped left brace, prints a literal '{'
        //   }}     - Escaped right brace, prints a literal '}'
        //
        // Examples:
        //   print("Hello, World!")                    // Output: Hello, World!\n
        //   print("Hello", "World", "!")              // Output: HelloWorld!\n
        //   print("Hello, {}!", "JaiScript")          // Output: Hello, JaiScript!\n
        //   print("{} + {} = {}", 2, 3, 5)            // Output: 2 + 3 = 5\n
        //   print("Line 1", skip_newline)             // Output: Line 1 (no newline)
        //   print("Debug: ", value, skip_flush)       // Output: Debug: <value> (no newline/flush)
        //
        eng_ref.add_variadic_function("print", [eng](const std::vector<script_value>& raw_args) -> checked_result<script_value> {
            // a class's own to_string() speaks for its instances, as in to_string(x) and `${x}`; such a text is never a format string
            std::vector<script_value> shown;
            const bool first_was_object = !raw_args.empty() && raw_args[0].deref().type() == script_value_type::jai_object_type;
            for (size_t i = 0; i < raw_args.size() && eng; ++i) {
                const script_value& a = raw_args[i];
                if (a.deref().type() != script_value_type::jai_object_type) { continue; }
                auto control = a.get_type_info();
                if (control && (control->type_name == "skip_newline_t" || control->type_name == "skip_flush_t")) { continue; }
                if (shown.empty()) { shown = raw_args; }
                auto text = eng->text_of(a);
                if (!text) { return text.error_value(); }
                if (eng->backend_unwinding()) { return script_value(std::monostate{}, eng); }
                shown[i] = script_value(std::move(text.value()), eng);
            }
            const std::vector<script_value>& args = shown.empty() ? raw_args : shown;
            std::ostream& out = eng ? eng->get_output_stream() : std::cout;

            if (args.empty()) {
                out << std::endl;
                return script_value(std::monostate{}, eng);
            }

            bool should_newline = true;
            bool should_flush = true;
            size_t effective_args = args.size();

            if (!args.empty()) {
                const auto& last_arg = args.back();
                auto type_info = last_arg.get_type_info();
                if (type_info && type_info->type_name == "skip_newline_t") {
                    should_newline = false;
                    should_flush = true;
                    effective_args--;
                } else if (type_info && type_info->type_name == "skip_flush_t") {
                    should_newline = false;
                    should_flush = false;
                    effective_args--;
                }
            }

            if (effective_args == 0) {
                if (should_newline) {
                    out << std::endl;
                } else if (should_flush) {
                    out << std::flush;
                }
                return script_value(std::monostate{}, eng);
            }

            if (effective_args == 1) {
                // Single-arg print is VERBATIM passthrough by design:
                // {{ }} escapes only apply when format substitution runs — no reason to
                // alter the string when there is nothing to substitute
                out << args[0].to_string();
                if (should_newline) {
                    out << std::endl;
                } else if (should_flush) {
                    out << std::flush;
                }
                return script_value(std::monostate{}, eng);
            }

            std::string first_str = args[0].to_string();
            bool has_format_specs = !first_was_object && has_format_placeholders(first_str);

            if (!has_format_specs) {
                for (size_t i = 0; i < effective_args; ++i) {
                    out << args[i].to_string();
                }
                if (should_newline) {
                    out << std::endl;
                } else if (should_flush) {
                    out << std::flush;
                }
                return script_value(std::monostate{}, eng);
            }

            // arg_offset=1: format string is args[0], actual values start at args[1]
            std::vector<script_value> format_args(args.begin(), args.begin() + effective_args);
            std::string result = process_format_string(first_str, format_args, 1);

            out << result;
            if (should_newline) {
                out << std::endl;
            } else if (should_flush) {
                out << std::flush;
            }
            return script_value(std::monostate{}, eng);
        });
        
        // Usage:
        //   format(value)                      - Converts value to string
        //   format(format_str, args...)        - If format_str contains {}, uses format-style
        //   format(arg1, arg2, ...)            - If no {} in first arg, concatenates all args
        //
        // Examples:
        //   format("Hello, {}!", "World")      // Returns: "Hello, World!"
        //   format("Hello", " ", "World")      // Returns: "Hello World"
        //   format("{} + {} = {}", 2, 3, 5)    // Returns: "2 + 3 = 5"
        //
        eng_ref.add_variadic_function("format", [eng](const std::vector<script_value>& args) -> script_value {
            if (args.empty()) {
                return script_value("", eng);
            }

            // Filter out control types (they don't make sense for format)
            std::vector<script_value> filtered_args;
            for (const auto& arg : args) {
                auto type_info = arg.get_type_info();
                if (!type_info || (type_info->type_name != "skip_newline_t" &&
                                   type_info->type_name != "skip_flush_t")) {
                    filtered_args.push_back(arg);
                }
            }

            if (filtered_args.empty()) {
                return script_value("", eng);
            }

            if (filtered_args.size() == 1) {
                return script_value(filtered_args[0].to_string(), eng);
            }

            std::string first_str = filtered_args[0].to_string();
            bool has_format_specs = has_format_placeholders(first_str);

            if (!has_format_specs) {
                std::string result;
                for (const auto& arg : filtered_args) {
                    result += arg.to_string();
                }
                return script_value(result, eng);
            }

            std::string result = process_format_string(first_str, filtered_args, 1);

            return script_value(result, eng);
        });
        
        // Register as variadic to bypass type matching issues
        // a class's own to_string() speaks for its instances, as in string + value
        constexpr engine::host_direct_invoke to_string_of = [](const void* target, const script_value* args, size_t) -> checked_result<script_value> {
            engine* e = static_cast<engine*>(const_cast<void*>(target));
            if (args[0].type() != script_value_type::jai_object_type) { return script_value(args[0].to_string(), e); }   // only an object's class speaks for it
            auto text = e->text_of(args[0]);
            if (!text) { return text.error_value(); }
            if (e->backend_unwinding()) { return script_value(std::monostate{}, e); }
            return script_value(std::move(text.value()), e);
        };
        eng_ref.add_variadic_function("to_string", [eng, to_string_of](const std::vector<script_value>& args) -> checked_result<script_value> {
            if (args.size() != 1) {
                throw runtime_error("to_string expects exactly 1 argument, got " + std::to_string(args.size()));
            }
            return to_string_of(eng, args.data(), 1);
        });
        eng_ref.add_host_direct("to_string", 1, to_string_of, eng);

        // Opaque host pointers (make_value(T*) on an UNREGISTERED
        // type) are non-null/truthy to script but the engine cannot name them — this
        // predicate is how scripts tell a registered/script object (true) from an opaque
        // host-only pass-through token (false). All ordinary values answer true.
        eng_ref.add_function("is_registered_type", [](const script_value& val) -> bool {
            const script_value& d = val.is_reference() ? val.deref() : val;
            if (d.raw_storage_index() == script_value::TYPEID_CPP_BOUND &&
                !d.is_int() && !d.is_float() && !d.is_bool() && !d.is_char() && !d.is_string()) {
                return false;
            }
            return true;
        });

        // Byte minting: explicit int -> char, range-checked.
        // The reverse of promotion; there is deliberately no IMPLICIT int -> char.
        eng_ref.add_function("to_char", [](script_int n) -> script_char {
            if (n < 0 || n > 255) {
                throw runtime_error("to_char expects 0..255, got " + std::to_string(n));
            }
            return static_cast<script_char>(static_cast<unsigned char>(n));
        });

        eng_ref.add_function("utf8_string", [eng = &eng_ref](const script_value& val) -> script_value {
            const script_value& v = val.deref();
            return script_value::make_utf8(v.is_text() ? v.unchecked_text() : v.to_string(), eng);
        });

        eng_ref.add_function("type_of", [](const script_value& val) -> std::string {
            switch (val.type()) {
                case script_value_type::jai_null_type: return "null";
                case script_value_type::jai_bool_type: return "bool";
                case script_value_type::jai_int_type: return "int";
                case script_value_type::jai_float_type: return "float";
                case script_value_type::jai_char_type: return "char";
                case script_value_type::jai_string_type: return "string";
                case script_value_type::jai_utf8_string_type: return "utf8_string";
                case script_value_type::jai_array_type: return "array";
                case script_value_type::jai_map_type: return "map";
                case script_value_type::jai_function_type: return "function";
                // A typed null keeps the bare kind (its tag is a class, its value is not)
                case script_value_type::jai_object_type:
                case script_value_type::jai_shared_ptr_type:
                    return val.is_object() ? object_type_name(val) : "object";
                case script_value_type::jai_reference_type: return "reference";
                case script_value_type::jai_weak_ptr_type: return "weak_ptr";
                case script_value_type::jai_vec2_type:
                case script_value_type::jai_vec3_type:
                case script_value_type::jai_vec4_type:
                case script_value_type::jai_color_type:
                case script_value_type::jai_ivec2_type:
                case script_value_type::jai_ivec3_type: return math_type_name(val.type());
                default: return "unknown";
            }
        });
    }

} // namespace stdlib
} // namespace jai
