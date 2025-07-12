#pragma once

#ifndef __JAISCRIPT_CORE_CLASS_BUILDER_HPP__
#define __JAISCRIPT_CORE_CLASS_BUILDER_HPP__

#include "engine.hpp"
#include "value.hpp"
#include "types.hpp"
#include "function_binder.hpp"
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
class class_instance {
public:
    class_instance(const std::string& class_name) : class_name_(class_name) {}
    
    ~class_instance();
    
    // Field access
    void set_field(const std::string& name, const script_value& value) {
        fields_.insert_or_assign(name, value);
    }
    
    script_value get_field(const std::string& name) const;
    
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
    script_value get_method(const std::string& name) const;
    
    // Set the class definition this instance belongs to
    void set_class_definition(std::shared_ptr<class_definition> class_def) {
        class_definition_ = class_def;
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
    
    // Add a script method (wraps AST execution in a function)
    // Implementation moved to script_class.hpp to avoid circular dependency
    void add_script_method(const std::string& name, std::shared_ptr<function_decl> ast, interpreter* interp);
    
    // Add a field with default value
    void add_field(const std::string& name, const script_value& default_value) {
        field_defaults_.insert_or_assign(name, default_value);
    }
    
    // Add a field with null default value
    void add_field(const std::string& name) {
        field_defaults_.insert_or_assign(name, script_value(std::monostate{}, engine_ref_));
    }
    
    // Get a method
    script_value get_method(const std::string& name) const {
        auto it = methods_.find(name);
        if (it != methods_.end()) {
            return it->second;
        }
        // Check parent class if we have inheritance
        if (parent_class_) {
            return parent_class_->get_method(name);
        }
        // Return null/void when method not found using stored engine reference
        return script_value(std::monostate{}, engine_ref_);
    }
    
    // Create an instance of this class
    std::shared_ptr<class_instance> create_instance() {
        auto instance = std::make_shared<class_instance>(name_);
        instance->set_class_definition(shared_from_this());
        
        // Register instance for hot reload tracking
        register_instance(std::weak_ptr<class_instance>(instance));
        
        // Initialize fields from parent classes first (if any)
        if (parent_class_) {
            auto parent_fields = parent_class_->get_all_field_defaults();
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
    std::unordered_map<std::string, script_value> get_all_field_defaults() const {
        std::unordered_map<std::string, script_value> all_fields;
        
        // Start with parent fields
        if (parent_class_) {
            all_fields = parent_class_->get_all_field_defaults();
        }
        
        // Add/override with our fields
        for (const auto& [name, value] : field_defaults_) {
            all_fields.insert_or_assign(name, value);
        }
        
        return all_fields;
    }
    
    // Set parent class for inheritance
    void set_parent(std::shared_ptr<class_definition> parent) {
        parent_class_ = parent;
        // Register this as a derived class of parent
        if (parent) {
            parent->add_derived_class(shared_from_this());
        }
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
        bool is_override = false;
        bool is_final = false;
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
    
    // Initialize fingerprint after class is first defined
    void initialize_fingerprint() {
        current_fingerprint_ = compute_fingerprint(field_defaults_, methods_);
    }
    
    // Hot reload support - migrate all instances
    void redefine_class(const std::unordered_map<std::string, script_value>& new_field_defaults,
                        const std::unordered_map<std::string, script_value>& new_methods,
                        std::weak_ptr<engine> engine_ref) {
        // Compute fingerprint for the new definition
        size_t new_fingerprint = compute_fingerprint(new_field_defaults, new_methods);
        
        // Ultra-fast path: completely identical class
        if (new_fingerprint == current_fingerprint_) {
            return;  // Nothing to do at all!
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
                    
                    // THEN call hot_reload_migrate() if it exists
                    if (migrate_method_it != methods_.end() && !migrate_method_it->second.is_null()) {
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
                    
                    // FINALLY migrate fields (remove old, keep new/updated values)
                    instance->migrate_fields(old_fields, new_field_defaults);
                } else {
                    // Fields haven't changed - skip the expensive field migration
                    // The class definition reference will be automatically updated since
                    // instances hold a weak_ptr that resolves to the current definition
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
        field_defaults_ = new_field_defaults;
        methods_ = new_methods;
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
        auto all_fields = get_all_field_defaults();
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
    std::unordered_map<std::string, script_value> field_defaults_;
    std::shared_ptr<class_definition> parent_class_;
    
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
        const std::unordered_map<std::string, script_value>& methods) {
        
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
        
        // Hash methods (names and signatures)
        std::vector<std::string> method_names;
        method_names.reserve(methods.size());
        for (const auto& [name, _] : methods) {
            method_names.push_back(name);
        }
        std::sort(method_names.begin(), method_names.end());
        
        for (const auto& name : method_names) {
            hash_combine(hash, name);
            // For now, just hash the name. Could hash AST/bytecode for deeper comparison
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
                    auto cpp_obj = std::make_shared<T>();
                    
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
            engine_.add_overloaded_function(class_name_, sizeof...(Args), [class_def = class_def_, class_name = class_name_, engine_ref](const std::vector<script_value>& args) -> script_value {
                try {
                    // Extract arguments using index-based unpacking
                    auto cpp_obj = class_builder<T>::createObjectImpl<Args...>(args, std::index_sequence_for<Args...>{}, engine_ref.get());
                    
                    // Create a class_instance to hold it
                    auto instance = class_def->create_instance();
                    
                    // Store the C++ object in the class_instance as a special field
                    instance->set_field(class_constants::CPP_OBJECT_FIELD, script_value::make_cpp_object(class_name, cpp_obj, engine_ref));
                    
                    // Return the class_instance wrapped in a value
                    return script_value::make_object(class_name, instance, engine_ref);
                } catch (const std::exception& e) {
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
            
            cpp_obj.get()->*member = args[1].as<P>();
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
            
            cpp_obj.get()->*member = args[1].as<P>();
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
    
    // Set base class
    template<typename Base>
    class_builder& base_class() {
        static_assert(std::is_base_of_v<Base, T>, 
                      "Specified type is not a base class of this class");
        
        // Set up inheritance relationship
        auto base_def = engine_.get_class_definition(typeid(Base).name());
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
                        
                        // Access storage directly using friend access
                        auto& storage = v.storage_;
                        auto objHolder = std::get<std::shared_ptr<script_value::object_holder>>(storage);
                        
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
                        
                        auto& cpp_storage = cpp_obj_value.storage_;
                        auto cpp_objHolder = std::get<std::shared_ptr<script_value::object_holder>>(cpp_storage);
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
                        
                        // Access storage directly using friend access
                        auto& storage = v.storage_;
                        auto objHolder = std::get<std::shared_ptr<script_value::object_holder>>(storage);
                        
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
                        
                        auto& cpp_storage = cpp_obj_value.storage_;
                        auto cpp_objHolder = std::get<std::shared_ptr<script_value::object_holder>>(cpp_storage);
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
        return std::make_shared<T>(detail::value_converter<Args>::from(args[Is], eng)...);
    }
    
    // Helper method for calling member functions
    template<typename R, typename... Args, size_t... Is>
    static script_value callMethodImpl(T* obj, R(T::*method)(Args...), const std::vector<script_value>& args, std::index_sequence<Is...>, engine* eng) {
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
        if constexpr (std::is_void_v<R>) {
            (obj->*method)(detail::value_converter<Args>::from(args[Is + 1], eng)...);
            return script_value(std::monostate{}, get_engine_weak_ptr(eng)); // null for void
        } else {
            R result = (obj->*method)(detail::value_converter<Args>::from(args[Is + 1], eng)...);
            return detail::value_converter<R>::to(result, eng);
        }
    }
    
    // Helper method for calling lambdas/callables
    template<typename R, typename ArgsTuple, typename Callable, size_t... Is>
    static script_value callCallableImpl(Callable&& callable, const std::vector<script_value>& args, std::index_sequence<Is...>, engine* eng) {
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
        auto obj_holder = std::get<std::shared_ptr<script_value::object_holder>>(val.storage_);
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
        
        // For other fields, use script_value's copy constructor (which handles deep copy)
        new_instance->set_field(name, value);
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
inline script_value class_instance::get_field(const std::string& name) const {
    auto it = fields_.find(name);
    if (it != fields_.end()) {
        return it->second;
    }
    
    // If not in instance fields, check class definition for default value
    if (auto def = class_definition_.lock()) {
        auto field_defaults = def->get_all_field_defaults();
        auto default_it = field_defaults.find(name);
        if (default_it != field_defaults.end()) {
            return default_it->second.clone();  // Return a copy of the default
        }
    }
    
    throw runtime_error("Field '" + name + "' not found and no default value available"); // null if not found
}

inline bool class_instance::has_field(const std::string& name) const {
    // Only allow fields that exist in the current class definition
    if (auto def = class_definition_.lock()) {
        auto field_defaults = def->get_all_field_defaults();
        return field_defaults.find(name) != field_defaults.end();
    }
    
    // If no class definition, fall back to instance fields
    return fields_.find(name) != fields_.end();
}

inline script_value class_instance::get_method(const std::string& name) const {
    if (auto def = class_definition_.lock()) {
        return def->get_method(name);
    }
    throw runtime_error("Class definition not available for method '" + name + "' lookup"); // null
}

// Define class_instance destructor after class_definition is complete
inline class_instance::~class_instance() {
    // Unregister from class definition when destroyed
    // Instance being destroyed
    if (auto class_def = class_definition_.lock()) {
        class_def->unregister_instance(this);
    }
}

} // namespace jai

// Include script class after class_definition is complete
#include "script_class.hpp"

#endif // __JAISCRIPT_CORE_CLASS_BUILDER_HPP__