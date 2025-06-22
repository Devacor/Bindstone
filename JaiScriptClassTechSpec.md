# JaiScript Class System Technical Specification

## Overview

This document defines the technical specification for JaiScript's class system, including native script classes, C++ integration, inheritance, and method dispatch. The design builds upon the existing ClassBuilder infrastructure while adding native script class support.

## Table of Contents

1. [Class Declaration Syntax](#class-declaration-syntax)
2. [Type System Integration](#type-system-integration)
3. [Memory Management](#memory-management)
4. [Method Resolution](#method-resolution)
5. [Inheritance System](#inheritance-system)
6. [C++ Integration](#cpp-integration)
7. [Serialization Strategy](#serialization-strategy)
8. [Implementation Plan](#implementation-plan)

## Class Declaration Syntax

### Basic Class Declaration

```jaiscript
class Point {
    // Field declarations with optional default values
    float x = 0.0;
    float y = 0.0;
    
    // Constructor
    Point(float px, float py) {
        x = px;
        y = py;
    }
    
    // Methods
    float distanceTo(Point& other) {
        float dx = x - other.x;
        float dy = y - other.y;
        return sqrt(dx * dx + dy * dy);
    }
    
    // Property-style methods (getter/setter)
    float magnitude() {
        return sqrt(x * x + y * y);
    }
}
```

### Access Modifiers

```jaiscript
class GameObject {
public:
    string name;
    bool visible = true;
    
    void update(float dt);
    
private:
    int id;
    float health = 100.0;
    
protected:
    array<Component> components;
    
    void internalUpdate();
}
```

### Inheritance

```jaiscript
// Script-to-script inheritance
class Enemy : GameObject {
    float damage = 10.0;
    
    Enemy(string enemyName, float enemyDamage) : GameObject(enemyName) {
        damage = enemyDamage;
    }
    
    // Override parent method
    void update(float dt) override {
        super::update(dt);  // Call parent implementation
        // Additional enemy-specific logic
    }
}

// Script inheriting from C++ class
class CustomCreature : Creature {
    string behavior = "aggressive";
    
    CustomCreature(string name) : Creature(name) {
        // Initialize script-specific fields
    }
    
    void attack() override {
        super::attack();  // Call C++ base implementation
        // Script-specific attack logic
    }
}
```

## Type System Integration

### Enhanced ValueType Enum

```cpp
enum class ValueType {
    // Existing types
    Null, Int, Float, String, Char, Bool,
    Array, Map, Object, Function, Reference, SharedPtr, WeakPtr,
    
    // New class-related types
    ScriptClass,      // Instance of script-defined class
    CppClass,         // Instance of C++-bound class
    ClassDefinition   // Class type itself (for static access)
};
```

### ScriptClass Storage

```cpp
struct ScriptClassInstance {
    std::string className;
    std::shared_ptr<ScriptClassDefinition> classDef;
    std::map<std::string, Value> fields;           // Script fields
    std::shared_ptr<void> cppObject = nullptr;     // C++ base object (if inheriting)
    std::weak_ptr<ScriptClassInstance> thisPtr;    // For method 'this' context
};

struct ScriptClassDefinition {
    std::string name;
    std::vector<FieldDeclaration> fields;
    std::vector<MethodDeclaration> methods;
    std::vector<ConstructorDeclaration> constructors;
    std::shared_ptr<ScriptClassDefinition> baseClass = nullptr;  // Script base
    std::shared_ptr<ClassDefinition> cppBaseClass = nullptr;     // C++ base
    AccessLevel defaultAccess = AccessLevel::Public;
};
```

### Unified Class Registry

```cpp
class ClassRegistry {
public:
    // Register script-defined class
    void registerScriptClass(std::shared_ptr<ScriptClassDefinition> classDef);
    
    // Register C++ class (existing ClassBuilder)
    void registerCppClass(std::shared_ptr<ClassDefinition> classDef);
    
    // Unified lookup
    std::shared_ptr<BaseClassDefinition> findClass(const std::string& name);
    
    // Check inheritance relationships
    bool isAssignableFrom(const std::string& derived, const std::string& base);
    
private:
    std::map<std::string, std::shared_ptr<ScriptClassDefinition>> scriptClasses_;
    std::map<std::string, std::shared_ptr<ClassDefinition>> cppClasses_;
};
```

## Memory Management

### Object Lifecycle

1. **Creation**: `new ClassName(args)` creates `std::shared_ptr<ScriptClassInstance>`
2. **Storage**: Objects stored in Value as `std::shared_ptr<ScriptClassInstance>`
3. **Method Calls**: Automatic `this` binding during method execution
4. **Destruction**: RAII cleanup when last reference is released

### Reference Semantics

```cpp
class ScriptClassInstance {
    std::weak_ptr<ScriptClassInstance> getSelfReference() {
        return thisPtr;
    }
    
    // Enable shared_from_this pattern for script objects
    std::shared_ptr<ScriptClassInstance> getSharedPtr() {
        return thisPtr.lock();
    }
};
```

### Hybrid C++/Script Objects

```cpp
// For script classes inheriting from C++ classes
struct HybridClassInstance : ScriptClassInstance {
    std::shared_ptr<void> cppBaseObject;  // C++ portion
    std::string cppBaseTypeName;          // For type safety
    
    template<typename T>
    T& getCppBase() {
        return *static_cast<T*>(cppBaseObject.get());
    }
};
```

## Method Resolution

### Method Dispatch Table

```cpp
struct MethodInfo {
    enum Type { Script, Cpp, Virtual } type;
    std::string name;
    std::vector<std::string> paramTypes;
    AccessLevel access;
    
    // For script methods
    std::shared_ptr<FunctionDecl> scriptMethod = nullptr;
    
    // For C++ methods
    std::function<Value(const std::vector<Value>&)> cppMethod = nullptr;
    
    // For virtual dispatch
    bool isVirtual = false;
    bool isOverride = false;
};

class MethodResolver {
public:
    // Resolve method call at runtime
    MethodInfo* resolveMethod(
        const std::string& className,
        const std::string& methodName,
        const std::vector<Value>& args
    );
    
    // Build complete method table for a class
    void buildMethodTable(std::shared_ptr<ScriptClassDefinition> classDef);
    
private:
    // Method resolution order (MRO) for inheritance
    std::vector<std::string> getMethodResolutionOrder(const std::string& className);
};
```

### Method Call Implementation

```cpp
Value callMethod(
    std::shared_ptr<ScriptClassInstance> instance,
    const std::string& methodName,
    const std::vector<Value>& args
) {
    auto methodInfo = resolver.resolveMethod(instance->className, methodName, args);
    
    switch (methodInfo->type) {
    case MethodInfo::Script:
        return callScriptMethod(instance, methodInfo, args);
    case MethodInfo::Cpp:
        return callCppMethod(instance, methodInfo, args);
    case MethodInfo::Virtual:
        // Check for overrides in derived classes
        return callVirtualMethod(instance, methodInfo, args);
    }
}
```

## Inheritance System

### Single Inheritance Chain

```cpp
class InheritanceChain {
public:
    struct ClassNode {
        std::string name;
        ClassType type;  // Script or Cpp
        std::shared_ptr<BaseClassDefinition> definition;
        std::shared_ptr<ClassNode> parent = nullptr;
    };
    
    // Build inheritance chain for type checking
    std::vector<ClassNode> getInheritanceChain(const std::string& className);
    
    // Virtual method resolution
    MethodInfo* resolveVirtualMethod(
        const std::string& className,
        const std::string& methodName
    );
};
```

### Super Call Implementation

```cpp
// In interpreter.cpp
void Interpreter::visitSuperExpr(SuperExpr* expr) {
    auto instance = getCurrentInstance();  // Get 'this' context
    auto baseClass = instance->classDef->baseClass;
    
    if (!baseClass) {
        throw RuntimeError("No base class for super call");
    }
    
    // Call method on base class, skipping current class override
    auto methodInfo = resolver.resolveMethodInClass(
        baseClass->name, 
        expr->methodName,
        getCurrentArgs()
    );
    
    pushValue(callMethodDirect(instance, methodInfo, getCurrentArgs()));
}
```

### Mixed Inheritance Support

```cpp
// Script class inheriting from C++ class
class ScriptToCppInheritance {
    std::shared_ptr<ScriptClassInstance> createInstance(
        const std::string& scriptClassName,
        const std::vector<Value>& constructorArgs
    ) {
        auto scriptDef = getScriptClassDef(scriptClassName);
        auto cppBaseDef = scriptDef->cppBaseClass;
        
        // Create C++ base object first
        auto cppInstance = cppBaseDef->createInstance(extractCppArgs(constructorArgs));
        
        // Create script wrapper
        auto scriptInstance = std::make_shared<HybridClassInstance>();
        scriptInstance->className = scriptClassName;
        scriptInstance->classDef = scriptDef;
        scriptInstance->cppBaseObject = cppInstance;
        
        // Initialize script fields
        initializeScriptFields(scriptInstance, constructorArgs);
        
        return scriptInstance;
    }
};
```

## C++ Integration

### Bidirectional Method Calls

```cpp
// C++ calling script methods
template<typename R, typename... Args>
R callScriptMethod(std::shared_ptr<ScriptClassInstance> instance, 
                   const std::string& methodName, 
                   Args... args) {
    std::vector<Value> scriptArgs = {Value(args)...};
    Value result = methodResolver.call(instance, methodName, scriptArgs);
    return result.as<R>();
}

// Script calling C++ methods (existing ClassBuilder mechanism)
Value callCppMethod(std::shared_ptr<ScriptClassInstance> instance,
                    MethodInfo* methodInfo,
                    const std::vector<Value>& args) {
    // Extract C++ object if hybrid inheritance
    if (auto hybrid = std::dynamic_pointer_cast<HybridClassInstance>(instance)) {
        // Prepend C++ object as 'self' parameter
        std::vector<Value> cppArgs;
        cppArgs.push_back(Value::makeObject(hybrid->cppBaseObject, hybrid->cppBaseTypeName));
        cppArgs.insert(cppArgs.end(), args.begin(), args.end());
        return methodInfo->cppMethod(cppArgs);
    }
    
    return methodInfo->cppMethod(args);
}
```

### Enhanced ClassBuilder Integration

```cpp
// Extend existing ClassBuilder to support script inheritance
template<typename T>
class ClassBuilder {
public:
    // Existing methods...
    
    // Allow script classes to inherit from this C++ class
    ClassBuilder& allowScriptInheritance() {
        classDef_->allowScriptInheritance = true;
        return *this;
    }
    
    // Virtual method registration for script overrides
    template<typename R, typename... Args>
    ClassBuilder& virtualMethod(const std::string& name, R(T::*method)(Args...)) {
        auto methodInfo = std::make_shared<MethodInfo>();
        methodInfo->name = name;
        methodInfo->isVirtual = true;
        methodInfo->cppMethod = bindVirtualMethod(method);
        classDef_->methods[name] = methodInfo;
        return *this;
    }
};
```

## Serialization Strategy

### Script Class Serialization

```cpp
struct SerializableClass {
    std::string className;
    std::string baseClassName;  // For inheritance chain reconstruction
    std::map<std::string, SerializableValue> fields;
    
    // C++ base object serialization (if applicable)
    bool hasCppBase = false;
    std::string cppBaseTypeName;
    std::vector<uint8_t> cppBaseData;  // Serialized C++ object
};

class ClassSerializer {
public:
    // Serialize script class instance
    SerializableClass serialize(std::shared_ptr<ScriptClassInstance> instance);
    
    // Deserialize and reconstruct
    std::shared_ptr<ScriptClassInstance> deserialize(const SerializableClass& data);
    
    // Handle C++ base object serialization
    void serializeCppBase(std::shared_ptr<void> cppObject, 
                         const std::string& typeName,
                         std::vector<uint8_t>& data);
};
```

### Hot-Reload Compatibility

```cpp
class ClassMigration {
public:
    // Check if class definition changed
    bool isCompatible(const ScriptClassDefinition& oldDef, 
                     const ScriptClassDefinition& newDef);
    
    // Migrate instance to new class definition
    std::shared_ptr<ScriptClassInstance> migrate(
        std::shared_ptr<ScriptClassInstance> oldInstance,
        std::shared_ptr<ScriptClassDefinition> newDef
    );
    
    // Handle field additions/removals
    void migrateFields(const std::map<std::string, Value>& oldFields,
                      std::map<std::string, Value>& newFields,
                      const std::vector<FieldDeclaration>& newFieldDefs);
};
```

## Implementation Plan

### Phase 1: Basic Script Classes

**Parser Extensions:**
- Add class declaration parsing in `parser.cpp`
- Extend AST with `ClassDecl`, `ConstructorDecl`, `MethodDecl` nodes
- Support field declarations with default values

**Interpreter Implementation:**
- Implement `visitClassDecl()` to register script classes
- Add class instantiation with `new` operator
- Basic method calls and field access

**Integration Points:**
- Extend `ClassRegistry` to handle both script and C++ classes
- Modify `Value` class to support `ScriptClass` type
- Update method resolution for script methods

### Phase 2: Inheritance and Method Dispatch

**Inheritance Support:**
- Single inheritance from script classes
- Method resolution order (MRO) implementation
- Super call support with `super::methodName()`

**Virtual Method System:**
- Virtual method dispatch table
- Override detection and validation
- Method signature compatibility checking

### Phase 3: C++ Integration

**Hybrid Classes:**
- Script classes inheriting from C++ classes
- Bidirectional method calls
- C++ object lifecycle management in script context

**Enhanced ClassBuilder:**
- Virtual method registration for C++ classes
- Script inheritance permissions
- Type conversion improvements

### Phase 4: Advanced Features

**Access Control:**
- Public/private/protected access modifiers
- Access validation during method calls and field access
- Friend class support (optional)

**Operator Overloading:**
- Support for arithmetic operators in script classes
- Comparison operators for custom types
- Assignment operator semantics

### Phase 5: Serialization and Hot-Reload

**State Preservation:**
- Class instance serialization
- C++ base object serialization integration
- Migration system for class definition changes

**Hot-Reload System:**
- Class compatibility checking
- Automatic instance migration
- Error handling for incompatible changes

## Performance Considerations

### Optimization Strategies

1. **Method Caching**: Cache resolved methods for frequently called operations
2. **Inline Field Access**: Direct field access without method calls for simple properties
3. **Virtual Call Optimization**: Minimize virtual dispatch overhead
4. **Memory Pooling**: Object pool for frequently created/destroyed instances
5. **JIT Opportunities**: Identify hot methods for potential future JIT compilation

### Memory Usage

- Shared method tables across instances of the same class
- Copy-on-write for inherited field defaults
- Weak references to prevent circular dependencies
- Efficient packing of field storage

## Security and Safety

### Type Safety

- Compile-time type checking where possible
- Runtime type validation for dynamic calls
- Null safety for object references
- Array bounds checking for container access

### Access Control

- Strict enforcement of access modifiers
- Method visibility validation
- Cross-language access control (script ↔ C++)

This technical specification provides a comprehensive foundation for implementing JaiScript's class system while maintaining compatibility with existing C++ bindings and ensuring robust performance characteristics.