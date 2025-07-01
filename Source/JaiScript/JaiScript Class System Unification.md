# JaiScript Class System Unification Plan

## Overview

JaiScript currently has three parallel class systems with significant code duplication:
1. **C++ Class Exposure** (`class_builder`, `class_definition`) - **Working well, will be our foundation**
2. **Script-Defined Classes** (`script_class_definition`, `class_interpreter`) - Will inherit from #1
3. **VM Classes** (`vm_class_definition`, bytecode compilation) - Will inherit from #1

This document outlines a plan to unify these systems by building on the existing, battle-tested `class_definition` infrastructure.

## Current State Analysis

### Duplicate Structures

| Component | C++ Classes | Script Classes | VM Classes |
|-----------|------------|----------------|------------|
| Definition | `class_definition` | `script_class_definition` | `vm_class_definition` |
| Instance | `class_instance` | `script_class_instance` | `vm_class_instance` |
| Method Info | Methods map | `method_info` | `vtable_entry` |
| Field Storage | `field_defaults_` | `field_declaration` | Field indices |
| Constructor | C++ function | `constructor_declaration` | Bytecode |
| Destructor | C++ function | Function AST | Bytecode |

### Code Duplication Metrics
- ~1,500 lines of duplicate class handling logic
- 3 separate method dispatch implementations
- 3 different field access mechanisms
- 3 constructor invocation patterns
- Multiple inheritance resolution algorithms

## Unified Architecture Design (Building on Existing)

### Evolution Strategy

Rather than creating new base classes, we'll evolve the existing `class_definition` and `class_instance` into our unified base classes. This preserves all working code while enabling unification.

### Step 1: Evolve class_definition

```cpp
// Current class_definition becomes our base - minimal changes needed!
class class_definition : public std::enable_shared_from_this<class_definition> {
public:
    enum class_type { 
        cpp_class,      // Existing
        script_class,   // Existing
        vm_class        // New addition
    };
    
    // Existing constructor for C++ classes
    class_definition(const std::string& name) : name_(name), class_type_(cpp_class) {}
    
    // Existing constructor for script classes  
    class_definition(const std::string& name, class_type type) : name_(name), class_type_(type) {}
    
    // ALL EXISTING METHODS REMAIN UNCHANGED
    // Just add new unified structures alongside:
    
    // New: Unified method storage (alongside existing methods_)
    void add_unified_method(const std::string& name, unified_method_info&& info) {
        unified_methods_[name] = std::move(info);
        // Also update existing methods_ for compatibility
        methods_[name] = script_value::make_function(/* adapter */);
    }
    
    // New: Get unified method (falls back to existing methods_)
    const unified_method_info* get_unified_method(const std::string& name) const {
        auto it = unified_methods_.find(name);
        if (it != unified_methods_.end()) {
            return &it->second;
        }
        // Fallback: wrap existing method in unified_method_info
        return wrap_legacy_method(name);
    }
    
private:
    // Existing fields remain
    std::string name_;
    std::map<std::string, script_value> methods_;
    std::map<std::string, script_value> field_defaults_;
    std::shared_ptr<class_definition> parent_class_;
    class_type class_type_;
    
    // New unified storage (gradually migrated to)
    std::map<std::string, unified_method_info> unified_methods_;
    std::map<std::string, unified_field_info> unified_fields_;
};
```

### Step 2: Evolve class_instance

```cpp
// Current class_instance becomes our base instance - no breaking changes!
class class_instance : public std::enable_shared_from_this<class_instance> {
protected:
    std::weak_ptr<base_class_definition> class_def_;
    
    // Optimized field storage with optional fast access
    std::map<std::string, script_value> field_values_;
    std::unordered_map<std::string, size_t> field_indices_; // Cache for fast lookup
    std::vector<script_value> indexed_fields_;              // For VM fast access
    
    // Direct C++ object storage (no double indirection)
    std::shared_ptr<void> cpp_object_;
    
    // Method dispatch cache
    mutable std::unordered_map<std::string, const unified_method_info*> method_cache_;
    
public:
    virtual ~base_instance() = default;
    
    // Type queries (inline for performance)
    bool is_cpp_instance() const { return cpp_object_ != nullptr; }
    bool is_script_instance() const { return !is_cpp_instance() && !is_vm_instance(); }
    virtual bool is_vm_instance() const { return false; }
    
    // Field access with caching
    script_value get_field(const std::string& name) const;
    void set_field(const std::string& name, const script_value& value);
    
    // Fast field access for VM (inline)
    script_value get_field_by_index(size_t index) const {
        return index < indexed_fields_.size() ? indexed_fields_[index] : script_value();
    }
    
    void set_field_by_index(size_t index, const script_value& value) {
        if (index < indexed_fields_.size()) {
            indexed_fields_[index] = value;
        }
    }
    
    // Direct C++ object access (no field lookup)
    template<typename T>
    T* get_cpp_object() const {
        return static_cast<T*>(cpp_object_.get());
    }
    
    std::shared_ptr<void> get_cpp_shared() const { return cpp_object_; }
    
    std::string get_class_name() const;
    std::shared_ptr<base_class_definition> get_class_definition() const;
    
    // Method cache management
    void cache_method(const std::string& name, const unified_method_info* method) const {
        method_cache_[name] = method;
    }
    
    const unified_method_info* get_cached_method(const std::string& name) const {
        auto it = method_cache_.find(name);
        return it != method_cache_.end() ? it->second : nullptr;
    }
};
```

### Unified Information Structures

```cpp
// Unified field information
struct unified_field_info {
    std::string name;
    std::string type_name;  // For future type checking
    access_level access = access_level::public_access;
    bool is_static = false;
    
    // Default value
    script_value default_value;
    
    // Optional getter/setter for C++ properties
    std::function<script_value(base_instance*)> getter;
    std::function<void(base_instance*, const script_value&)> setter;
    
    // VM optimization: field index
    std::optional<size_t> vm_index;
};

// Unified method information
struct unified_method_info {
    std::string name;
    std::vector<std::string> parameter_names;
    std::string return_type;  // For future type checking
    
    access_level access = access_level::public_access;
    bool is_static = false;
    bool is_virtual = false;
    bool is_override = false;
    bool is_final = false;
    
    // Implementation variants
    using method_impl = std::variant<
        script_function,                          // C++ function
        std::shared_ptr<function_decl>,          // Script AST
        std::shared_ptr<bytecode_function>       // VM bytecode
    >;
    
    method_impl implementation;
    
    // VM optimization: vtable index
    std::optional<size_t> vtable_index;
};

// Unified constructor information  
struct unified_constructor_info {
    std::vector<std::string> parameter_names;
    access_level access = access_level::public_access;
    
    // Implementation variants
    using ctor_impl = std::variant<
        std::function<std::shared_ptr<base_instance>(const std::vector<script_value>&)>,
        std::shared_ptr<constructor_decl>,
        std::shared_ptr<bytecode_function>
    >;
    
    ctor_impl implementation;
    
    // Constructor delegation
    bool has_delegation = false;
    delegation_type delegation_type = delegation_type::none;
    std::vector<script_value> delegation_args;
};

// Unified destructor information
struct unified_destructor_info {
    bool is_virtual = false;
    
    using dtor_impl = std::variant<
        std::function<void(base_instance*)>,
        std::shared_ptr<destructor_decl>,
        std::shared_ptr<bytecode_function>
    >;
    
    dtor_impl implementation;
};
```

### Step 3: Refactor Other Class Types

```cpp
// Script class definition now inherits from class_definition!
class script_class_definition : public class_definition {
public:
    script_class_definition(const std::string& name) 
        : class_definition(name, class_type::script_class) {
        // Inherits ALL functionality from class_definition!
        // Methods, fields, inheritance, instance creation - all work!
    }
    
    // Override only what's different for script classes
    void compile_to_bytecode() {
        // Script-specific: compile methods from AST to bytecode
        for (auto& [name, method] : get_script_methods()) {
            auto bytecode = compile_method(method);
            add_unified_method(name, make_vm_method(bytecode));
        }
    }
};

// VM class definition also inherits!
class vm_class_definition : public class_definition {
public:
    vm_class_definition(const std::string& name)
        : class_definition(name, class_type::vm_class) {
        // Start with all class_definition features
        optimize_for_vm();
    }
    
    // VM-specific optimizations
    void optimize_for_vm() {
        // Convert named fields to indexed access
        build_field_layout();
        // Build vtable from methods
        build_vtable();
    }
    
private:
    std::vector<size_t> field_indices_;  // Map field names to indices
    std::vector<const unified_method_info*> vtable_;  // Fast virtual dispatch
};
```

### Unified Method Dispatch

```cpp
class unified_method_dispatcher {
private:
    interpreter* interp_;
    vm* virtual_machine_;
    
    // Fast dispatch function pointers for avoiding std::visit overhead
    using dispatch_func = script_value(*)(
        unified_method_dispatcher*, 
        base_instance*, 
        const unified_method_info&, 
        const std::vector<script_value>&
    );
    
    static script_value dispatch_cpp(
        unified_method_dispatcher* self,
        base_instance* instance,
        const unified_method_info& method,
        const std::vector<script_value>& args
    ) {
        const auto& func = std::get<script_function>(method.implementation);
        return func(args);
    }
    
    static script_value dispatch_script(
        unified_method_dispatcher* self,
        base_instance* instance,
        const unified_method_info& method,
        const std::vector<script_value>& args
    ) {
        const auto& ast = std::get<std::shared_ptr<function_decl>>(method.implementation);
        return self->interp_->call_script_method(instance, ast, args);
    }
    
    static script_value dispatch_vm(
        unified_method_dispatcher* self,
        base_instance* instance,
        const unified_method_info& method,
        const std::vector<script_value>& args
    ) {
        const auto& bytecode = std::get<std::shared_ptr<bytecode_function>>(method.implementation);
        return self->virtual_machine_->execute_method(instance, bytecode, args);
    }
    
public:
    script_value dispatch(
        base_instance* instance,
        const unified_method_info& method,
        const std::vector<script_value>& args
    ) {
        // Prepare arguments with 'this' if needed
        std::vector<script_value> full_args;
        if (!method.is_static) {
            // For C++ objects, pass the instance directly
            if (instance->is_cpp_instance()) {
                full_args.push_back(script_value::make_object(
                    instance->get_class_name(), 
                    instance->shared_from_this()
                ));
            } else {
                full_args.push_back(script_value::make_object(instance->shared_from_this()));
            }
        }
        full_args.insert(full_args.end(), args.begin(), args.end());
        
        // Fast dispatch based on variant index
        static const dispatch_func dispatch_table[] = {
            &dispatch_cpp,
            &dispatch_script,
            &dispatch_vm
        };
        
        size_t index = method.implementation.index();
        return dispatch_table[index](this, instance, method, full_args);
    }
    
    // Optimized batch dispatch for property access patterns
    script_value dispatch_property_getter(
        base_instance* instance,
        const unified_method_info& getter
    ) {
        // Special fast path for property getters
        if (getter.implementation.index() == 0) { // C++ function
            const auto& func = std::get<script_function>(getter.implementation);
            return func({script_value::make_object(instance->get_class_name(), 
                                                 instance->shared_from_this())});
        }
        return dispatch(instance, getter, {});
    }
};
```

### Unified Class Registry

```cpp
class unified_class_registry {
private:
    // Single storage for all class types
    std::map<std::string, std::shared_ptr<base_class_definition>> classes_;
    
    // Type registries for backwards compatibility
    std::map<std::string, std::string> cpp_type_mappings_;
    
    // Singleton
    static unified_class_registry& instance();
    
public:
    // Registration methods
    void register_class(std::shared_ptr<base_class_definition> class_def);
    
    // Lookup methods
    std::shared_ptr<base_class_definition> find_class(const std::string& name) const;
    
    // Instance creation
    std::shared_ptr<base_instance> create_instance(
        const std::string& class_name,
        const std::vector<script_value>& args
    );
    
    // Type checking
    bool is_assignable_from(const std::string& derived, const std::string& base) const;
    
    // Migration helpers
    std::shared_ptr<class_definition> find_cpp_class(const std::string& name) const;
    std::shared_ptr<script_class_definition> find_script_class(const std::string& name) const;
};
```

## Preserving class_builder Compatibility

The unified architecture fully preserves all class_builder capabilities:

### Constructor Patterns
```cpp
// All constructor patterns continue to work unchanged
engine.register_class<Point>("Point")
    .constructor<>()           // Zero-arg
    .constructor<int, int>()   // Multi-arg
    .serialize_construct(...)  // Custom serialization
    .build();
```

### Method Binding Patterns
```cpp
// All method binding patterns preserved
.method("draw", &Widget::draw)                    // Member function pointer
.method("update", &Widget::update)                // Const method
.method("onClick", [](Widget& self) { ... })     // Lambda with self
.method("process", [](int x) { return x*2; })    // Regular lambda
```

### Property Access
```cpp
// Property patterns remain identical
.property("width", &Widget::width)                           // Direct member
.property("height", 
    [](const Widget& w) { return w.getHeight(); },         // Getter/setter lambdas
    [](Widget& w, int h) { w.setHeight(h); })
```

### Special Methods Preserved
- `_get_property` / `_set_property` for direct property access
- `_cpp_object` field storage pattern maintained
- Traditional `getProperty` / `setProperty` methods

### Performance Optimizations Added
- Direct `cpp_object_` access (no field lookup)
- Method dispatch caching
- Indexed field access for VM
- Fast property getter paths

## Migration Plan (Building on Existing)

### Phase 1: Extend class_definition (Day 1-2)
1. Add `class_type::vm_class` enum value
2. Add `unified_method_info` map alongside existing `methods_`
3. Add adapter functions to wrap existing methods
4. **No breaking changes** - all tests continue to pass
5. Benchmark performance to ensure no regression

### Phase 2: Refactor script_class_definition (Day 3-4)
1. Change `script_class_definition` to inherit from `class_definition`
2. Remove duplicate fields (name, methods, fields, etc.)
3. Use inherited `create_instance()` method
4. Use inherited method storage and dispatch
5. Script classes now automatically support all class_builder features!

### Phase 3: Create vm_class_definition (Day 5-6)
1. Create `vm_class_definition` inheriting from `class_definition`
2. Add VM-specific optimizations (field indices, vtable)
3. Compile script methods to bytecode using inherited method list
4. VM classes get all infrastructure for free

### Phase 4: Unify class_interpreter (Day 7-8)
1. Remove `class_interpreter` - no longer needed!
2. `interpreter::visit_class_decl` creates `script_class_definition`
3. Use existing `class_definition` method dispatch
4. Script classes now work exactly like C++ classes

### Phase 5: Testing & Polish (Day 9-10)
1. Comprehensive tests for script/C++ interoperability
2. Performance benchmarks
3. Remove any remaining duplicate code
4. Update documentation

### Key Advantages of This Approach
- **Immediate Benefits**: Script classes get all C++ features on Day 3
- **No Breaking Changes**: Existing code continues to work throughout
- **Incremental**: Can ship after any phase
- **Less Code**: Reusing existing infrastructure instead of duplicating
- **Battle-Tested**: Building on proven code, not starting fresh

## Benefits

### Code Reduction
- Eliminate ~1,500 lines of duplicate code
- Single implementation for common operations
- Shared optimization benefits all class types

### Consistency
- Uniform behavior across all class types
- Single mental model for developers
- Consistent error handling

### Performance
- Unified method dispatch with caching
- Shared vtable implementation
- Optimized field access paths

### Maintainability
- Single place to fix bugs
- Easier to add new features
- Cleaner architecture

### Interoperability
- Seamless script/C++ class mixing
- Unified inheritance chain
- Consistent object model

## Risks and Mitigation

### Risk: Breaking Existing Code
**Mitigation**: Keep existing APIs during migration, mark as deprecated

### Risk: Performance Regression
**Mitigation**: Benchmark before/after each phase, optimize hot paths

### Risk: Complex Migration
**Mitigation**: Incremental approach, comprehensive testing at each phase

### Risk: API Compatibility
**Mitigation**: Maintain backwards compatibility layer during transition

## Example: How Script Classes Will Work After Unification

```javascript
// JaiScript code
class Point {
    x = 0;
    y = 0;
    
    constructor(x, y) {
        this.x = x;
        this.y = y;
    }
    
    distanceTo(other) {
        let dx = other.x - this.x;
        let dy = other.y - this.y;
        return Math.sqrt(dx*dx + dy*dy);
    }
}

// This creates a script_class_definition that inherits from class_definition
// It automatically gets:
// - Proper field storage with defaults
// - Method dispatch that works with C++ classes
// - Instance creation via create_instance()
// - Inheritance support
// - Property access patterns
// - Everything that C++ classes have!
```

```cpp
// C++ code can seamlessly use script classes
auto point_class = engine.get_class_definition("Point");
auto p1 = point_class->create_instance();
p1->set_field("x", 10);
p1->set_field("y", 20);

// Or through the unified registry
auto p2 = registry.create_instance("Point", {30, 40});
```

## Success Criteria

1. All existing tests pass after migration
2. No performance regression (within 5%)
3. Script classes work identically to C++ classes
4. Can inherit script from C++, C++ from script
5. ~70% code reduction in class handling
6. Single class model throughout codebase

## Next Steps

1. Start with Phase 1 - extend class_definition with unified structures
2. Create benchmarks for current performance baseline
3. Implement script_class_definition inheriting from class_definition
4. Remove class_interpreter and simplify visit_class_decl
5. Celebrate massive code simplification!