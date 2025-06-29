#pragma once

#include "engine.hpp"
#include "value.hpp"
#include "types.hpp"
#include "function_binder.hpp"
#include "../serialization/archive.hpp"
#include <string>
#include <memory>
#include <functional>
#include <vector>
#include <map>
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
    
    // Access control for script classes
    enum class access_level {
        jai_public,
        jai_private,
        jai_protected
    };
    
    // Constructor declaration for script classes  
    struct constructor_declaration {
        std::string class_name;
        std::shared_ptr<std::vector<parameter>> parameters;  // Use pointer to avoid incomplete type
        std::shared_ptr<block_stmt> body;
        
        // Delegation information
        bool is_delegating = false;
        enum delegation_type { none, same_class, base_class } delegation_type = none;
        std::vector<expression_ptr> delegation_args;
        
        constructor_declaration(const std::string& name) : class_name(name) {}
    };
    
    // Method declaration for script classes
    struct method_declaration {
        std::string name;
        std::shared_ptr<std::vector<parameter>> parameters;  // Use pointer to avoid incomplete type
        type_info_ptr return_type;
        std::shared_ptr<block_stmt> body;
        access_level access = access_level::jai_public;
        bool is_override = false;
        bool is_virtual = true;  // All script methods are virtual by default
        
        method_declaration(const std::string& method_name) : name(method_name) {}
    };
    
    // Field declaration for script classes
    struct field_declaration {
        std::string name;
        type_info_ptr type;
        script_value default_value;
        access_level access = access_level::jai_public;
        
        field_declaration(const std::string& field_name, type_info_ptr field_type) 
            : name(field_name), type(field_type) {}
    };
}

namespace jai {

// Forward declarations
class class_definition;
class class_instance;

// Class instance representation in JaiScript
class class_instance {
public:
    class_instance(const std::string& class_name) : class_name_(class_name) {}
    
    // Field access
    void set_field(const std::string& name, const script_value& value) {
        fields_[name] = value;
    }
    
    script_value get_field(const std::string& name) const {
        auto it = fields_.find(name);
        if (it != fields_.end()) {
            return it->second;
        }
        return script_value(); // null if not found
    }
    
    bool has_field(const std::string& name) const {
        return fields_.find(name) != fields_.end();
    }
    
    const std::string& get_class_name() const { return class_name_; }
    
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
        auto cpp_field = get_field("_cpp_object");
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
        return !get_field("_cpp_object").is_null();
    }
    
    // Deep copy this instance
    std::shared_ptr<class_instance> deep_copy() const;
    
private:
    std::string class_name_;
    std::map<std::string, script_value> fields_;
    std::weak_ptr<class_definition> class_definition_;
    
    // Helper to extract C++ object from script_value
    static std::shared_ptr<void> extract_cpp_object_impl(const script_value& val);
};

// Class definition that holds methods and metadata for both C++ and script classes
class class_definition : public std::enable_shared_from_this<class_definition> {
public:
    enum class_type { cpp_class, script_class };
    
    // Constructor for C++ classes (existing)
    class_definition(const std::string& name) : name_(name), class_type_(cpp_class) {}
    
    // Constructor for script classes
    class_definition(const std::string& name, class_type type) : name_(name), class_type_(type) {}
    
    // Get the class type
    class_type get_class_type() const { return class_type_; }
    bool is_script_class() const { return class_type_ == script_class; }
    bool is_cpp_class() const { return class_type_ == cpp_class; }
    
    // Add a method to the class
    void add_method(const std::string& name, script_function func) {
        methods_[name] = script_value::make_function(func);
    }
    
    // Add a field with default value
    void add_field(const std::string& name, const script_value& default_value = script_value()) {
        field_defaults_[name] = default_value;
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
        return script_value();
    }
    
    // Create an instance of this class
    std::shared_ptr<class_instance> create_instance() {
        auto instance = std::make_shared<class_instance>(name_);
        instance->set_class_definition(shared_from_this());
        
        // Initialize fields with defaults
        for (const auto& [field_name, default_value] : field_defaults_) {
            instance->set_field(field_name, default_value);
        }
        
        return instance;
    }
    
    // Set parent class for inheritance
    void set_parent(std::shared_ptr<class_definition> parent) {
        parent_class_ = parent;
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
    
    // Script class specific methods
    void add_script_constructor(const constructor_declaration& constructor) {
        script_constructors_.push_back(constructor);
    }
    
    void add_script_method(const method_declaration& method) {
        script_methods_.push_back(method);
    }
    
    void add_script_field(const field_declaration& field) {
        script_fields_.push_back(field);
        // Also add to field_defaults_ for unified field access
        field_defaults_[field.name] = field.default_value;
    }
    
    const std::vector<constructor_declaration>& get_script_constructors() const {
        return script_constructors_;
    }
    
    const std::vector<method_declaration>& get_script_methods() const {
        return script_methods_;
    }
    
    const std::vector<field_declaration>& get_script_fields() const {
        return script_fields_;
    }
    
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
    
private:
    std::string name_;
    std::map<std::string, script_value> methods_;
    std::map<std::string, script_value> field_defaults_;
    std::shared_ptr<class_definition> parent_class_;
    
    // Script class specific fields
    class_type class_type_;
    std::vector<constructor_declaration> script_constructors_;
    std::vector<method_declaration> script_methods_;
    std::vector<field_declaration> script_fields_;
    access_level default_access_ = access_level::jai_public;
    
    // Mixed inheritance support - for script classes inheriting from C++ classes
    std::shared_ptr<class_definition> cpp_base_class_;
    
    // Copy function for deep copying objects
    copy_function copy_function_;
};

// Builder pattern for registering C++ classes to JaiScript
template<typename T>
class class_builder {
public:
    class_builder(engine& engine, const std::string& class_name) 
        : engine_(engine), class_name_(class_name) {
        class_def_ = std::make_shared<class_definition>(class_name);
        
        // Initialize serialization metadata
        serialization_metadata_.class_name = class_name;
        serialization_metadata_.current_version = 1;
    }
    
    // Add constructor
    template<typename... Args>
    class_builder& constructor() {
        // Register the constructor as an overloaded function
        if constexpr (sizeof...(Args) == 0) {
            // Zero-argument constructor
            engine_.add_overloaded_function(class_name_, 0, [class_def = class_def_, class_name = class_name_](const std::vector<script_value>& args) -> script_value {
                try {
                    // Create the C++ object
                    auto cpp_obj = std::make_shared<T>();
                    
                    // Create a class_instance to hold it
                    auto instance = class_def->create_instance();
                    
                    // Store the C++ object in the class_instance as a special field
                    instance->set_field("_cpp_object", script_value::make_cpp_object(class_name, cpp_obj));
                    
                    // Return the class_instance wrapped in a value
                    return script_value::make_object(class_name, instance);
                } catch (const std::exception& e) {
                    std::cerr << "Error in zero-arg constructor: " << e.what() << std::endl;
                    throw;
                }
            });
        } else {
            // Multi-argument constructor
            engine_.add_overloaded_function(class_name_, sizeof...(Args), [class_def = class_def_, class_name = class_name_](const std::vector<script_value>& args) -> script_value {
                try {
                    // Extract arguments using index-based unpacking
                    auto cpp_obj = class_builder<T>::createObjectImpl<Args...>(args, std::index_sequence_for<Args...>{});
                    
                    // Create a class_instance to hold it
                    auto instance = class_def->create_instance();
                    
                    // Store the C++ object in the class_instance as a special field
                    instance->set_field("_cpp_object", script_value::make_cpp_object(class_name, cpp_obj));
                    
                    // Return the class_instance wrapped in a value
                    return script_value::make_object(class_name, instance);
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
        auto method_func = [method](const std::vector<script_value>& args) -> script_value {
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
            auto cpp_obj_value = instance->get_field("_cpp_object");
            auto cpp_obj = cpp_obj_value.as<std::shared_ptr<T>>();
            
            // Call the method with unpacked arguments
            return class_builder<T>::callMethodImpl(cpp_obj.get(), method, args, std::index_sequence_for<Args...>{});
        };
        
        // Add method to the class definition (for object.method() calls)
        // Methods are stored per-class and accessed through the object instance
        class_def_->add_method(name, method_func);
        
        return *this;
    }
    
    // Add const method binding
    template<typename R, typename... Args>
    class_builder& method(const std::string& name, R(T::*method)(Args...) const) {
        auto method_func = [method](const std::vector<script_value>& args) -> script_value {
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
            auto cpp_obj_value = instance->get_field("_cpp_object");
            auto cpp_obj = cpp_obj_value.as<std::shared_ptr<T>>();
            
            // Call the method with unpacked arguments
            return class_builder<T>::callConstMethodImpl(cpp_obj.get(), method, args, std::index_sequence_for<Args...>{});
        };
        
        // Add method to the class definition (for object.method() calls)
        // Methods are stored per-class and accessed through the object instance
        class_def_->add_method(name, method_func);
        
        return *this;
    }
    
    // Add lambda/callable method binding - ChaiScript style!
    // Supports: .method("setText", [](Button& self, const std::string& text) { self.setText(text); })
    // Note: First parameter should be a reference to match ChaiScript convention
    template<typename Callable>
    class_builder& method(const std::string& name, Callable&& callable) {
        // Use function_traits to determine the signature
        using traits = detail::function_traits<std::decay_t<Callable>>;
        using args_tuple = typename traits::argument_types;
        
        // Check if the first parameter is a reference to T (the self parameter)
        constexpr bool has_self_param = traits::arity > 0 && 
            std::is_same_v<std::tuple_element_t<0, args_tuple>, T&>;
        
        auto method_func = [callable = std::forward<Callable>(callable), has_self_param](const std::vector<script_value>& args) -> script_value {
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
                auto cpp_obj_value = instance->get_field("_cpp_object");
                auto cpp_obj = cpp_obj_value.as<std::shared_ptr<T>>();
                
                // Call the lambda with the C++ object as first argument and remaining args
                return callLambdaWithSelf<typename traits::return_type, args_tuple>(
                    callable, cpp_obj.get(), args, std::make_index_sequence<traits::arity>{});
            } else {
                // Regular lambda without self parameter
                if (args.size() != traits::arity) {
                    throw runtime_error("Method expects " + std::to_string(traits::arity) + 
                                     " arguments, got " + std::to_string(args.size()));
                }
                
                // Call the lambda with unpacked arguments
                return callCallableImpl<typename traits::return_type, args_tuple>(callable, args, std::make_index_sequence<traits::arity>{});
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
        class_def_->add_method("_serialize_construct", [constructor = std::forward<constructor_func>(constructor), class_def = class_def_, class_name = class_name_](const std::vector<script_value>& args) -> script_value {
            if (args.size() != 1) {
                throw runtime_error("Serialization constructor expects exactly one argument (the serialized data)");
            }
            
            // Call the custom constructor with the serialized data
            T instance = constructor(args[0]);
            
            // Create a class_instance to hold it
            auto class_instance = class_def->create_instance();
            class_instance->set_field("_cpp_object", script_value::make_cpp_object(class_name, 
                std::make_shared<T>(std::move(instance))));
            
            return script_value::make_object(class_name, class_instance);
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
        class_def_->add_field(name, script_value()); // Register field name
        
        // Register serialization metadata
        serialization::property_metadata prop_meta;
        prop_meta.name = name;
        prop_meta.type = type_info::make<P>();
        prop_meta.version_added = 1; // Default to version 1
        serialization_metadata_.properties.push_back(prop_meta);
        
        // Add a special method that handles property access
        // The interpreter's visitMemberExpr will need to check for these
        class_def_->add_method("_get_" + name, [member](const std::vector<script_value>& args) -> script_value {
            if (args.empty()) {
                throw runtime_error("Property getter called without 'this' object");
            }
            
            // Extract the class_instance from the first argument (this)
            auto instance = args[0].as<std::shared_ptr<class_instance>>();
            
            // Get the C++ object from the special field
            auto cpp_obj_value = instance->get_field("_cpp_object");
            auto cpp_obj = cpp_obj_value.as<std::shared_ptr<T>>();
            
            return detail::value_converter<P>::to(cpp_obj.get()->*member);
        });
        
        class_def_->add_method("_set_" + name, [member](const std::vector<script_value>& args) -> script_value {
            if (args.size() < 2) {
                throw runtime_error("Property setter requires 'this' and value");
            }
            
            // Extract the class_instance from the first argument (this)
            auto instance = args[0].as<std::shared_ptr<class_instance>>();
            
            // Get the C++ object from the special field
            auto cpp_obj_value = instance->get_field("_cpp_object");
            auto cpp_obj = cpp_obj_value.as<std::shared_ptr<T>>();
            
            cpp_obj.get()->*member = args[1].as<P>();
            return script_value(); // null
        });
        
        // Also add traditional getter/setter methods for compatibility
        std::string getterName = "get" + name;
        getterName[3] = std::toupper(getterName[3]); // Capitalize first letter
        
        class_def_->add_method(getterName, [member, class_name = class_name_](const std::vector<script_value>& args) -> script_value {
            if (args.empty()) {
                throw runtime_error("Getter called without 'this' object");
            }
            
            // Extract the class_instance from the first argument (this)
            auto instance = args[0].as<std::shared_ptr<class_instance>>();
            
            // Get the C++ object from the special field
            auto cpp_obj_value = instance->get_field("_cpp_object");
            auto cpp_obj = cpp_obj_value.as<std::shared_ptr<T>>();
            
            return detail::value_converter<P>::to(cpp_obj.get()->*member);
        });
        
        // Add setter
        std::string setterName = "set" + name;
        setterName[3] = std::toupper(setterName[3]); // Capitalize first letter
        
        class_def_->add_method(setterName, [member, class_name = class_name_](const std::vector<script_value>& args) -> script_value {
            if (args.size() < 2) {
                throw runtime_error("Setter requires 'this' and value");
            }
            
            // Extract the class_instance from the first argument (this)
            auto instance = args[0].as<std::shared_ptr<class_instance>>();
            
            // Get the C++ object from the special field
            auto cpp_obj_value = instance->get_field("_cpp_object");
            auto cpp_obj = cpp_obj_value.as<std::shared_ptr<T>>();
            
            cpp_obj.get()->*member = args[1].as<P>();
            return script_value(); // null
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
        
        // Register serialization metadata
        serialization::serialization_registry::instance().register_class(class_name_, serialization_metadata_);
        
        // Register converters for this type (only for concrete types)
        // This allows functions returning T to automatically convert to value
        if constexpr (!std::is_abstract_v<T>) {
            // Register a custom converter that creates class_instance objects
            engine_.register_type_converterImpl(typeid(T).name(), 
                [class_def = class_def_, class_name = class_name_](const void* obj) -> script_value {
                    // Create a class_instance using the class definition
                    // This properly initializes all fields with their defaults
                    auto instance = class_def->create_instance();
                    
                    // Create a shared_ptr to the C++ object (by copying)
                    auto cpp_obj = std::make_shared<T>(*static_cast<const T*>(obj));
                    
                    // Store the C++ object in the class_instance
                    instance->set_field("_cpp_object", script_value::make_cpp_object(class_name, 
                        std::static_pointer_cast<void>(cpp_obj)));
                    
                    // Return the class_instance wrapped in a value
                    return script_value::make_object(class_name, 
                        std::static_pointer_cast<void>(instance));
                });
        }
        
    }
    
private:
    engine& engine_;
    std::string class_name_;
    std::shared_ptr<class_definition> class_def_;
    serialization::class_metadata serialization_metadata_;
    bool has_base_class_ = false;
    std::type_index base_type_index_ = std::type_index(typeid(void));
    
    // Helper method for creating objects with arguments
    template<typename... Args, size_t... Is>
    static std::shared_ptr<T> createObjectImpl(const std::vector<script_value>& args, std::index_sequence<Is...>) {
        return std::make_shared<T>(detail::value_converter<Args>::from(args[Is])...);
    }
    
    // Helper method for calling member functions
    template<typename R, typename... Args, size_t... Is>
    static script_value callMethodImpl(T* obj, R(T::*method)(Args...), const std::vector<script_value>& args, std::index_sequence<Is...>) {
        if constexpr (std::is_void_v<R>) {
            (obj->*method)(detail::value_converter<Args>::from(args[Is + 1])...);
            return script_value(); // null for void
        } else {
            R result = (obj->*method)(detail::value_converter<Args>::from(args[Is + 1])...);
            return detail::value_converter<R>::to(result);
        }
    }
    
    // Helper method for calling const member functions
    template<typename R, typename... Args, size_t... Is>
    static script_value callConstMethodImpl(const T* obj, R(T::*method)(Args...) const, const std::vector<script_value>& args, std::index_sequence<Is...>) {
        if constexpr (std::is_void_v<R>) {
            (obj->*method)(detail::value_converter<Args>::from(args[Is + 1])...);
            return script_value(); // null for void
        } else {
            R result = (obj->*method)(detail::value_converter<Args>::from(args[Is + 1])...);
            return detail::value_converter<R>::to(result);
        }
    }
    
    // Helper method for calling lambdas/callables
    template<typename R, typename ArgsTuple, typename Callable, size_t... Is>
    static script_value callCallableImpl(Callable&& callable, const std::vector<script_value>& args, std::index_sequence<Is...>) {
        if constexpr (std::is_void_v<R>) {
            callable(detail::value_converter<std::tuple_element_t<Is, ArgsTuple>>::from(args[Is])...);
            return script_value(); // null for void
        } else {
            R result = callable(detail::value_converter<std::tuple_element_t<Is, ArgsTuple>>::from(args[Is])...);
            return detail::value_converter<R>::to(result);
        }
    }
    
    // Helper method for calling lambdas with self parameter
    template<typename R, typename ArgsTuple, typename Callable, size_t... Is>
    static script_value callLambdaWithSelf(Callable&& callable, T* self, const std::vector<script_value>& args, std::index_sequence<Is...>) {
        // We need to call the lambda with:
        // - self as the first argument
        // - remaining args starting from args[1] mapped to tuple indices 1, 2, 3...
        return callLambdaWithSelfImpl<R, ArgsTuple, Callable>(
            std::forward<Callable>(callable), self, args, 
            std::make_index_sequence<sizeof...(Is) - 1>{}
        );
    }
    
    // implementation helper that correctly maps arguments
    template<typename R, typename ArgsTuple, typename Callable, size_t... Is>
    static script_value callLambdaWithSelfImpl(Callable&& callable, T* self, const std::vector<script_value>& args, std::index_sequence<Is...>) {
        if constexpr (std::is_void_v<R>) {
            // Call with self as first argument, then args[1], args[2], etc.
            callable(*self, detail::value_converter<std::tuple_element_t<Is + 1, ArgsTuple>>::from(args[Is + 1])...);
            return script_value(); // null for void
        } else {
            // For return type R&, we need special handling for method chaining
            if constexpr (std::is_reference_v<R> && std::is_same_v<std::remove_reference_t<R>, T>) {
                // Method returns T&, so we should return the original 'this' script_value for chaining
                callable(*self, detail::value_converter<std::tuple_element_t<Is + 1, ArgsTuple>>::from(args[Is + 1])...);
                return args[0]; // Return the original 'this' for chaining
            } else {
                R result = callable(*self, detail::value_converter<std::tuple_element_t<Is + 1, ArgsTuple>>::from(args[Is + 1])...);
                return detail::value_converter<R>::to(result);
            }
        }
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

// Helper function to create a class_builder for C++ classes
template<typename T>
class_builder<T> make_class_builder(engine& engine, const std::string& className) {
    // Extract base template name if this is a templated type
    std::string baseTemplateName = extract_base_template_name(className);
    
    // Register the base template name if it contains template syntax
    if (baseTemplateName != className) {
        engine.register_template_type(baseTemplateName);
    }
    
    return class_builder<T>(engine, className);
}

// Helper function to create a script class definition
inline std::shared_ptr<class_definition> make_script_class_definition(const std::string& class_name) {
    return std::make_shared<class_definition>(class_name, class_definition::script_class);
}

// implementation of class_instance methods (must be after class_definition)
inline script_value class_instance::get_method(const std::string& name) const {
    if (auto class_def = class_definition_.lock()) {
        return class_def->get_method(name);
    }
    return script_value(); // null if no class definition
}

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
    if (val.type() == value_type::jai_object_type) {
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
        if (name == "_cpp_object" && !value.is_null()) {
            // Get the class definition to access copy function
            if (auto class_def = class_definition_.lock()) {
                if (class_def->has_copy_function()) {
                    // Extract the C++ object
                    auto cpp_obj = extract_cpp_object_impl(value);
                    if (cpp_obj) {
                        // Use the copy function to create a new C++ object
                        auto new_cpp_obj = class_def->copy_object(cpp_obj.get());
                        
                        // Wrap in a new script_value
                        new_instance->set_field("_cpp_object", 
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
    
    return new_instance;
}

} // namespace jai