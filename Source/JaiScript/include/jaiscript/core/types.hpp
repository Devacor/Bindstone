#pragma once

#include "../jaiscript_fwd.hpp"
#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <memory>
#include <functional>
#include <optional>
#include <stdexcept>

namespace jai {

    // Fixed-size primitive types for cross-platform serialization
    using script_int = int64_t;      // Always 64-bit signed
    using script_float = double;     // Always 64-bit double precision
    using script_string = std::string;
    using script_char = char;       // 8-bit character
    using script_bool = bool;       // 1 byte boolean
    
    // Forward declarations
    class script_value;
    class engine;
    
    // Function type for script functions
    using script_function = std::function<script_value(const std::vector<script_value>&)>;
    
    // For serializable functions, we need to track additional info
    struct function_info {
        std::string name;           // Function name (for debugging)
        std::string script_text;     // Original script text (for script-defined functions)
        
        // Captured variables with their types and capture method
        struct captured_var {
            std::string name;
            value_type type;
            bool by_reference;       // true = captured by reference, false = by value
        };
        std::vector<captured_var> captured_vars;
        
        bool is_native = false;     // True for C++ functions (can't serialize)
    };
    
    // Instance variables for script execution (per-execution context)
    using instance_variables = std::unordered_map<std::string, script_value>;
    
    // Source location for error reporting
    struct source_location {
        std::string filename;
        size_t line = 1;
        size_t column = 1;
        
        std::string to_string() const {
            return filename + ":" + std::to_string(line) + ":" + std::to_string(column);
        }
    };
    
    // Base exception class with source location tracking
    class exception : public std::runtime_error {
    public:
        exception(const std::string& message, const source_location& location = {})
            : std::runtime_error(message), location_(location) {}
            
        const source_location& location() const { return location_; }
        
    private:
        source_location location_;
    };
    
    // Specific exception types
    class parse_error : public exception {
        using exception::exception;
    };
    
    class runtime_error : public exception {
        using exception::exception;
    };
    
    
    // Control flow exceptions for break/continue statements
    class break_exception : public exception {
    public:
        break_exception() : exception("break statement") {}
    };
    
    class continue_exception : public exception {
    public:
        continue_exception() : exception("continue statement") {}
    };
    
    // Script exception for try/catch/throw
    class script_exception : public runtime_error {
    public:
        script_exception(const std::string& message, const source_location& loc = {})
            : runtime_error(message, loc) {}
    };
    
} // namespace jai