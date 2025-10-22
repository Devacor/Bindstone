#pragma once

#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/value.hpp>
#include <jaiscript/core/class_builder.hpp>
#include <iostream>
#include <sstream>
#include <format>
#include <memory>
#include <cctype>

namespace jai {
namespace stdlib {

    // Helper function to check if a string contains format placeholders {} or {NUMBER}
    inline bool has_format_placeholders(const std::string& str) {
        size_t pos = 0;
        while (pos < str.length()) {
            size_t brace = str.find('{', pos);
            if (brace == std::string::npos) break;
            
            // Check if it's escaped
            if (brace + 1 < str.length() && str[brace + 1] == '{') {
                pos = brace + 2; // Skip escaped {{
                continue;
            }
            
            // Find closing brace
            size_t close = str.find('}', brace);
            if (close != std::string::npos) {
                std::string content = str.substr(brace + 1, close - brace - 1);
                
                // Check if it's {} or {NUMBER}
                if (content.empty()) {
                    return true; // Found {}
                }
                
                // Check if content is all digits (valid number)
                bool is_number = true;
                for (char c : content) {
                    if (!std::isdigit(c)) {
                        is_number = false;
                        break;
                    }
                }
                
                if (is_number) {
                    return true; // Found {NUMBER}
                }
            }
            
            pos = brace + 1;
        }
        return false;
    }
    
    // Helper function to process a format string with arguments
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
                // Found } before { or no { left
                result += format_str.substr(pos, next_close - pos);
                if (next_close + 1 < format_str.length() && format_str[next_close + 1] == '}') {
                    // }} -> }
                    result += '}';
                    pos = next_close + 2;
                    continue;
                } else {
                    // Single } - just append it
                    result += '}';
                    pos = next_close + 1;
                    continue;
                }
            }
            
            if (next_open == std::string::npos) {
                // No more format specifiers, append rest of string
                result += format_str.substr(pos);
                break;
            }
            
            // Append text before the brace
            result += format_str.substr(pos, next_open - pos);
            
            // Check for escaped brace
            if (next_open + 1 < format_str.length() && format_str[next_open + 1] == '{') {
                result += '{';
                pos = next_open + 2;
                continue;
            }
            
            // Find closing brace
            size_t close_brace = format_str.find('}', next_open);
            if (close_brace == std::string::npos) {
                // Invalid format string, append rest as-is
                result += format_str.substr(next_open);
                break;
            }
            
            // Extract content between braces
            std::string spec = format_str.substr(next_open + 1, close_brace - next_open - 1);
            
            if (spec.empty()) {
                // {} - use next argument
                if (next_arg < args.size()) {
                    result += args[next_arg].to_string();
                    next_arg++;
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
    
    // Helper function to process single argument with escape sequences
    inline std::string process_single_arg_escapes(const std::string& input) {
        std::string result;
        size_t pos = 0;
        
        while (pos < input.length()) {
            size_t next_open = input.find('{', pos);
            size_t next_close = input.find('}', pos);
            
            // Handle }} escaping
            if (next_close != std::string::npos && 
                (next_open == std::string::npos || next_close < next_open)) {
                result += input.substr(pos, next_close - pos);
                if (next_close + 1 < input.length() && input[next_close + 1] == '}') {
                    result += '}';
                    pos = next_close + 2;
                    continue;
                } else {
                    result += '}';
                    pos = next_close + 1;
                    continue;
                }
            }
            
            // Handle {{ escaping
            if (next_open != std::string::npos) {
                result += input.substr(pos, next_open - pos);
                if (next_open + 1 < input.length() && input[next_open + 1] == '{') {
                    result += '{';
                    pos = next_open + 2;
                    continue;
                } else {
                    result += '{';
                    pos = next_open + 1;
                    continue;
                }
            }
            
            // No more special characters
            result += input.substr(pos);
            break;
        }
        
        return result;
    }

    // Special types for controlling print behavior
    struct skip_newline_t {};
    struct skip_flush_t {};
    
    // Global instances that can be used in scripts
    inline const skip_newline_t skip_newline{};
    inline const skip_flush_t skip_flush{};

    // Register standard I/O functions with an engine
    inline void register_io_functions(engine& engine) {
        auto engine_weak = engine.weak_from_this();
        // Register the special control types
        class_builder<skip_newline_t>(engine, "skip_newline_t").build();
        class_builder<skip_flush_t>(engine, "skip_flush_t").build();
        
        // Create shared instances that can be used as singletons
        auto skip_newline_ptr = std::make_shared<skip_newline_t>();
        auto skip_flush_ptr = std::make_shared<skip_flush_t>();
        
        // Register global instances
        engine.add_global("skip_newline", script_value::make_object("skip_newline_t", skip_newline_ptr));
        engine.add_global("skip_flush", script_value::make_object("skip_flush_t", skip_flush_ptr));
        // print function - formatted output to stdout
        // 
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
        engine.add_variadic_function("print", [engine_weak](const std::vector<script_value>& args) -> script_value {
            if (args.empty()) {
                std::cout << std::endl;
                return script_value(std::monostate{}, engine_weak);
            }
            
            // Check if last argument is a control type
            bool should_newline = true;
            bool should_flush = true;
            size_t effective_args = args.size();
            
            if (!args.empty()) {
                const auto& last_arg = args.back();
                // Check type name to determine if it's a control type
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
                // Only control argument was passed
                if (should_newline) {
                    std::cout << std::endl;
                } else if (should_flush) {
                    std::cout << std::flush;
                }
                return script_value(std::monostate{}, engine_weak);
            }
            
            if (effective_args == 1) {
                // Single argument - just print it directly
                std::cout << args[0].to_string();
                if (should_newline) {
                    std::cout << std::endl;
                } else if (should_flush) {
                    std::cout << std::flush;
                }
                return script_value(std::monostate{}, engine_weak);
            }
            
            // Multiple arguments - check if first arg contains format placeholders
            std::string first_str = args[0].to_string();
            bool has_format_specs = has_format_placeholders(first_str);
            
            if (!has_format_specs) {
                // No format specifiers - just print all arguments sequentially
                for (size_t i = 0; i < effective_args; ++i) {
                    std::cout << args[i].to_string();
                }
                if (should_newline) {
                    std::cout << std::endl;
                } else if (should_flush) {
                    std::cout << std::flush;
                }
                return script_value(std::monostate{}, engine_weak);
            }
            
            // Has format specifiers - use format string logic
            // Note: for print, we pass effective_args as the vector size, and start at arg 1
            std::vector<script_value> format_args(args.begin(), args.begin() + effective_args);
            std::string result = process_format_string(first_str, format_args, 1);
            
            std::cout << result;
            if (should_newline) {
                std::cout << std::endl;
            } else if (should_flush) {
                std::cout << std::flush;
            }
            return script_value(std::monostate{}, engine_weak); // void return
        });
        
        // format function - builds formatted strings
        // 
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
        engine.add_variadic_function("format", [engine_weak](const std::vector<script_value>& args) -> script_value {
            if (args.empty()) {
                return script_value("", engine_weak);
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
                return script_value("", engine_weak);
            }
            
            if (filtered_args.size() == 1) {
                // Single argument - return as-is, no format processing
                return script_value(filtered_args[0].to_string(), engine_weak);
            }
            
            // Multiple arguments - check if first arg contains format placeholders
            std::string first_str = filtered_args[0].to_string();
            bool has_format_specs = has_format_placeholders(first_str);
            
            if (!has_format_specs) {
                // No format specifiers - concatenate all arguments
                std::string result;
                for (const auto& arg : filtered_args) {
                    result += arg.to_string();
                }
                return script_value(result, engine_weak);
            }
            
            // Has format specifiers - use format string logic
            std::string result = process_format_string(first_str, filtered_args, 1);
            
            return script_value(result, engine_weak);
        });
        
        // to_string function - converts value to string
        // Register as variadic to bypass type matching issues
        engine.add_variadic_function("to_string", [engine_weak](const std::vector<script_value>& args) -> script_value {
            if (args.size() != 1) {
                throw runtime_error("to_string expects exactly 1 argument, got " + std::to_string(args.size()));
            }
            return script_value(args[0].to_string(), engine_weak);
        });
        
        // type_of function - returns the type name of a value
        engine.add_function("type_of", [engine_weak](const script_value& val) -> std::string {
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
                case script_value_type::jai_weak_ptr_type: return "weak_ptr";
                default: return "unknown";
            }
        });
    }

} // namespace stdlib
} // namespace jai