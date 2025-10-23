#pragma once

#include "value.hpp"
#include <jaiscript/detail/ast.hpp>
#include <memory>
#include <vector>
#include <string>
#include <system_error>

namespace jai {

// Forward declarations
class environment;
class string_symbolizer;
class class_definition;

/**
 * Abstract interface for script execution backends.
 * This allows us to have both interpreter and bytecode VM implementations.
 */
class execution_backend {
public:
    virtual ~execution_backend() = default;
    
    // Core execution
    virtual script_value execute(const std::vector<declaration_ptr>& declarations) = 0;
    virtual void prepare_for_execution() = 0;
    
    // Variable access (needed by engine)
    virtual script_value get_variable(const std::string& name) const = 0;
    virtual bool has_variable(const std::string& name) const = 0;
    
    // Scope management (for instance variables)
    virtual void push_scope() = 0;
    virtual void pop_scope() = 0;
    virtual void define_variable(const std::string& name, const script_value& value) = 0;
    
    // Configuration
    virtual void set_has_custom_numeric_ops(bool value) = 0;
    virtual void set_subscript_resolver(std::function<script_value(const std::vector<script_value>&)> resolver) = 0;
    virtual void set_class_lookup_callback(std::function<std::shared_ptr<class_definition>(const std::string&)> callback) = 0;
    virtual void set_engine_reference(std::weak_ptr<engine> engine_ref) = 0;
    
    // Exception handling
    virtual bool is_unwinding() const = 0;
    virtual const script_exception& get_current_exception() const = 0;

    // Optional: Get backend name for debugging/logging
    virtual std::string get_backend_name() const = 0;
};

using execution_backend_ptr = std::unique_ptr<execution_backend>;

} // namespace jai