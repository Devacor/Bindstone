#pragma once

#ifndef __JAISCRIPT_CORE_TYPES_HPP__
#define __JAISCRIPT_CORE_TYPES_HPP__

#include <jaiscript/jaiscript_fwd.hpp>
#include <jaiscript/core/checked_result.hpp>
#include <algorithm>
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

    // Forward declarations
    class script_value;
    class engine;

    // Fixed-size primitive types for cross-platform serialization
    using script_int = int64_t;      // Always 64-bit signed
    using script_float = double;     // Always 64-bit double precision
    using script_string = std::string;
    using script_char = char;       // 8-bit character
    using script_bool = bool;       // 1 byte boolean
    
    // Container types. script_array is the ONE array node behind TYPEID_ARRAY (the
    // strong_ptr pointee — its address is the array's identity for ordering, borrows,
    // and COW probes). Typed kinds (docs/typed_array_design.md stage 2) store raw
    // primitive buffers instead of 32-byte script_values; the airtight array<T> tag
    // (stage 0) + demote-on-stamp keep node kind ⟺ view tag. values() is the HETERO
    // element surface only — every caller either sits behind a kind check or handles
    // typed nodes through get()/set()/typed buffers (kind-dispatch helpers live in
    // value.hpp where script_value is complete).
    class script_array {
    public:
        enum class kind_t : uint8_t { hetero = 0, i64 = 1, f64 = 2 };

        // Every body that instantiates a vector<script_value> member (including the
        // DEFAULTED special members - construction paths instantiate the vector dtor
        // for unwind cleanup) lives in value.hpp where script_value is complete: MSVC
        // defers those instantiations, Clang/GCC perform them here per the standard.
        script_array();
        explicit script_array(kind_t k);
        script_array(const script_array&);
        script_array(script_array&&) noexcept;
        script_array& operator=(const script_array&);
        script_array& operator=(script_array&&) noexcept;
        ~script_array();

        kind_t kind() const noexcept { return kind_; }
        bool is_typed() const noexcept { return kind_ != kind_t::hetero; }

        size_t size() const noexcept;
        bool empty() const noexcept { return size() == 0; }
        void clear() noexcept;
        void reserve(size_t n);

        std::vector<script_value>& values() noexcept { return values_; }
        const std::vector<script_value>& values() const noexcept { return values_; }
        std::vector<script_int>& ints() noexcept { return ints_; }
        const std::vector<script_int>& ints() const noexcept { return ints_; }
        std::vector<script_float>& floats() noexcept { return floats_; }
        const std::vector<script_float>& floats() const noexcept { return floats_; }

        // Kind-dispatched element access — defined in value.hpp (script_value complete
        // there; the element-touching vector ops instantiate at definition, so every
        // body that destroys/moves elements lives there too). get materializes;
        // set/push take a PRE-CONVERTED value (the caller ran element-type conversion;
        // typed kinds unbox, hetero stores as-is).
        inline void pop_back();
        inline void erase_at(size_t i);
        inline void reverse();
        inline script_value get(size_t i, engine* eng) const;
        inline void set(size_t i, script_value v);
        inline void push(script_value pre_converted);
        // Boxed snapshot of every element (typed builtin paths that need a hetero
        // vector: comparator sorts, predicate scans) — defined in value.hpp.
        inline std::vector<script_value> materialize_values(engine* eng) const;
        // Rebox a typed buffer into hetero storage IN PLACE (same node identity) — the
        // demote-on-stamp kernel for var-laundered typed arrays.
        inline void demote_to_hetero(engine* eng);

    private:
        kind_t kind_ = kind_t::hetero;
        std::vector<script_value> values_;
        std::vector<script_int> ints_;
        std::vector<script_float> floats_;
    };
    // Transparent probe for CONSTANT string keys: map lookups compare against the raw
    // constant bytes instead of materializing a script_value per access (`rec["px"]`
    // was one string allocation per read). Ordering is defined in value.cpp and MUST
    // mirror script_value::operator<=> exactly (type rank first, then content).
    struct map_string_key_probe {
        std::string_view text;
    };
    struct script_value_map_less {
        using is_transparent = void;
        bool operator()(const script_value& a, const script_value& b) const;
        bool operator()(const script_value& a, const map_string_key_probe& p) const;
        bool operator()(const map_string_key_probe& p, const script_value& b) const;
    };
    using script_map = std::map<script_value, script_value, script_value_map_less>;
    
    // Function type for script functions
    // Returns checked_result to allow proper error propagation without exceptions
    using script_function = std::function<checked_result<script_value>(const std::vector<script_value>&)>;
    
    // For serializable functions, we need to track additional info
    struct function_info {
        std::string name;           // Function name (for debugging)
        std::string script_text;     // Original script text (for script-defined functions)
        
        // Captured variables with their types and capture method
        struct captured_var {
            std::string name;
            script_value_type type;
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
    
    // One frame of a captured script stack trace (innermost first)
    struct stack_frame {
        std::string function;
        std::string file;
        size_t line = 0;
    };

    // Base exception class with source location tracking
    // Uses std::string for nice formatted messages (cold path, user-facing)
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
        using runtime_error::runtime_error;
    };
    
} // namespace jai

#endif // __JAISCRIPT_CORE_TYPES_HPP__