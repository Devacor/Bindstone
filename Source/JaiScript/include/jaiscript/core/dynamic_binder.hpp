#pragma once

#ifndef __JAISCRIPT_CORE_dynamic_binder_HPP__
#define __JAISCRIPT_CORE_dynamic_binder_HPP__
#define JAISCRIPT_dynamic_binder_HPP_INCLUDED

#include "engine.hpp"
#include "value.hpp"
#include "types.hpp"
#include "function_binder.hpp"
#include "parameter_storage.hpp"
#include "conversion_registry.hpp"
#include "bound_array.hpp"
#include "bound_map.hpp"
#include "bound_cpp_vector.hpp"
#include <jaiscript/serialization/archive.hpp>
#include <jaiscript/properties/property_schema.hpp>
#include <jaiscript/properties/observable_property.hpp>
#include <jaiscript/signals/signal_impl.hpp>
#include <jaiscript/core/property_type_converter.hpp>
#include <string>
#include <memory>
#include <functional>
#include <vector>
#include <map>
#include <unordered_set>
#include <typeindex>
#include <unordered_map>
#include <set>
#include <algorithm>
#include <type_traits>
#include <iostream>
#include <typeindex>

// Forward declarations for script class support
namespace jai {
    class function_decl;
    class ConstructorDecl;
    class MethodDecl;
    class FieldDecl;
    class block_stmt;
    class expression;
    struct parameter;  // Defined in ast.hpp
    using expression_ptr = std::shared_ptr<expression>;
    
    // Note: access_level, constructor_declaration, method_declaration,
    // and field_declaration are now defined in script_class.hpp
    
    class interpreter;  // Forward declaration for add_script_method
    struct script_defined_function;  // Defined in interpreter.hpp
}

namespace jai {

// Common types for class system
enum class access_level {
    public_access,
    private_access,
    protected_access
};

enum class delegation_type {
    none,
    same_class,
    base_class
};

// Forward declarations
class class_definition;
template<typename T> class signal_emitter;
template<typename T> class signal;
template<typename T> class observable_property;

// ============================================================================
// observable_property_ref<T> - Script-side reference to an observable property
// ============================================================================
//
// This wrapper class is used to expose observable_property<T> to scripts.
// It provides:
//   - get() - returns the current value
//   - on_change(callback) - connects a callback for value changes
//   - Transparent wrapper behavior - forwards operations to the underlying value
//
// Usage in scripts:
//   player.score           // Returns observable_property_ref<int>
//   player.score + 5       // Transparent wrapper forwards + to int (returns 5)
//   player.score.on_change([&](auto old, auto new) { ... })  // Connects callback
//
template<typename T>
class observable_property_ref {
public:
    observable_property_ref(observable_property<T>* prop, property_manager* mgr)
        : prop_(prop), mgr_(mgr) {}

    // Get the current value
    T get() const { return prop_ ? prop_->get() : T{}; }

    // Get pointer to the observable_property (for on_change registration)
    observable_property<T>* property() const { return prop_; }

    // Get the property_manager (for receiver tracking)
    property_manager* manager() const { return mgr_; }

private:
    observable_property<T>* prop_;
    property_manager* mgr_;
};
class class_instance;

// Class instance representation in JaiScript
class class_instance : public std::enable_shared_from_this<class_instance> {
public:
    class_instance(const std::string& class_name, engine* eng = nullptr)
        : class_name_(class_name), engine_(eng), invalid_value_(std::monostate{}, eng) {}
    
    ~class_instance();
    
    // Field access
    void set_field(uint64_t id, const script_value& value) {
        // Check if the field already exists and is a reference
        auto it = fields_.find(id);
        if (it != fields_.end() && it->second.is_reference()) {
            // If the field is a reference, update the referenced value instead of replacing the reference
            it->second.deref() = value;
        } else {
            // Normal field assignment
            fields_.insert_or_assign(id, value);
        }
    }
    
    const script_value& get_field(uint64_t id, bool throw_if_missing = true) const;
    script_value& get_field(uint64_t id, bool throw_if_missing = true);

    bool has_field(uint64_t id) const;

    // Check if field has been set on this instance (not just defined in class)
    bool has_field_value(uint64_t id) const {
        return fields_.find(id) != fields_.end();
    }

    const std::string& get_class_name() const { return class_name_; }
    
    
    // Migrate fields for hot reload
    void migrate_fields(const std::set<uint64_t>& old_field_ids,
                       const std::unordered_map<uint64_t, script_value>& new_field_defaults) {
        std::unordered_map<uint64_t, script_value> new_fields;
        new_fields.reserve(new_field_defaults.size());  // Pre-allocate for efficiency

        // Keep existing fields that are still in the new definition
        for (const auto& [id, default_value] : new_field_defaults) {
            auto it = fields_.find(id);
            if (it != fields_.end()) {
                // Field exists in both old and new - keep current value
                new_fields.emplace(id, std::move(it->second));
            } else {
                // New field - use default value
                new_fields.emplace(id, default_value.clone());
            }
        }

        // Replace fields map (removes old fields)
        fields_ = std::move(new_fields);
    }
    
    // Get method from class definition
    script_value get_method(uint64_t id, bool throw_if_missing = true) const;
    
    // Set the class definition this instance belongs to
    void set_class_definition(std::shared_ptr<class_definition> class_def) {
        class_def_ = class_def.get();
    }
    
    // Get all fields for implicit this support
    const std::unordered_map<uint64_t, script_value>& get_fields() const {
        return fields_;
    }
    
    // Get the class definition this instance belongs to (raw pointer - fast, no atomics)
    class_definition* get_class_definition() const {
        return class_def_;
    }
    
    // Check if this is a script class instance (implemented after class_definition)
    bool is_script_class() const;
    
    // Check if this is a C++ class instance (implemented after class_definition)
    bool is_cpp_class() const;
    
    // Get the underlying C++ object (if this is a C++ class instance)
    std::shared_ptr<void> get_cpp_object() const {
        uint64_t field_id = get_cpp_object_field_id();
        if (field_id == 0) return nullptr;

        auto cpp_field = get_field(field_id);
        if (!cpp_field.is_null() && cpp_field.is_object()) {
            // Extract the object_holder's data
            // This requires friend access or a public method
            return extract_cpp_object_impl(cpp_field);
        }
        return nullptr;
    }

    // Get the C++ object as a specific type
    template<typename T>
    std::shared_ptr<T> get_cpp_object_as() const {
        auto obj = get_cpp_object();
        return std::static_pointer_cast<T>(obj);
    }

    // Check if this instance has a C++ object
    bool has_cpp_object() const {
        uint64_t field_id = get_cpp_object_field_id();
        if (field_id == 0) return false;
        return !get_field(field_id).is_null();
    }
    
    // Deep copy this instance
    std::shared_ptr<class_instance> deep_copy() const;

    // Copy all fields from another instance (for shared_ptr auto-unwrap assignment)
    // Used when assigning same-type object contents: shared_ptr<T> a; a = T(); copies T's fields into *a
    void copy_fields_from(const class_instance& other) {
        for (const auto& [id, value] : other.fields_) {
            // Clone each field to maintain value semantics within the object
            fields_.insert_or_assign(id, value.clone());
        }
    }

    // Helper to get the CPP_OBJECT_FIELD ID (cached for performance)
    // Implemented after class_definition is complete
    uint64_t get_cpp_object_field_id() const;

    // Bound method caching for performance optimization
    // Returns cached bound method if exists, or null value if not cached
    const script_value& get_cached_bound_method(uint64_t method_id) const {
        auto it = bound_method_cache_.find(method_id);
        if (it != bound_method_cache_.end()) {
            return it->second;
        }
        return invalid_value_;
    }

    // Cache a bound method for later reuse
    void cache_bound_method(uint64_t method_id, const script_value& bound_method) const {
        bound_method_cache_[method_id] = bound_method;
    }

    // Check if a bound method is cached
    bool has_cached_bound_method(uint64_t method_id) const {
        return bound_method_cache_.find(method_id) != bound_method_cache_.end();
    }

private:
    std::string class_name_;
    std::unordered_map<uint64_t, script_value> fields_;
    class_definition* class_def_ = nullptr;  // Raw pointer (no atomic ops) - class_definitions never die before engine
    engine* engine_ = nullptr;  // Engine pointer (raw - no atomic ops) for creating values
    mutable uint64_t cpp_object_field_id_ = 0;  // Cached field ID
    mutable script_value invalid_value_;  // Used for get_field when field not found and throw_if_missing=false
    mutable std::unordered_map<uint64_t, script_value> bound_method_cache_;  // Cached bound methods

    static std::shared_ptr<void> extract_cpp_object_impl(const script_value& val);
};

// Class definition that holds methods and metadata for both C++ and script classes
class class_definition : public std::enable_shared_from_this<class_definition> {
protected:
    // Helper for validating method arguments
    template<size_t ExpectedArgs>
    static void validate_method_args(const std::vector<script_value>& args, 
                                    const std::string& method_name) {
        if (args.empty()) {
            throw runtime_error("Method '" + method_name + "' called without 'this' object");
        }
        if (args.size() != ExpectedArgs + 1) {
            throw runtime_error("Method '" + method_name + "' expects " + 
                              std::to_string(ExpectedArgs) + 
                              " arguments, got " + std::to_string(args.size() - 1));
        }
    }

public:
    enum class_type { cpp_class, script_class, vm_class };
    
    // Virtual destructor for polymorphism
    virtual ~class_definition() = default;
    
    // Constructor for C++ classes (existing)
    class_definition(std::string_view name, uint64_t type_id, engine* eng)
        : name_(name), type_id_(type_id), type_info_(nullptr), engine_(eng),
          null_field_value_(std::monostate{}, eng), class_type_(cpp_class) {
        if (eng) {
            type_info_ = eng->get_type_info_object(type_id_);
        }
    }

    // Constructor for script classes
    class_definition(std::string_view name, uint64_t type_id, class_type type, engine* eng)
        : name_(name), type_id_(type_id), type_info_(nullptr), engine_(eng),
          null_field_value_(std::monostate{}, eng), class_type_(type) {
        if (eng) {
            type_info_ = eng->get_type_info_object(type_id_);
        }
    }
    
    // Get the class type
    class_type get_class_type() const { return class_type_; }
    bool is_script_class() const { return class_type_ == script_class; }
    bool is_cpp_class() const { return class_type_ == cpp_class; }
    bool is_vm_class() const { return class_type_ == vm_class; }

    // Fast path check: does this class have any property getters (_get_* methods)?
    // If false, member access can skip the getter lookup entirely
    // Includes inherited getters (cached when parent/base is set)
    bool has_property_getters() const { return has_property_getters_; }

    // Get the persistent type_info for this class
    type_info_ptr get_type_info() const { return type_info_; }

    // Set the persistent type_info (used for bootstrap registration)
    void set_type_info(type_info_ptr type_info) { type_info_ = type_info; }

    // Add a method to the class by ID (avoids string allocation when ID is already known)
    void add_method_by_id(uint64_t name_id, script_function func, bool is_property_getter = false) {
        auto eng = engine_;
        if (!eng) return;

        if (is_property_getter) {
            has_property_getters_ = true;
        }

        // No arity tracking - just store directly
        methods_.insert_or_assign(name_id, script_value::make_function(func, engine_));
    }

    // Add a method to the class (with arity-based overloading support for C++ methods)
    void add_method(const std::string& name, script_function func, size_t arity = SIZE_MAX) {
        auto eng = engine_;
        if (!eng) return;

        // Track if this is a property getter for fast-path optimization
        if (name.size() > 5 && name.substr(0, 5) == "_get_") {
            has_property_getters_ = true;
        }

        uint64_t name_id = eng->symbolize(name);

        // If arity is provided, store in overloads map for arity-based dispatch
        if (arity != SIZE_MAX) {
            // Store overload by arity
            cpp_method_overloads_[name_id][arity] = func;
            method_arities_[name_id].push_back(arity);

            // Create/update dispatcher that selects overload by argument count
            // Use raw pointer - engine lifetime guarantees class_definition lifetime
            // Safety: check args[0].get_engine() - if null, engine was destroyed (and class_def with it)
            class_definition* class_def_ptr = this;

            methods_.insert_or_assign(name_id, script_value::make_function(
                [class_def_ptr, name_id](const std::vector<script_value>& args) -> checked_result<script_value> {
                    // Safety check: if 'this' arg's engine is null, the engine was destroyed
                    // which means the class_definition is also destroyed (owned by engine)
                    if (args.empty() || args[0].get_engine() == nullptr) {
                        return checked_result<script_value>(make_error_code(runtime_error_code::class_not_found),
                                                            "Class definition no longer exists");
                    }

                    // For instance methods, first arg is 'this', so script-visible arg count is args.size() - 1
                    // This matches the arity stored during registration (number of parameters excluding this)
                    size_t script_arg_count = args.size() - 1;

                    auto& overloads_for_name = class_def_ptr->cpp_method_overloads_[name_id];
                    auto it = overloads_for_name.find(script_arg_count);
                    if (it != overloads_for_name.end()) {
                        return it->second(args);
                    }

                    // No matching overload found for this argument count
                    return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch),
                                                        "Method argument count mismatch: no overload accepts this number of arguments",
                                                        name_id);
                },
                engine_
            ));
        } else {
            // No arity tracking - just store directly (for lambdas with unknown arity)
            methods_.insert_or_assign(name_id, script_value::make_function(func, engine_));
        }
    }
    
    // Add a static method to the class (with optional arity for C++ methods)
    void add_static_method(const std::string& name, script_function func, size_t arity = SIZE_MAX) {
        auto eng = engine_;
        if (!eng) return;

        // Track if this is a property getter for fast-path optimization
        if (name.size() > 5 && name.substr(0, 5) == "_get_") {
            has_property_getters_ = true;
        }

        uint64_t name_id = eng->symbolize(name);
        static_methods_.insert_or_assign(name_id, script_value::make_function(func, engine_));

        // If arity is provided, store it for arity-aware collision detection
        // (arity == SIZE_MAX means arity unknown, typically for generic lambdas)
        if (arity != SIZE_MAX) {
            // Track arity for C++ methods
            static_method_arities_[name_id].push_back(arity);
        }
    }

    // ID-based version to avoid string conversion
    void add_static_method(uint64_t name_id, script_function func, size_t arity = SIZE_MAX) {
        static_methods_.insert_or_assign(name_id, script_value::make_function(func, engine_));

        // If arity is provided, store it for arity-aware collision detection
        // (arity == SIZE_MAX means arity unknown, typically for generic lambdas)
        if (arity != SIZE_MAX) {
            // Track arity for C++ methods
            static_method_arities_[name_id].push_back(arity);
        }
    }
    
    // Add a script method (wraps AST execution in a function)
    // Implementation moved to script_class.hpp to avoid circular dependency
    void add_script_method(std::string_view name, std::shared_ptr<function_decl> ast, interpreter* interp, std::shared_ptr<environment> definition_env);

    // Add a static script method (wraps AST execution in a function without 'this')
    void add_static_script_method(std::string_view name, std::shared_ptr<function_decl> ast, interpreter* interp, std::shared_ptr<environment> definition_env);

    // Add a field with default value
    void add_field(std::string_view name, const script_value& default_value) {
        auto eng = engine_;
        if (!eng) return;

        uint64_t name_id = eng->symbolize(name);

        // Ensure the default value has an engine reference
        if (default_value.get_engine() == nullptr && !!engine_) {
            // Create a copy with engine reference
            script_value value_with_engine(default_value);
            value_with_engine.set_engine(engine_);
            field_defaults_.insert_or_assign(name_id, value_with_engine);
        } else {
            field_defaults_.insert_or_assign(name_id, default_value);
        }
        field_defaults_cache_valid_ = false;  // Invalidate cache
    }

    // Add a field with null default value
    void add_field(const std::string& name) {
        auto eng = engine_;
        if (!eng) return;

        uint64_t name_id = eng->symbolize(name);
        field_defaults_.insert_or_assign(name_id, script_value(std::monostate{}, engine_));
        field_defaults_cache_valid_ = false;  // Invalidate cache
    }
    
    // Add a static field with initial value
    void add_static_field(uint64_t id, const script_value& initial_value) {
        static_fields_.insert(id);

        // Ensure the value has an engine reference
        if (initial_value.get_engine() == nullptr && !!engine_) {
            script_value value_with_engine(initial_value);
            value_with_engine.set_engine(engine_);
            static_field_values_.insert_or_assign(id, value_with_engine);
        } else {
            static_field_values_.insert_or_assign(id, initial_value);
        }
    }
    
    // Get static field value by pointer (const version) - No exceptions
    // Returns nullptr if not found. Does NOT check parent classes (C++ semantics).
    // Static members belong to the class itself, they are not inherited.
    const script_value* get_static_field_ptr(uint64_t id) const {
        auto it = static_field_values_.find(id);
        if (it != static_field_values_.end()) {
            return &it->second;
        }
        return nullptr;
    }

    // Get static field value by pointer (non-const version) - No exceptions
    // Returns nullptr if not found. Does NOT check parent classes (C++ semantics).
    // Static members belong to the class itself, they are not inherited.
    script_value* get_static_field_ptr(uint64_t id) {
        auto it = static_field_values_.find(id);
        if (it != static_field_values_.end()) {
            return &it->second;
        }
        return nullptr;
    }

    // Get static field value by reference (const version)
    // Returns reference to the value if found, otherwise returns reference to null value
    const script_value& get_static_field(uint64_t id) const {
        const script_value* ptr = get_static_field_ptr(id);
        if (ptr) {
            return *ptr;
        }
        // Fallback - should not be reached if caller checks first
        // Use mutable member to avoid static lifetime issues
        null_field_value_ = script_value::make_null(engine_);
        return null_field_value_;
    }

    // Get static field value by reference (non-const version)
    // Returns reference to the value if found, otherwise returns reference to null value
    script_value& get_static_field(uint64_t id) {
        script_value* ptr = get_static_field_ptr(id);
        if (ptr) {
            return *ptr;
        }
        // Fallback - should not be reached if caller checks first
        null_field_value_ = script_value::make_null(engine_);
        return null_field_value_;
    }
    
    // Set static field value - returns true if field was found and set
    // No exceptions. Callers MUST check return value (enforced by [[nodiscard]]).
    // Does NOT check parent classes (C++ semantics) - static members are not inherited.
    [[nodiscard]] bool set_static_field(uint64_t id, const script_value& value) {
        auto it = static_field_values_.find(id);
        if (it != static_field_values_.end()) {
            it->second = value;
            return true;
        }
        return false;  // Field not found - static members are not inherited
    }

    // Set static field value (rvalue reference version)
    [[nodiscard]] bool set_static_field(uint64_t id, script_value&& value) {
        auto it = static_field_values_.find(id);
        if (it != static_field_values_.end()) {
            it->second = std::move(value);
            return true;
        }
        return false;  // Field not found - static members are not inherited
    }

    // Check if field is static (does NOT check parent classes - C++ semantics)
    bool is_static_field(uint64_t id) const {
        return static_fields_.find(id) != static_fields_.end();
    }

    // Check if static field exists (does NOT check parent classes - C++ semantics)
    bool has_static_field(uint64_t id) const {
        return static_field_values_.find(id) != static_field_values_.end();
    }
    
    // Get a method
    script_value get_method(uint64_t id, bool throw_if_missing = true) const {
        auto it = methods_.find(id);
        if (it != methods_.end()) {
            return it->second;
        }

        // Check parent classes (left-to-right) if we have inheritance
        for (const auto& parent : parent_classes_) {
            if (parent) {
                auto result = parent->get_method(id, false);
                if (!result.is_invalid()) {
                    return result;
                }
            }
        }

        // Also check C++ base class for mixed inheritance (script inheriting from C++)
        if (cpp_base_class_) {
            auto result = cpp_base_class_->get_method(id, false);
            if (!result.is_invalid()) {
                return result;
            }
        }

        if (throw_if_missing) {
            // Get the method name from the symbolizer for error message
            std::string name;
            if (auto eng = engine_) {
                name = eng->get_symbolizer()->get_string(id);
            } else {
                name = std::to_string(id);
            }
            throw runtime_error("Method '" + name + "' not found in class '" + name_ + "'");
        }

        // Return invalid value as sentinel
        return script_value::make_invalid(engine_);
    }
    
    // Check if this class has a method (does not check parent classes)
    bool has_method(uint64_t id) const {
        return methods_.find(id) != methods_.end();
    }

    // Get a static method (does NOT check parent classes - C++ semantics)
    // Static methods belong to the class itself, they are not inherited.
    script_value get_static_method(uint64_t id, bool throw_if_missing = true) const {
        auto it = static_methods_.find(id);
        if (it != static_methods_.end()) {
            return it->second;
        }

        if (throw_if_missing) {
            // Get the method name from the symbolizer for error message
            std::string name;
            if (auto eng = engine_) {
                name = eng->get_symbolizer()->get_string(id);
            } else {
                name = std::to_string(id);
            }
            throw runtime_error("Static method '" + name + "' not found in class '" + name_ + "'");
        }
        return script_value::make_null(engine_);
    }

    // Check if this class has a static method (does NOT check parent classes - C++ semantics)
    bool has_static_method(uint64_t id) const {
        return static_methods_.find(id) != static_methods_.end();
    }

    // Check if this class has a static method with specific name_id and arity
    // Defined in script_class.hpp where function_decl is available
    bool has_static_method_with_arity(uint64_t name_id, size_t arity) const;

    // Get all static field names (only this class, not parent)
    std::vector<std::string_view> get_static_field_names() const {
        std::vector<std::string_view> names;
        names.reserve(static_fields_.size());
        if (auto eng = engine_) {
            for (const auto& field_id : static_fields_) {
                names.push_back(eng->get_symbolizer()->get_string(field_id));
            }
        }
        return names;
    }

    // Get all static method names (only this class, not parent)
    std::vector<std::string_view> get_static_method_names() const {
        std::vector<std::string_view> names;
        names.reserve(static_methods_.size());
        if (auto eng = engine_) {
            for (const auto& [method_id, _] : static_methods_) {
                names.push_back(eng->get_symbolizer()->get_string(method_id));
            }
        }
        return names;
    }

    // Create an instance of this class
    std::shared_ptr<class_instance> create_instance() {
        // Create raw pointer first with engine reference
        auto* raw_instance = new class_instance(name_, engine_);

        // Check if this is a script class with a destructor
        std::shared_ptr<class_instance> instance;
        auto destructor_name = "~" + name_;
        uint64_t destructor_id = 0;
        if (auto eng = engine_) {
            destructor_id = eng->symbolize(destructor_name);
        }

        if (is_script_class() && destructor_id != 0 && has_method(destructor_id)) {
            // Create with custom deleter to call script destructor
            auto class_def = shared_from_this();

            instance = std::shared_ptr<class_instance>(raw_instance,
                [class_def, destructor_name](class_instance* ptr) {
                    if (ptr) {
                        // Call destructors from derived to base (like C++)
                        // For multiple inheritance, call destructors in reverse order of construction
                        // Use a recursive helper to handle the inheritance tree
                        std::function<void(std::shared_ptr<class_definition>)> call_destructors;
                        call_destructors = [&](std::shared_ptr<class_definition> current_class) {
                            if (!current_class) return;

                            // First call this class's destructor
                            auto current_destructor_name = "~" + current_class->name_;
                            uint64_t destructor_id = 0;
                            if (auto eng = current_class->get_engine()) {
                                destructor_id = eng->symbolize(current_destructor_name);
                            }
                            if (destructor_id == 0) return;

                            auto method_it = current_class->methods_.find(destructor_id);

                            if (method_it != current_class->methods_.end()) {
                                try {
                                    // Create a temporary shared_ptr for 'this' parameter
                                    // This is safe because we're in the deleter and control lifetime
                                    std::shared_ptr<class_instance> temp_this(ptr, [](class_instance*){});

                                    // Call the destructor with 'this' as argument
                                    const script_function& destructor_func = method_it->second.as_function();
                                    auto result = destructor_func({script_value::make_object(current_class->name_, temp_this, current_class->get_engine())});
                                    // Ignore result in destructor context - errors are swallowed
                                    (void)result;
                                } catch (...) {
                                    // Swallow exceptions in destructors
                                }
                            }

                            // Then call parent destructors (left-to-right, same as construction order)
                            for (const auto& parent : current_class->parent_classes_) {
                                call_destructors(parent);
                            }
                        };

                        // Start the destruction chain from the most derived class
                        call_destructors(class_def);

                        delete ptr;
                    }
                });
        } else {
            // Regular shared_ptr for non-script classes or classes without destructors
            instance = std::shared_ptr<class_instance>(raw_instance);
        }
        
        instance->set_class_definition(shared_from_this());
        
        // Register instance for hot reload tracking
        register_instance(std::weak_ptr<class_instance>(instance));
        
        // Initialize fields from parent classes first (left-to-right precedence)
        for (const auto& parent : parent_classes_) {
            if (parent) {
                const auto& parent_fields = parent->get_all_field_defaults();
                for (const auto& [field_id, default_value] : parent_fields) {
                    // Only set if not already set (left-to-right: first parent wins)
                    if (!instance->has_field(field_id)) {
                        instance->set_field(field_id, default_value);
                    }
                }
            }
        }

        // Initialize fields with defaults (may override parent fields)
        for (const auto& [field_id, default_value] : field_defaults_) {
            instance->set_field(field_id, default_value);
        }
        
        return instance;
    }
    
    // Get all field defaults including inherited ones
    const std::unordered_map<uint64_t, script_value>& get_all_field_defaults() const {
        // Rebuild cache if invalid
        if (!field_defaults_cache_valid_) {
            all_field_defaults_cache_.clear();

            // Collect parent fields (left-to-right precedence)
            for (const auto& parent : parent_classes_) {
                if (parent) {
                    const auto& parent_fields = parent->get_all_field_defaults();
                    for (const auto& [id, value] : parent_fields) {
                        // Only add if not already present (left-to-right: first parent wins)
                        if (all_field_defaults_cache_.find(id) == all_field_defaults_cache_.end()) {
                            all_field_defaults_cache_[id] = value;
                        }
                    }
                }
            }

            // Add/override with our fields (derived class always wins)
            for (const auto& [id, value] : field_defaults_) {
                all_field_defaults_cache_.insert_or_assign(id, value);
            }

            field_defaults_cache_valid_ = true;
        }

        return all_field_defaults_cache_;
    }
    
    // Set parent class for inheritance (single inheritance - kept for compatibility)
    // Note: This clears existing parents. Use add_parent() to append instead.
    void set_parent(std::shared_ptr<class_definition> parent) {
        parent_classes_.clear();
        if (parent) {
            parent_classes_.push_back(parent);
            parent->add_derived_class(shared_from_this());
            // Inherit property getter flag from parent
            if (parent->has_property_getters()) {
                has_property_getters_ = true;
            }
        }
        field_defaults_cache_valid_ = false;  // Invalidate cache when parent changes
    }

    // Add a parent class (appends to existing parents)
    // Returns true if successful, false if diamond inheritance would be created
    // Idempotent: adding the same parent twice is a no-op (returns true)
    [[nodiscard]] bool add_parent(std::shared_ptr<class_definition> parent) {
        if (!parent) return true;

        // Check if this parent is already registered (idempotent - safe to call twice)
        for (const auto& existing : parent_classes_) {
            if (existing.get() == parent.get()) {
                return true;  // Already registered, nothing to do
            }
        }

        // Temporarily add the parent
        parent_classes_.push_back(parent);
        field_defaults_cache_valid_ = false;

        // Check for diamond inheritance
        if (has_diamond_inheritance()) {
            parent_classes_.pop_back();  // Roll back
            return false;
        }

        // Register this as a derived class of the parent
        parent->add_derived_class(shared_from_this());
        if (parent->has_property_getters()) {
            has_property_getters_ = true;
        }

        return true;
    }

    // Set multiple parent classes for multiple inheritance
    // Returns true if successful, false if diamond inheritance detected
    [[nodiscard]] bool set_parents(const std::vector<std::shared_ptr<class_definition>>& parents) {
        parent_classes_ = parents;
        field_defaults_cache_valid_ = false;  // Invalidate cache when parents change

        // Check for diamond inheritance (not supported)
        if (has_diamond_inheritance()) {
            parent_classes_.clear();  // Roll back the change
            return false;
        }

        // Register this as a derived class of all parents and inherit property getter flags
        for (auto& parent : parent_classes_) {
            if (parent) {
                parent->add_derived_class(shared_from_this());
                if (parent->has_property_getters()) {
                    has_property_getters_ = true;
                }
            }
        }

        return true;
    }

    // Get parent classes
    const std::vector<std::shared_ptr<class_definition>>& get_parent_classes() const {
        return parent_classes_;
    }
    
    // Get first parent class for inheritance (for compatibility with single inheritance code)
    // Also checks cpp_base_class_ for mixed inheritance (script inheriting from C++)
    std::shared_ptr<class_definition> get_parent() const {
        if (!parent_classes_.empty()) {
            return parent_classes_[0];
        }
        // Check for C++ base class (mixed inheritance)
        if (cpp_base_class_) {
            return cpp_base_class_;
        }
        return nullptr;
    }

    // Check if this class's inheritance hierarchy contains a diamond pattern
    // Returns true if any base class is reachable through multiple paths
    bool has_diamond_inheritance() const {
        // Quick check: if we have 0 or 1 parents, no diamond possible
        if (parent_classes_.size() <= 1) {
            return false;
        }

        std::unordered_set<const class_definition*> visited;
        std::vector<const class_definition*> to_visit;

        // Start with all direct parents and mark them as visited
        for (const auto& parent : parent_classes_) {
            if (parent) {
                to_visit.push_back(parent.get());
                visited.insert(parent.get());
            }
        }

        // BFS through the inheritance tree
        size_t index = 0;
        while (index < to_visit.size()) {
            const class_definition* current = to_visit[index++];

            // Add all parents of this class to the queue
            for (const auto& parent : current->parent_classes_) {
                if (parent) {
                    // If we've seen this parent before, it's a diamond!
                    if (visited.count(parent.get()) > 0) {
                        return true;
                    }
                    visited.insert(parent.get());
                    to_visit.push_back(parent.get());
                }
            }
        }

        return false;
    }

    // Track derived classes for hot reload
    void add_derived_class(std::weak_ptr<class_definition> derived) {
        derived_classes_.push_back(derived);
    }
    
    // Set C++ base class for mixed inheritance (script class inheriting from C++ class)
    void set_cpp_base_class(std::shared_ptr<class_definition> cpp_base) {
        cpp_base_class_ = cpp_base;
        // Inherit property getter flag from C++ base class
        if (cpp_base && cpp_base->has_property_getters()) {
            has_property_getters_ = true;
        }
    }
    
    std::shared_ptr<class_definition> get_cpp_base_class() const { return cpp_base_class_; }

    // Property setter ID cache for fast runtime lookup
    // Register a field_id -> setter_id mapping (called during property registration)
    void register_property_setter(uint64_t field_id, uint64_t setter_id) {
        property_setter_ids_[field_id] = setter_id;
    }

    // Get the setter method ID for a field, or 0 if not found
    uint64_t get_property_setter_id(uint64_t field_id) const {
        auto it = property_setter_ids_.find(field_id);
        if (it != property_setter_ids_.end()) {
            return it->second;
        }
        return 0;
    }

    // Check if a field has a registered setter
    bool has_property_setter(uint64_t field_id) const {
        return property_setter_ids_.find(field_id) != property_setter_ids_.end();
    }

    // Transparent wrapper support - check if this type wraps another and should forward operations
    bool is_transparent_wrapper() const { return is_transparent_wrapper_; }

    // Get the underlying value from a transparent wrapper
    // Returns null script_value if not a wrapper or unwrap fails
    script_value unwrap(script_value& wrapper_value) const {
        if (!is_transparent_wrapper_ || !unwrap_function_) {
            return script_value(std::monostate{}, engine_);
        }
        return unwrap_function_(wrapper_value, engine_);
    }

    // Set the unwrap function (called by dynamic_binder::transparent_wrapper)
    void set_unwrap_function(std::function<script_value(script_value&, engine*)> fn) {
        is_transparent_wrapper_ = true;
        unwrap_function_ = std::move(fn);
    }

    const std::string& get_name() const { return name_; }
    uint64_t get_type_id() const { return type_id_; }

    // Set type_id (called by engine during registration)
    void set_type_id(uint64_t type_id) { type_id_ = type_id; }

    // Get the engine reference
    engine* get_engine() const { return engine_; }

    // Helper to get the CPP_OBJECT_FIELD ID (cached for performance)
    uint64_t get_cpp_object_field_id() const {
        if (cpp_object_field_id_ == 0) {
            if (auto eng = engine_) {
                cpp_object_field_id_ = eng->symbolize(class_constants::CPP_OBJECT_FIELD);
            }
        }
        return cpp_object_field_id_;
    }

    // Get all registered property names, including inherited properties by default
    // Set include_inherited=false to get only locally-defined properties
    std::vector<std::string> get_property_names(bool include_inherited = true) const {
        std::vector<std::string> properties;
        std::unordered_set<std::string> seen;  // Avoid duplicates if derived re-declares

        if (auto eng = engine_) {
            // First, add parent class properties (if inheriting)
            if (include_inherited) {
                // Check parent classes (for C++ inheritance chains)
                for (const auto& parent : parent_classes_) {
                    if (parent) {
                        for (const auto& prop : parent->get_property_names(true)) {
                            if (seen.insert(prop).second) {
                                properties.push_back(prop);
                            }
                        }
                    }
                }
                // Check C++ base class (for mixed inheritance)
                if (cpp_base_class_) {
                    for (const auto& prop : cpp_base_class_->get_property_names(true)) {
                        if (seen.insert(prop).second) {
                            properties.push_back(prop);
                        }
                    }
                }
            }

            // Then add this class's own properties (may override parent names)
            for (const auto& [id, default_value] : field_defaults_) {
                std::string prop_name(eng->get_symbolizer()->get_string(id));
                if (seen.insert(prop_name).second) {
                    properties.push_back(prop_name);
                }
            }
        }
        return properties;
    }

    // Check if this class is a subtype of the given base class name
    // Walks up the inheritance hierarchy to find a match
    // Used for type compatibility checking in function overload resolution
    bool is_subtype_of(const std::string& base_class_name) const {
        // Same class is trivially a subtype of itself
        if (name_ == base_class_name) {
            return true;
        }

        // Check all parent classes (handles multiple inheritance)
        for (const auto& parent : parent_classes_) {
            if (parent && parent->is_subtype_of(base_class_name)) {
                return true;
            }
        }

        // Also check C++ base class for mixed inheritance
        if (cpp_base_class_ && cpp_base_class_->is_subtype_of(base_class_name)) {
            return true;
        }

        return false;
    }

    // Script class specific methods - removed old implementation
    // These are now handled by the simplified script_class_definition
    // which inherits from class_definition
    
    // Access control
    void set_default_access(access_level access) { default_access_ = access; }
    access_level get_default_access() const { return default_access_; }
    
    // Copy function support for deep copying
    using copy_function = std::function<std::shared_ptr<void>(const void*)>;
    
    void set_copy_function(copy_function copier) {
        copy_function_ = std::move(copier);
    }
    
    bool has_copy_function() const {
        return copy_function_ != nullptr;
    }
    
    std::shared_ptr<void> copy_object(const void* src) const {
        if (!copy_function_) {
            throw runtime_error("No copy constructor available for class " + name_);
        }
        return copy_function_(src);
    }
    
    // TODO: Add support for script-defined destructors
    // Currently only C++ classes can have destructors registered
    
    // Method metadata for optimizations
    struct method_metadata {
        bool is_virtual = false;
        access_level access = access_level::public_access;
        size_t vtable_index = 0;  // For VM optimization
    };
    
    // Set metadata for a method
    void set_method_metadata(std::string_view name, const method_metadata& metadata) {
        auto eng = engine_;
        if (!eng) return;
        uint64_t name_id = eng->symbolize(name);
        method_metadata_[name_id] = metadata;
    }

    // Get metadata for a method
    const method_metadata* get_method_metadata(std::string_view name) const {
        auto eng = engine_;
        if (!eng) return nullptr;
        uint64_t name_id = eng->symbolize(name);
        auto it = method_metadata_.find(name_id);
        return it != method_metadata_.end() ? &it->second : nullptr;
    }

    // Get mutable metadata for a method (for updating)
    method_metadata* get_method_metadata_mutable(std::string_view name) {
        auto eng = engine_;
        if (!eng) return nullptr;
        uint64_t name_id = eng->symbolize(name);
        auto it = method_metadata_.find(name_id);
        return it != method_metadata_.end() ? &it->second : nullptr;
    }
    
    // Find method in this class or parent classes, returns the class that has it
    struct method_info {
        std::shared_ptr<class_definition> owner_class;
        method_metadata* metadata;
    };
    
    method_info find_method(std::string_view name) {
        auto eng = engine_;
        if (!eng) return { nullptr, nullptr };

        uint64_t name_id = eng->symbolize(name);

        // First check this class
        if (has_method(name_id)) {
            return { shared_from_this(), get_method_metadata_mutable(name) };
        }

        // Check parent classes
        auto parent = get_parent();
        while (parent) {
            if (parent->has_method(name_id)) {
                return { parent, parent->get_method_metadata_mutable(name) };
            }
            parent = parent->get_parent();
        }

        return { nullptr, nullptr };
    }
    
    // Initialize fingerprint after class is first defined
    void initialize_fingerprint() {
        current_fingerprint_ = compute_fingerprint(field_defaults_, methods_, static_methods_);
    }
    
    // Hot reload support - migrate all instances
    void redefine_class(const std::unordered_map<uint64_t, script_value>& new_field_defaults,
                        const std::unordered_map<uint64_t, script_value>& new_methods,
                        const std::unordered_map<uint64_t, script_value>& new_static_methods,
                        engine* engine_ref) {
        field_defaults_cache_valid_ = false;  // Invalidate cache on hot reload
        // Compute fingerprint for the new definition
        size_t new_fingerprint = compute_fingerprint(new_field_defaults, new_methods, new_static_methods);

        // Ultra-fast path: completely identical class
        if (new_fingerprint == current_fingerprint_) {
            // Fingerprint matches means fields, methods, and static methods are all identical
            // No need to update anything except instance pointers (which might be stale)
            // Update instance class_definition pointers
            // because the old class_definition object may be going away
            for (size_t i = 0; i < instances_.size(); ++i) {
                if (auto instance = instances_[i].lock()) {
                    try {
                        instance->set_class_definition(shared_from_this());
                    } catch (const std::exception& e) {
                        std::cerr << "Failed to update class definition for instance: " << e.what() << std::endl;
                    }
                }
            }
            return;  // No field/method migration needed - everything is identical
        }
        // Check if fields have changed
        bool fields_changed = false;

        // Quick size check first
        if (field_defaults_.size() != new_field_defaults.size()) {
            fields_changed = true;
        } else {
            // Check if all field IDs match
            for (const auto& [id, _] : field_defaults_) {
                if (new_field_defaults.find(id) == new_field_defaults.end()) {
                    fields_changed = true;
                    break;
                }
            }
            // Also check the reverse - any new fields not in old?
            if (!fields_changed) {
                for (const auto& [id, _] : new_field_defaults) {
                    if (field_defaults_.find(id) == field_defaults_.end()) {
                        fields_changed = true;
                        break;
                    }
                }
            }
        }

        // Store old field IDs for migration (only if needed)
        std::set<uint64_t> old_fields;
        if (fields_changed) {
            for (const auto& [id, _] : field_defaults_) {
                old_fields.insert(id);
            }
        }

        // Check if new class has a hot_reload_migrate method
        uint64_t migrate_method_id = 0;
        if (engine_ref) {
            migrate_method_id = engine_ref->symbolize("hot_reload_migrate");
        }
        auto migrate_method_it = new_methods.find(migrate_method_id);

        // Update methods BEFORE migrating instances so that hot_reload_migrate can access new getters/setters
        methods_ = new_methods;
        static_methods_ = new_static_methods;

        // Recompute has_property_getters_ flag: start with inherited value, then check new methods
        has_property_getters_ = false;
        // Inherit from parent classes
        for (const auto& parent : parent_classes_) {
            if (parent && parent->has_property_getters()) {
                has_property_getters_ = true;
                break;
            }
        }
        // Inherit from C++ base class
        if (!has_property_getters_ && cpp_base_class_ && cpp_base_class_->has_property_getters()) {
            has_property_getters_ = true;
        }
        // Check new methods for _get_* patterns
        if (!has_property_getters_ && engine_ref) {
            for (const auto& [method_id, _] : new_methods) {
                std::string_view name = engine_ref->get_symbolizer()->get_string(method_id);
                if (name.size() > 5 && name.substr(0, 5) == "_get_") {
                    has_property_getters_ = true;
                    break;
                }
            }
        }

        // Update field_defaults_ BEFORE migrating instances so that get_field() sees correct fields
        // This prevents removed fields from being lazily re-created from old defaults
        field_defaults_.clear();
        for (const auto& [id, value] : new_field_defaults) {
            if (value.get_engine() == nullptr && engine_ref) {
                // Create a copy with engine reference
                script_value value_with_engine(value);
                value_with_engine.set_engine(engine_ref);
                field_defaults_[id] = value_with_engine;
            } else {
                field_defaults_[id] = value;
            }
        }

        // Migrate all instances and clean up expired ones in a single pass
        size_t write_idx = 0;
        for (size_t read_idx = 0; read_idx < instances_.size(); ++read_idx) {
            if (auto instance = instances_[read_idx].lock()) {
                // Found live instance to migrate
                
                // Only perform field migration if fields have changed
                if (fields_changed) {
                    // First, add new fields with defaults (but keep old fields for migration)
                    for (const auto& [name, default_value] : new_field_defaults) {
                        if (!instance->has_field(name)) {
                            instance->set_field(name, default_value.clone());
                        }
                    }
                    
                    // Call hot_reload_migrate() if it exists AFTER adding new fields
                    // This allows the migration method to access both old and new fields
                    if (migrate_method_it != new_methods.end() && !migrate_method_it->second.is_null()) {
                        // Call hot_reload_migrate method
                        // Create a script_value wrapper for this instance
                        auto instance_value = script_value::make_object(name_, instance, engine_ref);

                        // Call the method with 'this' as the instance
                        const script_function& migrate_func = migrate_method_it->second.as_function();
                        std::vector<script_value> args = {instance_value};
                        auto result = migrate_func(args);
                        if (!result) {
                            // Migration failed - log but continue with hot-reload
                            std::cerr << "Hot reload migration error for " << name_
                                      << ": " << result.message() << std::endl;
                        }
                        // Migration completed (successfully or with error logged)
                    } else {
                        // No hot_reload_migrate method provided
                    }
                    
                    // Finally migrate fields (removes old fields not in new definition)
                    instance->migrate_fields(old_fields, new_field_defaults);
                } else {
                    // Fields haven't changed - skip the expensive field migration
                }
                
                // Update the class definition reference in the instance
                // This is critical - the instance has a weak_ptr to the OLD class_definition
                // which will be destroyed after hot reload
                try {
                    instance->set_class_definition(shared_from_this());
                } catch (const std::exception& e) {
                    std::cerr << "Failed to update class definition for instance: " << e.what() << std::endl;
                    // Try alternative approach - the engine should have the class_def
                    // Skip for now, will need engine reference
                }
                
                // Keep valid instance
                if (write_idx != read_idx) {
                    instances_[write_idx] = std::move(instances_[read_idx]);
                }
                ++write_idx;
            }
        }
        instances_.resize(write_idx);  // Trim expired instances

        // Clear method metadata since methods have been updated
        method_metadata_.clear();

        // Notify derived classes to update their instances and clean up expired ones
        write_idx = 0;
        for (size_t read_idx = 0; read_idx < derived_classes_.size(); ++read_idx) {
            if (auto derived = derived_classes_[read_idx].lock()) {
                // Derived class instances need to re-migrate to pick up new base fields
                derived->update_instances_from_base();
                
                // Keep valid derived class
                if (write_idx != read_idx) {
                    derived_classes_[write_idx] = std::move(derived_classes_[read_idx]);
                }
                ++write_idx;
            }
        }
        derived_classes_.resize(write_idx);  // Trim expired derived classes
        
        // Update the fingerprint for future comparisons
        current_fingerprint_ = new_fingerprint;
    }
    
    // Update instances when base class changes
    void update_instances_from_base() {
        field_defaults_cache_valid_ = false;  // Invalidate cache when base class changes
        const auto& all_fields = get_all_field_defaults();
        std::set<uint64_t> dummy_old_fields; // Not used for base updates

        for (auto& weak_instance : instances_) {
            if (auto instance = weak_instance.lock()) {
                instance->migrate_fields(dummy_old_fields, all_fields);
            }
        }

        // Recursively notify derived classes (for multi-level inheritance)
        for (auto& weak_derived : derived_classes_) {
            if (auto derived = weak_derived.lock()) {
                derived->update_instances_from_base();
            }
        }
    }
    
    // Register an instance (for hot reload tracking)
    void register_instance(std::weak_ptr<class_instance> instance) {
        instances_.push_back(instance);
        // Instance registered for hot reload tracking
    }
    
    // Unregister an instance (called from destructor)
    void unregister_instance(class_instance* instance) {
        instances_.erase(
            std::remove_if(instances_.begin(), instances_.end(),
                [instance](const std::weak_ptr<class_instance>& w) {
                    auto locked = w.lock();
                    return !locked || locked.get() == instance;
                }),
            instances_.end()
        );
    }
    
private:
    std::string name_;
    uint64_t type_id_;  // Interned type name for fast comparisons
    type_info_ptr type_info_;  // Persistent type_info for this class
    engine* engine_ = nullptr;  // Engine pointer (raw - no atomic ops) for script_value creation
    std::unordered_map<uint64_t, script_value> methods_;
    std::unordered_map<uint64_t, std::vector<size_t>> method_arities_;  // Track arities for C++ methods (name_id -> list of arities)
    std::unordered_map<uint64_t, std::unordered_map<size_t, script_function>> cpp_method_overloads_;  // C++ method overloads by arity for dispatch
    std::unordered_map<uint64_t, std::vector<std::shared_ptr<function_decl>>> method_overloads_;  // Track overloads by name_id for type-based resolution (for script methods)
    std::unordered_map<uint64_t, script_value> static_methods_;  // Static method storage
    std::unordered_map<uint64_t, std::vector<std::shared_ptr<function_decl>>> static_method_overloads_;  // Track overloads by name_id and arity (for script methods)
    std::unordered_map<uint64_t, std::vector<size_t>> static_method_arities_;  // Track arities for C++ methods (name_id -> list of arities)
    std::unordered_map<uint64_t, script_value> field_defaults_;
    std::unordered_map<uint64_t, script_value> static_field_values_;  // Static field storage
    std::unordered_set<uint64_t> static_fields_;  // Track which fields are static
    std::vector<std::shared_ptr<class_definition>> parent_classes_;  // Support multiple inheritance
    mutable std::unordered_map<uint64_t, script_value> all_field_defaults_cache_;
    mutable bool field_defaults_cache_valid_ = false;
    mutable uint64_t cpp_object_field_id_ = 0;  // Cached field ID for CPP_OBJECT_FIELD
    mutable script_value null_field_value_;  // Used for get_static_field when field not found

    // Script class specific fields
    class_type class_type_;
    access_level default_access_ = access_level::public_access;
    
    // Mixed inheritance support - for script classes inheriting from C++ classes
    std::shared_ptr<class_definition> cpp_base_class_;

    // Property setter ID cache - maps field_id to _set_<field> method ID
    // Pre-computed during property registration for fast runtime lookup
    std::unordered_map<uint64_t, uint64_t> property_setter_ids_;

    // Fast path optimization: skip getter lookup if class has no property getters
    // Set to true when any _get_* method is registered, OR inherited from parent/base classes
    bool has_property_getters_ = false;

    // Transparent wrapper support - allows types to forward operations to an underlying value
    // When an operation isn't found on the wrapper, unwrap and retry on the underlying value
    bool is_transparent_wrapper_ = false;
    std::function<script_value(script_value&, engine*)> unwrap_function_;

    // Copy function for deep copying objects
    copy_function copy_function_;
    
    // Method metadata for optimizations
    std::unordered_map<uint64_t, method_metadata> method_metadata_;
    
    // Track all instances for hot reload
    std::vector<std::weak_ptr<class_instance>> instances_;
    
    // Track derived classes for hot reload
    std::vector<std::weak_ptr<class_definition>> derived_classes_;
    
    // Class fingerprint for detecting identical redefinitions
    size_t current_fingerprint_ = 0;
    
    // Helper to combine hashes
    template<typename T>
    static void hash_combine(size_t& seed, const T& val) {
        seed ^= std::hash<T>{}(val) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    }
    
    // Compute fingerprint for a class definition
    static size_t compute_fingerprint(
        const std::unordered_map<uint64_t, script_value>& field_defaults,
        const std::unordered_map<uint64_t, script_value>& methods,
        const std::unordered_map<uint64_t, script_value>& static_methods = {}) {

        size_t hash = 0;

        // Hash fields (IDs and types, not values)
        // Sort by ID for consistent ordering
        std::vector<uint64_t> field_ids;
        field_ids.reserve(field_defaults.size());
        for (const auto& [id, _] : field_defaults) {
            field_ids.push_back(id);
        }
        std::sort(field_ids.begin(), field_ids.end());

        for (const auto& id : field_ids) {
            hash_combine(hash, id);
            // Could also hash default value types if needed
        }

        // Hash methods (IDs and their identity)
        // We need to detect when method implementations change, not just IDs
        std::vector<std::pair<uint64_t, size_t>> method_hashes;
        method_hashes.reserve(methods.size());
        for (const auto& [id, value] : methods) {
            // Hash the method ID AND something about its implementation
            // For functions, we can use the internal function object address as a discriminator
            // This detects when the same method ID gets a new implementation
            size_t impl_hash = 0;
            if (value.is_function()) {
                try {
                    // Access the function from the variant storage
                    const auto& func = value.as_function();
                    // Hash the type-erased target pointer of the std::function
                    // Different lambda/function implementations will have different addresses
                    if (func.target_type() != typeid(void)) {
                        impl_hash = reinterpret_cast<size_t>(&func);
                    }
                } catch (...) {
                    // If we can't access the function, use a default hash
                    impl_hash = 0;
                }
            }
            method_hashes.emplace_back(id, impl_hash);
        }
        std::sort(method_hashes.begin(), method_hashes.end());

        for (const auto& [id, impl_hash] : method_hashes) {
            hash_combine(hash, id);
            hash_combine(hash, impl_hash); // Include implementation identity
        }

        // Hash static methods (IDs and their identity)
        std::vector<std::pair<uint64_t, size_t>> static_method_hashes;
        static_method_hashes.reserve(static_methods.size());
        for (const auto& [id, value] : static_methods) {
            size_t impl_hash = 0;
            if (value.is_function()) {
                try {
                    const auto& func = value.as_function();
                    if (func.target_type() != typeid(void)) {
                        impl_hash = reinterpret_cast<size_t>(&func);
                    }
                } catch (...) {
                    impl_hash = 0;
                }
            }
            static_method_hashes.emplace_back(id, impl_hash);
        }
        std::sort(static_method_hashes.begin(), static_method_hashes.end());

        for (const auto& [id, impl_hash] : static_method_hashes) {
            hash_combine(hash, id);
            hash_combine(hash, std::string("static")); // Distinguish from instance methods
            hash_combine(hash, impl_hash); // Include implementation identity
        }

        return hash;
    }
};

// Helper function to extract base template name from full type name
// Examples: "Point<int>" -> "Point", "MyMap<std::string, int>" -> "MyMap", "Button" -> "Button"
inline std::string extract_base_template_name(const std::string& fullTypeName) {
    size_t anglePos = fullTypeName.find('<');
    if (anglePos != std::string::npos) {
        return fullTypeName.substr(0, anglePos);
    }
    return fullTypeName; // No template, return as-is
}

// Type trait to detect if a type has a constructor that takes engine*
template<typename T>
concept has_engine_constructor = requires(engine* eng) {
    T(eng);
};

// Detail namespace for factory registration helpers
namespace dynamic_binder_detail {
    using namespace serialization;

    // Helper to create wrapped script_value from C++ object
    template<typename T>
    script_value wrap_cpp_object(std::shared_ptr<T> cpp_obj, const std::string& class_name, engine* engine_ptr) {
        // Get engine for wrapping
        auto eng = engine_ptr;
        if (!eng) {
            throw serialization_error("Engine expired during deserialization");
        }

        // Get class definition
        auto class_def = eng->get_class_definition(class_name);
        if (!class_def) {
            throw serialization_error("Class definition not found: " + class_name);
        }

        // Create a class_instance to hold it
        auto instance = class_def->create_instance();

        // Store the C++ object in the class_instance
        uint64_t cpp_object_field_id = eng->symbolize(class_constants::CPP_OBJECT_FIELD);
        instance->set_field(cpp_object_field_id,
            script_value::make_cpp_object(class_name, class_def->get_type_id(), cpp_obj, eng));

        // Return wrapped object
        return script_value::make_object(class_name, instance, eng);
    }

    // Archive-only factory registration (no context needed)
    // Uses any_archive_reader for type-erased callback storage
    template<typename T, typename FactoryFunc>
    std::function<script_value(serialization::any_archive_reader&, uint32_t)>
    make_archive_only_factory(FactoryFunc&& factory, std::string class_name, engine* engine_ptr) {
        return [factory = std::forward<FactoryFunc>(factory), class_name = std::move(class_name), engine_ptr]
               (serialization::any_archive_reader& archive, uint32_t version) -> script_value {
            auto cpp_obj = factory(archive);
            return wrap_cpp_object<T>(cpp_obj, class_name, engine_ptr);
        };
    }

    // Forward declarations for context-based deserialization factories
    // These functions require archive_reader to be fully defined before instantiation.
    // Implementations are provided in dynamic_binder_serialization.hpp which should be
    // included explicitly by code that uses these context-based factories.
    template<typename T, typename ContextType, typename FactoryFunc>
    std::function<script_value(serialization::any_archive_reader&, uint32_t)>
    make_context_only_factory(FactoryFunc&& factory, std::string class_name, engine* engine_ptr);

    template<typename T, typename ContextType, typename FactoryFunc>
    std::function<script_value(serialization::any_archive_reader&, uint32_t)>
    make_context_archive_factory(FactoryFunc&& factory, std::string class_name, engine* engine_ptr);
}

// Tag for opting out of registration-time type dependency validation
// Use this with .property() to acknowledge circular dependencies
// Example: .property("child", &Parent::child, skip_type_check)
struct skip_type_check_t {};
inline constexpr skip_type_check_t skip_type_check{};

namespace dynamic_binder_validation {
    // Helper to detect if a type is a standard container
    template<typename T>
    struct is_std_container : std::false_type {};

    template<typename... Args>
    struct is_std_container<std::vector<Args...>> : std::true_type {};

    template<typename... Args>
    struct is_std_container<std::map<Args...>> : std::true_type {};

    template<typename... Args>
    struct is_std_container<std::unordered_map<Args...>> : std::true_type {};

    template<typename... Args>
    struct is_std_container<std::set<Args...>> : std::true_type {};

    template<typename... Args>
    struct is_std_container<std::unordered_set<Args...>> : std::true_type {};

    // Helper to detect if a type is a signal type (jai::signal_emitter, jai::signal)
    // Signal types are internal callback mechanisms and don't need registration validation
    template<typename T>
    struct is_signal_type : std::false_type {};

    template<typename Sig>
    struct is_signal_type<signal_emitter<Sig>> : std::true_type {};

    template<typename Sig>
    struct is_signal_type<signal<Sig>> : std::true_type {};

    // Helper to unwrap smart pointers to get the inner type
    // For std::shared_ptr<T>, std::weak_ptr<T>, std::unique_ptr<T> -> extracts T
    // For other types -> returns the type as-is
    template<typename T>
    struct unwrap_smart_pointer {
        using type = T;
    };

    template<typename U>
    struct unwrap_smart_pointer<std::shared_ptr<U>> {
        using type = U;
    };

    template<typename U>
    struct unwrap_smart_pointer<std::weak_ptr<U>> {
        using type = U;
    };

    template<typename U>
    struct unwrap_smart_pointer<std::unique_ptr<U>> {
        using type = U;
    };

    template<typename T>
    using unwrap_smart_pointer_t = typename unwrap_smart_pointer<T>::type;

    // Helper to get the validation type (unwraps smart pointers and removes cv/ref)
    // For std::shared_ptr<CustomType> -> extracts CustomType
    // For int& -> extracts int
    template<typename T>
    struct get_validation_type {
        using bare_type = std::remove_cv_t<std::remove_reference_t<T>>;
        using type = unwrap_smart_pointer_t<bare_type>;
    };

    template<typename T>
    using get_validation_type_t = typename get_validation_type<T>::type;

    // Helper to determine if a type needs registration validation
    // Returns true for types that should be registered with dynamic_binder
    // Returns false for primitives, std::string, STL containers, signal types, and script_value
    // IMPORTANT: Smart pointers are unwrapped - we validate the inner type
    template<typename T>
    constexpr bool needs_registration_check() {
        using inner_type = get_validation_type_t<T>;  // Unwrap smart pointers

        return !std::is_fundamental_v<inner_type> &&
               !std::is_same_v<inner_type, std::string> &&
               !is_std_container<inner_type>::value &&
               !is_signal_type<inner_type>::value &&
               !std::is_same_v<inner_type, script_value> &&
               std::is_class_v<inner_type>;
    }

}

// ============================================================================
// detail namespace - Implementation helpers (already inside namespace jai)
// ============================================================================

namespace detail {
    // ============================================================================
    // extract_cpp_object_ptr<T> - Extract C++ object from class_instance or cpp_bound
    // ============================================================================
    //
    // This helper extracts a raw T* pointer from a script_value that represents
    // a C++ object. Handles two cases:
    //   1. class_instance wrapper: extracts via get_field(cpp_object_field_id)
    //   2. cpp_bound value: extracts directly from cpp_bound_ptr_
    //
    // Used by dynamic_binder method handlers to support method chaining where
    // methods return T& (creating cpp_bound values) that are then used as 'this'
    // for subsequent method calls.
    //
    template<typename T>
    T* extract_cpp_object_ptr(const script_value& val) {
        // Case 1: Non-owning cpp_bound reference (from T& return)
        if (val.is_cpp_bound()) {
            T* ptr = val.get_cpp_bound_as<T>();
            if (ptr) return ptr;
            // If cpp_bound but wrong type, fall through to try class_instance path
        }

        // Case 2: class_instance wrapper (normal constructed object)
        auto instance = val.as<std::shared_ptr<class_instance>>();
        auto cpp_obj_value = instance->get_field(instance->get_cpp_object_field_id());

        // The cpp_object field could also be cpp_bound or a shared_ptr
        if (cpp_obj_value.is_cpp_bound()) {
            return cpp_obj_value.get_cpp_bound_as<T>();
        }

        auto cpp_obj = cpp_obj_value.as<std::shared_ptr<T>>();
        return cpp_obj.get();
    }
} // namespace detail

// ============================================================================
// Bind Mode for auto_build()
// ============================================================================

enum class bind_mode {
    all,        // Base classes + properties + auto-detected methods/constructors
    properties, // Base classes + properties only (no auto methods/constructors)
    hierarchy   // Base classes only (no properties or auto methods)
};

// ============================================================================
// Concepts for auto-detection (used by auto_build)
// ============================================================================

namespace auto_bind_concepts {

// Detect if T has to_string() method
template<typename T>
concept has_to_string = requires(const T& t) {
    { t.to_string() } -> std::convertible_to<std::string>;
};

// Detect if T has size() method
template<typename T>
concept has_size = requires(const T& t) {
    { t.size() } -> std::convertible_to<size_t>;
};

// Detect if T has empty() method
template<typename T>
concept has_empty = requires(const T& t) {
    { t.empty() } -> std::convertible_to<bool>;
};

// Detect if T has operator==
template<typename T>
concept has_equality = requires(const T& a, const T& b) {
    { a == b } -> std::convertible_to<bool>;
};

// Detect if T uses property_owner CRTP (has _jai_owner_type)
template<typename T>
concept is_property_owner = requires {
    typename T::_jai_owner_type;
};

// Detect if T has base types defined
template<typename T>
concept has_base_types = requires {
    typename T::_jai_base_types;
};

// Forward declaration for jai_auto_bind detection
template<typename U> class dynamic_binder;

// Detect if T has static jai_auto_bind(dynamic_binder<T>&) method
// This allows classes to register their own private members during auto_bind()
template<typename T>
concept has_jai_auto_bind = requires(dynamic_binder<T>& builder) {
    { T::jai_auto_bind(builder) } -> std::same_as<void>;
};

} // namespace auto_bind_concepts

// Builder pattern for registering C++ classes to JaiScript
template<typename T>
class dynamic_binder {
public:
    dynamic_binder(engine& engine, const std::string& class_name)
        : engine_(engine), class_name_(class_name) {
        // Extract base template name if this is a templated type
        std::string baseTemplateName = extract_base_template_name(class_name);

        // Register the base template name if it contains template syntax
        if (baseTemplateName != class_name) {
            engine.register_template_type(baseTemplateName);
        }

        // Intern the class name for fast type comparisons
        uint64_t type_id = engine.get_symbolizer()->intern(class_name);

        class_def_ = std::make_shared<class_definition>(class_name, type_id, &engine_);
        
        // Initialize serialization metadata
        serialization_metadata_.class_name = class_name;
        serialization_metadata_.current_version = 1;
    }
    
    // Constructor that accepts a shared_ptr<engine>
    dynamic_binder(std::shared_ptr<engine>& engine_ptr, const std::string& class_name)
        : dynamic_binder(*engine_ptr, class_name) {
    }

    // Destructor - automatically call build() if not already called
    // This ensures the class is registered even if the user forgets to call build()
    ~dynamic_binder() {
        if (!built_) {
            build();
        }
    }

    // Disable copy (would cause double-build issues)
    dynamic_binder(const dynamic_binder&) = delete;
    dynamic_binder& operator=(const dynamic_binder&) = delete;

    // Enable move
    dynamic_binder(dynamic_binder&& other) noexcept
        : engine_(other.engine_)
        , class_name_(std::move(other.class_name_))
        , class_def_(std::move(other.class_def_))
        , serialization_metadata_(std::move(other.serialization_metadata_))
        , has_base_class_(other.has_base_class_)
        , base_type_index_(other.base_type_index_)
        , has_explicit_constructor_(other.has_explicit_constructor_)
        , built_(other.built_)
    {
        // Mark the moved-from object as built to prevent double-registration
        other.built_ = true;
    }

    // Add constructor
    template<typename... Args>
    dynamic_binder& constructor() {
        has_explicit_constructor_ = true;  // Track that user explicitly registered a constructor

        // Register the constructor as an overloaded function
        if constexpr (sizeof...(Args) == 0) {
            // Zero-argument constructor
            engine* engine_ptr = &engine_;
            engine_.add_overloaded_function(class_name_, 0, [class_def = class_def_, class_name = class_name_, engine_ptr](const std::vector<script_value>& args) -> script_value {
                try {
                    // Create the C++ object
                    std::shared_ptr<T> cpp_obj;

                    // Check if T has an engine constructor and use it if available
                    if constexpr (has_engine_constructor<T>) {
                        // Use the engine-aware constructor
                        cpp_obj = std::make_shared<T>(engine_ptr);
                    } else {
                        // Use the default constructor
                        cpp_obj = std::make_shared<T>();
                    }

                    // Create a class_instance to hold it
                    auto instance = class_def->create_instance();

                    // Store the C++ object in the class_instance as a special field
                    instance->set_field(instance->get_cpp_object_field_id(), script_value::make_cpp_object(class_name, class_def->get_type_id(), cpp_obj, engine_ptr));

                    // Return the class_instance wrapped in a value
                    return script_value::make_object(class_name, instance, engine_ptr);
                } catch (const std::exception& e) {
                    std::cerr << "Error in zero-arg constructor: " << e.what() << std::endl;
                    throw;
                }
            });
        } else {
            // Multi-argument constructor
            engine* engine_ptr = &engine_;

            // Helper to avoid template parameter pack in lambda (MSVC workaround)
            using constructor_helper = std::shared_ptr<T>(*)(const std::vector<script_value>&, engine*);
            constructor_helper helper = [](const std::vector<script_value>& args, engine* eng) -> std::shared_ptr<T> {
                return dynamic_binder<T>::template createObjectImpl<Args...>(args, std::index_sequence_for<Args...>{}, eng);
            };

            engine_.add_overloaded_function(class_name_, sizeof...(Args), [class_def = class_def_, class_name = class_name_, engine_ptr, helper](const std::vector<script_value>& args) -> script_value {
                try {
                    // Extract arguments using index-based unpacking via helper
                    auto cpp_obj = helper(args, engine_ptr);

                    // Create a class_instance to hold it
                    auto instance = class_def->create_instance();

                    // Store the C++ object in the class_instance as a special field
                    instance->set_field(instance->get_cpp_object_field_id(), script_value::make_cpp_object(class_name, class_def->get_type_id(), cpp_obj, engine_ptr));

                    // Return the class_instance wrapped in a value
                    return script_value::make_object(class_name, instance, engine_ptr);
                } catch (const std::exception&) {
                    throw;
                }
            });
        }
        
        return *this;
    }
    
    // Add method binding - member function pointer version
    template<typename R, typename... Args>
    dynamic_binder& method(const std::string& name, R(T::*method)(Args...)) {
        auto method_func = [method, engine_ptr = &engine_](const std::vector<script_value>& args) -> script_value {
            if (args.empty()) {
                throw runtime_error("Method called without 'this' object");
            }
            
            // Validate argument count (first arg is 'this', so we need sizeof...(Args) + 1 total)
            if (args.size() != sizeof...(Args) + 1) {
                throw runtime_error("Method expects " + std::to_string(sizeof...(Args)) +
                                 " arguments, got " + std::to_string(args.size() - 1));
            }

            // Extract the C++ object from the first argument (this)
            // Handles both class_instance wrappers and cpp_bound values (for method chaining)
            T* cpp_obj = detail::extract_cpp_object_ptr<T>(args[0]);

            // Call the method with unpacked arguments
            if (auto eng = engine_ptr) {
                return dynamic_binder<T>::callMethodImpl(cpp_obj, method, args, std::index_sequence_for<Args...>{}, eng);
            }
            throw runtime_error("Engine no longer exists");
        };

        // Add method to the class definition (for object.method() calls)
        // Methods are stored per-class and accessed through the object instance
        class_def_->add_method(name, method_func, sizeof...(Args));

        return *this;
    }

    // Add const method binding
    template<typename R, typename... Args>
    dynamic_binder& method(const std::string& name, R(T::*method)(Args...) const) {
        auto method_func = [method, engine_ptr = &engine_](const std::vector<script_value>& args) -> script_value {
            if (args.empty()) {
                throw runtime_error("Method called without 'this' object");
            }

            // Validate argument count (first arg is 'this', so we need sizeof...(Args) + 1 total)
            if (args.size() != sizeof...(Args) + 1) {
                throw runtime_error("Method expects " + std::to_string(sizeof...(Args)) +
                                 " arguments, got " + std::to_string(args.size() - 1));
            }

            // Extract the C++ object from the first argument (this)
            // Handles both class_instance wrappers and cpp_bound values (for method chaining)
            T* cpp_obj = detail::extract_cpp_object_ptr<T>(args[0]);

            // Call the method with unpacked arguments
            if (engine_ptr) {
                return dynamic_binder<T>::callConstMethodImpl(cpp_obj, method, args, std::index_sequence_for<Args...>{}, engine_ptr);
            }
            throw runtime_error("Engine no longer exists");
        };

        // Add method to the class definition (for object.method() calls)
        // Methods are stored per-class and accessed through the object instance
        class_def_->add_method(name, method_func, sizeof...(Args));

        return *this;
    }

    // Add lambda/callable method binding
    // Supports: .method("setText", [](Button& self, const std::string& text) { self.setText(text); })
    // Note: First parameter can be a reference to self for accessing the object
    template<typename Callable>
    dynamic_binder& method(const std::string& name, Callable&& callable) {
        // Detect accidental use of member data pointers - use property() instead
        static_assert(!std::is_member_object_pointer_v<std::decay_t<Callable>>,
            "Member data pointers cannot be passed to method(). Use property() instead.");

        // Use function_traits to determine the signature
        using traits = detail::function_traits<std::decay_t<Callable>>;
        using args_tuple = typename traits::argument_types;
        
        // Check if the first parameter is a reference to T (the self parameter)
        // Support both T& and const T& for lambda methods
        constexpr bool has_self_param = traits::arity > 0 &&
            (std::is_same_v<std::tuple_element_t<0, args_tuple>, T&> ||
             std::is_same_v<std::tuple_element_t<0, args_tuple>, const T&>);
        
        auto method_func = [callable = std::forward<Callable>(callable), has_self_param, engine_ptr = &engine_](const std::vector<script_value>& args) -> script_value {
            if (has_self_param) {
                // Lambda expects T& as first parameter, we need to extract it from args[0]
                // args[0] is the class_instance, remaining args are the actual parameters
                if (args.empty()) {
                    throw runtime_error("Method called without 'this' object");
                }
                
                // Expected argument count is arity - 1 (excluding self) + 1 (for 'this')
                if (args.size() != traits::arity) {
                    throw runtime_error("Method expects " + std::to_string(traits::arity - 1) +
                                     " arguments, got " + std::to_string(args.size() - 1));
                }

                // Extract the C++ object from the first argument (this)
                // Handles both class_instance wrappers and cpp_bound values (for method chaining)
                T* cpp_obj = detail::extract_cpp_object_ptr<T>(args[0]);

                // Call the lambda with the C++ object as first argument and remaining args
                if (auto eng = engine_ptr) {
                    return callLambdaWithSelf<typename traits::return_type, args_tuple>(
                        callable, cpp_obj, args, std::make_index_sequence<traits::arity>{}, eng);
                }
                throw runtime_error("Engine no longer exists");
            } else {
                // Regular lambda without self parameter
                if (args.size() != traits::arity) {
                    throw runtime_error("Method expects " + std::to_string(traits::arity) + 
                                     " arguments, got " + std::to_string(args.size()));
                }
                
                // Call the lambda with unpacked arguments
                if (auto eng = engine_ptr) {
                    return callCallableImpl<typename traits::return_type, args_tuple>(callable, args, std::make_index_sequence<traits::arity>{}, eng);
                }
                throw runtime_error("Engine no longer exists");
            }
        };

        // Add method to the class definition (for object.method() calls)
        // Methods are stored per-class and accessed through the object instance
        // Arity from script perspective: if has_self_param, arity is traits::arity - 1, else traits::arity
        constexpr size_t script_arity = has_self_param ? traits::arity - 1 : traits::arity;
        class_def_->add_method(name, method_func, script_arity);

        return *this;
    }
    
    // Add static method binding - for regular function pointers
    template<typename R, typename... Args>
    dynamic_binder& static_method(const std::string& name, R(*func)(Args...)) {
        auto static_method_func = [func, engine_ptr = &engine_](const std::vector<script_value>& args) -> script_value {
            // Validate argument count (no 'this' for static methods)
            if (args.size() != sizeof...(Args)) {
                throw runtime_error("Static method expects " + std::to_string(sizeof...(Args)) + 
                                 " arguments, got " + std::to_string(args.size()));
            }
            
            // Call the static function directly
            if constexpr (std::is_void_v<R>) {
                callStaticFunctionImpl(func, args, std::make_index_sequence<sizeof...(Args)>{});
                
                if (auto eng = engine_ptr) {
                    return script_value::make_null(eng);
                }
                throw runtime_error("Engine no longer exists");
            } else {
                auto result = callStaticFunctionImpl(func, args, std::make_index_sequence<sizeof...(Args)>{});
                
                if (auto eng = engine_ptr) {
                    return script_value(result, eng);
                }
                throw runtime_error("Engine no longer exists");
            }
        };
        
        // Add static method to the class definition with arity
        class_def_->add_static_method(name, static_method_func, sizeof...(Args));

        return *this;
    }
    
    // Add static method binding - for lambdas and callables
    template<typename Callable>
    dynamic_binder& static_method(const std::string& name, Callable&& callable) {
        // Detect accidental use of member pointers
        static_assert(!std::is_member_pointer_v<std::decay_t<Callable>>,
            "Member pointers cannot be passed to static_method(). Use method() or property() instead.");

        using traits = detail::function_traits<std::decay_t<Callable>>;
        
        auto static_method_func = [callable = std::forward<Callable>(callable), engine_ptr = &engine_](const std::vector<script_value>& args) -> script_value {
            // Validate argument count
            if (args.size() != traits::arity) {
                throw runtime_error("Static method expects " + std::to_string(traits::arity) + 
                                 " arguments, got " + std::to_string(args.size()));
            }
            
            // Call the callable directly (no 'this' parameter)
            using return_type = typename traits::return_type;
            if constexpr (std::is_void_v<return_type>) {
                callStaticCallableImpl(callable, args, std::make_index_sequence<traits::arity>{});
                
                if (auto eng = engine_ptr) {
                    return script_value::make_null(eng);
                }
                throw runtime_error("Engine no longer exists");
            } else {
                auto result = callStaticCallableImpl(callable, args, std::make_index_sequence<traits::arity>{});
                
                if (auto eng = engine_ptr) {
                    return script_value(result, eng);
                }
                throw runtime_error("Engine no longer exists");
            }
        };

        // Add static method to the class definition with arity
        class_def_->add_static_method(name, static_method_func, traits::arity);

        return *this;
    }

    // Add custom serialization constructor for non-default constructible types
    template<typename constructor_func>
    dynamic_binder& serialize_construct(constructor_func&& constructor) {
        // Store the custom constructor in the class definition
        class_def_->add_method("_serialize_construct", [constructor = std::forward<constructor_func>(constructor), class_def = class_def_, class_name = class_name_, engine_ptr = &engine_](const std::vector<script_value>& args) -> script_value {
            if (args.size() != 1) {
                throw runtime_error("Serialization constructor expects exactly one argument (the serialized data)");
            }
            
            // Call the custom constructor with the serialized data
            T instance = constructor(args[0]);
            
            // Create a class_instance to hold it
            auto class_instance = class_def->create_instance();
            class_instance->set_field(instance->get_cpp_object_field_id(), script_value::make_cpp_object(class_name,
                class_def->get_type_id(), std::make_shared<T>(std::move(instance)), engine_ptr));

            if (auto eng = engine_ptr) {
                return script_value::make_object(class_name, class_instance, eng);
            }
            throw runtime_error("Engine no longer exists");
        });
        
        // Also register serialization metadata
        auto& metadata = serialization_metadata_;
        metadata.custom_construct = [constructor = std::forward<constructor_func>(constructor)](serialization::any_archive_reader& ar, uint32_t version) -> script_value {
            // Convert archive data to script_value for the constructor
            // This is a simplified implementation - real version would need proper conversion
            script_value data = script_value(); // TODO: Convert archive to script_value
            T instance = constructor(data, version);

            // TODO: Wrap in class_instance and return as script_value
            return script_value();
        };
        
        return *this;
    }
    
    // Set class version
    dynamic_binder& version(uint32_t v) {
        serialization_metadata_.current_version = v;
        return *this;
    }

    // Add property/field binding (with automatic registration validation)
    template<typename P>
    dynamic_binder& property(const std::string& name, P T::*member) {
        return property_impl<P>(name, member, false);
    }

    // Add property/field binding (opt-out of registration validation)
    // Use this to explicitly acknowledge circular dependencies
    template<typename P>
    dynamic_binder& property(const std::string& name, P T::*member, skip_type_check_t) {
        return property_impl<P>(name, member, true);
    }

private:
    // Internal implementation for property registration with validation
    template<typename P>
    dynamic_binder& property_impl(const std::string& name, P T::*member, bool skip_validation) {
        // Validate that property type is registered (unless explicitly skipped)
        if constexpr (dynamic_binder_validation::needs_registration_check<P>()) {
            if (!skip_validation) {
                // Extract the validation type (unwraps smart pointers)
                using validation_type_t = typename dynamic_binder_validation::get_validation_type<P>::type;

                // Check if this type has been registered with dynamic_binder
                // We use std::type_index to identify types, just like the rest of the codebase
                auto prop_type_index = std::type_index(typeid(validation_type_t));
                auto registered_class = engine_.get_class_definition_by_type(prop_type_index);

                if (!registered_class) {
                    throw std::runtime_error(
                        "Property '" + name + "' of class '" + class_name_ + "' uses unregistered type '" +
                        typeid(P).name() + "'. " +
                        "You must register this type with dynamic_binder before registering '" + class_name_ + "', " +
                        "or use skip_type_check to explicitly acknowledge circular dependencies:\n" +
                        "  .property(\"" + name + "\", &" + class_name_ + "::" + name + ", jai::skip_type_check)"
                    );
                }
            }
        }

        // Register the property as a special field that knows how to access the C++ member
        // We'll store a lambda that can get/set the value
        class_def_->add_field(name, script_value(std::monostate{}, &engine_)); // Register field name

        // Register serialization metadata
        serialization::property_metadata prop_meta;
        prop_meta.name = name;
        prop_meta.type = engine_.get_type_info_for_cpp_type<P>();
        serialization_metadata_.properties.push_back(prop_meta);
        
        // Add a special method that handles property access
        // The interpreter's visitMemberExpr will need to check for these
        class_def_->add_method("_get_" + name, [member, engine_ptr = &engine_](const std::vector<script_value>& args) -> script_value {
            if (args.empty()) {
                throw runtime_error("Property getter called without 'this' object");
            }

            // Extract the C++ object from the first argument (this)
            // Handles both class_instance wrappers and cpp_bound values (for method chaining)
            T* cpp_obj = detail::extract_cpp_object_ptr<T>(args[0]);

            if (auto eng = engine_ptr) {
                // Special handling for std::vector<T> - wrap in bound_cpp_vector for zero-copy access
                if constexpr (is_specialization_v<P, std::vector>) {
                    using element_type = typename P::value_type;
                    // Create bound_cpp_vector wrapper that references the C++ vector directly
                    auto wrapper = std::make_shared<bound_cpp_vector<element_type>>(
                        cpp_obj->*member, eng);
                    return eng->make_object(wrapper);
                }
                else {
                    return detail::value_converter<P>::to(cpp_obj->*member, eng);
                }
            }
            throw runtime_error("Engine no longer exists");
        });

        // Only create setter for copy-assignable types
        if constexpr (std::is_copy_assignable_v<P>) {
            class_def_->add_method("_set_" + name, [member, engine_ptr = &engine_](const std::vector<script_value>& args) -> script_value {
                if (args.size() < 2) {
                    throw runtime_error("Property setter requires 'this' and value");
                }

                // Extract the C++ object from the first argument (this)
                // Handles both class_instance wrappers and cpp_bound values (for method chaining)
                T* cpp_obj = detail::extract_cpp_object_ptr<T>(args[0]);

                // Special case: if P is script_value, don't convert
                if constexpr (std::is_same_v<P, script_value>) {
                    // deref() returns *this if not a reference, so this handles both cases
                    (cpp_obj->*member).deref() = args[1].clone();
                } else {
                    cpp_obj->*member = args[1].as<P>();
                }
                return script_value(std::monostate{}, engine_ptr); // null
            });

            // Register field_id -> setter_id mapping for fast runtime lookup
            uint64_t field_id = engine_.symbolize(name);
            uint64_t setter_id = engine_.symbolize("_set_" + name);
            class_def_->register_property_setter(field_id, setter_id);
        }

        // Register bound_cpp_vector<T> if this property is a std::vector
        if constexpr (is_specialization_v<P, std::vector>) {
            using element_type = typename P::value_type;
            std::string wrapper_type_name = std::string("bound_cpp_vector<") + typeid(element_type).name() + ">";

            // Check if already registered
            auto existing = engine_.get_class_definition_by_type(std::type_index(typeid(bound_cpp_vector<element_type>)));
            if (!existing) {
                // Register bound_cpp_vector<element_type> with array-like methods
                dynamic_binder<bound_cpp_vector<element_type>>(engine_, wrapper_type_name)
                    .method("size", &bound_cpp_vector<element_type>::size)
                    .method("empty", &bound_cpp_vector<element_type>::empty)
                    .method("clear", &bound_cpp_vector<element_type>::clear)
                    .method("push_back", static_cast<void(bound_cpp_vector<element_type>::*)(const element_type&)>(&bound_cpp_vector<element_type>::push_back))
                    .method("push", static_cast<void(bound_cpp_vector<element_type>::*)(const element_type&)>(&bound_cpp_vector<element_type>::push_back)) // Alias
                    .method("pop_back", &bound_cpp_vector<element_type>::pop_back)
                    .method("pop", &bound_cpp_vector<element_type>::pop_back) // Alias
                    .method("front", static_cast<element_type&(bound_cpp_vector<element_type>::*)()>(&bound_cpp_vector<element_type>::front))
                    .method("back", static_cast<element_type&(bound_cpp_vector<element_type>::*)()>(&bound_cpp_vector<element_type>::back))
                    .method("at", static_cast<element_type&(bound_cpp_vector<element_type>::*)(size_t)>(&bound_cpp_vector<element_type>::at))
                    .method("[]", static_cast<element_type&(bound_cpp_vector<element_type>::*)(size_t)>(&bound_cpp_vector<element_type>::operator[]))
                    .build();
            }
        }

        return *this;
    }

public:
    // Add property with getter/setter (supports both lambdas and member function pointers)
    template<typename Getter, typename Setter>
    dynamic_binder& property(const std::string& name, Getter&& getter, Setter&& setter) {
        // Register the property as a field
        class_def_->add_field(name, script_value(std::monostate{}, &engine_)); // Register field name

        // Register serialization metadata
        constexpr bool is_read_only = std::is_null_pointer_v<std::decay_t<Setter>> || std::is_same_v<std::decay_t<Setter>, std::nullptr_t>;

        serialization::property_metadata prop_meta;
        prop_meta.name = name;
        prop_meta.read_only = is_read_only;

        // Try to deduce type from getter return type
        if constexpr (std::is_member_function_pointer_v<std::decay_t<Getter>>) {
            using getter_traits = detail::function_traits<std::decay_t<Getter>>;
            using return_type = typename getter_traits::return_type;
            prop_meta.type = engine_.get_type_info_for_cpp_type<std::decay_t<return_type>>();
        } else {
            // For lambdas, we can't easily deduce the type at compile time
            // Leave type as nullptr - it will be determined at runtime if needed
            prop_meta.type = nullptr;
        }

        serialization_metadata_.properties.push_back(prop_meta);

        // Add getter method
        class_def_->add_method("_get_" + name, [getter = std::forward<Getter>(getter), engine_ptr = &engine_](const std::vector<script_value>& args) -> script_value {
            if (args.empty()) {
                throw runtime_error("Property getter called without 'this' object");
            }

            // Extract the C++ object from the first argument (this)
            // Handles both class_instance wrappers and cpp_bound values (for method chaining)
            T* cpp_obj = detail::extract_cpp_object_ptr<T>(args[0]);

            // Check if getter is a member function pointer
            if constexpr (std::is_member_function_pointer_v<std::decay_t<Getter>>) {
                // Call member function pointer: (obj->*getter)()
                if (auto eng = engine_ptr) {
                    return detail::value_converter<decltype((cpp_obj->*getter)())>::to((cpp_obj->*getter)(), eng);
                }
                throw runtime_error("Engine no longer exists");
            } else {
                // Call lambda: getter(*cpp_obj)
                if (auto eng = engine_ptr) {
                    return detail::value_converter<decltype(getter(*cpp_obj))>::to(getter(*cpp_obj), eng);
                }
                throw runtime_error("Engine no longer exists");
            }
        });

        // Add setter method - use different implementations based on setter type
        if constexpr (std::is_null_pointer_v<std::decay_t<Setter>> || std::is_same_v<std::decay_t<Setter>, std::nullptr_t>) {
            // Readonly property - add a no-op setter
            class_def_->add_method("_set_" + name, [engine_ptr = &engine_](const std::vector<script_value>& args) -> script_value {
                // Read-only property, do nothing
                return script_value(std::monostate{}, engine_ptr);
            });
        } else if constexpr (std::is_member_function_pointer_v<std::decay_t<Setter>>) {
            // Member function pointer setter
            class_def_->add_method("_set_" + name, [setter = std::forward<Setter>(setter), engine_ptr = &engine_](const std::vector<script_value>& args) -> script_value {
                if (args.size() < 2) {
                    throw runtime_error("Property setter requires 'this' and value");
                }

                // Extract the C++ object from the first argument (this)
                // Handles both class_instance wrappers and cpp_bound values (for method chaining)
                T* cpp_obj = detail::extract_cpp_object_ptr<T>(args[0]);

                using setter_traits = detail::function_traits<std::decay_t<Setter>>;
                using value_type = std::tuple_element_t<0, typename setter_traits::argument_types>;
                auto value = args[1].as<value_type>();
                (cpp_obj->*setter)(value);
                return script_value(std::monostate{}, engine_ptr);
            });
        } else {
            // Lambda setter
            class_def_->add_method("_set_" + name, [setter = std::forward<Setter>(setter), engine_ptr = &engine_](const std::vector<script_value>& args) -> script_value {
                if (args.size() < 2) {
                    throw runtime_error("Property setter requires 'this' and value");
                }

                // Extract the C++ object from the first argument (this)
                // Handles both class_instance wrappers and cpp_bound values (for method chaining)
                T* cpp_obj = detail::extract_cpp_object_ptr<T>(args[0]);

                using setter_traits = detail::function_traits<std::decay_t<Setter>>;
                using value_type = std::tuple_element_t<1, typename setter_traits::argument_types>;
                auto value = args[1].as<value_type>();
                setter(*cpp_obj, value);
                return script_value(std::monostate{}, engine_ptr);
            });
        }

        // Register field_id -> setter_id mapping for fast runtime lookup
        uint64_t field_id = engine_.symbolize(name);
        uint64_t setter_id = engine_.symbolize("_set_" + name);
        class_def_->register_property_setter(field_id, setter_id);

        return *this;
    }
    
    // Add static property binding - for simple variable access
    template<typename P>
    dynamic_binder& static_property(const std::string& name, P* static_var) {
        // Convert name to ID
        uint64_t name_id = engine_.symbolize(name);

        // Add static field with getter for the variable
        class_def_->add_static_field(name_id, script_value(*static_var, &engine_));

        // Add getter method for read access
        std::string getter_name = "_get_" + name;
        uint64_t getter_id = engine_.symbolize(getter_name);
        class_def_->add_static_method(getter_id, [static_var, engine_ptr = &engine_](const std::vector<script_value>& args) -> script_value {
            if (auto eng = engine_ptr) {
                return script_value(*static_var, eng);
            }
            throw runtime_error("Engine no longer exists");
        });

        return *this;
    }
    
    // Add static property with lambda getter/setter
    template<typename Getter, typename Setter>
    dynamic_binder& static_property(const std::string& name, Getter&& getter, Setter&& setter) {
        // Add getter method
        std::string getter_name = "_get_" + name;
        uint64_t getter_id = engine_.symbolize(getter_name);
        class_def_->add_static_method(getter_id, [getter = std::forward<Getter>(getter), engine_ptr = &engine_](const std::vector<script_value>& args) -> script_value {
            if (auto eng = engine_ptr) {
                return detail::value_converter<decltype(getter())>::to(getter(), eng);
            }
            throw runtime_error("Engine no longer exists");
        });

        // Note: setter parameter kept for API compatibility but not registered as setX()
        // Static property assignment uses set_static_field() directly in the interpreter
        (void)setter;  // Suppress unused warning

        return *this;
    }
    
    // Add base class - establishes inheritance relationship (appends, doesn't clear)
    // This enables:
    // 1. Method inheritance (derived can call base methods)
    // 2. Polymorphic copy support
    // Can be chained: .base_class<A>().base_class<B>() for multiple inheritance
    template<typename Base>
    dynamic_binder& base_class() {
        static_assert(std::is_base_of_v<Base, T>,
                      "Specified type is not a base class of this class");

        // Set up inheritance relationship - use type_index lookup instead of typeid name
        auto base_def = engine_.get_class_definition_by_type(std::type_index(typeid(Base)));
        if (base_def) {
            // Use add_parent to append (validates for diamond inheritance)
            // add_parent is idempotent (returns true if already registered)
            // but returns false for true diamond inheritance
            if (!class_def_->add_parent(base_def)) {
                throw std::runtime_error("Diamond inheritance detected: class '" + class_name_ +
                    "' would have multiple paths to the same base class");
            }
        }

        // Store base type info for polymorphic copy registration
        // For multiple inheritance, this stores the last base (first base is used for primary polymorphism)
        if (!has_base_class_) {
            base_type_index_ = std::type_index(typeid(Base));
        }
        has_base_class_ = true;

        return *this;
    }
    
    // Add explicit type conversion support - general purpose
    template<typename From, typename To>
    dynamic_binder& add_type_conversion(std::function<To(const From&)> converter) {
        // Register the conversion with the engine
        // This would need to be implemented in the engine's type system
        // Usage: .add_type_conversion<SafeComponent<Button>, std::shared_ptr<Button>>([](const auto& item) { return item.self(); })
        return *this;
    }

private:

public:
    // Register a deserialization factory for non-default constructors
    // Supports three signatures automatically detected via template metaprogramming:
    // 1. Archive-only: [](serialization::archive_reader& archive) -> std::shared_ptr<T>
    // 2. Context-only: [](ContextType* ctx) -> std::shared_ptr<T>
    // 3. Context + archive: [](ContextType* ctx, serialization::archive_reader& archive) -> std::shared_ptr<T>
    template<typename ContextType = void, typename FactoryFunc>
    dynamic_binder& deserialization_factory(FactoryFunc&& factory) {
        using namespace serialization;

        // Detect factory signature using function traits
        using factory_traits = detail::function_traits<std::decay_t<FactoryFunc>>;
        constexpr size_t arg_count = factory_traits::arity;

        // Dispatch to appropriate helper based on signature
        if constexpr (arg_count == 1) {
            // Check if single arg is serialization::any_archive_reader& or context pointer
            using arg0_type = std::tuple_element_t<0, typename factory_traits::argument_types>;

            // Check if the decayed type (without reference/pointer) is any_archive_reader
            if constexpr (std::is_same_v<std::decay_t<arg0_type>, serialization::any_archive_reader>) {
                // Archive-only factory: [](serialization::any_archive_reader& archive) -> std::shared_ptr<T>
                serialization_metadata_.custom_construct =
                    dynamic_binder_detail::make_archive_only_factory<T>(
                        std::forward<FactoryFunc>(factory), class_name_, &engine_);
            } else if constexpr (!std::is_void_v<ContextType>) {
                // Context-only factory: [](ContextType* ctx) -> std::shared_ptr<T>
                serialization_metadata_.custom_construct =
                    dynamic_binder_detail::make_context_only_factory<T, ContextType>(
                        std::forward<FactoryFunc>(factory), class_name_, &engine_);
            } else {
                // Context-only factory requires ContextType to be specified
                static_assert(!std::is_void_v<ContextType>,
                    "Context-only factory requires a non-void ContextType template parameter. "
                    "Use deserialization_factory<YourContextType>([](YourContextType* ctx) { ... })");
            }
        } else if constexpr (arg_count == 2) {
            if constexpr (!std::is_void_v<ContextType>) {
                // Context + archive factory: [](ContextType* ctx, serialization::any_archive_reader& archive) -> std::shared_ptr<T>
                serialization_metadata_.custom_construct =
                    dynamic_binder_detail::make_context_archive_factory<T, ContextType>(
                        std::forward<FactoryFunc>(factory), class_name_, &engine_);
            } else {
                static_assert(!std::is_void_v<ContextType>,
                    "Context+archive factory requires a non-void ContextType template parameter. "
                    "Use deserialization_factory<YourContextType>([](YourContextType* ctx, serialization::any_archive_reader& ar) { ... })");
            }
        } else {
            static_assert(arg_count <= 2, "Deserialization factory must take 1 or 2 arguments");
        }

        return *this;
    }

    // Register post-deserialization hook for migration and data transformation
    // This is a convenience method that registers a "post_deserialize" method
    // that will be automatically called after properties are loaded from archives
    //
    // The hook receives the version number that was serialized, which can be used
    // for migration logic. You can use either signature:
    //
    // Examples:
    //   // Without version parameter (simple computed values)
    //   .post_deserialize_hook([](MyClass& self) {
    //       self.computed_field = self.width * self.height;
    //   })
    //
    //   // With version parameter (for migration)
    //   .post_deserialize_hook([](MyClass& self, int version) {
    //       if (version < 2) {
    //           // Migrate from v1 to v2
    //           self.new_field = self.compute_from_old_fields();
    //       }
    //   })
    template<typename Callable>
    dynamic_binder& post_deserialize_hook(Callable&& callable) {
        // Register as a regular method named "post_deserialize"
        return method("post_deserialize", std::forward<Callable>(callable));
    }

    // ============================================================================
    // transparent_wrapper() - Mark this type as a transparent wrapper
    // ============================================================================
    //
    // When an operation (method call, operator, etc.) is not found on the wrapper,
    // the interpreter will call the unwrap function and retry the operation on
    // the underlying value.
    //
    // This enables patterns like:
    //   player.score + 5        // score is observable_property<int>, forwards + to int
    //   player.score.on_change  // on_change is on the wrapper itself
    //   player.cat.meow()       // cat is observable_property<Cat>, forwards meow() to Cat
    //
    // The unwrap function receives a reference to the wrapper object and returns
    // a script_value containing the underlying value.
    //
    // Usage:
    //   dynamic_binder<observable_property<int>>(eng, "observable_int")
    //       .transparent_wrapper([](observable_property<int>& self) { return self.get(); })
    //       .method("on_change", ...)
    //       .build();
    //
    template<typename UnwrapFn>
    dynamic_binder& transparent_wrapper(UnwrapFn&& unwrap_fn) {
        engine* eng = &engine_;

        // Create the unwrap function that works with script_value
        auto wrapped_fn = [unwrap_fn = std::forward<UnwrapFn>(unwrap_fn), eng](script_value& wrapper_sv, engine* e) -> script_value {
            // Extract the C++ object from the script_value
            if (!wrapper_sv.is_object()) {
                return script_value(std::monostate{}, e);
            }

            auto holder = wrapper_sv.get_object_holder();
            if (!holder) {
                return script_value(std::monostate{}, e);
            }

            // Get the typed pointer - check if it's a class_instance wrapper
            T* cpp_ptr = nullptr;
            if (holder->is_class_instance_wrapper) {
                auto instance = std::static_pointer_cast<class_instance>(holder->data);
                if (instance) {
                    cpp_ptr = instance->get_cpp_object_as<T>().get();
                }
            } else {
                auto typed_ptr = std::static_pointer_cast<T>(holder->data);
                cpp_ptr = typed_ptr.get();
            }

            if (!cpp_ptr) {
                return script_value(std::monostate{}, e);
            }

            // Call the unwrap function and convert result to script_value
            auto underlying_value = unwrap_fn(*cpp_ptr);
            return script_value_from_cpp(underlying_value, e);
        };

        class_def_->set_unwrap_function(std::move(wrapped_fn));
        return *this;
    }

    // ============================================================================
    // auto_bind() - Apply auto-detection of base classes and common methods
    // ============================================================================
    //
    // Usage:
    //   dynamic_binder<Player>(eng, "Player")
    //       .auto_bind()                       // Applies base classes + common methods
    //       .method("custom", &Player::custom); // Add more custom bindings
    //
    //   dynamic_binder<Player>(eng, "Player")
    //       .auto_bind(bind_mode::hierarchy);  // Just base classes, no auto methods
    //
    dynamic_binder& auto_bind(bind_mode mode = bind_mode::all) {
        // Apply base classes from _jai_base_types if present
        if constexpr (auto_bind_concepts::has_base_types<T>) {
            apply_base_classes_from_tuple(std::type_identity<typename T::_jai_base_types>{});
        }

        // Bind properties from type_registry if T is a property_owner
        if constexpr (auto_bind_concepts::is_property_owner<T>) {
            bind_properties_from_schema();
        }

        // Apply auto-detected methods if mode is 'all'
        if (mode == bind_mode::all) {
            if constexpr (auto_bind_concepts::has_to_string<T>) {
                method("to_string", &T::to_string);
            }
            if constexpr (auto_bind_concepts::has_size<T>) {
                method("size", &T::size);
            }
            if constexpr (auto_bind_concepts::has_empty<T>) {
                method("empty", &T::empty);
            }
            if constexpr (auto_bind_concepts::has_equality<T>) {
                method("==", [](const T& a, const T& b) -> bool { return a == b; });
                method("!=", [](const T& a, const T& b) -> bool { return !(a == b); });
            }
        }

        // Call T::jai_auto_bind(*this) if it exists - allows classes to register private members
        if constexpr (auto_bind_concepts::has_jai_auto_bind<T>) {
            T::jai_auto_bind(*this);
        }

        return *this;
    }

private:
    // Bind properties from type_registry schema for property_owner classes
    // This is called when T is complete, so we can directly access T's property_mgr
    void bind_properties_from_schema() {
        // Get the schema for this type (includes inherited properties)
        auto all_props = type_registry::instance().all_properties<T>();

        for (const auto* prop_meta : all_props) {
            if (!prop_meta) continue;

            const std::string prop_name = prop_meta->name;
            const std::type_index value_type = prop_meta->value_type_id;
            const bool is_observable = prop_meta->is_observable;

            // Dispatch to the appropriate template based on value type
            // We use a type-switch here because we have runtime type_index
            if (!try_bind_property_typed<int>(prop_name, value_type, is_observable) &&
                !try_bind_property_typed<float>(prop_name, value_type, is_observable) &&
                !try_bind_property_typed<double>(prop_name, value_type, is_observable) &&
                !try_bind_property_typed<bool>(prop_name, value_type, is_observable) &&
                !try_bind_property_typed<std::string>(prop_name, value_type, is_observable) &&
                !try_bind_property_typed<int64_t>(prop_name, value_type, is_observable) &&
                !try_bind_property_typed<uint64_t>(prop_name, value_type, is_observable) &&
                !try_bind_property_typed<int32_t>(prop_name, value_type, is_observable) &&
                !try_bind_property_typed<uint32_t>(prop_name, value_type, is_observable) &&
                !try_bind_property_typed<int16_t>(prop_name, value_type, is_observable) &&
                !try_bind_property_typed<uint16_t>(prop_name, value_type, is_observable) &&
                !try_bind_property_typed<int8_t>(prop_name, value_type, is_observable) &&
                !try_bind_property_typed<uint8_t>(prop_name, value_type, is_observable)) {
                // Try the global type converter registry for custom types
                try_bind_property_from_registry(prop_name, value_type);
            }
        }
    }

    // Try to bind a property if the value type matches ValueT
    // Returns true if bound, false if type doesn't match
    // For observable properties, also binds an observation method
    template<typename ValueT>
    bool try_bind_property_typed(const std::string& prop_name, std::type_index value_type, bool is_observable) {
        if (value_type != std::type_index(typeid(ValueT))) {
            return false;
        }

        engine* eng = &engine_;

        // For observable properties, register the wrapper type and use a different getter
        if (is_observable) {
            // Ensure the observable_property_ref<ValueT> type is registered
            ensure_observable_ref_type_registered<ValueT>();

            // Create getter that returns the wrapper for observable properties
            // This enables player.score.on_change(callback) syntax
            auto getter = [prop_name, eng](T& self) -> script_value {
                auto* base_prop = self.property_mgr.get(prop_name);
                if (!base_prop) {
                    return script_value(std::monostate{}, eng);
                }
                auto* obs_prop = dynamic_cast<jai::observable_property<ValueT>*>(base_prop);
                if (!obs_prop) {
                    return script_value(std::monostate{}, eng);
                }
                // Create an observable_property_ref wrapper and return it as a script object
                auto ref = std::make_shared<observable_property_ref<ValueT>>(obs_prop, &self.property_mgr);
                return eng->make_object(ref);
            };

            // Create setter that assigns through the property to trigger the signal
            auto setter = [prop_name](T& self, ValueT val) {
                jai::property_base* base_prop = self.property_mgr.get(prop_name);
                if (!base_prop) return;
                jai::observable_property<ValueT>* obs_prop = dynamic_cast<jai::observable_property<ValueT>*>(base_prop);
                if (obs_prop) {
                    *obs_prop = std::move(val);
                }
            };

            property(prop_name, std::move(getter), std::move(setter));

            // Also bind the legacy on_<prop>_change method for backwards compatibility
            bind_observable_property<ValueT>(prop_name);
        } else {
            // Regular (non-observable) properties just return the value directly
            auto getter = [prop_name, eng](T& self) -> script_value {
                if (auto* prop = self.property_mgr.template get<ValueT>(prop_name)) {
                    return script_value_from_cpp(prop->get(), eng);
                }
                return script_value(std::monostate{}, eng);
            };

            auto setter = [prop_name](T& self, ValueT val) {
                jai::property_base* base_prop = self.property_mgr.get(prop_name);
                if (!base_prop) return;
                jai::property<ValueT>* regular_prop = dynamic_cast<jai::property<ValueT>*>(base_prop);
                if (regular_prop) {
                    regular_prop->get() = std::move(val);
                }
            };

            property(prop_name, std::move(getter), std::move(setter));
        }

        return true;
    }

    // Bind observation method for an observable property
    // Allows scripts to connect callbacks that fire when the property changes
    // Supports both:
    //   - obj.on_score_change(callback)   (legacy API)
    //   - obj.score.on_change(callback)   (new API via transparent wrapper)
    template<typename ValueT>
    void bind_observable_property(const std::string& prop_name) {
        engine* eng = &engine_;
        std::string observe_method_name = "on_" + prop_name + "_change";

        // Method signature: on_<prop>_change(callback) where callback(old_value, new_value)
        // The connection persists for the lifetime of the owner object
        method(observe_method_name, [prop_name, eng](T& self, const script_value& callback) -> script_value {
            if (!callback.is_function()) {
                throw runtime_error("on_" + prop_name + "_change requires a function argument");
            }

            // Get the property_base by name and cast to observable_property
            auto* base_prop = self.property_mgr.get(prop_name);
            if (!base_prop) {
                throw runtime_error("Property '" + prop_name + "' not found");
            }

            // Dynamic cast to observable_property to access the on_change signal
            auto* obs_prop = dynamic_cast<observable_property<ValueT>*>(base_prop);
            if (!obs_prop) {
                throw runtime_error("Property '" + prop_name + "' is not observable");
            }

            // Capture the script function
            const script_function& script_func = callback.as_function();

            // Create a C++ callback that invokes the script function
            // The observable_property's on_change signal passes (old_value, new_value)
            auto cpp_callback = [script_func, eng](const ValueT& old_val, const ValueT& new_val) {
                // Convert C++ values to script_values
                script_value old_sv = script_value_from_cpp(old_val, eng);
                script_value new_sv = script_value_from_cpp(new_val, eng);

                // Call the script function
                auto result = script_func({old_sv, new_sv});
                // Ignore result (void callback)
            };

            // Connect to the signal and get the receiver
            auto recv = obs_prop->on_change.connect(std::move(cpp_callback));

            // Track the receiver in property_manager so it lives as long as the owner
            self.property_mgr.template track_receiver<void(const ValueT&, const ValueT&)>(recv);

            return script_value(std::monostate{}, eng); // Return null (disconnect not yet supported)
        });
    }

    // Register the observable_property_ref<ValueT> wrapper type if not already registered.
    // This wrapper enables the player.score.on_change(callback) syntax by:
    //   1. Having an on_change method for callback registration
    //   2. Being a transparent wrapper that forwards operations to the underlying value
    template<typename ValueT>
    void ensure_observable_ref_type_registered() {
        using RefType = observable_property_ref<ValueT>;

        // Generate type name based on ValueT
        std::string type_name = "observable_property_ref<" + std::string(typeid(ValueT).name()) + ">";

        // Check if already registered (engine tracks registered types)
        if (engine_.is_type_registered(type_name)) {
            return;
        }

        // Register the wrapper type
        dynamic_binder<RefType>(engine_, type_name)
            // Transparent wrapper - unwraps to the value type for arithmetic, etc.
            .transparent_wrapper([](RefType& self) -> ValueT {
                return self.get();
            })
            // on_change method - connects a callback to the property's signal
            .method("on_change", [](RefType& self, const script_value& callback) -> script_value {
                if (!callback.is_function()) {
                    throw runtime_error("on_change requires a function argument");
                }

                auto* obs_prop = self.property();
                auto* prop_mgr = self.manager();
                if (!obs_prop || !prop_mgr) {
                    throw runtime_error("Invalid observable property reference");
                }

                engine* eng = callback.get_engine();
                const script_function& script_func = callback.as_function();

                // Create callback that invokes the script function
                auto cpp_callback = [script_func, eng](const ValueT& old_val, const ValueT& new_val) {
                    script_value old_sv = script_value_from_cpp(old_val, eng);
                    script_value new_sv = script_value_from_cpp(new_val, eng);
                    auto result = script_func({old_sv, new_sv});
                };

                // Connect and track the receiver
                auto recv = obs_prop->on_change.connect(std::move(cpp_callback));
                prop_mgr->template track_receiver<void(const ValueT&, const ValueT&)>(recv);

                return script_value(std::monostate{}, eng);
            })
            .build();
    }

    // Try to bind a property using the global type converter registry
    // This handles custom types like MV::Point<int, int> that have been registered
    // via property_type_converter_registrar
    //
    // TODO: Implement fully once property_manager has type-erased value access
    // For now, complex property types need to be bound manually
    void try_bind_property_from_registry(const std::string& /*prop_name*/, std::type_index value_type) {
        // Check if converter exists (for future use)
        if (!property_type_converter_registry::instance().has_converter(value_type)) {
            return;  // No converter registered - skip silently
        }
        // Complex type binding not yet implemented
        // Custom types need to be bound manually for now
    }

    // Helper to convert C++ value to script_value
    // For primitive types, uses direct construction for efficiency.
    // For custom types (classes registered via jai::registrar), delegates to value_converter
    // which uses the engine's type conversion registry.
    template<typename ValueT>
    static script_value script_value_from_cpp(const ValueT& value, engine* eng) {
        if constexpr (std::is_same_v<ValueT, int> || std::is_same_v<ValueT, int32_t>) {
            return script_value(static_cast<script_int>(value), eng);
        } else if constexpr (std::is_same_v<ValueT, int64_t>) {
            return script_value(value, eng);
        } else if constexpr (std::is_same_v<ValueT, uint64_t> || std::is_same_v<ValueT, uint32_t> ||
                            std::is_same_v<ValueT, uint16_t> || std::is_same_v<ValueT, uint8_t> ||
                            std::is_same_v<ValueT, int16_t> || std::is_same_v<ValueT, int8_t>) {
            return script_value(static_cast<script_int>(value), eng);
        } else if constexpr (std::is_same_v<ValueT, float>) {
            return script_value(static_cast<script_float>(value), eng);
        } else if constexpr (std::is_same_v<ValueT, double>) {
            return script_value(static_cast<script_float>(value), eng);
        } else if constexpr (std::is_same_v<ValueT, bool>) {
            return script_value(value, eng);
        } else if constexpr (std::is_same_v<ValueT, std::string>) {
            return script_value(value, eng);
        } else {
            // For custom types, use value_converter which handles registered classes
            // This works for any type registered via jai::registrar
            return detail::value_converter<ValueT>::to(value, eng);
        }
    }

    // Helper to convert script_value to C++ type
    // For primitive types, uses direct accessor methods.
    // For custom types, delegates to value_converter which handles registered classes.
    template<typename ValueT>
    static ValueT script_value_to_cpp(const script_value& val, engine* eng = nullptr) {
        if constexpr (std::is_same_v<ValueT, int> || std::is_same_v<ValueT, int32_t>) {
            return static_cast<ValueT>(val.as_int());
        } else if constexpr (std::is_same_v<ValueT, int64_t>) {
            return val.as_int();
        } else if constexpr (std::is_same_v<ValueT, uint64_t> || std::is_same_v<ValueT, uint32_t> ||
                            std::is_same_v<ValueT, uint16_t> || std::is_same_v<ValueT, uint8_t> ||
                            std::is_same_v<ValueT, int16_t> || std::is_same_v<ValueT, int8_t>) {
            return static_cast<ValueT>(val.as_int());
        } else if constexpr (std::is_same_v<ValueT, float>) {
            return static_cast<float>(val.as_float());
        } else if constexpr (std::is_same_v<ValueT, double>) {
            return val.as_float();
        } else if constexpr (std::is_same_v<ValueT, bool>) {
            return val.as_bool();
        } else if constexpr (std::is_same_v<ValueT, std::string>) {
            return val.as_string();
        } else {
            // For custom types, use value_converter which handles registered classes
            return detail::value_converter<ValueT>::from(val, eng);
        }
    }

public:

    // Finalize registration
    void build() {
        // Prevent double-build
        if (built_) return;
        built_ = true;

        // Auto-register default constructor if:
        // 1. User didn't explicitly register any constructor
        // 2. Type is default constructible
        // 3. Type is not abstract
        if constexpr (!std::is_abstract_v<T> && std::is_default_constructible_v<T>) {
            if (!has_explicit_constructor_) {
                // Auto-register default constructor
                engine* engine_ptr = &engine_;
                engine_.add_overloaded_function(class_name_, 0, [class_def = class_def_, class_name = class_name_, engine_ptr](const std::vector<script_value>& args) -> script_value {
                    // Create the C++ object
                    std::shared_ptr<T> cpp_obj;

                    // Check if T has an engine constructor and use it if available
                    if constexpr (has_engine_constructor<T>) {
                        cpp_obj = std::make_shared<T>(engine_ptr);
                    } else {
                        cpp_obj = std::make_shared<T>();
                    }

                    // Create a class_instance to hold it
                    auto instance = class_def->create_instance();

                    // Store the C++ object in the class_instance as a special field
                    instance->set_field(instance->get_cpp_object_field_id(), script_value::make_cpp_object(class_name, class_def->get_type_id(), cpp_obj, engine_ptr));

                    // Return the class_instance wrapped in a value
                    return script_value::make_object(class_name, instance, engine_ptr);
                });
            }
        }

        // Register automatic copy function for copyable types
        if constexpr (std::is_copy_constructible_v<T>) {
            class_def_->set_copy_function([](const void* src) -> std::shared_ptr<void> {
                const T* typed_src = static_cast<const T*>(src);
                return std::make_shared<T>(*typed_src);
            });
            
            // If this is a polymorphic type with a base class, register polymorphic copier
            if constexpr (std::is_polymorphic_v<T>) {
                if (has_base_class_) {
                    engine_.register_polymorphic_copier<T>(
                        std::type_index(typeid(T)), 
                        base_type_index_,
                        [](const void* obj) -> std::shared_ptr<void> {
                            const T* typed = static_cast<const T*>(obj);
                            return std::make_shared<T>(*typed);
                        }
                    );
                }
            }
        }
        
        engine_.add_class<T>(class_name_, class_def_);

        // Register serialization metadata with the engine's registry (with type_index for runtime lookup)
        engine_.get_serialization_registry().register_class(class_name_, std::type_index(typeid(T)), serialization_metadata_);
        
        
        // Register custom conversions for shared_ptr<T> to handle class_instance wrapping
        if constexpr (!std::is_abstract_v<T>) {
            engine_.add_custom_conversion<std::shared_ptr<T>>(
                // From script_value to shared_ptr<T>
                [class_name = class_name_](const script_value& v) -> std::shared_ptr<T> {
                    if (v.is_object()) {
                        // Check if this is a raw C++ object or a class_instance wrapper
                        auto obj_holder = v.get_object_holder();
                        if (obj_holder) {
                            if (obj_holder->is_class_instance_wrapper) {
                                // This is a class_instance wrapper - extract the class_instance then get the C++ object
                                auto instance = std::static_pointer_cast<class_instance>(obj_holder->data);
                                return instance->get_cpp_object_as<T>();
                            } else {
                                // This is a raw C++ object - extract it directly
                                return std::static_pointer_cast<T>(obj_holder->data);
                            }
                        }
                    }
                    throw runtime_error("Cannot convert script_value to shared_ptr<" + class_name + ">");
                },
                // From shared_ptr<T> to script_value
                [class_def = class_def_, class_name = class_name_, engine_ptr = &engine_](const std::shared_ptr<T>& obj) -> script_value {
                    if (!obj) {
                        return script_value(std::monostate{}, engine_ptr);
                    }
                    
                    // Create a class_instance to wrap the object
                    auto instance = class_def->create_instance();
                    instance->set_field(instance->get_cpp_object_field_id(),
                        script_value::make_cpp_object(class_name, class_def->get_type_id(), std::static_pointer_cast<void>(obj), engine_ptr));
                    
                    // Return wrapped in script_value
                    return script_value::make_object(class_name, std::static_pointer_cast<void>(instance), engine_ptr);
                }
            );
        }
        
        // Register converters for copyable types only (used for value conversions requiring copy)
        // Non-copyable types like property_owner classes must use shared_ptr conversions instead
        if constexpr (!std::is_abstract_v<T> && std::is_copy_constructible_v<T>) {
            // Register a C++ type converter that creates class_instance objects
            engine_.register_type_converter_impl(typeid(T),
                [class_def = class_def_, class_name = class_name_, engine_ptr = &engine_](const void* obj) -> script_value {
                    // Create a class_instance using the class definition
                    // This properly initializes all fields with their defaults
                    auto instance = class_def->create_instance();

                    // Create a shared_ptr to the C++ object (by copying)
                    auto cpp_obj = std::make_shared<T>(*static_cast<const T*>(obj));

                    // Store the C++ object in the class_instance
                    if (auto eng = engine_ptr) {
                        uint64_t cpp_object_field_id = eng->symbolize(class_constants::CPP_OBJECT_FIELD);
                        instance->set_field(cpp_object_field_id, script_value::make_cpp_object(class_name,
                            class_def->get_type_id(), std::static_pointer_cast<void>(cpp_obj), engine_ptr));
                    }

                    // Return the class_instance wrapped in a value
                    if (auto eng = engine_ptr) {
                        return script_value::make_object(class_name,
                            std::static_pointer_cast<void>(instance), eng);
                    }
                    throw runtime_error("Engine no longer exists");
                });

            // Also register with conversion registry for container conversions
            auto custom_conv = engine_.get_conversion_registry();
            if (custom_conv && !custom_conv->template has_conversion<T>()) {
                custom_conv->template register_conversion<T>(
                    // From script_value to T
                    [](const script_value& v) -> T {
                        // Direct access to avoid template recursion issues
                        if (v.type() != script_value_type::jai_object_type) {
                            throw runtime_error("Cannot convert non-object to " + std::string(typeid(T).name()));
                        }

                        // Get object holder using public method
                        auto objHolder = v.get_object_holder();
                        if (!objHolder) {
                            throw runtime_error("Cannot convert: script_value is not an object type");
                        }

                        if (!objHolder->is_class_instance_wrapper) {
                            throw runtime_error("Object is not a class_instance wrapper");
                        }

                        // Cast to class_instance
                        auto instance = std::static_pointer_cast<class_instance>(objHolder->data);
                        if (!instance) {
                            throw runtime_error("Failed to cast to class_instance");
                        }

                        // Get the C++ object field directly - don't use has_field since it's an internal field
                        auto cpp_obj_value = instance->get_field(instance->get_cpp_object_field_id());
                        if (cpp_obj_value.is_null()) {
                            throw runtime_error("C++ object field not found in class_instance");
                        }

                        // Extract the C++ object directly to avoid recursion
                        if (cpp_obj_value.type() != script_value_type::jai_object_type) {
                            throw runtime_error("C++ object field is not an object");
                        }

                        auto cpp_objHolder = cpp_obj_value.get_object_holder();
                        if (!cpp_objHolder) {
                            throw runtime_error("Field value is not an object type");
                        }
                        auto cpp_obj = std::static_pointer_cast<T>(cpp_objHolder->data);

                        if (!cpp_obj) {
                            throw runtime_error("Failed to cast C++ object to expected type");
                        }

                        return *cpp_obj;
                    },
                    // From T to script_value
                    [class_def = class_def_, class_name = class_name_, engine_ptr = &engine_](const T& obj) -> script_value {
                        // Create a class_instance using the class definition
                        auto instance = class_def->create_instance();

                        // Create a shared_ptr to the C++ object (by copying)
                        auto cpp_obj = std::make_shared<T>(obj);

                        // Store the C++ object in the class_instance
                        instance->set_field(instance->get_cpp_object_field_id(),
                            script_value::make_cpp_object(class_name,
                                class_def->get_type_id(), std::static_pointer_cast<void>(cpp_obj), engine_ptr));

                        // Return the class_instance wrapped in a value
                        if (auto eng = engine_ptr) {
                            return script_value::make_object(class_name,
                                std::static_pointer_cast<void>(instance), eng);
                        }
                        throw runtime_error("Engine no longer exists");
                    }
                );
            }
        }

        // Register shared_ptr<T> conversions for all non-abstract types (doesn't require copy)
        if constexpr (!std::is_abstract_v<T>) {
            auto custom_conv = engine_.get_conversion_registry();

            // Register std::shared_ptr<T> conversion
            if (!custom_conv->template has_conversion<std::shared_ptr<T>>()) {
                custom_conv->template register_conversion<std::shared_ptr<T>>(
                    // From script_value to std::shared_ptr<T>
                    [class_name = class_name_](const script_value& v) -> std::shared_ptr<T> {
                        // Direct extraction logic to avoid circular dependency
                        if (v.type() != script_value_type::jai_object_type) {
                            throw runtime_error("Cannot convert non-object to shared_ptr<" + class_name + ">");
                        }
                        
                        // Get object holder using public method
                        auto objHolder = v.get_object_holder();
                        if (!objHolder) {
                            throw runtime_error("Cannot convert: script_value is not an object type");
                        }
                        
                        // Check if it's a raw C++ object (not wrapped in class_instance)
                        if (!objHolder->is_class_instance_wrapper) {
                            // Direct C++ object - just cast and return
                            return std::static_pointer_cast<T>(objHolder->data);
                        }
                        
                        // It's a class_instance wrapper - extract the C++ object
                        auto instance = std::static_pointer_cast<class_instance>(objHolder->data);
                        if (!instance) {
                            throw runtime_error("Failed to cast to class_instance");
                        }
                        
                        // Get the C++ object field
                        auto cpp_obj_value = instance->get_field(instance->get_cpp_object_field_id());
                        if (cpp_obj_value.is_null()) {
                            throw runtime_error("C++ object field not found in class_instance");
                        }
                        
                        // Extract the C++ object
                        if (cpp_obj_value.type() != script_value_type::jai_object_type) {
                            throw runtime_error("C++ object field is not an object");
                        }
                        
                        auto cpp_objHolder = cpp_obj_value.get_object_holder();
                        if (!cpp_objHolder) {
                            throw runtime_error("Field value is not an object type");
                        }
                        return std::static_pointer_cast<T>(cpp_objHolder->data);
                    },
                    // From std::shared_ptr<T> to script_value
                    [class_def = class_def_, class_name = class_name_, engine_ptr = &engine_](const std::shared_ptr<T>& obj) -> script_value {
                        if (!obj) {
                            return script_value(std::monostate{}, engine_ptr); // null
                        }
                        
                        // Create a class_instance using the class definition
                        auto instance = class_def->create_instance();
                        
                        // Store the shared_ptr directly (no copy needed)
                        instance->set_field(instance->get_cpp_object_field_id(),
                            script_value::make_cpp_object(class_name,
                                class_def->get_type_id(), std::static_pointer_cast<void>(obj), engine_ptr));
                        
                        // Return the class_instance wrapped in a value
                        if (auto eng = engine_ptr) {
                            return script_value::make_object(class_name, 
                                std::static_pointer_cast<void>(instance), eng);
                        }
                        throw runtime_error("Engine no longer exists");
                    }
                );
            }
        }
        
        // Automatically register container conversions for this type
        // Do this AFTER registering the base type conversion
        register_container_conversions();
    }
    
private:
    // Helper to apply base classes for each type in a tuple
    // Collects all base definitions and sets them at once for efficiency
    // (single diamond check, single cache invalidation)
    // Uses std::type_identity to avoid requiring default constructors on base classes
    template<typename... Bases>
    void apply_base_classes_from_tuple(std::type_identity<std::tuple<Bases...>>) {
        if constexpr (sizeof...(Bases) == 0) {
            return;  // No bases to register
        } else if constexpr (sizeof...(Bases) == 1) {
            // Single inheritance - use base_class<> directly
            (base_class<Bases>(), ...);
        } else {
            // Multiple inheritance - collect all definitions and use set_parents()
            // This is more efficient: single diamond check instead of N checks
            std::vector<std::shared_ptr<class_definition>> parent_defs;
            parent_defs.reserve(sizeof...(Bases));

            // Collect base definitions (fold expression with static_assert validation)
            (collect_base_definition<Bases>(parent_defs), ...);

            // Set all parents at once
            if (!parent_defs.empty()) {
                if (!class_def_->set_parents(parent_defs)) {
                    throw std::runtime_error("Diamond inheritance detected: class '" + class_name_ +
                        "' would have multiple paths to the same base class");
                }
                has_base_class_ = true;
                // Store first base type index for polymorphic copy
                base_type_index_ = std::type_index(typeid(std::tuple_element_t<0, std::tuple<Bases...>>));
            }
        }
    }

    // Helper to collect a single base class definition
    template<typename Base>
    void collect_base_definition(std::vector<std::shared_ptr<class_definition>>& parent_defs) {
        static_assert(std::is_base_of_v<Base, T>,
                      "Specified type is not a base class of this class");
        auto base_def = engine_.get_class_definition_by_type(std::type_index(typeid(Base)));
        if (base_def) {
            parent_defs.push_back(base_def);
        }
    }

    engine& engine_;
    std::string class_name_;
    std::shared_ptr<class_definition> class_def_;
    serialization::class_metadata serialization_metadata_;
    bool has_base_class_ = false;
    std::type_index base_type_index_ = std::type_index(typeid(void));
    bool has_explicit_constructor_ = false;  // Track if user registered any constructor
    bool built_ = false;  // Track if build() was called
    
    // Automatically register container conversions for type T
    void register_container_conversions() {
        if constexpr (!std::is_abstract_v<T>) {
            // Get the conversion manager for high-level API
            auto conv_mgr = engine_.get_conversion_manager();
            
            // Register base type conversion
            if constexpr (std::is_copy_constructible_v<T> && std::is_default_constructible_v<T>) {
                engine_.add_custom_conversion<T>(
                    // From script_value to T
                    [class_name = class_name_](const script_value& v) -> T {
                        if (v.is_object()) {
                            auto instance = v.as<std::shared_ptr<class_instance>>();
                            if (instance) {
                                return *instance->get_cpp_object_as<T>();
                            }
                        }
                        throw runtime_error("Cannot convert script_value to " + class_name);
                    },
                    // From T to script_value (create a class_instance)
                    [class_def = class_def_, class_name = class_name_, engine_ptr = &engine_](const T& obj) -> script_value {
                        auto instance = class_def->create_instance();
                        auto cpp_obj = std::make_shared<T>(obj);
                        instance->set_field(instance->get_cpp_object_field_id(),
                            script_value::make_cpp_object(class_name,
                                class_def->get_type_id(), std::static_pointer_cast<void>(cpp_obj), engine_ptr));
                        return script_value::make_object(class_name, instance, engine_ptr);
                    }
                );
            }
            
            // Register vector conversions
            // Only register value-type vector if T is copyable
            if constexpr (std::is_copy_constructible_v<T>) {
                conv_mgr.add_vector_conversion<T>();
            }
            conv_mgr.add_vector_conversion<std::shared_ptr<T>>();

            // Register common map conversions with this type as value
            // Only register T map conversions if T is copyable and default constructible (required for std::map)
            if constexpr (std::is_copy_constructible_v<T> && std::is_default_constructible_v<T>) {
                conv_mgr.add_map_conversion<std::string, T>();
                conv_mgr.add_map_conversion<int, T>();
                conv_mgr.add_map_conversion<int64_t, T>();
            }
            // shared_ptr<T> is always default constructible
            conv_mgr.add_map_conversion<std::string, std::shared_ptr<T>>();
            conv_mgr.add_map_conversion<int, std::shared_ptr<T>>();
            conv_mgr.add_map_conversion<int64_t, std::shared_ptr<T>>();
            
            // Register bound array conversions for zero-copy performance
            conv_mgr.add_bound_array_conversion<T>();
            conv_mgr.add_bound_array_conversion<std::shared_ptr<T>>();
            
            // Register bound map conversions
            // Only register for default constructible types
            if constexpr (std::is_default_constructible_v<T>) {
                conv_mgr.add_bound_map_conversion<std::string, T>();
                conv_mgr.add_bound_map_conversion<int, T>();
            }
            // shared_ptr<T> is always default constructible
            conv_mgr.add_bound_map_conversion<std::string, std::shared_ptr<T>>();
            conv_mgr.add_bound_map_conversion<int, std::shared_ptr<T>>();
            
            // Register common map conversions with this type as key (if it has operator<)
            if constexpr (std::is_default_constructible_v<T> && requires(const T& a, const T& b) { a < b; }) {
                conv_mgr.add_map_conversion<T, std::string>();
                conv_mgr.add_map_conversion<T, int>();
                conv_mgr.add_map_conversion<T, int64_t>();
                conv_mgr.add_map_conversion<T, double>();
                conv_mgr.add_map_conversion<T, bool>();
            }
        }
    }
    
    // Helper method for creating objects with arguments
    template<typename... Args, size_t... Is>
    static std::shared_ptr<T> createObjectImpl(const std::vector<script_value>& args, std::index_sequence<Is...>, engine* eng) {
        // Create parameter storage on stack
        detail::parameter_storage storage;
        detail::parameter_storage::scope_guard guard(eng, &storage);

        return std::make_shared<T>(detail::value_converter<Args>::from(args[Is], eng)...);
    }
    
    // Helper method for calling member functions
    template<typename R, typename... Args, size_t... Is>
    static script_value callMethodImpl(T* obj, R(T::*method)(Args...), const std::vector<script_value>& args, std::index_sequence<Is...>, engine* eng) {
        // Create parameter storage on stack
        detail::parameter_storage storage;
        detail::parameter_storage::scope_guard guard(eng, &storage);

        if constexpr (std::is_void_v<R>) {
            (obj->*method)(detail::value_converter<Args>::from(args[Is + 1], eng)...);
            return script_value(std::monostate{}, eng); // null for void
        } else {
            R result = (obj->*method)(detail::value_converter<Args>::from(args[Is + 1], eng)...);
            return detail::value_converter<R>::to(result, eng);
        }
    }
    
    // Helper method for calling const member functions
    template<typename R, typename... Args, size_t... Is>
    static script_value callConstMethodImpl(const T* obj, R(T::*method)(Args...) const, const std::vector<script_value>& args, std::index_sequence<Is...>, engine* eng) {
        // Create parameter storage on stack
        detail::parameter_storage storage;
        detail::parameter_storage::scope_guard guard(eng, &storage);

        if constexpr (std::is_void_v<R>) {
            (obj->*method)(detail::value_converter<Args>::from(args[Is + 1], eng)...);
            return script_value(std::monostate{}, eng); // null for void
        } else {
            R result = (obj->*method)(detail::value_converter<Args>::from(args[Is + 1], eng)...);
            return detail::value_converter<R>::to(result, eng);
        }
    }
    
    // Helper method for calling static functions
    template<typename R, typename... Args, size_t... Is>
    static R callStaticFunctionImpl(R(*func)(Args...), const std::vector<script_value>& args, std::index_sequence<Is...>) {
        // Create parameter storage on stack - note: no engine, so storage won't be set
        // This is fine for basic types but will fail for containers
        detail::parameter_storage storage;
        detail::parameter_storage::scope_guard guard(nullptr, &storage);

        return func(detail::value_converter<Args>::from(args[Is], nullptr)...);
    }
    
    // Helper method for calling static callables/lambdas
    template<typename Callable, size_t... Is>
    static auto callStaticCallableImpl(Callable&& callable, const std::vector<script_value>& args, std::index_sequence<Is...>) {
        // Create parameter storage on stack - note: no engine, so storage won't be set
        // This is fine for basic types but will fail for containers
        detail::parameter_storage storage;
        detail::parameter_storage::scope_guard guard(nullptr, &storage);

        using traits = detail::function_traits<std::decay_t<Callable>>;
        using args_tuple = typename traits::argument_types;

        return callable(detail::value_converter<std::tuple_element_t<Is, args_tuple>>::from(args[Is], nullptr)...);
    }
    
    // Helper method for calling lambdas/callables
    template<typename R, typename ArgsTuple, typename Callable, size_t... Is>
    static script_value callCallableImpl(Callable&& callable, const std::vector<script_value>& args, std::index_sequence<Is...>, engine* eng) {
        // Create parameter storage on stack
        detail::parameter_storage storage;
        detail::parameter_storage::scope_guard guard(eng, &storage);

        if constexpr (std::is_void_v<R>) {
            callable(detail::value_converter<std::tuple_element_t<Is, ArgsTuple>>::from(args[Is], eng)...);
            return script_value(std::monostate{}, eng); // null for void
        } else {
            R result = callable(detail::value_converter<std::tuple_element_t<Is, ArgsTuple>>::from(args[Is], eng)...);
            return detail::value_converter<R>::to(result, eng);
        }
    }
    
    // Helper method for calling lambdas with self parameter
    template<typename R, typename ArgsTuple, typename Callable, size_t... Is>
    static script_value callLambdaWithSelf(Callable&& callable, T* self, const std::vector<script_value>& args, std::index_sequence<Is...>, engine* eng) {
        // We need to call the lambda with:
        // - self as the first argument
        // - remaining args starting from args[1] mapped to tuple indices 1, 2, 3...
        return callLambdaWithSelfImpl<R, ArgsTuple, Callable>(
            std::forward<Callable>(callable), self, args, 
            std::make_index_sequence<sizeof...(Is) - 1>{}, eng
        );
    }
    
    // implementation helper that correctly maps arguments
    template<typename R, typename ArgsTuple, typename Callable, size_t... Is>
    static script_value callLambdaWithSelfImpl(Callable&& callable, T* self, const std::vector<script_value>& args, std::index_sequence<Is...>, engine* eng) {
        // Create parameter storage on stack
        detail::parameter_storage storage;
        detail::parameter_storage::scope_guard guard(eng, &storage);

        if constexpr (std::is_void_v<R>) {
            // Call with self as first argument, then args[1], args[2], etc.
            callable(*self, detail::value_converter<std::tuple_element_t<Is + 1, ArgsTuple>>::from(args[Is + 1], eng)...);
            return script_value(std::monostate{}, eng); // null for void
        } else {
            // For return type R&, we need special handling for method chaining
            if constexpr (std::is_reference_v<R> && std::is_same_v<std::remove_reference_t<R>, T>) {
                // Method returns T&, so we should return the original 'this' script_value for chaining
                callable(*self, detail::value_converter<std::tuple_element_t<Is + 1, ArgsTuple>>::from(args[Is + 1], eng)...);
                return args[0]; // Return the original 'this' for chaining
            } else {
                R result = callable(*self, detail::value_converter<std::tuple_element_t<Is + 1, ArgsTuple>>::from(args[Is + 1], eng)...);
                return detail::value_converter<R>::to(result, eng);
            }
        }
    }
    
};

// Helper function to create a script class definition
inline std::shared_ptr<class_definition> make_script_class_definition(const std::string& class_name, engine* eng) {
    return std::make_shared<class_definition>(class_name, class_definition::script_class, eng);
}

// implementation of class_instance methods (must be after class_definition)

inline bool class_instance::is_script_class() const {
    return class_def_ && class_def_->is_script_class();
}

inline bool class_instance::is_cpp_class() const {
    return class_def_ && class_def_->is_cpp_class();
}

inline std::shared_ptr<void> class_instance::extract_cpp_object_impl(const script_value& val) {
    // Access the private object_holder through friend access
    if (val.type() == script_value_type::jai_object_type) {
        auto obj_holder = val.get_object_holder();
        if (!obj_holder) {
            throw runtime_error("script_value is not an object type");
        }
        return obj_holder->data;
    }
    return nullptr;
}

// Implementation of get_cpp_object_field_id() after class_definition is complete
inline uint64_t class_instance::get_cpp_object_field_id() const {
    if (cpp_object_field_id_ == 0 && class_def_) {
        if (auto eng = class_def_->get_engine()) {
            cpp_object_field_id_ = eng->symbolize(class_constants::CPP_OBJECT_FIELD);
        }
    }
    return cpp_object_field_id_;
}

inline std::shared_ptr<class_instance> class_instance::deep_copy() const {
    auto new_instance = std::make_shared<class_instance>(class_name_, engine_);

    // Copy all fields
    uint64_t cpp_obj_id = get_cpp_object_field_id();
    for (const auto& [id, value] : fields_) {
        // Special handling for _cpp_object field
        if (id == cpp_obj_id && !value.is_null()) {
            // Get the class definition to access copy function
            if (class_def_ && class_def_->has_copy_function()) {
                // Extract the C++ object
                auto cpp_obj = extract_cpp_object_impl(value);
                if (cpp_obj) {
                    // Use the copy function to create a new C++ object
                    auto new_cpp_obj = class_def_->copy_object(cpp_obj.get());

                    // FIX #9: Check if copy_object succeeded
                    if (!new_cpp_obj) {
                        throw runtime_error("Failed to copy C++ object of type '" + class_name_ +
                            "'. The registered copy function returned null.");
                    }

                    // Wrap in a new script_value (use engine ref from the existing value)
                    auto eng_ref = value.get_engine();
                    new_instance->set_field(cpp_obj_id,
                        script_value::make_cpp_object(class_name_, class_def_->get_type_id(), new_cpp_obj, eng_ref));
                    continue;
                }
            }
        }

        // For other fields, use script_value's clone method for deep copy
        new_instance->set_field(id, value.clone());
    }

    // Copy class definition reference
    new_instance->class_def_ = class_def_;

    // Register the new instance for hot reload tracking
    if (class_def_) {
        class_def_->register_instance(std::weak_ptr<class_instance>(new_instance));
    }

    return new_instance;
}

// Implementations that depend on class_definition being complete
inline const script_value& class_instance::get_field(uint64_t id, bool throw_if_missing) const {
    auto it = fields_.find(id);
    if (it != fields_.end()) {
        return it->second;
    }

    // If not in instance fields, check class definition for default value
    if (class_def_) {
        auto& field_defaults = class_def_->get_all_field_defaults();
        auto default_it = field_defaults.find(id);
        if (default_it != field_defaults.end()) {
            return default_it->second;  // Return reference to the default
        }

        // Check parent classes for inherited field defaults
        for (const auto& parent : class_def_->get_parent_classes()) {
            if (parent) {
                auto& parent_defaults = parent->get_all_field_defaults();
                auto parent_it = parent_defaults.find(id);
                if (parent_it != parent_defaults.end()) {
                    return parent_it->second;
                }
            }
        }
    }

    if (throw_if_missing) {
        // Get the field name from the symbolizer for error message
        std::string field_name;
        if (class_def_) {
            if (auto eng = class_def_->get_engine()) {
                field_name = eng->get_symbolizer()->get_string(id);
            } else {
                field_name = std::to_string(id);
            }
        } else {
            field_name = std::to_string(id);
        }
        throw runtime_error("Field '" + field_name + "' not found and no default value available");
    }

    // For non-throwing case, return reference to instance's invalid_value member
    if (class_def_) {
        invalid_value_ = script_value::make_invalid(class_def_->get_engine());
    } else {
        invalid_value_ = script_value::make_invalid(nullptr);
    }
    return invalid_value_;
}

inline script_value& class_instance::get_field(uint64_t id, bool throw_if_missing) {
    auto it = fields_.find(id);
    if (it != fields_.end()) {
        return it->second;
    }

    // If not in instance fields, we need to create it with the default value
    if (class_def_) {
        auto& field_defaults = class_def_->get_all_field_defaults();
        auto default_it = field_defaults.find(id);
        if (default_it != field_defaults.end()) {
            // Create the field with a clone of the default value
            auto [new_it, _] = fields_.emplace(id, default_it->second.clone());
            return new_it->second;
        }

        // Check parent classes for inherited field defaults
        for (const auto& parent : class_def_->get_parent_classes()) {
            if (parent) {
                auto& parent_defaults = parent->get_all_field_defaults();
                auto parent_it = parent_defaults.find(id);
                if (parent_it != parent_defaults.end()) {
                    // Create the field with a clone of the parent's default value
                    auto [new_it, _] = fields_.emplace(id, parent_it->second.clone());
                    return new_it->second;
                }
            }
        }

        // Also check C++ base class for mixed inheritance
        auto cpp_base = class_def_->get_cpp_base_class();
        if (cpp_base) {
            auto& cpp_defaults = cpp_base->get_all_field_defaults();
            auto cpp_it = cpp_defaults.find(id);
            if (cpp_it != cpp_defaults.end()) {
                // Create the field with a clone of the C++ base class default value
                auto [new_it, _] = fields_.emplace(id, cpp_it->second.clone());
                return new_it->second;
            }
        }
    }

    if (throw_if_missing) {
        // Get the field name from the symbolizer for error message
        std::string field_name;
        if (class_def_) {
            if (auto eng = class_def_->get_engine()) {
                field_name = eng->get_symbolizer()->get_string(id);
            } else {
                field_name = std::to_string(id);
            }
        } else {
            field_name = std::to_string(id);
        }
        throw runtime_error("Field '" + field_name + "' not found and no default value available");
    }

    // For non-throwing case, return reference to instance's invalid_value member
    if (class_def_) {
        invalid_value_ = script_value::make_invalid(class_def_->get_engine());
    } else {
        invalid_value_ = script_value::make_invalid(nullptr);
    }
    return invalid_value_;
}

inline bool class_instance::has_field(uint64_t id) const {
    // Check if the field exists in this instance
    if (fields_.find(id) != fields_.end()) {
        return true;
    }

    // Check class definition for inherited fields
    if (class_def_) {
        const auto& all_fields = class_def_->get_all_field_defaults();
        if (all_fields.find(id) != all_fields.end()) {
            return true;
        }

        // Also check C++ base class for mixed inheritance
        auto cpp_base = class_def_->get_cpp_base_class();
        if (cpp_base) {
            const auto& cpp_fields = cpp_base->get_all_field_defaults();
            if (cpp_fields.find(id) != cpp_fields.end()) {
                return true;
            }
        }
    }

    return false;
}

inline script_value class_instance::get_method(uint64_t id, bool throw_if_missing) const {
    if (class_def_) {
        return class_def_->get_method(id, throw_if_missing);
    }

    if (throw_if_missing) {
        throw runtime_error("Class definition not available for method with ID " + std::to_string(id) + " lookup");
    }
    return script_value::make_invalid(nullptr);
}

// Define class_instance destructor after class_definition is complete
inline class_instance::~class_instance() {
    // Unregister from class definition when destroyed
    if (class_def_) {
        class_def_->unregister_instance(this);
    }
}

// Context-based factory implementations use type erasure (see make_context_extractor above)
// to avoid MSVC template instantiation issues with incomplete archive_reader type.
// The make_context_extractor function is instantiated at call sites where archive_reader is fully defined.

} // namespace jai

// Include script class after class_definition is complete
#include "script_class.hpp"

#endif // __JAISCRIPT_CORE_dynamic_binder_HPP__
