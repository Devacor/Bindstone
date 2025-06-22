#pragma once

#include "types.hpp"
#include "../jaiscript_fwd.hpp"
#include <vector>
#include <memory>

namespace JaiScript {

    // Forward declaration
    class TypeInfo;
    using TypeInfoPtr = std::shared_ptr<TypeInfo>;
    
    // Represents complete type information including generic parameters
    class TypeInfo {
    public:
        ValueType baseType;
        std::string typeName;  // For Object<T>, this would be the class name
        
        // Generic type parameters
        std::vector<TypeInfoPtr> typeParams;
        
        // Constructors
        TypeInfo(ValueType type) : baseType(type) {}
        TypeInfo(ValueType type, const std::string& name) : baseType(type), typeName(name) {}
        
        // Factory methods for common types
        static TypeInfoPtr makeInt() { 
            return std::make_shared<TypeInfo>(ValueType::Int); 
        }
        static TypeInfoPtr makeFloat() { 
            return std::make_shared<TypeInfo>(ValueType::Float); 
        }
        static TypeInfoPtr makeString() { 
            return std::make_shared<TypeInfo>(ValueType::String); 
        }
        static TypeInfoPtr makeBool() { 
            return std::make_shared<TypeInfo>(ValueType::Bool); 
        }
        static TypeInfoPtr makeChar() { 
            return std::make_shared<TypeInfo>(ValueType::Char); 
        }
        
        static TypeInfoPtr makeVoid() {
            auto info = std::make_shared<TypeInfo>(ValueType::Null);
            info->typeName = "void";
            return info;
        }
        
        // Array<T>
        static TypeInfoPtr makeArray(TypeInfoPtr elementType) {
            auto info = std::make_shared<TypeInfo>(ValueType::Array);
            info->typeParams.push_back(elementType);
            return info;
        }
        
        // Map<K,V>
        static TypeInfoPtr makeMap(TypeInfoPtr keyType, TypeInfoPtr valueType) {
            auto info = std::make_shared<TypeInfo>(ValueType::Map);
            info->typeParams.push_back(keyType);
            info->typeParams.push_back(valueType);
            return info;
        }
        
        // Object<T>
        static TypeInfoPtr makeObject(const std::string& className) {
            return std::make_shared<TypeInfo>(ValueType::Object, className);
        }
        
        // SharedPtr<T>
        static TypeInfoPtr makeSharedPtr(TypeInfoPtr pointeeType) {
            auto info = std::make_shared<TypeInfo>(ValueType::SharedPtr);
            info->typeParams.push_back(pointeeType);
            return info;
        }
        
        // WeakPtr<T>
        static TypeInfoPtr makeWeakPtr(TypeInfoPtr pointeeType) {
            auto info = std::make_shared<TypeInfo>(ValueType::WeakPtr);
            info->typeParams.push_back(pointeeType);
            return info;
        }
        
        // T&
        static TypeInfoPtr makeReference(TypeInfoPtr referencedType) {
            auto info = std::make_shared<TypeInfo>(ValueType::Reference);
            info->typeParams.push_back(referencedType);
            return info;
        }
        
        // Function<ReturnType(Args...)>
        static TypeInfoPtr makeFunction(TypeInfoPtr returnType, std::vector<TypeInfoPtr> argTypes) {
            auto info = std::make_shared<TypeInfo>(ValueType::Function);
            info->typeParams.push_back(returnType);
            info->typeParams.insert(info->typeParams.end(), argTypes.begin(), argTypes.end());
            return info;
        }
        
        // Type checking
        bool isArray() const { return baseType == ValueType::Array; }
        bool isMap() const { return baseType == ValueType::Map; }
        bool isObject() const { return baseType == ValueType::Object; }
        bool isFunction() const { return baseType == ValueType::Function; }
        bool isReference() const { return baseType == ValueType::Reference; }
        bool isSharedPtr() const { return baseType == ValueType::SharedPtr; }
        bool isWeakPtr() const { return baseType == ValueType::WeakPtr; }
        
        // Get type parameters
        TypeInfoPtr getElementType() const {  // For Array<T>, SharedPtr<T>, etc.
            return typeParams.empty() ? nullptr : typeParams[0];
        }
        
        TypeInfoPtr getKeyType() const {  // For Map<K,V>
            return typeParams.empty() ? nullptr : typeParams[0];
        }
        
        TypeInfoPtr getValueType() const {  // For Map<K,V>
            return typeParams.size() < 2 ? nullptr : typeParams[1];
        }
        
        TypeInfoPtr getReturnType() const {  // For Function
            return typeParams.empty() ? nullptr : typeParams[0];
        }
        
        std::vector<TypeInfoPtr> getArgTypes() const {  // For Function
            if (typeParams.size() <= 1) return {};
            return std::vector<TypeInfoPtr>(typeParams.begin() + 1, typeParams.end());
        }
        
        // String representation for debugging
        std::string toString() const;
        
        // Equality comparison
        bool equals(const TypeInfo& other) const;
    };
    
    // Example usage in JaiScript syntax:
    // int x = 5;                           // TypeInfo::makeInt()
    // array<int> nums = {1, 2, 3};         // TypeInfo::makeArray(makeInt())
    // map<string, int> scores;             // TypeInfo::makeMap(makeString(), makeInt())
    // SharedPtr<Creature> creature;        // TypeInfo::makeSharedPtr(makeObject("Creature"))
    // Function<int(float, float)> add;     // TypeInfo::makeFunction(makeInt(), {makeFloat(), makeFloat()})
    
} // namespace JaiScript