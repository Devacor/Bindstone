#pragma once

#include "../core/engine.hpp"
#include "../core/value.hpp"
#include <iostream>
#include <sstream>
#include <format>

namespace jai {
namespace stdlib {

    // Register standard I/O functions with an engine
    inline void register_io_functions(engine& engine) {
        // print function - formatted output to stdout
        // 
        // Usage:
        //   print()                           - Prints just a newline
        //   print(value)                      - Prints value followed by newline
        //   print(format, args...)            - Printf-style formatting with placeholders
        //
        // Format string placeholders:
        //   {}     - Sequential placeholder, replaced with next argument
        //   {n}    - Positional placeholder, replaced with nth argument (0-based)
        //   {{     - Escaped left brace, prints a literal '{'
        //   }}     - Escaped right brace, prints a literal '}'
        //
        // Examples:
        //   print("Hello, World!")                    // Output: Hello, World!
        //   print("Hello, {}!", "JaiScript")          // Output: Hello, JaiScript!
        //   print("{} + {} = {}", 2, 3, 5)            // Output: 2 + 3 = 5
        //   print("{1} comes after {0}", "A", "B")   // Output: B comes after A
        //   print("Use {{}} for placeholders")        // Output: Use {} for placeholders
        //
        engine.add_variadic_function("print", [](const std::vector<script_value>& args) -> script_value {
            if (args.empty()) {
                std::cout << std::endl;
                return script_value();
            }
            
            if (args.size() == 1) {
                // Single argument - just print it directly
                std::cout << args[0].to_string() << std::endl;
                return script_value();
            }
            
            // First argument is the format string
            std::string format_str = args[0].to_string();
            std::string result;
            
            size_t pos = 0;
            size_t next_arg = 1;
            
            while (pos < format_str.length()) {
                size_t brace_pos = format_str.find('{', pos);
                
                if (brace_pos == std::string::npos) {
                    // No more format specifiers, append rest of string
                    result += format_str.substr(pos);
                    break;
                }
                
                // Append text before the brace
                result += format_str.substr(pos, brace_pos - pos);
                
                // Check for escaped brace
                if (brace_pos + 1 < format_str.length() && format_str[brace_pos + 1] == '{') {
                    result += '{';
                    pos = brace_pos + 2;
                    continue;
                }
                
                // Find closing brace
                size_t close_brace = format_str.find('}', brace_pos);
                if (close_brace == std::string::npos) {
                    // Invalid format string, append rest as-is
                    result += format_str.substr(brace_pos);
                    break;
                }
                
                // Extract content between braces
                std::string spec = format_str.substr(brace_pos + 1, close_brace - brace_pos - 1);
                
                if (spec.empty()) {
                    // {} - use next argument
                    if (next_arg < args.size()) {
                        result += args[next_arg].to_string();
                        next_arg++;
                    }
                } else {
                    // {n} - use positional argument
                    try {
                        size_t arg_index = std::stoull(spec) + 1; // +1 because format string is args[0]
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
            
            std::cout << result << std::endl;
            return script_value(); // void return
        });
        
        // to_string function - converts value to string
        engine.add_function("to_string", [](const script_value& val) -> std::string {
            return val.to_string();
        });
        
        // type_of function - returns the type name of a value
        engine.add_function("type_of", [](const script_value& val) -> std::string {
            switch (val.type()) {
                case script_value_type::jai_null_type: return "null";
                case script_value_type::jai_bool_type: return "bool";
                case script_value_type::jai_int_type: return "int";
                case script_value_type::jai_float_type: return "float";
                case script_value_type::jai_char_type: return "char";
                case script_value_type::jai_string_type: return "string";
                case script_value_type::jai_array_type: return "array";
                case script_value_type::jai_map_type: return "map";
                case script_value_type::jai_function_type: return "function";
                case script_value_type::jai_object_type: return "object";
                case script_value_type::jai_reference_type: return "reference";
                case script_value_type::jai_shared_ptr_type: return "shared_ptr";
                case script_value_type::jai_weak_ptr_type: return "weak_ptr";
                default: return "unknown";
            }
        });
    }

} // namespace stdlib
} // namespace jai