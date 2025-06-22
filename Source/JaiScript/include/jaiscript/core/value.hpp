#pragma once

#include "types.hpp"
#include "type_info.hpp"
#include "../jaiscript_fwd.hpp"
#include <variant>
#include <memory>
#include <compare>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace JaiScript {

    // Helper trait to detect shared_ptr specializations
    template<typename T, template<typename...> class Template>
    struct is_specialization : std::false_type {};
    
    template<template<typename...> class Template, typename... Args>
    struct is_specialization<Template<Args...>, Template> : std::true_type {};
    
    template<typename T, template<typename...> class Template>
    inline constexpr bool is_specialization_v = is_specialization<T, Template>::value;

    class Value {
    public:
        // Constructors for each type
        Value() : typeInfo_(nullptr), storage_(std::monostate{}) {
            // Null value
        }
        Value(std::nullptr_t) : Value() {}
        Value(Int i) : typeInfo_(TypeInfo::makeInt()), storage_(i) {}
        Value(Float f) : typeInfo_(TypeInfo::makeFloat()), storage_(f) {}
        Value(const String& s) : typeInfo_(TypeInfo::makeString()), storage_(s) {}
        Value(String&& s) : typeInfo_(TypeInfo::makeString()), storage_(std::move(s)) {}
        Value(const char* s) : typeInfo_(TypeInfo::makeString()), storage_(String(s)) {}
        Value(Char c) : typeInfo_(TypeInfo::makeChar()), storage_(c) {}
        Value(Bool b) : typeInfo_(TypeInfo::makeBool()), storage_(b) {}
        
        // Move constructor and assignment operator for performance
        Value(Value&& other) noexcept 
            : typeInfo_(std::move(other.typeInfo_)), storage_(std::move(other.storage_)) {
            // Reset the moved-from object to a valid null state
            other.typeInfo_ = nullptr;
            other.storage_ = std::monostate{};
        }
        
        Value& operator=(Value&& other) noexcept {
            if (this != &other) {
                typeInfo_ = std::move(other.typeInfo_);
                storage_ = std::move(other.storage_);
                // Reset the moved-from object to a valid null state
                other.typeInfo_ = nullptr;
                other.storage_ = std::monostate{};
            }
            return *this;
        }
        
        // Copy constructor and assignment operator (explicitly defaulted for clarity)
        Value(const Value& other) = default;
        Value& operator=(const Value& other) = default;
        
        // Factory methods for complex types
        static Value makeArray(TypeInfoPtr elementType);
        static Value makeMap(TypeInfoPtr keyType, TypeInfoPtr valueType);
        static Value makeObject(const std::string& typeName, std::shared_ptr<void> data);
        static Value makeSharedPtr(const Value& value);
        static Value makeWeakPtr(const Value& value);
        static Value makeReference(Value& target);
        static Value makeFunction(const ScriptFunction& func);
        
        // Type information
        TypeInfoPtr getTypeInfo() const { return typeInfo_; }
        ValueType type() const { return typeInfo_ ? typeInfo_->baseType : ValueType::Null; }
        bool isNull() const { return type() == ValueType::Null; }
        bool isInt() const { return type() == ValueType::Int; }
        bool isFloat() const { return type() == ValueType::Float; }
        bool isString() const { return type() == ValueType::String; }
        bool isChar() const { return type() == ValueType::Char; }
        bool isBool() const { return type() == ValueType::Bool; }
        bool isArray() const { return type() == ValueType::Array; }
        bool isMap() const { return type() == ValueType::Map; }
        bool isObject() const { return type() == ValueType::Object; }
        bool isFunction() const { return type() == ValueType::Function; }
        bool isReference() const { return type() == ValueType::Reference; }
        bool isSharedPtr() const { return type() == ValueType::SharedPtr; }
        bool isWeakPtr() const { return type() == ValueType::WeakPtr; }
        
        // Value extraction (inlined for performance)
        inline Int asInt() const {
            if (type() != ValueType::Int) {
                throw RuntimeError("Value is not an integer");
            }
            return std::get<Int>(storage_);
        }
        
        inline Float asFloat() const {
            if (type() != ValueType::Float) {
                throw RuntimeError("Value is not a float");
            }
            return std::get<Float>(storage_);
        }
        
        inline const String& asString() const {
            if (type() != ValueType::String) {
                throw RuntimeError("Value is not a string");
            }
            return std::get<String>(storage_);
        }
        
        inline Bool asBool() const {
            if (type() != ValueType::Bool) {
                throw RuntimeError("Value is not a boolean");
            }
            return std::get<Bool>(storage_);
        }
        
        inline Char asChar() const {
            if (type() != ValueType::Char) {
                throw RuntimeError("Value is not a character");
            }
            return std::get<Char>(storage_);
        }
        
        inline const std::vector<Value>& asArray() const {
            if (type() != ValueType::Array) {
                throw RuntimeError("Value is not an array");
            }
            return *std::get<std::shared_ptr<std::vector<Value>>>(storage_);
        }
        
        inline const std::map<Value, Value>& asMap() const {
            if (type() != ValueType::Map) {
                throw RuntimeError("Value is not a map");
            }
            return *std::get<std::shared_ptr<std::map<Value, Value>>>(storage_);
        }
        
        const ScriptFunction& asFunction() const;
        
        // Generic extraction with type checking
        template<typename T>
        T as() const {
            if constexpr (std::is_same_v<T, Int>) {
                return asInt();
            } else if constexpr (std::is_same_v<T, Float>) {
                return asFloat();
            } else if constexpr (std::is_same_v<T, String>) {
                return asString();
            } else if constexpr (std::is_same_v<T, Char>) {
                return asChar();
            } else if constexpr (std::is_same_v<T, Bool>) {
                return asBool();
            }
            // Allow common C++ type conversions with bounds checking
            else if constexpr (std::is_same_v<T, int>) {
                Int val = asInt();
                if (val < std::numeric_limits<int>::min() || val > std::numeric_limits<int>::max()) {
                    throw RuntimeError("Integer value out of range for int");
                }
                return static_cast<int>(val);
            } else if constexpr (std::is_same_v<T, int8_t>) {
                Int val = asInt();
                if (val < std::numeric_limits<int8_t>::min() || val > std::numeric_limits<int8_t>::max()) {
                    throw RuntimeError("Integer value out of range for int8_t");
                }
                return static_cast<int8_t>(val);
            } else if constexpr (std::is_same_v<T, int16_t>) {
                Int val = asInt();
                if (val < std::numeric_limits<int16_t>::min() || val > std::numeric_limits<int16_t>::max()) {
                    throw RuntimeError("Integer value out of range for int16_t");
                }
                return static_cast<int16_t>(val);
            } else if constexpr (std::is_same_v<T, int32_t>) {
                Int val = asInt();
                if (val < std::numeric_limits<int32_t>::min() || val > std::numeric_limits<int32_t>::max()) {
                    throw RuntimeError("Integer value out of range for int32_t");
                }
                return static_cast<int32_t>(val);
            } else if constexpr (std::is_same_v<T, int64_t>) {
                return static_cast<int64_t>(asInt());  // No bounds check needed, same size
            } else if constexpr (std::is_same_v<T, uint8_t>) {
                Int val = asInt();
                if (val < 0 || val > std::numeric_limits<uint8_t>::max()) {
                    throw RuntimeError("Integer value out of range for uint8_t (must be 0-255)");
                }
                return static_cast<uint8_t>(val);
            } else if constexpr (std::is_same_v<T, uint16_t>) {
                Int val = asInt();
                if (val < 0 || val > std::numeric_limits<uint16_t>::max()) {
                    throw RuntimeError("Integer value out of range for uint16_t (must be non-negative)");
                }
                return static_cast<uint16_t>(val);
            } else if constexpr (std::is_same_v<T, uint32_t>) {
                Int val = asInt();
                if (val < 0 || val > std::numeric_limits<uint32_t>::max()) {
                    throw RuntimeError("Integer value out of range for uint32_t (must be non-negative)");
                }
                return static_cast<uint32_t>(val);
            } else if constexpr (std::is_same_v<T, uint64_t>) {
                Int val = asInt();
                if (val < 0) {
                    throw RuntimeError("Integer value must be non-negative for uint64_t");
                }
                return static_cast<uint64_t>(val);
            } else if constexpr (std::is_same_v<T, size_t>) {
                Int val = asInt();
                if (val < 0) {
                    throw RuntimeError("Integer value must be non-negative for size_t");
                }
                return static_cast<size_t>(val);
            } else if constexpr (std::is_same_v<T, float>) {
                return static_cast<float>(asFloat());
            } else if constexpr (std::is_same_v<T, double>) {
                return asFloat();  // Float is already double
            } else if constexpr (std::is_same_v<T, std::string>) {
                return asString();  // String is already std::string
            }
            // Support for shared_ptr extraction from objects
            else if constexpr (std::is_same_v<T, std::shared_ptr<ClassInstance>>) {
                if (type() != ValueType::Object) {
                    throw RuntimeError("Value is not an object");
                }
                auto objHolder = std::get<std::shared_ptr<ObjectHolder>>(storage_);
                return std::static_pointer_cast<ClassInstance>(objHolder->data);
            }
            // Support for shared_ptr<void> extraction
            else if constexpr (std::is_same_v<T, std::shared_ptr<void>>) {
                if (type() != ValueType::Object) {
                    throw RuntimeError("Value is not an object");
                }
                auto objHolder = std::get<std::shared_ptr<ObjectHolder>>(storage_);
                return objHolder->data;
            }
            // Support for shared_ptr<UserType> extraction from objects
            else if constexpr (is_specialization_v<T, std::shared_ptr>) {
                if (type() != ValueType::Object) {
                    throw RuntimeError("Value is not an object");
                }
                auto objHolder = std::get<std::shared_ptr<ObjectHolder>>(storage_);
                
                // Check if we need to use the custom extractor (for ClassInstance wrapping)
                if (customExtractor_) {
                    auto extracted = customExtractor_(objHolder->typeName, objHolder->data);
                    if (extracted) {
                        return std::static_pointer_cast<typename T::element_type>(extracted);
                    }
                }
                
                // Otherwise use static cast (for objects created directly)
                return std::static_pointer_cast<typename T::element_type>(objHolder->data);
            }
            // Support for extracting custom objects by value (dereference shared_ptr)
            else if constexpr (std::is_class_v<T> && 
                             !std::is_same_v<T, std::string> &&
                             !is_specialization_v<T, std::vector> &&
                             !is_specialization_v<T, std::map>) {
                // For custom classes, try to extract shared_ptr and dereference
                if (type() == ValueType::Object) {
                    auto ptr = as<std::shared_ptr<T>>();
                    return *ptr;
                }
                throw RuntimeError("Cannot extract custom type by value from non-object");
            }
            else {
                throw RuntimeError("Unsupported type conversion");
            }
        }
        
        // Conversion to string for debugging
        std::string toString() const;
        
        // Operators
        bool operator==(const Value& other) const;
        bool operator!=(const Value& other) const { return !(*this == other); }
        
        // C++20 spaceship operator for complete ordering
        std::strong_ordering operator<=>(const Value& other) const;
        
        // Custom extractor for unwrapping objects (e.g., extracting C++ objects from ClassInstance)
        using ExtractorFunc = std::function<std::shared_ptr<void>(const std::string&, std::shared_ptr<void>)>;
        
        // Set custom extractor for unwrapping objects
        static void setCustomExtractor(ExtractorFunc extractor) {
            customExtractor_ = std::move(extractor);
        }
        
    private:
        TypeInfoPtr typeInfo_;  // Complete type information
        
        // For object storage - external serialization will handle this
        struct ObjectHolder {
            std::string typeName;           // Type identification for serialization
            std::shared_ptr<void> data;     // The actual object
            
            // Note: Serialization functions will be managed externally
            // by ISerializer implementations to keep JaiScript dependency-free
        };
        
        // Reference wrapper for reference types
        struct ReferenceHolder {
            Value* target;  // Points to the referenced value
        };
        
        // Type-erased storage using variant for efficiency
        using Storage = std::variant<
            std::monostate,                               // Null
            Int,                                           // Int
            Float,                                         // Float
            String,                                        // String
            Char,                                          // Char
            Bool,                                          // Bool
            std::shared_ptr<std::vector<Value>>,          // Array<T>
            std::shared_ptr<std::map<Value, Value>>,      // Map<K,V>
            std::shared_ptr<ObjectHolder>,                // Object<T>
            ScriptFunction,                                // Function
            std::shared_ptr<ReferenceHolder>,             // T&
            std::shared_ptr<Value>,                       // SharedPtr<T>
            std::weak_ptr<Value>                          // WeakPtr<T>
        >;
        
        Storage storage_;
        
        // Custom extractor static member
        static inline ExtractorFunc customExtractor_;
        
        friend class Engine;
        friend class Interpreter;
    };
    
} // namespace JaiScript