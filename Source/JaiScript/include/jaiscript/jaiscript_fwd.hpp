#pragma once

namespace JaiScript {
    
    class Engine;
    class Value;
    class Function;
    class ClassDefinition;
    class ClassInstance;
    template<typename T> class ClassBuilder;
    class Scope;
    class ParseError;
    class RuntimeError;
    class SerializationError;
    
    struct SourceLocation;
    
    enum class ValueType {
        Null,
        Int,
        Float,
        String,
        Char,
        Bool,
        Array,          // Array<T>
        Map,            // Map<K,V>
        Object,         // Object<T> - typed object
        Function,       // Function<ReturnType(Args...)>
        Reference,      // T& - Reference to another value
        SharedPtr,      // SharedPtr<T>
        WeakPtr         // WeakPtr<T>
    };
    
} // namespace JaiScript