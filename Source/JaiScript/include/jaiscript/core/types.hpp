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

namespace JaiScript {

    // Fixed-size primitive types for cross-platform serialization
    using Int = int64_t;      // Always 64-bit signed
    using Float = double;     // Always 64-bit double precision
    using String = std::string;
    using Char = char;       // 8-bit character
    using Bool = bool;       // 1 byte boolean
    
    // Forward declarations
    class Value;
    class Engine;
    
    // Function type for script functions
    using ScriptFunction = std::function<Value(const std::vector<Value>&)>;
    
    // For serializable functions, we need to track additional info
    struct FunctionInfo {
        std::string name;           // Function name (for debugging)
        std::string scriptText;     // Original script text (for script-defined functions)
        
        // Captured variables with their types and capture method
        struct CapturedVar {
            std::string name;
            ValueType type;
            bool byReference;       // true = captured by reference, false = by value
        };
        std::vector<CapturedVar> capturedVars;
        
        bool isNative = false;     // True for C++ functions (can't serialize)
    };
    
    // Local variables for script execution
    using LocalVariables = std::unordered_map<std::string, Value>;
    
    // Source location for error reporting
    struct SourceLocation {
        std::string filename;
        size_t line = 1;
        size_t column = 1;
        
        std::string toString() const {
            return filename + ":" + std::to_string(line) + ":" + std::to_string(column);
        }
    };
    
    // Base exception class
    class Exception : public std::exception {
    public:
        Exception(const std::string& message, const SourceLocation& location = {})
            : message_(message), location_(location) {}
            
        const char* what() const noexcept override { return message_.c_str(); }
        const SourceLocation& location() const { return location_; }
        
    private:
        std::string message_;
        SourceLocation location_;
    };
    
    // Specific exception types
    class ParseError : public Exception {
        using Exception::Exception;
    };
    
    class RuntimeError : public Exception {
        using Exception::Exception;
    };
    
    class SerializationError : public Exception {
        using Exception::Exception;
    };
    
} // namespace JaiScript