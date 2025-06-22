#pragma once

#include "engine.hpp"
#include "value.hpp"
#include "types.hpp"
#include "function_binder.hpp"
#include <string>
#include <memory>
#include <functional>
#include <vector>
#include <map>
#include <type_traits>
#include <iostream>

namespace JaiScript {

// Forward declarations
class ClassDefinition;
class ClassInstance;

// Class instance representation in JaiScript
class ClassInstance {
public:
    ClassInstance(const std::string& className) : className_(className) {}
    
    // Field access
    void setField(const std::string& name, const Value& value) {
        fields_[name] = value;
    }
    
    Value getField(const std::string& name) const {
        auto it = fields_.find(name);
        if (it != fields_.end()) {
            return it->second;
        }
        return Value(); // null if not found
    }
    
    bool hasField(const std::string& name) const {
        return fields_.find(name) != fields_.end();
    }
    
    const std::string& getClassName() const { return className_; }
    
    // Get method from class definition
    Value getMethod(const std::string& name) const;
    
    // Set the class definition this instance belongs to
    void setClassDefinition(std::shared_ptr<ClassDefinition> classDef) {
        classDefinition_ = classDef;
    }
    
private:
    std::string className_;
    std::map<std::string, Value> fields_;
    std::weak_ptr<ClassDefinition> classDefinition_;
};

// Class definition that holds methods and metadata
class ClassDefinition : public std::enable_shared_from_this<ClassDefinition> {
public:
    ClassDefinition(const std::string& name) : name_(name) {}
    
    // Add a method to the class
    void addMethod(const std::string& name, ScriptFunction func) {
        methods_[name] = Value::makeFunction(func);
    }
    
    // Add a field with default value
    void addField(const std::string& name, const Value& defaultValue = Value()) {
        fieldDefaults_[name] = defaultValue;
    }
    
    // Get a method
    Value getMethod(const std::string& name) const {
        auto it = methods_.find(name);
        if (it != methods_.end()) {
            return it->second;
        }
        // Check parent class if we have inheritance
        if (parentClass_) {
            return parentClass_->getMethod(name);
        }
        return Value();
    }
    
    // Create an instance of this class
    std::shared_ptr<ClassInstance> createInstance() {
        auto instance = std::make_shared<ClassInstance>(name_);
        instance->setClassDefinition(shared_from_this());
        
        // Initialize fields with defaults
        for (const auto& [fieldName, defaultValue] : fieldDefaults_) {
            instance->setField(fieldName, defaultValue);
        }
        
        return instance;
    }
    
    // Set parent class for inheritance
    void setParent(std::shared_ptr<ClassDefinition> parent) {
        parentClass_ = parent;
    }
    
    const std::string& getName() const { return name_; }
    
private:
    std::string name_;
    std::map<std::string, Value> methods_;
    std::map<std::string, Value> fieldDefaults_;
    std::shared_ptr<ClassDefinition> parentClass_;
};

// Builder pattern for registering C++ classes to JaiScript
template<typename T>
class ClassBuilder {
public:
    ClassBuilder(Engine& engine, const std::string& className) 
        : engine_(engine), className_(className) {
        classDef_ = std::make_shared<ClassDefinition>(className);
    }
    
    // Add constructor
    template<typename... Args>
    ClassBuilder& constructor() {
        // Register the constructor as an overloaded function
        if constexpr (sizeof...(Args) == 0) {
            // Zero-argument constructor
            engine_.addOverloadedFunction(className_, 0, [this, className = className_](const std::vector<Value>& args) -> Value {
                try {
                    // Create the C++ object
                    auto cppObj = std::make_shared<T>();
                    
                    // Create a ClassInstance to hold it
                    auto instance = classDef_->createInstance();
                    
                    // Store the C++ object in the ClassInstance as a special field
                    instance->setField("__cpp_object", Value::makeObject(className + "_cpp", cppObj));
                    
                    // Return the ClassInstance wrapped in a Value
                    return Value::makeObject(className, instance);
                } catch (const std::exception& e) {
                    std::cerr << "Error in zero-arg constructor: " << e.what() << std::endl;
                    throw;
                }
            });
        } else {
            // Multi-argument constructor
            engine_.addOverloadedFunction(className_, sizeof...(Args), [this, className = className_](const std::vector<Value>& args) -> Value {
                try {
                    // Extract arguments using index-based unpacking
                    auto cppObj = createObjectImpl<Args...>(args, std::index_sequence_for<Args...>{});
                    
                    // Create a ClassInstance to hold it
                    auto instance = classDef_->createInstance();
                    
                    // Store the C++ object in the ClassInstance as a special field
                    instance->setField("__cpp_object", Value::makeObject(className + "_cpp", cppObj));
                    
                    // Return the ClassInstance wrapped in a Value
                    return Value::makeObject(className, instance);
                } catch (const std::exception& e) {
                    std::cerr << "Error in multi-arg constructor: " << e.what() << std::endl;
                    throw;
                }
            });
        }
        
        return *this;
    }
    
    // Add method binding - member function pointer version
    template<typename R, typename... Args>
    ClassBuilder& method(const std::string& name, R(T::*method)(Args...)) {
        classDef_->addMethod(name, [method](const std::vector<Value>& args) -> Value {
            if (args.empty()) {
                throw RuntimeError("Method called without 'this' object");
            }
            
            // Validate argument count (first arg is 'this', so we need sizeof...(Args) + 1 total)
            if (args.size() != sizeof...(Args) + 1) {
                throw RuntimeError("Method expects " + std::to_string(sizeof...(Args)) + 
                                 " arguments, got " + std::to_string(args.size() - 1));
            }
            
            // Extract the ClassInstance from the first argument (this)
            auto instance = args[0].as<std::shared_ptr<ClassInstance>>();
            
            // Get the C++ object from the special field
            auto cppObjValue = instance->getField("__cpp_object");
            auto cppObj = cppObjValue.as<std::shared_ptr<T>>();
            
            // Call the method with unpacked arguments
            return ClassBuilder<T>::callMethodImpl(cppObj.get(), method, args, std::index_sequence_for<Args...>{});
        });
        
        return *this;
    }
    
    // Add const method binding
    template<typename R, typename... Args>
    ClassBuilder& method(const std::string& name, R(T::*method)(Args...) const) {
        classDef_->addMethod(name, [method](const std::vector<Value>& args) -> Value {
            if (args.empty()) {
                throw RuntimeError("Method called without 'this' object");
            }
            
            if (args.size() != sizeof...(Args) + 1) {
                throw RuntimeError("Method expects " + std::to_string(sizeof...(Args)) + 
                                 " arguments, got " + std::to_string(args.size() - 1));
            }
            
            // Extract the ClassInstance from the first argument (this)
            auto instance = args[0].as<std::shared_ptr<ClassInstance>>();
            
            // Get the C++ object from the special field
            auto cppObjValue = instance->getField("__cpp_object");
            auto cppObj = cppObjValue.as<std::shared_ptr<T>>();
            
            return ClassBuilder<T>::callConstMethodImpl(cppObj.get(), method, args, std::index_sequence_for<Args...>{});
        });
        
        return *this;
    }
    
    // Add lambda/callable method binding - ChaiScript style!
    // Supports: .method("setText", [](Button& self, const std::string& text) { self.setText(text); })
    // Note: First parameter should be a reference to match ChaiScript convention
    template<typename Callable>
    ClassBuilder& method(const std::string& name, Callable&& callable) {
        // Use function_traits to determine the signature
        using traits = detail::function_traits<std::decay_t<Callable>>;
        using args_tuple = typename traits::argument_types;
        
        // Check if the first parameter is a reference to T (the self parameter)
        constexpr bool has_self_param = traits::arity > 0 && 
            std::is_same_v<std::tuple_element_t<0, args_tuple>, T&>;
        
        classDef_->addMethod(name, [callable = std::forward<Callable>(callable), has_self_param](const std::vector<Value>& args) -> Value {
            if (has_self_param) {
                // Lambda expects T& as first parameter, we need to extract it from args[0]
                // args[0] is the ClassInstance, remaining args are the actual parameters
                if (args.empty()) {
                    throw RuntimeError("Method called without 'this' object");
                }
                
                // Expected argument count is arity - 1 (excluding self) + 1 (for 'this')
                if (args.size() != traits::arity) {
                    throw RuntimeError("Method expects " + std::to_string(traits::arity - 1) + 
                                     " arguments, got " + std::to_string(args.size() - 1));
                }
                
                // Extract the C++ object from the ClassInstance
                auto instance = args[0].as<std::shared_ptr<ClassInstance>>();
                auto cppObjValue = instance->getField("__cpp_object");
                auto cppObj = cppObjValue.as<std::shared_ptr<T>>();
                
                // Call the lambda with the C++ object as first argument and remaining args
                return callLambdaWithSelf<typename traits::return_type, args_tuple>(
                    callable, cppObj.get(), args, std::make_index_sequence<traits::arity>{});
            } else {
                // Regular lambda without self parameter
                if (args.size() != traits::arity) {
                    throw RuntimeError("Method expects " + std::to_string(traits::arity) + 
                                     " arguments, got " + std::to_string(args.size()));
                }
                
                // Call the lambda with unpacked arguments
                return callCallableImpl<typename traits::return_type, args_tuple>(callable, args, std::make_index_sequence<traits::arity>{});
            }
        });
        
        return *this;
    }
    
    // Add property/field binding
    template<typename P>
    ClassBuilder& property(const std::string& name, P T::*member) {
        // Register the property as a special field that knows how to access the C++ member
        // We'll store a lambda that can get/set the value
        classDef_->addField(name, Value()); // Register field name
        
        // Add a special method that handles property access
        // The interpreter's visitMemberExpr will need to check for these
        classDef_->addMethod("__get_" + name, [member](const std::vector<Value>& args) -> Value {
            if (args.empty()) {
                throw RuntimeError("Property getter called without 'this' object");
            }
            
            // Extract the ClassInstance from the first argument (this)
            auto instance = args[0].as<std::shared_ptr<ClassInstance>>();
            
            // Get the C++ object from the special field
            auto cppObjValue = instance->getField("__cpp_object");
            auto cppObj = cppObjValue.as<std::shared_ptr<T>>();
            
            return detail::ValueConverter<P>::to(cppObj.get()->*member);
        });
        
        classDef_->addMethod("__set_" + name, [member](const std::vector<Value>& args) -> Value {
            if (args.size() < 2) {
                throw RuntimeError("Property setter requires 'this' and value");
            }
            
            // Extract the ClassInstance from the first argument (this)
            auto instance = args[0].as<std::shared_ptr<ClassInstance>>();
            
            // Get the C++ object from the special field
            auto cppObjValue = instance->getField("__cpp_object");
            auto cppObj = cppObjValue.as<std::shared_ptr<T>>();
            
            cppObj.get()->*member = args[1].as<P>();
            return Value(); // null
        });
        
        // Also add traditional getter/setter methods for compatibility
        std::string getterName = "get" + name;
        getterName[3] = std::toupper(getterName[3]); // Capitalize first letter
        
        classDef_->addMethod(getterName, [member, className = className_](const std::vector<Value>& args) -> Value {
            if (args.empty()) {
                throw RuntimeError("Getter called without 'this' object");
            }
            
            // Extract the ClassInstance from the first argument (this)
            auto instance = args[0].as<std::shared_ptr<ClassInstance>>();
            
            // Get the C++ object from the special field
            auto cppObjValue = instance->getField("__cpp_object");
            auto cppObj = cppObjValue.as<std::shared_ptr<T>>();
            
            return detail::ValueConverter<P>::to(cppObj.get()->*member);
        });
        
        // Add setter
        std::string setterName = "set" + name;
        setterName[3] = std::toupper(setterName[3]); // Capitalize first letter
        
        classDef_->addMethod(setterName, [member, className = className_](const std::vector<Value>& args) -> Value {
            if (args.size() < 2) {
                throw RuntimeError("Setter requires 'this' and value");
            }
            
            // Extract the ClassInstance from the first argument (this)
            auto instance = args[0].as<std::shared_ptr<ClassInstance>>();
            
            // Get the C++ object from the special field
            auto cppObjValue = instance->getField("__cpp_object");
            auto cppObj = cppObjValue.as<std::shared_ptr<T>>();
            
            cppObj.get()->*member = args[1].as<P>();
            return Value(); // null
        });
        
        return *this;
    }
    
    // Set base class
    template<typename Base>
    ClassBuilder& inherits() {
        // For C++ inheritance to work properly, we need to ensure that
        // methods on the base class can be called on derived instances.
        // Since we're dealing with C++ objects stored as shared_ptr<T>,
        // we need to register the base class methods on this class too.
        
        // Note: This is a simplified approach. A full implementation would
        // need to look up the base class definition from the engine.
        // For now, we'll rely on the fact that virtual methods will work
        // through the C++ vtable when we have a pointer to the derived class.
        
        return *this;
    }
    
    // Add explicit type conversion support - general purpose
    template<typename From, typename To>
    ClassBuilder& addTypeConversion(std::function<To(const From&)> converter) {
        // Register the conversion with the engine
        // This would need to be implemented in the engine's type system
        // Usage: .addTypeConversion<SafeComponent<Button>, std::shared_ptr<Button>>([](const auto& item) { return item.self(); })
        return *this;
    }
    
    // Finalize registration
    void build() {
        engine_.addClass<T>(className_, classDef_);
        
        // Register converters for this type (only for concrete types)
        // This allows functions returning T to automatically convert to Value
        if constexpr (!std::is_abstract_v<T>) {
            engine_.registerTypeConverter<T>(className_);
        }
        
    }
    
private:
    Engine& engine_;
    std::string className_;
    std::shared_ptr<ClassDefinition> classDef_;
    
    // Helper method for creating objects with arguments
    template<typename... Args, size_t... Is>
    std::shared_ptr<T> createObjectImpl(const std::vector<Value>& args, std::index_sequence<Is...>) {
        return std::make_shared<T>(args[Is].as<Args>()...);
    }
    
    // Helper method for calling member functions
    template<typename R, typename... Args, size_t... Is>
    static Value callMethodImpl(T* obj, R(T::*method)(Args...), const std::vector<Value>& args, std::index_sequence<Is...>) {
        if constexpr (std::is_void_v<R>) {
            (obj->*method)(detail::ValueConverter<Args>::from(args[Is + 1])...);
            return Value(); // null for void
        } else {
            R result = (obj->*method)(detail::ValueConverter<Args>::from(args[Is + 1])...);
            return detail::ValueConverter<R>::to(result);
        }
    }
    
    // Helper method for calling const member functions
    template<typename R, typename... Args, size_t... Is>
    static Value callConstMethodImpl(const T* obj, R(T::*method)(Args...) const, const std::vector<Value>& args, std::index_sequence<Is...>) {
        if constexpr (std::is_void_v<R>) {
            (obj->*method)(detail::ValueConverter<Args>::from(args[Is + 1])...);
            return Value(); // null for void
        } else {
            R result = (obj->*method)(detail::ValueConverter<Args>::from(args[Is + 1])...);
            return detail::ValueConverter<R>::to(result);
        }
    }
    
    // Helper method for calling lambdas/callables
    template<typename R, typename ArgsTuple, typename Callable, size_t... Is>
    static Value callCallableImpl(Callable&& callable, const std::vector<Value>& args, std::index_sequence<Is...>) {
        if constexpr (std::is_void_v<R>) {
            callable(detail::ValueConverter<std::tuple_element_t<Is, ArgsTuple>>::from(args[Is])...);
            return Value(); // null for void
        } else {
            R result = callable(detail::ValueConverter<std::tuple_element_t<Is, ArgsTuple>>::from(args[Is])...);
            return detail::ValueConverter<R>::to(result);
        }
    }
    
    // Helper method for calling lambdas with self parameter
    template<typename R, typename ArgsTuple, typename Callable, size_t... Is>
    static Value callLambdaWithSelf(Callable&& callable, T* self, const std::vector<Value>& args, std::index_sequence<Is...>) {
        // We need to call the lambda with:
        // - self as the first argument
        // - remaining args starting from args[1] mapped to tuple indices 1, 2, 3...
        return callLambdaWithSelfImpl<R, ArgsTuple, Callable>(
            std::forward<Callable>(callable), self, args, 
            std::make_index_sequence<sizeof...(Is) - 1>{}
        );
    }
    
    // Implementation helper that correctly maps arguments
    template<typename R, typename ArgsTuple, typename Callable, size_t... Is>
    static Value callLambdaWithSelfImpl(Callable&& callable, T* self, const std::vector<Value>& args, std::index_sequence<Is...>) {
        if constexpr (std::is_void_v<R>) {
            // Call with self as first argument, then args[1], args[2], etc.
            callable(*self, detail::ValueConverter<std::tuple_element_t<Is + 1, ArgsTuple>>::from(args[Is + 1])...);
            return Value(); // null for void
        } else {
            // For return type R&, we need special handling for method chaining
            if constexpr (std::is_reference_v<R> && std::is_same_v<std::remove_reference_t<R>, T>) {
                // Method returns T&, so we should return the original 'this' Value for chaining
                callable(*self, detail::ValueConverter<std::tuple_element_t<Is + 1, ArgsTuple>>::from(args[Is + 1])...);
                return args[0]; // Return the original 'this' for chaining
            } else {
                R result = callable(*self, detail::ValueConverter<std::tuple_element_t<Is + 1, ArgsTuple>>::from(args[Is + 1])...);
                return detail::ValueConverter<R>::to(result);
            }
        }
    }
    
};

// Helper function to create a ClassBuilder
template<typename T>
ClassBuilder<T> makeClassBuilder(Engine& engine, const std::string& className) {
    return ClassBuilder<T>(engine, className);
}

// Implementation of ClassInstance::getMethod
inline Value ClassInstance::getMethod(const std::string& name) const {
    if (auto classDef = classDefinition_.lock()) {
        return classDef->getMethod(name);
    }
    return Value(); // null if no class definition
}

} // namespace JaiScript