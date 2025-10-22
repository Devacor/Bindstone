# VM Reference Semantics Implementation Design

## Problem Statement

The current VM implementation treats all values as copies on the stack, which means:
- `arr.push(4)` doesn't modify the original array
- `map["key"] = value` doesn't update the stored map
- Method calls that should mutate objects have no effect

## Solution Overview

Implement a reference system in the VM that mirrors the interpreter's behavior:
1. **Reference Values**: Add a new value type that points to storage locations
2. **Reference-aware Opcodes**: Implement LOAD_REFERENCE, STORE_REFERENCE, MAKE_REFERENCE
3. **Subscript References**: Make MAP_GET/ARRAY_GET return references when appropriate
4. **Method Call References**: Pass objects by reference to methods

## Design Details

### 1. Reference Value Type

Add to `script_value`:
```cpp
struct reference {
    enum class storage_type { LOCAL, GLOBAL, FIELD, SUBSCRIPT };
    storage_type type;
    union {
        struct { size_t index; } local;
        struct { std::string* name; } global;
        struct { script_value* object; std::string* field; } field;
        struct { script_value* container; script_value key; } subscript;
    };
};
```

### 2. Reference-Aware Opcodes

#### MAKE_REFERENCE
Creates a reference to a variable or subscript:
- For locals: Creates reference to stack slot
- For globals: Creates reference to environment entry
- For subscripts: Creates reference to container[key]

#### LOAD_REFERENCE
Dereferences and loads the actual value:
- Follows the reference to get the value
- Used when reference needs to be converted to value

#### STORE_REFERENCE  
Stores through a reference:
- Updates the location pointed to by the reference
- Critical for `map["key"] = value` semantics

### 3. Modified Opcodes

#### MAP_GET / ARRAY_GET
Instead of pushing values, push references when used in lvalue context:
```cpp
// In assignment: map["key"] = value
// MAP_GET pushes reference
// STORE_REFERENCE updates through reference

// In expression: x = map["key"]  
// MAP_GET pushes reference
// Auto-dereference on assignment
```

#### CALL_METHOD
Pass object as reference to allow mutations:
```cpp
// Instead of:
script_value object = pop();  // Copy

// Use:
script_value& object = peek(arg_count);  // Reference
// ... call method with reference ...
pop(); // Remove after call
```

### 4. Compiler Changes

The compiler needs to detect lvalue vs rvalue contexts:
```cpp
// Lvalue context (needs reference):
map["key"] = value;     // MAP_GET returns reference
arr[0] = value;         // ARRAY_GET returns reference

// Rvalue context (needs value):  
x = map["key"];         // MAP_GET + auto-dereference
print(arr[0]);          // ARRAY_GET + auto-dereference
```

### 5. Storage Model

References point to stable storage locations:
- **Locals**: Point to indices in VM locals array
- **Globals**: Point to environment entries
- **Fields**: Point to object fields  
- **Subscripts**: Point to container elements

## Implementation Steps

1. **Add Reference Type**: Extend script_value with reference variant
2. **Implement Reference Opcodes**: MAKE_REFERENCE, LOAD_REFERENCE, STORE_REFERENCE
3. **Update Subscript Opcodes**: MAP_GET/ARRAY_GET to return references
4. **Fix Method Calls**: Pass objects by reference
5. **Compiler Context Tracking**: Detect lvalue/rvalue contexts
6. **Auto-dereference**: Implement automatic reference-to-value conversion

## Testing Strategy

1. **Basic Reference Tests**: Test reference creation and dereferencing
2. **Mutation Tests**: Verify arr.push(), map updates persist
3. **Method Call Tests**: Ensure object methods can mutate state
4. **Chaining Tests**: Test reference chains like obj.field.method()
5. **Performance Tests**: Ensure references don't significantly impact performance

## Risks and Mitigations

1. **Dangling References**: References to deleted objects
   - Mitigation: Use weak references or validity checks

2. **Reference Cycles**: Potential memory leaks
   - Mitigation: Already using shared_ptr with cycle detection

3. **Performance Impact**: Additional indirection
   - Mitigation: Optimize common patterns, inline dereferences

## Alternative Approaches Considered

1. **Pointer-based Stack**: Store pointers instead of values
   - Rejected: Too invasive, breaks existing code

2. **Copy-on-Write**: Lazy copying
   - Rejected: Complex, doesn't match C++ semantics

3. **Hybrid Stack**: Mix values and references
   - Selected: Best balance of performance and correctness