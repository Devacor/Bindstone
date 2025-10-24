#pragma once

#ifndef __JAISCRIPT_CORE_CLASS_BUILDER_HPP__
#define __JAISCRIPT_CORE_CLASS_BUILDER_HPP__

#include "engine.hpp"
#include "value.hpp"
#include "types.hpp"
#include "function_binder.hpp"
#include "parameter_storage.hpp"
#include "conversion_registry.hpp"
#include "bound_array.hpp"
#include "bound_map.hpp"
#include <jaiscript/serialization/archive.hpp>
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

// Common constants for the class system
namespace class_constants {
    inline const std::string CPP_OBJECT_FIELD = "_cpp_object";
}

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
class class_instance;

// Class instance representation in JaiScript
class class_instance : public std::enable_shared_from_this<class_instance> {
public:
    class_instance(const std::string& class_name) : class_name_(class_name) {}
    
    ~class_instance();
    
    // Field access
    void set_field(const std::string& name, const script_value& value) {
        // Check if the field already exists and is a reference
        auto it = fields_.find(name);
        if (it != fields_.end() && it->second.is_reference()) {
            // If the field is a reference, update the referenced value instead of replacing the reference
            it->second.deref() = value;
        } else {
            // Normal field assignment
            fields_.insert_or_assign(name, value);
        }
    }
    
    const script_value& get_field(const std::string& name, bool throw_if_missing = true) const;
    script_value& get_field(const std::string& name, bool throw_if_missing = true);
    
    bool has_field(const std::string& name) const;
    
    const std::string& get_class_name() const { return class_name_; }
    
    
    // Migrate fields for hot reload
    void migrate_fields(const std::set<std::string>& old_field_names,
                       const std::unordered_map<std::string, script_value>& new_field_defaults) {
        std::unordered_map<std::string, script_value> new_fields;
        new_fields.reserve(new_field_defaults.size());  // Pre-allocate for efficiency
        
        // Keep existing fields that are still in the new definition
        for (const auto& [name, default_value] : new_field_defaults) {
            auto it = fields_.find(name);
            if (it != fields_.end()) {
                // Field exists in both old and new - keep current value
                new_fields.emplace(name, std::move(it->second));
            } else {
                // New field - use default value
                new_fields.emplace(name, default_value.clone());
            }
        }
        
        // Replace fields map (removes old fields)
        fields_ = std::move(new_fields);
    }
    
    // Get method from class definition
    script_value get_method(const std::string& name, bool throw_if_missing = true) const;
    
    // Set the class definition this instance belongs to
    void set_class_definition(std::shared_ptr<class_definition> class_def) {
        class_definition_ = class_def;
    }
    
    // Get all fields for implicit this support
    const std::unordered_map<std::string, script_value>& get_fields() const {
        return fields_;
    }
    
    // Get the class definition this instance belongs to
    std::shared_ptr<class_definition> get_class_definition() const {
        return class_definition_.lock();
    }
    
    // Check if this is a script class instance (implemented after class_definition)
    bool is_script_class() const;
    
    // Check if this is a C++ class instance (implemented after class_definition)
    bool is_cpp_class() const;
    
    // Get the underlying C++ object (if this is a C++ class instance)
    std::shared_ptr<void> get_cpp_object() const {
        auto cpp_field = get_field(class_constants::CPP_OBJECT_FIELD);
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
        return !get_field(class_constants::CPP_OBJECT_FIELD).is_null();
    }
    
    // Deep copy this instance
    std::shared_ptr<class_instance> deep_copy() const;
    
private:
    std::string class_name_;
    std::unordered_map<std::string, script_value> fields_;
    std::weak_ptr<class_definition> class_definition_;
    
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
    class_definition(const std::string& name, std::weak_ptr<engine> eng) : name_(name), class_type_(cpp_class), engine_ref_(eng) {}
    
    // Constructor for script classes
    class_definition(const std::string& name, class_type type, std::weak_ptr<engine> eng) : name_(name), class_type_(type), engine_ref_(eng) {}
    
    // Get the class type
    class_type get_class_type() const { return class_type_; }
    bool is_script_class() const { return class_type_ == script_class; }
    bool is_cpp_class() const { return class_type_ == cpp_class; }
    bool is_vm_class() const { return class_type_ == vm_class; }
    
    // Add a method to the class
    void add_method(const std::string& name, script_function func) {
        methods_.insert_or_assign(name, script_value::make_function(func, engine_ref_));
    }
    
    // Add a static method to the class
    void add_static_method(const std::string& name, script_function func) {
        static_methods_.insert_or_assign(name, script_value::make_function(func, engine_ref_));
    }
    
    // Add a script method (wraps AST execution in a function)
    // Implementation moved to script_class.hpp to avoid circular dependency
    void add_script_method(const std::string& name, std::shared_ptr<function_decl> ast, interpreter* interp);
    
    // Add a static script method (wraps AST execution in a function without 'this')
    void add_static_script_method(const std::string& name, std::shared_ptr<function_decl> ast, interpreter* interp);
    
    // Add a field with default value
    void add_field(const std::string& name, const script_value& default_value) {
        // Ensure the default value has an engine reference
        if (default_value.get_engine_ref().expired() && !engine_ref_.expired()) {
            // Create a copy with engine reference
            script_value value_with_engine(default_value);
            value_with_engine.set_engine_ref(engine_ref_);
            field_defaults_.insert_or_assign(name, value_with_engine);
        } else {
            field_defaults_.insert_or_assign(name, default_value);
        }
        field_defaults_cache_valid_ = false;  // Invalidate cache
    }
    
    // Add a field with null default value
    void add_field(const std::string& name) {
        field_defaults_.insert_or_assign(name, script_value(std::monostate{}, engine_ref_));
        field_defaults_cache_valid_ = false;  // Invalidate cache
    }
    
    // Add a static field with initial value
    void add_static_field(const std::string& name, const script_value& initial_value) {
        static_fields_.insert(name);
        
        // Ensure the value has an engine reference
        if (initial_value.get_engine_ref().expired() && !engine_ref_.expired()) {
            script_value value_with_engine(initial_value);
            value_with_engine.set_engine_ref(engine_ref_);
            static_field_values_.insert_or_assign(name, value_with_engine);
        } else {
            static_field_values_.insert_or_assign(name, initial_value);
        }
    }
    
    // Get static field value by reference (const version)
    const script_value& get_static_field(const std::string& name) const {
        auto it = static_field_values_.find(name);
        if (it != static_field_values_.end()) {
            return it->second;
        }
        
        // Check parent class
        if (parent_class_) {
            return parent_class_->get_static_field(name);
        }
        
        throw runtime_error("Static field '" + name + "' not found in class '" + name_ + "'");
    }
    
    // Get static field value by reference (non-const version)
    script_value& get_static_field(const std::string& name) {
        auto it = static_field_values_.find(name);
        if (it != static_field_values_.end()) {
            return it->second;
        }
        
        // Check parent class
        if (parent_class_) {
            return parent_class_->get_static_field(name);
        }
        
        throw runtime_error("Static field '" + name + "' not found in class '" + name_ + "'");
    }
    
    // Set static field value
    void set_static_field(const std::string& name, const script_value& value) {
        auto it = static_field_values_.find(name);
        if (it != static_field_values_.end()) {
            it->second = value;
            return;
        }
        
        // Check parent class
        if (parent_class_ && parent_class_->has_static_field(name)) {
            parent_class_->set_static_field(name, value);
            return;
        }
        
        throw runtime_error("Static field '" + name + "' not found in class '" + name_ + "'");
    }
    
    // Check if field is static
    bool is_static_field(const std::string& name) const {
        return static_fields_.find(name) != static_fields_.end() ||
               (parent_class_ && parent_class_->is_static_field(name));
    }
    
    // Check if static field exists
    bool has_static_field(const std::string& name) const {
        return static_field_values_.find(name) != static_field_values_.end() ||
               (parent_class_ && parent_class_->has_static_field(name));
    }
    
    // Get a method
    script_value get_method(const std::string& name, bool throw_if_missing = true) const {
        auto it = methods_.find(name);
        if (it != methods_.end()) {
            return it->second;
        }
        
        
        // Check parent class if we have inheritance
        if (parent_class_) {
            return parent_class_->get_method(name, throw_if_missing);
        }
        
        if (throw_if_missing) {
            throw runtime_error("Method '" + name + "' not found in class '" + name_ + "'");
        }
        
        // Return invalid value as sentinel
        return script_value::make_invalid(engine_ref_);
    }
    
    // Check if this class has a method (does not check parent classes)
    bool has_method(const std::string& name) const {
        return methods_.find(name) != methods_.end();
    }
    
    // Get a static method
    script_value get_static_method(const std::string& name, bool throw_if_missing = true) const {
        auto it = static_methods_.find(name);
        if (it != static_methods_.end()) {
            return it->second;
        }
        
        // Check parent class
        if (parent_class_) {
            try {
                return parent_class_->get_static_method(name, throw_if_missing);
            } catch (const runtime_error&) {
                // Parent doesn't have it either
            }
        }
        
        if (throw_if_missing) {
            throw runtime_error("Static method '" + name + "' not found in class '" + name_ + "'");
        }
        return script_value::make_null(engine_ref_);
    }
    
    // Check if this class has a static method (does not check parent classes)
    bool has_static_method(const std::string& name) const {
        return static_methods_.find(name) != static_methods_.end();
    }
    
    // Check if this class or parent classes have a static method
    bool has_static_method_recursive(const std::string& name) const {
        if (has_static_method(name)) {
            return true;
        }
        return parent_class_ && parent_class_->has_static_method_recursive(name);
    }

    // Get all static field names (only this class, not parent)
    std::vector<std::string> get_static_field_names() const {
        std::vector<std::string> names;
        names.reserve(static_fields_.size());
        for (const auto& field_name : static_fields_) {
            names.push_back(field_name);
        }
        return names;
    }

    // Get all static method names (only this class, not parent)
    std::vector<std::string> get_static_method_names() const {
        std::vector<std::string> names;
        names.reserve(static_methods_.size());
        for (const auto& [method_name, _] : static_methods_) {
            names.push_back(method_name);
        }
        return names;
    }

    // Create an instance of this class
    std::shared_ptr<class_instance> create_instance() {
        // Create raw pointer first
        auto* raw_instance = new class_instance(name_);

        // Check if this is a script class with a destructor
        std::shared_ptr<class_instance> instance;
        if (is_script_class() && has_method("~" + name_)) {
            // Create with custom deleter to call script destructor
            auto class_def = shared_from_this();
            auto destructor_name = "~" + name_;

            instance = std::shared_ptr<class_instance>(raw_instance,
                [class_def, destructor_name](class_instance* ptr) {
                    if (ptr) {
                        // Try to find and call the destructor
                        auto method_it = class_def->methods_.find(destructor_name);
                        if (method_it != class_def->methods_.end()) {
                            try {
                                // Create a temporary shared_ptr for 'this' parameter
                                // This is safe because we're in the deleter and control lifetime
                                std::shared_ptr<class_instance> temp_this(ptr, [](class_instance*){});

                                // Call the destructor with 'this' as argument
                                // Use the class definition's engine reference
                                const script_function& destructor_func = method_it->second.as_function();
                                destructor_func({script_value::make_object(class_def->name_, temp_this, class_def->get_engine_ref())});
                            } catch (...) {
                                // Swallow exceptions in destructors
                            }
                        }
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
        
        // Initialize fields from parent classes first (if any)
        if (parent_class_) {
            const auto& parent_fields = parent_class_->get_all_field_defaults();
            for (const auto& [field_name, default_value] : parent_fields) {
                instance->set_field(field_name, default_value);
            }
        }
        
        // Initialize fields with defaults (may override parent fields)
        for (const auto& [field_name, default_value] : field_defaults_) {
            instance->set_field(field_name, default_value);
        }
        
        return instance;
    }
    
    // Get all field defaults including inherited ones
    const std::unordered_map<std::string, script_value>& get_all_field_defaults() const {
        // Rebuild cache if invalid
        if (!field_defaults_cache_valid_) {
            all_field_defaults_cache_.clear();
            
            // Start with parent fields
            if (parent_class_) {
                const auto& parent_fields = parent_class_->get_all_field_defaults();
                all_field_defaults_cache_ = parent_fields;  // Copy parent fields
            }
            
            // Add/override with our fields
            for (const auto& [name, value] : field_defaults_) {
                all_field_defaults_cache_.insert_or_assign(name, value);
            }
            
            field_defaults_cache_valid_ = true;
        }
        
        return all_field_defaults_cache_;
    }
    
    // Set parent class for inheritance
    void set_parent(std::shared_ptr<class_definition> parent) {
        parent_class_ = parent;
        field_defaults_cache_valid_ = false;  // Invalidate cache when parent changes
        // Register this as a derived class of parent
        if (parent) {
            parent->add_derived_class(shared_from_this());
        }
    }
    
    // Get parent class for inheritance
    std::shared_ptr<class_definition> get_parent() const {
        return parent_class_;
    }
    
    // Track derived classes for hot reload
    void add_derived_class(std::weak_ptr<class_definition> derived) {
        derived_classes_.push_back(derived);
    }
    
    // Set C++ base class for mixed inheritance (script class inheriting from C++ class)
    void set_cpp_base_class(std::shared_ptr<class_definition> cpp_base) {
        cpp_base_class_ = cpp_base;
    }
    
    std::shared_ptr<class_definition> get_cpp_base_class() const { return cpp_base_class_; }
    
    const std::string& get_name() const { return name_; }
    
    // Get the engine reference
    std::weak_ptr<engine> get_engine_ref() const { return engine_ref_; }
    
    // Get all registered property names from fieldDefaults_
    std::vector<std::string> get_property_names() const {
        std::vector<std::string> properties;
        for (const auto& [name, default_value] : field_defaults_) {
            properties.push_back(name);
        }
        return properties;
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
    void set_method_metadata(const std::string& name, const method_metadata& metadata) {
        method_metadata_[name] = metadata;
    }
    
    // Get metadata for a method
    const method_metadata* get_method_metadata(const std::string& name) const {
        auto it = method_metadata_.find(name);
        return it != method_metadata_.end() ? &it->second : nullptr;
    }
    
    // Get mutable metadata for a method (for updating)
    method_metadata* get_method_metadata_mutable(const std::string& name) {
        auto it = method_metadata_.find(name);
        return it != method_metadata_.end() ? &it->second : nullptr;
    }
    
    // Find method in this class or parent classes, returns the class that has it
    struct method_info {
        std::shared_ptr<class_definition> owner_class;
        method_metadata* metadata;
    };
    
    method_info find_method(const std::string& name) {
        // First check this class
        if (has_method(name)) {
            return { shared_from_this(), get_method_metadata_mutable(name) };
        }
        
        // Check parent classes
        auto parent = get_parent();
        while (parent) {
            if (parent->has_method(name)) {
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
    void redefine_class(const std::unordered_map<std::string, script_value>& new_field_defaults,
                        const std::unordered_map<std::string, script_value>& new_methods,
                        const std::unordered_map<std::string, script_value>& new_static_methods,
                        std::weak_ptr<engine> engine_ref) {
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
            // Check if all field names match
            for (const auto& [name, _] : field_defaults_) {
                if (new_field_defaults.find(name) == new_field_defaults.end()) {
                    fields_changed = true;
                    break;
                }
            }
            // Also check the reverse - any new fields not in old?
            if (!fields_changed) {
                for (const auto& [name, _] : new_field_defaults) {
                    if (field_defaults_.find(name) == field_defaults_.end()) {
                        fields_changed = true;
                        break;
                    }
                }
            }
        }
        
        // Store old field names for migration (only if needed)
        std::set<std::string> old_fields;
        if (fields_changed) {
            for (const auto& [name, _] : field_defaults_) {
                old_fields.insert(name);
            }
        }
        
        // Check if new class has a hot_reload_migrate method
        auto migrate_method_it = new_methods.find("hot_reload_migrate");

        // Update methods BEFORE migrating instances so that hot_reload_migrate can access new getters/setters
        methods_ = new_methods;
        static_methods_ = new_static_methods;

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
                        try {
                            // Create a script_value wrapper for this instance
                            auto instance_value = script_value::make_object(name_, instance, engine_ref);

                            // Call the method with 'this' as the instance
                            const script_function& migrate_func = migrate_method_it->second.as_function();
                            std::vector<script_value> args = {instance_value};
                            migrate_func(args);
                            // Migration completed successfully
                        } catch (const std::exception& e) {
                            // Log migration errors but don't stop the hot-reload
                            std::cerr << "Hot reload migration error for " << name_
                                      << ": " << e.what() << std::endl;
                        }
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
        
        // NOW update the class definition after all migrations are complete
        // Ensure all field defaults have engine references
        field_defaults_.clear();
        for (const auto& [name, value] : new_field_defaults) {
            if (value.get_engine_ref().expired() && !engine_ref_.expired()) {
                // Create a copy with engine reference
                script_value value_with_engine(value);
                value_with_engine.set_engine_ref(engine_ref_);
                field_defaults_[name] = value_with_engine;
            } else {
                field_defaults_[name] = value;
            }
        }

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
        std::set<std::string> dummy_old_fields; // Not used for base updates
        
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
    std::weak_ptr<engine> engine_ref_;  // Engine reference for script_value creation
    std::unordered_map<std::string, script_value> methods_;
    std::unordered_map<std::string, script_value> static_methods_;  // Static method storage
    std::unordered_map<std::string, script_value> field_defaults_;
    std::unordered_map<std::string, script_value> static_field_values_;  // Static field storage
    std::unordered_set<std::string> static_fields_;  // Track which fields are static
    std::shared_ptr<class_definition> parent_class_;
    mutable std::unordered_map<std::string, script_value> all_field_defaults_cache_;
    mutable bool field_defaults_cache_valid_ = false;
    
    // Script class specific fields
    class_type class_type_;
    access_level default_access_ = access_level::public_access;
    
    // Mixed inheritance support - for script classes inheriting from C++ classes
    std::shared_ptr<class_definition> cpp_base_class_;
    
    // Copy function for deep copying objects
    copy_function copy_function_;
    
    // Method metadata for optimizations
    std::unordered_map<std::string, method_metadata> method_metadata_;
    
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
        const std::unordered_map<std::string, script_value>& field_defaults,
        const std::unordered_map<std::string, script_value>& methods,
        const std::unordered_map<std::string, script_value>& static_methods = {}) {
        
        size_t hash = 0;
        
        // Hash fields (names and types, not values)
        // Sort by name for consistent ordering
        std::vector<std::string> field_names;
        field_names.reserve(field_defaults.size());
        for (const auto& [name, _] : field_defaults) {
            field_names.push_back(name);
        }
        std::sort(field_names.begin(), field_names.end());
        
        for (const auto& name : field_names) {
            hash_combine(hash, name);
            // Could also hash default value types if needed
        }
        
        // Hash methods (names and their identity)
        // We need to detect when method implementations change, not just names
        std::vector<std::pair<std::string, size_t>> method_hashes;
        method_hashes.reserve(methods.size());
        for (const auto& [name, value] : methods) {
            // Hash the method name AND something about its implementation
            // For functions, we can use the internal function object address as a discriminator
            // This detects when the same method name gets a new implementation
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
            method_hashes.emplace_back(name, impl_hash);
        }
        std::sort(method_hashes.begin(), method_hashes.end());

        for (const auto& [name, impl_hash] : method_hashes) {
            hash_combine(hash, name);
            hash_combine(hash, impl_hash); // Include implementation identity
        }
        
        // Hash static methods (names and their identity)
        std::vector<std::pair<std::string, size_t>> static_method_hashes;
        static_method_hashes.reserve(static_methods.size());
        for (const auto& [name, value] : static_methods) {
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
            static_method_hashes.emplace_back(name, impl_hash);
        }
        std::sort(static_method_hashes.begin(), static_method_hashes.end());

        for (const auto& [name, impl_hash] : static_method_hashes) {
            hash_combine(hash, name);
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

// Type trait to detect if a type has a constructor that takes std::weak_ptr<engine>
template<typename T>
concept has_engine_constructor = requires(std::weak_ptr<engine> eng) {
    T(eng);
};

// Builder pattern for registering C++ classes to JaiScript
template<typename T>
class class_builder {
public:
    class_builder(engine& engine, const std::string& class_name) 
        : engine_(engine), class_name_(class_name) {
        // Extract base template name if this is a templated type
        std::string baseTemplateName = extract_base_template_name(class_name);
        
        // Register the base template name if it contains template syntax
        if (baseTemplateName != class_name) {
            engine.register_template_type(baseTemplateName);
        }
        
        auto engine_weak = std::weak_ptr<jai::engine>(engine_.shared_from_this());
        class_def_ = std::make_shared<class_definition>(class_name, engine_weak);
        
        // Initialize serialization metadata
        serialization_metadata_.class_name = class_name;
        serialization_metadata_.current_version = 1;
    }
    
    // Constructor that accepts a shared_ptr<engine>
    class_builder(std::shared_ptr<engine>& engine_ptr, const std::string& class_name) 
        : class_builder(*engine_ptr, class_name) {
    }
    
    // Add constructor
    template<typename... Args>
    class_builder& constructor() {
        // Register the constructor as an overloaded function
        if constexpr (sizeof...(Args) == 0) {
            // Zero-argument constructor
            auto engine_ref = engine_.shared_from_this();
            engine_.add_overloaded_function(class_name_, 0, [class_def = class_def_, class_name = class_name_, engine_ref](const std::vector<script_value>& args) -> script_value {
                try {
                    // Create the C++ object
                    std::shared_ptr<T> cpp_obj;
                    
                    // Check if T has an engine constructor and use it if available
                    if constexpr (has_engine_constructor<T>) {
                        // Use the engine-aware constructor
                        cpp_obj = std::make_shared<T>(std::weak_ptr<engine>(engine_ref));
                    } else {
                        // Use the default constructor
                        cpp_obj = std::make_shared<T>();
                    }
                    
                    // Create a class_instance to hold it
                    auto instance = class_def->create_instance();
                    
                    // Store the C++ object in the class_instance as a special field
                    instance->set_field(class_constants::CPP_OBJECT_FIELD, script_value::make_cpp_object(class_name, cpp_obj, engine_ref));
                    
                    // Return the class_instance wrapped in a value
                    return script_value::make_object(class_name, instance, engine_ref);
                } catch (const std::exception& e) {
                    std::cerr << "Error in zero-arg constructor: " << e.what() << std::endl;
                    throw;
                }
            });
        } else {
            // Multi-argument constructor
            auto engine_ref = engine_.shared_from_this();

            // Helper to avoid template parameter pack in lambda (MSVC workaround)
            using constructor_helper = std::shared_ptr<T>(*)(const std::vector<script_value>&, engine*);
            constructor_helper helper = [](const std::vector<script_value>& args, engine* eng) -> std::shared_ptr<T> {
                return class_builder<T>::template createObjectImpl<Args...>(args, std::index_sequence_for<Args...>{}, eng);
            };

            engine_.add_overloaded_function(class_name_, sizeof...(Args), [class_def = class_def_, class_name = class_name_, engine_ref, helper](const std::vector<script_value>& args) -> script_value {
                try {
                    // Extract arguments using index-based unpacking via helper
                    auto cpp_obj = helper(args, engine_ref.get());

                    // Create a class_instance to hold it
                    auto instance = class_def->create_instance();

                    // Store the C++ object in the class_instance as a special field
                    instance->set_field(class_constants::CPP_OBJECT_FIELD, script_value::make_cpp_object(class_name, cpp_obj, engine_ref));

                    // Return the class_instance wrapped in a value
                    return script_value::make_object(class_name, instance, engine_ref);
                } catch (const std::exception&) {
                    throw;
                }
            });
        }
        
        return *this;
    }
    
    // Add method binding - member function pointer version
    template<typename R, typename... Args>
    class_builder& method(const std::string& name, R(T::*method)(Args...)) {
        auto method_func = [method, engine_weak = std::weak_ptr<engine>(engine_.shared_from_this())](const std::vector<script_value>& args) -> script_value {
            if (args.empty()) {
                throw runtime_error("Method called without 'this' object");
            }
            
            // Validate argument count (first arg is 'this', so we need sizeof...(Args) + 1 total)
            if (args.size() != sizeof...(Args) + 1) {
                throw runtime_error("Method expects " + std::to_string(sizeof...(Args)) + 
                                 " arguments, got " + std::to_string(args.size() - 1));
            }
            
            // Extract the class_instance from the first argument (this)
            auto instance = args[0].as<std::shared_ptr<class_instance>>();
            
            // Get the C++ object from the special field
            auto cpp_obj_value = instance->get_field(class_constants::CPP_OBJECT_FIELD);
            auto cpp_obj = cpp_obj_value.as<std::shared_ptr<T>>();
            
            // Call the method with unpacked arguments
            if (auto eng = engine_weak.lock()) {
                return class_builder<T>::callMethodImpl(cpp_obj.get(), method, args, std::index_sequence_for<Args...>{}, eng.get());
            }
            throw runtime_error("Engine no longer exists");
        };
        
        // Add method to the class definition (for object.method() calls)
        // Methods are stored per-class and accessed through the object instance
        class_def_->add_method(name, method_func);
        
        return *this;
    }
    
    // Add const method binding
    template<typename R, typename... Args>
    class_builder& method(const std::string& name, R(T::*method)(Args...) const) {
        auto method_func = [method, engine_weak = std::weak_ptr<engine>(engine_.shared_from_this())](const std::vector<script_value>& args) -> script_value {
            if (args.empty()) {
                throw runtime_error("Method called without 'this' object");
            }
            
            // Validate argument count (first arg is 'this', so we need sizeof...(Args) + 1 total)
            if (args.size() != sizeof...(Args) + 1) {
                throw runtime_error("Method expects " + std::to_string(sizeof...(Args)) + 
                                 " arguments, got " + std::to_string(args.size() - 1));
            }
            
            // Extract the class_instance from the first argument (this)
            auto instance = args[0].as<std::shared_ptr<class_instance>>();
            
            // Get the C++ object from the special field
            auto cpp_obj_value = instance->get_field(class_constants::CPP_OBJECT_FIELD);
            auto cpp_obj = cpp_obj_value.as<std::shared_ptr<T>>();
            
            // Call the method with unpacked arguments
            if (auto eng = engine_weak.lock()) {
                return class_builder<T>::callConstMethodImpl(cpp_obj.get(), method, args, std::index_sequence_for<Args...>{}, eng.get());
            }
            throw runtime_error("Engine no longer exists");
        };
        
        // Add method to the class definition (for object.method() calls)
        // Methods are stored per-class and accessed through the object instance
        class_def_->add_method(name, method_func);
        
        return *this;
    }
    
    // Add lambda/callable method binding
    // Supports: .method("setText", [](Button& self, const std::string& text) { self.setText(text); })
    // Note: First parameter can be a reference to self for accessing the object
    template<typename Callable>
    class_builder& method(const std::string& name, Callable&& callable) {
        // Use function_traits to determine the signature
        using traits = detail::function_traits<std::decay_t<Callable>>;
        using args_tuple = typename traits::argument_types;
        
        // Check if the first parameter is a reference to T (the self parameter)
        constexpr bool has_self_param = traits::arity > 0 && 
            std::is_same_v<std::tuple_element_t<0, args_tuple>, T&>;
        
        auto method_func = [callable = std::forward<Callable>(callable), has_self_param, engine_weak = std::weak_ptr<engine>(engine_.shared_from_this())](const std::vector<script_value>& args) -> script_value {
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
                
                // Extract the C++ object from the class_instance
                auto instance = args[0].as<std::shared_ptr<class_instance>>();
                auto cpp_obj_value = instance->get_field(class_constants::CPP_OBJECT_FIELD);
                auto cpp_obj = cpp_obj_value.as<std::shared_ptr<T>>();
                
                // Call the lambda with the C++ object as first argument and remaining args
                if (auto eng = engine_weak.lock()) {
                    return callLambdaWithSelf<typename traits::return_type, args_tuple>(
                        callable, cpp_obj.get(), args, std::make_index_sequence<traits::arity>{}, eng.get());
                }
                throw runtime_error("Engine no longer exists");
            } else {
                // Regular lambda without self parameter
                if (args.size() != traits::arity) {
                    throw runtime_error("Method expects " + std::to_string(traits::arity) + 
                                     " arguments, got " + std::to_string(args.size()));
                }
                
                // Call the lambda with unpacked arguments
                if (auto eng = engine_weak.lock()) {
                    return callCallableImpl<typename traits::return_type, args_tuple>(callable, args, std::make_index_sequence<traits::arity>{}, eng.get());
                }
                throw runtime_error("Engine no longer exists");
            }
        };
        
        // Add method to the class definition (for object.method() calls)
        // Methods are stored per-class and accessed through the object instance
        class_def_->add_method(name, method_func);
        
        return *this;
    }
    
    // Add static method binding - for regular function pointers
    template<typename R, typename... Args>
    class_builder& static_method(const std::string& name, R(*func)(Args...)) {
        auto static_method_func = [func, engine_weak = std::weak_ptr<engine>(engine_.shared_from_this())](const std::vector<script_value>& args) -> script_value {
            // Validate argument count (no 'this' for static methods)
            if (args.size() != sizeof...(Args)) {
                throw runtime_error("Static method expects " + std::to_string(sizeof...(Args)) + 
                                 " arguments, got " + std::to_string(args.size()));
            }
            
            // Call the static function directly
            if constexpr (std::is_void_v<R>) {
                callStaticFunctionImpl(func, args, std::make_index_sequence<sizeof...(Args)>{});
                
                if (auto eng = engine_weak.lock()) {
                    return script_value::make_null(eng);
                }
                throw runtime_error("Engine no longer exists");
            } else {
                auto result = callStaticFunctionImpl(func, args, std::make_index_sequence<sizeof...(Args)>{});
                
                if (auto eng = engine_weak.lock()) {
                    return script_value(result, eng);
                }
                throw runtime_error("Engine no longer exists");
            }
        };
        
        // Add static method to the class definition
        class_def_->add_static_method(name, static_method_func);
        
        return *this;
    }
    
    // Add static method binding - for lambdas and callables
    template<typename Callable>
    class_builder& static_method(const std::string& name, Callable&& callable) {
        using traits = detail::function_traits<std::decay_t<Callable>>;
        
        auto static_method_func = [callable = std::forward<Callable>(callable), engine_weak = std::weak_ptr<engine>(engine_.shared_from_this())](const std::vector<script_value>& args) -> script_value {
            // Validate argument count
            if (args.size() != traits::arity) {
                throw runtime_error("Static method expects " + std::to_string(traits::arity) + 
                                 " arguments, got " + std::to_string(args.size()));
            }
            
            // Call the callable directly (no 'this' parameter)
            using return_type = typename traits::return_type;
            if constexpr (std::is_void_v<return_type>) {
                callStaticCallableImpl(callable, args, std::make_index_sequence<traits::arity>{});
                
                if (auto eng = engine_weak.lock()) {
                    return script_value::make_null(eng);
                }
                throw runtime_error("Engine no longer exists");
            } else {
                auto result = callStaticCallableImpl(callable, args, std::make_index_sequence<traits::arity>{});
                
                if (auto eng = engine_weak.lock()) {
                    return script_value(result, eng);
                }
                throw runtime_error("Engine no longer exists");
            }
        };
        
        // Add static method to the class definition
        class_def_->add_static_method(name, static_method_func);
        
        return *this;
    }
    
    // Add custom serialization constructor for non-default constructible types
    template<typename constructor_func>
    class_builder& serialize_construct(constructor_func&& constructor) {
        // Store the custom constructor in the class definition
        class_def_->add_method("_serialize_construct", [constructor = std::forward<constructor_func>(constructor), class_def = class_def_, class_name = class_name_, engine_weak = std::weak_ptr<engine>(engine_.shared_from_this())](const std::vector<script_value>& args) -> script_value {
            if (args.size() != 1) {
                throw runtime_error("Serialization constructor expects exactly one argument (the serialized data)");
            }
            
            // Call the custom constructor with the serialized data
            T instance = constructor(args[0]);
            
            // Create a class_instance to hold it
            auto class_instance = class_def->create_instance();
            class_instance->set_field(class_constants::CPP_OBJECT_FIELD, script_value::make_cpp_object(class_name, 
                std::make_shared<T>(std::move(instance))));
            
            if (auto eng = engine_weak.lock()) {
                return script_value::make_object(class_name, class_instance, eng);
            }
            throw runtime_error("Engine no longer exists");
        });
        
        // Also register serialization metadata
        auto& metadata = serialization_metadata_;
        metadata.custom_construct = [constructor = std::forward<constructor_func>(constructor)](serialization::archive_reader& ar, uint32_t version) -> script_value {
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
    class_builder& version(uint32_t v) {
        serialization_metadata_.current_version = v;
        return *this;
    }
    
    // Add deleted property for binary compatibility
    template<typename PropType>
    class_builder& deleted_property(const std::string& name, 
                                  serialization::version_removed removed = serialization::version_removed(UINT32_MAX)) {
        serialization::property_metadata prop_meta;
        prop_meta.name = name;
        prop_meta.type = type_info::make<PropType>();
        prop_meta.is_deleted = true;
        prop_meta.version_removed = removed.version;
        
        serialization_metadata_.properties.push_back(prop_meta);
        return *this;
    }
    
    // Specify explicit property list for a version
    class_builder& version_properties(uint32_t version, const std::vector<std::string>& properties) {
        serialization_metadata_.version_property_lists[version] = properties;
        return *this;
    }

    // Add property/field binding
    template<typename P>
    class_builder& property(const std::string& name, P T::*member) {
        // Register the property as a special field that knows how to access the C++ member
        // We'll store a lambda that can get/set the value
        class_def_->add_field(name, script_value(std::monostate{}, std::weak_ptr<engine>(engine_.shared_from_this()))); // Register field name
        
        // Register serialization metadata
        serialization::property_metadata prop_meta;
        prop_meta.name = name;
        prop_meta.type = type_info::make<P>();
        prop_meta.version_added = 1; // Default to version 1
        serialization_metadata_.properties.push_back(prop_meta);
        
        // Add a special method that handles property access
        // The interpreter's visitMemberExpr will need to check for these
        class_def_->add_method("_get_" + name, [member, engine_weak = std::weak_ptr<engine>(engine_.shared_from_this())](const std::vector<script_value>& args) -> script_value {
            if (args.empty()) {
                throw runtime_error("Property getter called without 'this' object");
            }
            
            // Extract the class_instance from the first argument (this)
            auto instance = args[0].as<std::shared_ptr<class_instance>>();
            
            // Get the C++ object from the special field
            auto cpp_obj_value = instance->get_field(class_constants::CPP_OBJECT_FIELD);
            auto cpp_obj = cpp_obj_value.as<std::shared_ptr<T>>();
            
            if (auto eng = engine_weak.lock()) {
                return detail::value_converter<P>::to(cpp_obj.get()->*member, eng.get());
            }
            throw runtime_error("Engine no longer exists");
        });
        
        class_def_->add_method("_set_" + name, [member, engine_weak = std::weak_ptr<engine>(engine_.shared_from_this())](const std::vector<script_value>& args) -> script_value {
            if (args.size() < 2) {
                throw runtime_error("Property setter requires 'this' and value");
            }
            
            // Extract the class_instance from the first argument (this)
            auto instance = args[0].as<std::shared_ptr<class_instance>>();
            
            // Get the C++ object from the special field
            auto cpp_obj_value = instance->get_field(class_constants::CPP_OBJECT_FIELD);
            auto cpp_obj = cpp_obj_value.as<std::shared_ptr<T>>();
            
            // Special case: if P is script_value, don't convert
            if constexpr (std::is_same_v<P, script_value>) {
                // deref() returns *this if not a reference, so this handles both cases
                (cpp_obj.get()->*member).deref() = args[1].clone();
            } else {
                cpp_obj.get()->*member = args[1].as<P>();
            }
            return script_value(std::monostate{}, engine_weak); // null
        });
        
        // Also add traditional getter/setter methods for compatibility
        std::string getterName = "get" + name;
        getterName[3] = std::toupper(getterName[3]); // Capitalize first letter
        
        class_def_->add_method(getterName, [member, class_name = class_name_, engine_weak = std::weak_ptr<engine>(engine_.shared_from_this())](const std::vector<script_value>& args) -> script_value {
            if (args.empty()) {
                throw runtime_error("Getter called without 'this' object");
            }
            
            // Extract the class_instance from the first argument (this)
            auto instance = args[0].as<std::shared_ptr<class_instance>>();
            
            // Get the C++ object from the special field
            auto cpp_obj_value = instance->get_field(class_constants::CPP_OBJECT_FIELD);
            auto cpp_obj = cpp_obj_value.as<std::shared_ptr<T>>();
            
            if (auto eng = engine_weak.lock()) {
                return detail::value_converter<P>::to(cpp_obj.get()->*member, eng.get());
            }
            throw runtime_error("Engine no longer exists");
        });
        
        // Add setter
        std::string setterName = "set" + name;
        setterName[3] = std::toupper(setterName[3]); // Capitalize first letter
        
        class_def_->add_method(setterName, [member, class_name = class_name_, engine_weak = std::weak_ptr<engine>(engine_.shared_from_this())](const std::vector<script_value>& args) -> script_value {
            if (args.size() < 2) {
                throw runtime_error("Setter requires 'this' and value");
            }
            
            // Extract the class_instance from the first argument (this)
            auto instance = args[0].as<std::shared_ptr<class_instance>>();
            
            // Get the C++ object from the special field
            auto cpp_obj_value = instance->get_field(class_constants::CPP_OBJECT_FIELD);
            auto cpp_obj = cpp_obj_value.as<std::shared_ptr<T>>();
            
            // Special case: if P is script_value, don't convert
            if constexpr (std::is_same_v<P, script_value>) {
                // deref() returns *this if not a reference, so this handles both cases
                (cpp_obj.get()->*member).deref() = args[1].clone();
            } else {
                cpp_obj.get()->*member = args[1].as<P>();
            }
            return script_value(std::monostate{}, engine_weak); // null
        });
        
        return *this;
    }
    
    // Add property with lambda getter/setter
    template<typename Getter, typename Setter>
    class_builder& property(const std::string& name, Getter&& getter, Setter&& setter) {
        // Register the property as a field
        class_def_->add_field(name, script_value(std::monostate{}, std::weak_ptr<engine>(engine_.shared_from_this()))); // Register field name
        
        // Add getter method
        class_def_->add_method("_get_" + name, [getter = std::forward<Getter>(getter)](const std::vector<script_value>& args) -> script_value {
            if (args.empty()) {
                throw runtime_error("Property getter called without 'this' object");
            }
            
            // Extract the class_instance from the first argument (this)
            auto instance = args[0].as<std::shared_ptr<class_instance>>();
            
            // Get the C++ object from the special field
            auto cpp_obj_value = instance->get_field(class_constants::CPP_OBJECT_FIELD);
            auto cpp_obj = cpp_obj_value.as<std::shared_ptr<T>>();
            
            // Call the getter lambda with the C++ object
            return detail::value_converter<decltype(getter(*cpp_obj))>::to(getter(*cpp_obj));
        });
        
        // Add setter method
        class_def_->add_method("_set_" + name, [setter = std::forward<Setter>(setter), engine_weak = std::weak_ptr<engine>(engine_.shared_from_this())](const std::vector<script_value>& args) -> script_value {
            if (args.size() < 2) {
                throw runtime_error("Property setter requires 'this' and value");
            }
            
            // Extract the class_instance from the first argument (this)
            auto instance = args[0].as<std::shared_ptr<class_instance>>();
            
            // Get the C++ object from the special field
            auto cpp_obj_value = instance->get_field(class_constants::CPP_OBJECT_FIELD);
            auto cpp_obj = cpp_obj_value.as<std::shared_ptr<T>>();
            
            // Extract the value to set
            using setter_traits = detail::function_traits<std::decay_t<Setter>>;
            using value_type = std::tuple_element_t<1, typename setter_traits::argument_types>;
            auto value = args[1].as<value_type>();
            
            // Call the setter lambda
            setter(*cpp_obj, value);
            return script_value(std::monostate{}, engine_weak); // null
        });
        
        // Also add traditional getter/setter methods for compatibility
        std::string getterName = "get" + name;
        getterName[3] = std::toupper(getterName[3]); // Capitalize first letter
        
        class_def_->add_method(getterName, [getter = std::forward<Getter>(getter)](const std::vector<script_value>& args) -> script_value {
            if (args.empty()) {
                throw runtime_error("Getter called without 'this' object");
            }
            
            auto instance = args[0].as<std::shared_ptr<class_instance>>();
            auto cpp_obj_value = instance->get_field(class_constants::CPP_OBJECT_FIELD);
            auto cpp_obj = cpp_obj_value.as<std::shared_ptr<T>>();
            
            return detail::value_converter<decltype(getter(*cpp_obj))>::to(getter(*cpp_obj));
        });
        
        // Add setter
        std::string setterName = "set" + name;
        setterName[3] = std::toupper(setterName[3]); // Capitalize first letter
        
        class_def_->add_method(setterName, [setter = std::forward<Setter>(setter), engine_weak = std::weak_ptr<engine>(engine_.shared_from_this())](const std::vector<script_value>& args) -> script_value {
            if (args.size() < 2) {
                throw runtime_error("Setter requires 'this' and value");
            }
            
            auto instance = args[0].as<std::shared_ptr<class_instance>>();
            auto cpp_obj_value = instance->get_field(class_constants::CPP_OBJECT_FIELD);
            auto cpp_obj = cpp_obj_value.as<std::shared_ptr<T>>();
            
            using setter_traits = detail::function_traits<std::decay_t<Setter>>;
            using value_type = std::tuple_element_t<1, typename setter_traits::argument_types>;
            auto value = args[1].as<value_type>();
            
            setter(*cpp_obj, value);
            return script_value(std::monostate{}, engine_weak); // null
        });
        
        return *this;
    }
    
    // Add static property binding - for simple variable access
    template<typename P>
    class_builder& static_property(const std::string& name, P* static_var) {
        // Add static field with getter for the variable
        class_def_->add_static_field(name, script_value(*static_var, engine_.shared_from_this()));
        
        // Add getter method for read access
        class_def_->add_static_method("_get_" + name, [static_var, engine_weak = std::weak_ptr<engine>(engine_.shared_from_this())](const std::vector<script_value>& args) -> script_value {
            if (auto eng = engine_weak.lock()) {
                return script_value(*static_var, eng);
            }
            throw runtime_error("Engine no longer exists");
        });
        
        // Add setter method for write access
        std::string setterName = "set" + name;
        setterName[3] = std::toupper(setterName[3]); // Capitalize first letter
        
        class_def_->add_static_method(setterName, [static_var, engine_weak = std::weak_ptr<engine>(engine_.shared_from_this())](const std::vector<script_value>& args) -> script_value {
            if (args.size() != 1) {
                throw runtime_error("Static property setter requires exactly one argument");
            }
            
            *static_var = args[0].as<P>();
            
            if (auto eng = engine_weak.lock()) {
                return script_value(std::monostate{}, eng);
            }
            throw runtime_error("Engine no longer exists");
        });
        
        return *this;
    }
    
    // Add static property with lambda getter/setter
    template<typename Getter, typename Setter>
    class_builder& static_property(const std::string& name, Getter&& getter, Setter&& setter) {
        // Add getter method
        class_def_->add_static_method("_get_" + name, [getter = std::forward<Getter>(getter), engine_weak = std::weak_ptr<engine>(engine_.shared_from_this())](const std::vector<script_value>& args) -> script_value {
            if (auto eng = engine_weak.lock()) {
                return detail::value_converter<decltype(getter())>::to(getter(), eng.get());
            }
            throw runtime_error("Engine no longer exists");
        });
        
        // Add setter method
        std::string setterName = "set" + name;
        setterName[3] = std::toupper(setterName[3]); // Capitalize first letter
        
        class_def_->add_static_method(setterName, [setter = std::forward<Setter>(setter), engine_weak = std::weak_ptr<engine>(engine_.shared_from_this())](const std::vector<script_value>& args) -> script_value {
            if (args.size() != 1) {
                throw runtime_error("Static property setter requires exactly one argument");
            }
            
            using setter_traits = detail::function_traits<std::decay_t<Setter>>;
            using value_type = std::tuple_element_t<0, typename setter_traits::argument_types>;
            auto value = args[0].as<value_type>();
            
            setter(value);
            
            if (auto eng = engine_weak.lock()) {
                return script_value(std::monostate{}, eng);
            }
            throw runtime_error("Engine no longer exists");
        });
        
        return *this;
    }
    
    // Set base class
    template<typename Base>
    class_builder& base_class() {
        static_assert(std::is_base_of_v<Base, T>,
                      "Specified type is not a base class of this class");

        // Set up inheritance relationship - use type_index lookup instead of typeid name
        auto base_def = engine_.get_class_definition_by_type(std::type_index(typeid(Base)));
        if (base_def) {
            class_def_->set_parent(base_def);
        }

        // Store base type info for polymorphic copy registration
        has_base_class_ = true;
        base_type_index_ = std::type_index(typeid(Base));

        return *this;
    }
    
    // Add explicit type conversion support - general purpose
    template<typename From, typename To>
    class_builder& add_type_conversion(std::function<To(const From&)> converter) {
        // Register the conversion with the engine
        // This would need to be implemented in the engine's type system
        // Usage: .add_type_conversion<SafeComponent<Button>, std::shared_ptr<Button>>([](const auto& item) { return item.self(); })
        return *this;
    }
    
    // Finalize registration
    void build() {
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
        
        // Register serialization metadata with the engine's registry
        engine_.get_serialization_registry().register_class(class_name_, serialization_metadata_);
        
        
        // Register custom conversions for shared_ptr<T> to handle class_instance wrapping
        if constexpr (!std::is_abstract_v<T>) {
            engine_.add_custom_conversion<std::shared_ptr<T>>(
                // From script_value to shared_ptr<T>
                [class_name = class_name_](const script_value& v) -> std::shared_ptr<T> {
                    if (v.is_object()) {
                        // Try to extract as shared_ptr<class_instance> first
                        if (v.as<std::shared_ptr<class_instance>>()) {
                            auto instance = v.as<std::shared_ptr<class_instance>>();
                            // Extract the C++ object from the instance
                            return instance->get_cpp_object_as<T>();
                        }
                    }
                    throw runtime_error("Cannot convert script_value to shared_ptr<" + class_name + ">");
                },
                // From shared_ptr<T> to script_value
                [class_def = class_def_, class_name = class_name_, engine_weak = std::weak_ptr<engine>(engine_.shared_from_this())](const std::shared_ptr<T>& obj) -> script_value {
                    if (!obj) {
                        return script_value(std::monostate{}, engine_weak);
                    }
                    
                    // Create a class_instance to wrap the object
                    auto instance = class_def->create_instance();
                    instance->set_field(class_constants::CPP_OBJECT_FIELD, 
                        script_value::make_cpp_object(class_name, std::static_pointer_cast<void>(obj), engine_weak));
                    
                    // Return wrapped in script_value
                    return script_value::make_object(class_name, std::static_pointer_cast<void>(instance), engine_weak);
                }
            );
        }
        
        // Register converters for this type (only for concrete types)
        // This allows functions returning T to automatically convert to value
        if constexpr (!std::is_abstract_v<T>) {
            // Register a C++ type converter that creates class_instance objects
            engine_.register_type_converter_impl(typeid(T), 
                [class_def = class_def_, class_name = class_name_, engine_weak = std::weak_ptr<engine>(engine_.shared_from_this())](const void* obj) -> script_value {
                    // Create a class_instance using the class definition
                    // This properly initializes all fields with their defaults
                    auto instance = class_def->create_instance();
                    
                    // Create a shared_ptr to the C++ object (by copying)
                    auto cpp_obj = std::make_shared<T>(*static_cast<const T*>(obj));
                    
                    // Store the C++ object in the class_instance
                    instance->set_field("_cpp_object", script_value::make_cpp_object(class_name, 
                        std::static_pointer_cast<void>(cpp_obj), engine_weak));
                    
                    // Return the class_instance wrapped in a value
                    if (auto eng = engine_weak.lock()) {
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
                        
                        if (!objHolder->is_cpp_class_instance) {
                            throw runtime_error("Object is not a class_instance wrapper");
                        }
                        
                        // Cast to class_instance
                        auto instance = std::static_pointer_cast<class_instance>(objHolder->data);
                        if (!instance) {
                            throw runtime_error("Failed to cast to class_instance");
                        }
                        
                        // Get the C++ object field directly - don't use has_field since it's an internal field
                        auto cpp_obj_value = instance->get_field(class_constants::CPP_OBJECT_FIELD);
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
                    [class_def = class_def_, class_name = class_name_, engine_weak = std::weak_ptr<engine>(engine_.shared_from_this())](const T& obj) -> script_value {
                        // Create a class_instance using the class definition
                        auto instance = class_def->create_instance();
                        
                        // Create a shared_ptr to the C++ object (by copying)
                        auto cpp_obj = std::make_shared<T>(obj);
                        
                        // Store the C++ object in the class_instance
                        instance->set_field(class_constants::CPP_OBJECT_FIELD, 
                            script_value::make_cpp_object(class_name, 
                                std::static_pointer_cast<void>(cpp_obj), engine_weak));
                        
                        // Return the class_instance wrapped in a value
                        if (auto eng = engine_weak.lock()) {
                            return script_value::make_object(class_name, 
                                std::static_pointer_cast<void>(instance), eng);
                        }
                        throw runtime_error("Engine no longer exists");
                    }
                );
            }
            
            // Also register std::shared_ptr<T> conversion
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
                        if (!objHolder->is_cpp_class_instance) {
                            // Direct C++ object - just cast and return
                            return std::static_pointer_cast<T>(objHolder->data);
                        }
                        
                        // It's a class_instance wrapper - extract the C++ object
                        auto instance = std::static_pointer_cast<class_instance>(objHolder->data);
                        if (!instance) {
                            throw runtime_error("Failed to cast to class_instance");
                        }
                        
                        // Get the C++ object field
                        auto cpp_obj_value = instance->get_field(class_constants::CPP_OBJECT_FIELD);
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
                    [class_def = class_def_, class_name = class_name_, engine_weak = std::weak_ptr<engine>(engine_.shared_from_this())](const std::shared_ptr<T>& obj) -> script_value {
                        if (!obj) {
                            return script_value(std::monostate{}, engine_weak); // null
                        }
                        
                        // Create a class_instance using the class definition
                        auto instance = class_def->create_instance();
                        
                        // Store the shared_ptr directly (no copy needed)
                        instance->set_field(class_constants::CPP_OBJECT_FIELD, 
                            script_value::make_cpp_object(class_name, 
                                std::static_pointer_cast<void>(obj)));
                        
                        // Return the class_instance wrapped in a value
                        if (auto eng = engine_weak.lock()) {
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
    engine& engine_;
    std::string class_name_;
    std::shared_ptr<class_definition> class_def_;
    serialization::class_metadata serialization_metadata_;
    bool has_base_class_ = false;
    std::type_index base_type_index_ = std::type_index(typeid(void));
    
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
                    [class_def = class_def_, class_name = class_name_, engine_weak = std::weak_ptr<engine>(engine_.shared_from_this())](const T& obj) -> script_value {
                        auto instance = class_def->create_instance();
                        auto cpp_obj = std::make_shared<T>(obj);
                        instance->set_field(class_constants::CPP_OBJECT_FIELD, 
                            script_value::make_cpp_object(class_name, 
                                std::static_pointer_cast<void>(cpp_obj), engine_weak));
                        return script_value::make_object(class_name, instance, engine_weak);
                    }
                );
            }
            
            // Register vector conversions
            conv_mgr.add_vector_conversion<T>();
            conv_mgr.add_vector_conversion<std::shared_ptr<T>>();
            
            // Register common map conversions with this type as value
            // Only register T map conversions if T is default constructible (required for std::map)
            if constexpr (std::is_default_constructible_v<T>) {
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
        detail::parameter_storage::scope_guard guard(&storage);
        
        return std::make_shared<T>(detail::value_converter<Args>::from(args[Is], eng)...);
    }
    
    // Helper method for calling member functions
    template<typename R, typename... Args, size_t... Is>
    static script_value callMethodImpl(T* obj, R(T::*method)(Args...), const std::vector<script_value>& args, std::index_sequence<Is...>, engine* eng) {
        // Create parameter storage on stack
        detail::parameter_storage storage;
        detail::parameter_storage::scope_guard guard(&storage);
        
        if constexpr (std::is_void_v<R>) {
            (obj->*method)(detail::value_converter<Args>::from(args[Is + 1], eng)...);
            return script_value(std::monostate{}, get_engine_weak_ptr(eng)); // null for void
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
        detail::parameter_storage::scope_guard guard(&storage);
        
        if constexpr (std::is_void_v<R>) {
            (obj->*method)(detail::value_converter<Args>::from(args[Is + 1], eng)...);
            return script_value(std::monostate{}, get_engine_weak_ptr(eng)); // null for void
        } else {
            R result = (obj->*method)(detail::value_converter<Args>::from(args[Is + 1], eng)...);
            return detail::value_converter<R>::to(result, eng);
        }
    }
    
    // Helper method for calling static functions
    template<typename R, typename... Args, size_t... Is>
    static R callStaticFunctionImpl(R(*func)(Args...), const std::vector<script_value>& args, std::index_sequence<Is...>) {
        // Create parameter storage on stack
        detail::parameter_storage storage;
        detail::parameter_storage::scope_guard guard(&storage);
        
        return func(detail::value_converter<Args>::from(args[Is], nullptr)...);
    }
    
    // Helper method for calling static callables/lambdas
    template<typename Callable, size_t... Is>
    static auto callStaticCallableImpl(Callable&& callable, const std::vector<script_value>& args, std::index_sequence<Is...>) {
        // Create parameter storage on stack
        detail::parameter_storage storage;
        detail::parameter_storage::scope_guard guard(&storage);
        
        using traits = detail::function_traits<std::decay_t<Callable>>;
        using args_tuple = typename traits::argument_types;
        
        return callable(detail::value_converter<std::tuple_element_t<Is, args_tuple>>::from(args[Is], nullptr)...);
    }
    
    // Helper method for calling lambdas/callables
    template<typename R, typename ArgsTuple, typename Callable, size_t... Is>
    static script_value callCallableImpl(Callable&& callable, const std::vector<script_value>& args, std::index_sequence<Is...>, engine* eng) {
        // Create parameter storage on stack
        detail::parameter_storage storage;
        detail::parameter_storage::scope_guard guard(&storage);
        
        if constexpr (std::is_void_v<R>) {
            callable(detail::value_converter<std::tuple_element_t<Is, ArgsTuple>>::from(args[Is], eng)...);
            return script_value(std::monostate{}, get_engine_weak_ptr(eng)); // null for void
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
        detail::parameter_storage::scope_guard guard(&storage);
        
        if constexpr (std::is_void_v<R>) {
            // Call with self as first argument, then args[1], args[2], etc.
            callable(*self, detail::value_converter<std::tuple_element_t<Is + 1, ArgsTuple>>::from(args[Is + 1], eng)...);
            return script_value(std::monostate{}, get_engine_weak_ptr(eng)); // null for void
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
inline std::shared_ptr<class_definition> make_script_class_definition(const std::string& class_name, std::weak_ptr<engine> eng) {
    return std::make_shared<class_definition>(class_name, class_definition::script_class, eng);
}

// implementation of class_instance methods (must be after class_definition)

inline bool class_instance::is_script_class() const {
    if (auto class_def = class_definition_.lock()) {
        return class_def->is_script_class();
    }
    return false;
}

inline bool class_instance::is_cpp_class() const {
    if (auto class_def = class_definition_.lock()) {
        return class_def->is_cpp_class();
    }
    return false;
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

inline std::shared_ptr<class_instance> class_instance::deep_copy() const {
    auto new_instance = std::make_shared<class_instance>(class_name_);

    // Copy all fields
    for (const auto& [name, value] : fields_) {
        // Special handling for _cpp_object field
        if (name == class_constants::CPP_OBJECT_FIELD && !value.is_null()) {
            // Get the class definition to access copy function
            if (auto class_def = class_definition_.lock()) {
                if (class_def->has_copy_function()) {
                    // Extract the C++ object
                    auto cpp_obj = extract_cpp_object_impl(value);
                    if (cpp_obj) {
                        // Use the copy function to create a new C++ object
                        auto new_cpp_obj = class_def->copy_object(cpp_obj.get());

                        // Wrap in a new script_value
                        new_instance->set_field(class_constants::CPP_OBJECT_FIELD,
                            script_value::make_cpp_object(class_name_, new_cpp_obj));
                        continue;
                    }
                }
            }
        }

        // For other fields, use script_value's clone method for deep copy
        new_instance->set_field(name, value.clone());
    }

    // Copy class definition reference
    new_instance->class_definition_ = class_definition_;

    // Register the new instance for hot reload tracking
    if (auto class_def = class_definition_.lock()) {
        class_def->register_instance(std::weak_ptr<class_instance>(new_instance));
    }

    return new_instance;
}

// Implementations that depend on class_definition being complete
inline const script_value& class_instance::get_field(const std::string& name, bool throw_if_missing) const {
    auto it = fields_.find(name);
    if (it != fields_.end()) {
        return it->second;
    }
    
    // If not in instance fields, check class definition for default value
    if (auto def = class_definition_.lock()) {
        auto& field_defaults = def->get_all_field_defaults();
        auto default_it = field_defaults.find(name);
        if (default_it != field_defaults.end()) {
            return default_it->second;  // Return reference to the default
        }
    }
    
    if (throw_if_missing) {
        throw runtime_error("Field '" + name + "' not found and no default value available");
    }
    
    // For non-throwing case, we need to return a reference to something
    // Create a static invalid value to return
    static thread_local script_value invalid_value = script_value::make_invalid(std::weak_ptr<engine>{});
    return invalid_value;
}

inline script_value& class_instance::get_field(const std::string& name, bool throw_if_missing) {
    auto it = fields_.find(name);
    if (it != fields_.end()) {
        return it->second;
    }
    
    // If not in instance fields, we need to create it with the default value
    if (auto def = class_definition_.lock()) {
        auto& field_defaults = def->get_all_field_defaults();
        auto default_it = field_defaults.find(name);
        if (default_it != field_defaults.end()) {
            // Create the field with a clone of the default value
            auto [new_it, _] = fields_.emplace(name, default_it->second.clone());
            return new_it->second;
        }
    }
    
    if (throw_if_missing) {
        throw runtime_error("Field '" + name + "' not found and no default value available");
    }
    
    // For non-throwing case, we need to return a reference to something
    // Create a static invalid value to return
    static thread_local script_value invalid_value = script_value::make_invalid(std::weak_ptr<engine>{});
    return invalid_value;
}

inline bool class_instance::has_field(const std::string& name) const {
    // Check if the field exists in this instance
    // After hot reload with migrate_fields, the instance will have exactly
    // the fields it should have - new fields are added and old fields removed
    return fields_.find(name) != fields_.end();
}

inline script_value class_instance::get_method(const std::string& name, bool throw_if_missing) const {
    if (auto def = class_definition_.lock()) {
        return def->get_method(name, throw_if_missing);
    }

    // DEBUG: Diagnose why class_definition_ weak_ptr failed to lock
    std::cerr << "[DEBUG] class_instance::get_method('" << name << "') - class_definition_ failed to lock for class '"
              << class_name_ << "'" << std::endl;
    std::cerr << "[DEBUG] class_definition_.expired() = " << class_definition_.expired() << std::endl;
    std::cerr << "[DEBUG] class_definition_.use_count() = " << class_definition_.use_count() << std::endl;

    if (throw_if_missing) {
        throw runtime_error("Class definition not available for method '" + name + "' lookup");
    }
    return script_value::make_invalid(std::weak_ptr<engine>{});
}

// Define class_instance destructor after class_definition is complete
inline class_instance::~class_instance() {
    // Unregister from class definition when destroyed
    if (auto class_def = class_definition_.lock()) {
        class_def->unregister_instance(this);
    }
}

} // namespace jai

// Include script class after class_definition is complete
#include "script_class.hpp"

#endif // __JAISCRIPT_CORE_CLASS_BUILDER_HPP__