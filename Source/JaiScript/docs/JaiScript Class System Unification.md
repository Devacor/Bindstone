# JaiScript Class System Unification Plan (Simplified)

## Overview

JaiScript currently has three parallel class systems with significant code duplication:
1. **C++ Class Exposure** (`class_builder`, `class_definition`) - **Working perfectly, provides the foundation**
2. **Script-Defined Classes** (`script_class_definition`, `class_interpreter`) - Will inherit from #1
3. **VM Classes** (`vm_class_definition`, bytecode compilation) - Will inherit from #1

## Key Insight: `script_value` Already Unifies Everything!

The existing `methods_` map storing `script_value` objects can handle ALL method types:
- C++ methods: Already stored as `script_value::make_function(cpp_function)`
- Script methods: Can wrap AST execution in a function
- VM methods: Can wrap bytecode execution in a function

**No need for complex unified structures - the existing design already supports everything!**

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

## Simplified Architecture (Using Existing Infrastructure)

### The Beautiful Realization

The existing `class_definition` already has everything we need! The `methods_` map storing `script_value` objects can handle all method types uniformly.

### How It Works

```cpp
class class_definition : public std::enable_shared_from_this<class_definition> {
public:
    enum class_type { cpp_class, script_class, vm_class };
    
    // Existing methods storage works for EVERYTHING
    std::map<std::string, script_value> methods_;
    
    // Add method for C++ classes (existing)
    void add_method(const std::string& name, script_function func) {
        methods_[name] = script_value::make_function(func);
    }
    
    // Add method for script classes (new)
    void add_script_method(const std::string& name, std::shared_ptr<function_decl> ast) {
        methods_[name] = script_value::make_function(
            [ast, this](const std::vector<script_value>& args) -> script_value {
                return interpreter_->execute_function(ast, args);
            }
        );
    }
    
    // Add method for VM classes (new)
    void add_vm_method(const std::string& name, std::shared_ptr<bytecode_function> bytecode) {
        methods_[name] = script_value::make_function(
            [bytecode, this](const std::vector<script_value>& args) -> script_value {
                return vm_->execute_bytecode(bytecode, args);
            }
        );
    }
    
    // Single get_method works for all types!
    script_value get_method(const std::string& name) const {
        auto it = methods_.find(name);
        return it != methods_.end() ? it->second : script_value();
    }
    
    // Optional: Add metadata for optimizations
    struct method_metadata {
        bool is_virtual = false;
        bool is_override = false;
        size_t vtable_index = 0;
        access_level access = access_level::public_access;
    };
    std::map<std::string, method_metadata> method_metadata_;
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

### Step 3: Script Classes Just Inherit!

```cpp
// Script class definition is now trivial!
class script_class_definition : public class_definition {
public:
    script_class_definition(const std::string& name) 
        : class_definition(name, class_type::script_class) {
        // That's it! Everything just works!
        // Methods, fields, inheritance, instance creation - all inherited!
    }
    
    // Helper to add script methods with AST
    void add_method_from_ast(const std::string& name, std::shared_ptr<function_decl> ast) {
        // Store AST for later compilation if needed
        method_asts_[name] = ast;
        
        // Add to methods_ as a callable function
        add_method(name, [ast, this](const std::vector<script_value>& args) -> script_value {
            // Execute through interpreter when called
            return get_interpreter()->execute_function(ast, args);
        });
    }
    
private:
    // Store ASTs for potential VM compilation later
    std::map<std::string, std::shared_ptr<function_decl>> method_asts_;
};

// VM compilation is now just an optimization pass!
void compile_script_class_to_vm(script_class_definition* script_class) {
    for (auto& [name, ast] : script_class->method_asts_) {
        auto bytecode = compile_to_bytecode(ast);
        
        // Replace interpreter execution with VM execution
        script_class->add_method(name, [bytecode](const std::vector<script_value>& args) {
            return vm_execute(bytecode, args);
        });
    }
}
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

## Simplified Migration Plan

### Phase 1: Minimal Changes to class_definition (Day 1)
1. Add `class_type::vm_class` enum value ✓
2. Add `add_script_method()` helper that wraps AST in function
3. Add optional `method_metadata_` map for optimization hints
4. **No breaking changes** - existing code unchanged

### Phase 2: Make script_class_definition inherit (Day 2)
1. Change `script_class_definition` to inherit from `class_definition`
2. Delete ALL duplicate code (it's all in the parent!)
3. Add `add_method_from_ast()` helper
4. Script classes instantly work with all C++ infrastructure!

### Phase 3: Update interpreter (Day 3)
1. `visit_class_decl` creates `script_class_definition` instead of custom type
2. Methods added via `add_method_from_ast()`
3. Delete `class_interpreter` - no longer needed!
4. Field access already works through inherited `get_field`/`set_field`

### Phase 4: Testing & Cleanup (Day 4)
1. Test script/C++ class interoperability
2. Remove old duplicate structures
3. Celebrate massive simplification!

### Why This Works So Well
- **script_value** was already designed as a universal wrapper
- **methods_** map already stores everything as callable functions  
- Script methods are just functions that call the interpreter
- VM methods are just functions that call the VM
- **Zero performance impact** - same number of indirections as before

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