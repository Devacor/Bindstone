# shared_ptr<T> and weak_ptr<T> Refactoring Plan

## Current Problem

The current implementation of `shared_ptr<T>` creates an unnecessary nested ownership structure that breaks `weak_ptr<T>` reference counting:

- **Outer layer**: `shared_ptr<script_value>` (stored in variant at value.hpp:913)
- **Inner layer**: `shared_ptr<object_holder>` (stored in variant at value.hpp:910)

When a `weak_ptr<T>` is created, it stores `weak_ptr<object_holder>`, which only tracks the **inner** shared_ptr. This misses the **outer** ownership layer, causing objects to be destroyed prematurely when all outer references are released, even though weak_ptr expects them to remain alive.

### Example of the Problem

```javascript
auto obj = shared_ptr<LifetimeTest>(LifetimeTest("test1"));
auto weak = weak_ptr<LifetimeTest>(obj);
auto locked = weak.lock();  // Creates NEW object_holder (reference count = 1)

obj = null;      // Outer shared_ptr<script_value> destroyed
locked = null;   // Inner object_holder destroyed - object gone!

// But weak_ptr still thinks it's tracking the object
auto locked2 = weak.lock();  // Should return null, but behavior is undefined
```

## Correct Design

`shared_ptr<T>` should be **syntactic explicitness** for opting into pointer/refcounting semantics. It should be a **TYPE MARKER ONLY**, not a storage structure change.

### Key Principles

1. **Storage should be identical**: Both `LifetimeTest` and `shared_ptr<LifetimeTest>` should use the same `shared_ptr<object_holder>` storage (variant index 10 at value.hpp:910)

2. **Only cloning behavior differs**:
   - **Default (no wrapper)**: Clone on copy (value semantics)
   - **shared_ptr<T>**: Don't clone on copy (reference semantics via shared_ptr)

3. **weak_ptr<T> works naturally**: Since both types use the same `shared_ptr<object_holder>` storage, `weak_ptr<object_holder>` correctly tracks all references

### Visual Comparison

#### Current (Wrong)
```
Regular object:     shared_ptr<object_holder>
shared_ptr<T>:      shared_ptr<script_value> -> shared_ptr<object_holder>
                    ^^^^^^^^^^^^^^^^^^^^^^^^    ^^^^^^^^^^^^^^^^^^^^^^^^^
                         OUTER LAYER                 INNER LAYER
weak_ptr<T>:        weak_ptr<object_holder>  (only tracks inner!)
```

#### Correct Design
```
Regular object:     shared_ptr<object_holder>  + type_info (clone on copy)
shared_ptr<T>:      shared_ptr<object_holder>  + type_info (share on copy)
weak_ptr<T>:        weak_ptr<object_holder>    (tracks the SAME shared_ptr!)
```

## Behavioral Examples

### Value Semantics (Default)
```javascript
class Counter {
    int value = 0;
    auto increment() { value = value + 1; }
}

auto a = Counter();
auto b = a;  // CLONES the object
a.increment();
print(a.value);  // 1
print(b.value);  // 0 (separate copy)
```

### Reference Semantics (shared_ptr<T>)
```javascript
auto a = shared_ptr<Counter>(Counter());
auto b = a;  // SHARES the object (no clone)
a.increment();
print(a.value);  // 1
print(b.value);  // 1 (same object)
```

### Note: Script Classes Already Have Reference Semantics

**IMPORTANT**: Script classes are already internally stored as `shared_ptr<object_holder>`, so they **already have reference semantics by default**:

```javascript
class Counter {
    int value = 0;
    auto increment() { value = value + 1; }
}

auto a = Counter();
auto b = a;  // NO CLONE - script classes share by default
a.increment();
print(a.value);  // 1
print(b.value);  // 1 (same object - reference semantics!)
```

This means:
- For **script classes**: `shared_ptr<Counter>` is redundant (but allowed for explicitness)
- For **primitive types and containers**: `shared_ptr<T>` opts into reference semantics
- The refactor should NOT change this existing behavior

### Primitives and shared_ptr<int> - Automatic Wrapping

**IMPORTANT**: Primitives like `int`, `float`, `string`, etc. are stored directly in the variant (not as `object_holder`). To support `shared_ptr<int>`, we automatically wrap primitives when needed.

```javascript
// Use case:
auto a = shared_ptr<int>(42);
auto b = a;
a = 100;
// b should still be 42 (each has its own shared_ptr to the wrapped value)
```

**Solution**: When creating `shared_ptr<int>`, automatically wrap the primitive in an `object_holder`:

```cpp
// In shared_ptr<T> constructor
auto value = pop_value();

// Check if it's a primitive type
bool is_primitive = (value.type() == script_value_type::jai_int_type ||
                     value.type() == script_value_type::jai_float_type ||
                     value.type() == script_value_type::jai_bool_type ||
                     value.type() == script_value_type::jai_string_type ||
                     value.type() == script_value_type::jai_char_type);

if (is_primitive) {
    // Wrap the primitive in an object_holder
    auto holder = std::make_shared<object_holder>();
    holder->type_name = "__wrapped_" + value.get_type_info()->type_name();

    // Store the primitive value in the holder
    holder->set_field("__value", value);

    // Create the wrapped script_value
    script_value wrapped(std::monostate{}, engine_ref_);
    wrapped.set_object_holder(holder);
    wrapped.set_type_info(type_info::make_shared_ptr(value.get_type_info()));

    push_value(std::move(wrapped));
    return checked_result<void>();
}
```

**Accessing Wrapped Primitives**: Need transparent unwrapping when used in expressions:

```cpp
// When a shared_ptr<int> is used in an operation (e.g., addition, comparison)
// The interpreter should automatically unwrap it:

auto unwrap_if_needed(script_value& val) -> script_value {
    if (val.type() == script_value_type::jai_object_type &&
        val.get_type_info()->base_type == script_value_type::jai_shared_ptr_type &&
        val.get_type_info()->element_type()->is_primitive()) {
        // This is a wrapped primitive - unwrap it
        auto holder = val.get_object_holder();
        return holder->get_field("__value");
    }
    return val;
}
```

**Usage Example**:
```javascript
auto a = shared_ptr<int>(42);
auto b = a;  // Both point to the same wrapped int

print(a);    // Prints: 42 (automatically unwrapped)
print(a + 1);  // Prints: 43 (unwrapped for operation)

// Reference semantics demonstrated:
// Note: Can't actually mutate the int directly since it's wrapped
// This is more useful for passing around without cloning
```

**Pros**:
- Uniform storage model across all types
- `shared_ptr<int>` works intuitively
- Users don't need manual wrapper classes
- Transparent unwrapping makes it seamless

**Cons**:
- Slight allocation overhead for wrapped primitives
- Primitives become immutable when wrapped (can't do `a = 100` to change the wrapped value)
- Need unwrapping logic in operations

**Note**: Wrapped primitives are immutable. If you want to mutate a shared primitive, wrap it in a class:

```javascript
class IntBox {
    int value = 0;
}

auto a = IntBox();  // Script classes already have reference semantics
auto b = a;
a.value = 100;
// b.value is also 100 (truly shared and mutable)
```

### Containers (Arrays and Maps)

Arrays and maps are already stored as `shared_ptr` in the variant:
- `std::shared_ptr<std::vector<script_value>>` (variant index 7)
- `std::shared_ptr<std::map<script_value, script_value>>` (variant index 8)

This means **containers already have reference semantics by default**, similar to script classes:

```javascript
auto arr1 = [1, 2, 3];
auto arr2 = arr1;  // NO CLONE - arrays share by default
arr1[0] = 99;
print(arr2[0]);  // 99 (same array)
```

**Implication**: `shared_ptr<Array<int>>` is redundant and should probably be forbidden (similar to primitives), since arrays are already shared. However, this may be allowed for consistency with the type system.

**Recommendation**:
- **Option A**: Forbid `shared_ptr<Array<T>>` and `shared_ptr<Map<K,V>>` since they're already shared
- **Option B**: Allow it but treat it as a no-op (just marks the type, doesn't change behavior)

Either way, the refactoring steps remain the same - containers won't need special handling beyond what's already described.

### Summary: What Has Reference Semantics?

| Type | Default Behavior | shared_ptr<T> Allowed? | Notes |
|------|------------------|----------------------|-------|
| Script classes (`Counter`, `Node`, etc.) | Reference semantics (already `shared_ptr<object_holder>`) | **Yes** (redundant but allowed for explicitness) | Most common use case |
| Primitives (`int`, `float`, `string`, `bool`, `char`) | Value semantics (direct in variant) | **Yes** (automatically wrapped in object_holder) | Wrapped primitives are immutable, overhead for wrapping |
| Arrays (`Array<T>` or `[...]`) | Reference semantics (already `shared_ptr<vector>`) | Maybe (see recommendation) | Already shared by default, so redundant |
| Maps (`Map<K,V>` or `{...}`) | Reference semantics (already `shared_ptr<map>`) | Maybe (see recommendation) | Already shared by default, so redundant |
| Functions | Reference semantics (already `shared_ptr`) | Maybe | Rare use case |

**Key Insight**: In JaiScript, **only primitives have value semantics by default**. Everything else (classes, containers, functions) already has reference semantics. This is why `shared_ptr<T>` is most useful for primitives or explicitness with classes.

**Automatic Wrapping**: When you create `shared_ptr<int>(42)`, the primitive is automatically wrapped in an `object_holder` to provide uniform storage, then transparently unwrapped when used in operations.

## Reference Handles (Separate Mechanism)

JaiScript also supports **reference handles** via `auto&`:

```javascript
auto obj = Counter();
auto& ref = obj;  // Reference handle
ref.increment();  // Modifies original
```

This is a **separate mechanism** that:
- Uses explicit reference holders (variant index 12 at value.hpp:912)
- Requires `.deref()` in the C++ layer to access the underlying value
- Is **not** related to shared_ptr/weak_ptr

## Refactoring Steps

### 1. Update Type System

**File**: `include/jaiscript/core/value.hpp`

The type info already has `base_type == jai_shared_ptr_type` to mark shared_ptr types. This should be retained as the type marker.

**No changes needed** - the type system is correct.

### 2. Change Value Storage Logic

**File**: `source/implementation/value.cpp`

Currently, `shared_ptr<T>` values are stored as `shared_ptr<script_value>` (variant index 12). This needs to change:

**Current code** (approximate location):
```cpp
// Creating a shared_ptr<T> value
auto obj_value = script_value::make_object(...);
auto wrapped = std::make_shared<script_value>(std::move(obj_value));
result.set_shared_ptr(wrapped);  // Stores shared_ptr<script_value>
```

**Should be**:
```cpp
// Creating a shared_ptr<T> value
auto obj_value = script_value::make_object(...);
obj_value.set_type_info(type_info::make_shared_ptr(element_type));  // Mark as shared_ptr TYPE
result = std::move(obj_value);  // Store directly as object_holder
```

The key change: **Don't wrap in `shared_ptr<script_value>`**, just mark the type as `jai_shared_ptr_type`.

### 3. Update Clone Logic

**File**: `source/implementation/interpreter.cpp` or wherever clone/copy happens

Add a check: **If type is `jai_shared_ptr_type`, don't clone, just copy the shared_ptr**:

```cpp
script_value script_value::clone() const {
    // Check if this is a shared_ptr type - don't clone, just share
    if (type_info_ && type_info_->base_type == script_value_type::jai_shared_ptr_type) {
        // Just copy the storage (shares the shared_ptr<object_holder>)
        return *this;  // Shallow copy
    }

    // Otherwise, perform deep clone (existing logic)
    // ...
}
```

### 4. Update shared_ptr<T> Constructor

**File**: `source/implementation/interpreter.cpp`

**Current location**: Around lines 4165-4185 (shared_ptr constructor implementation)

**Current behavior**: Wraps object in `shared_ptr<script_value>`

**Should be** (with automatic primitive wrapping):
```cpp
// Get the value to wrap
auto value = pop_value();

// Check if it's already a shared_ptr
if (value.type() == script_value_type::jai_shared_ptr_type) {
    push_value(std::move(value));
    return checked_result<void>();
}

// Check if it's a primitive type
bool is_primitive = (value.type() == script_value_type::jai_int_type ||
                     value.type() == script_value_type::jai_float_type ||
                     value.type() == script_value_type::jai_bool_type ||
                     value.type() == script_value_type::jai_string_type ||
                     value.type() == script_value_type::jai_char_type);

if (is_primitive) {
    // Wrap the primitive in an object_holder
    auto holder = std::make_shared<object_holder>();
    holder->type_name = "__wrapped_" + value.get_type_info()->type_name();
    holder->set_field("__value", value);

    // Create the wrapped script_value
    script_value wrapped(std::monostate{}, engine_ref_);
    wrapped.set_object_holder(holder);
    wrapped.set_type_info(type_info::make_shared_ptr(value.get_type_info()));

    push_value(std::move(wrapped));
    return checked_result<void>();
}

// For objects, just mark as shared_ptr type (no wrapping needed)
if (value.type() == script_value_type::jai_object_type) {
    auto element_type = value.get_type_info();
    value.set_type_info(type_info::make_shared_ptr(element_type));
    push_value(std::move(value));
    return checked_result<void>();
}

// Unsupported type (functions, arrays, maps - may need special handling)
return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));
```

**Note**: This implementation automatically wraps primitives in `object_holder` to provide uniform storage. Arrays and maps may need similar handling or should be forbidden (since they're already reference types).

### 5. Add Automatic Unwrapping for Wrapped Primitives

**File**: `source/implementation/interpreter.cpp`

**New helper function** to transparently unwrap `shared_ptr<int>` etc. when used in operations:

```cpp
// Helper to unwrap shared_ptr primitives when needed
script_value unwrap_if_primitive(const script_value& val) {
    // Check if this is a wrapped primitive
    if (val.type() == script_value_type::jai_object_type &&
        val.get_type_info() &&
        val.get_type_info()->base_type == script_value_type::jai_shared_ptr_type &&
        val.get_type_info()->element_type() &&
        val.get_type_info()->element_type()->is_primitive()) {

        // This is a shared_ptr<int> or similar - unwrap it
        auto holder = val.get_object_holder();
        if (holder) {
            return holder->get_field("__value");
        }
    }
    return val;
}
```

**Use in operations**: Call this before arithmetic, comparison, or other primitive operations:

```cpp
// In addition operator
auto rhs = unwrap_if_primitive(pop_value());
auto lhs = unwrap_if_primitive(pop_value());
// Now perform addition on unwrapped values...
```

**Use in print/display**: Call when converting to string for output

### 6. Update Method Forwarding

**File**: `source/implementation/interpreter.cpp`

**Current location**: Around lines 1450-1480 (method call resolution)

Currently, there's logic to unwrap `shared_ptr<T>` to access methods:

```cpp
if (obj.type() == script_value_type::jai_shared_ptr_type) {
    // Unwrap to get the underlying object
    auto holder = obj.get_object_holder();
    // ... call method on holder
}
```

**After refactor**: This logic should be **removed** or simplified, since `shared_ptr<T>` and regular objects now use the same storage. Method calls work identically:

```cpp
// No special case needed - both types have object_holder
auto holder = obj.get_object_holder();
// ... call method on holder
```

**Note**: For wrapped primitives (shared_ptr<int>), there are no methods to call, so this isn't an issue.

### 7. Simplify weak_ptr.lock()

**File**: `source/implementation/interpreter.cpp`

**Current location**: Lines 486-529

**Current code**: Complex logic to handle both object and shared_ptr types differently

**After refactor**: Much simpler, since both types use the same storage:

```cpp
auto weak_ptr = self.get_weak_ptr();
if (auto locked = weak_ptr.lock()) {
    // Reconstruct a script_value from the object_holder
    script_value result(std::monostate{}, interp->get_engine_ref());

    // Preserve the original type info (including shared_ptr marker)
    auto weak_type_info = self.get_type_info();
    if (weak_type_info && weak_type_info->element_type()) {
        result.set_type_info(weak_type_info->element_type());
    } else {
        result.set_type_info(type_info::make_object(locked->type_name));
    }

    // Directly assign the locked shared_ptr
    result.set_object_holder(locked);

    return result;
}
// Return null if expired
return script_value(std::monostate{}, interp->get_engine_ref());
```

### 8. Update weak_ptr Constructor

**File**: `source/implementation/interpreter.cpp`

**Current location**: Lines 4207-4227

**Current code**: Has special handling to unwrap shared_ptr types

**After refactor**: Simplified, since both types use same storage:

```cpp
auto obj = pop_value();

// Check if obj is an object or shared_ptr
if (obj.type() != script_value_type::jai_object_type &&
    obj.type() != script_value_type::jai_shared_ptr_type) {
    return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));
}

// No unwrapping needed - both types have object_holder (including wrapped primitives)
push_value(script_value::make_weak_ptr(obj, engine_ref_));
return checked_result<void>();
```

### 9. Update weak_from_this()

**File**: `source/implementation/interpreter.cpp`

**Current location**: Search for "weak_from_this" implementation

Should work naturally after the refactor, but verify it correctly:
1. Gets the current object's `shared_ptr<object_holder>`
2. Creates a `weak_ptr<object_holder>` from it
3. Wraps in script_value with type `jai_weak_ptr_type`

**Note**: wrapped primitives (shared_ptr<int>) don't have methods, so `weak_from_this()` won't be callable on them.

### 10. Remove Unused Variant Type (Optional Cleanup)

**File**: `include/jaiscript/core/value.hpp`

**Current location**: Line 913

```cpp
std::shared_ptr<script_value>,               // shared_ptr<T>
```

This variant type is no longer needed since `shared_ptr<T>` uses the same storage as regular objects. However, **removing it requires reindexing all variant access**, so it may be safer to:

**Option A**: Leave it in the variant but never use it
**Option B**: Remove it and update all variant index constants (more invasive but cleaner)

## Testing Strategy

After implementing the refactor, verify:

### 1. Reference Semantics Work
```javascript
auto a = shared_ptr<Counter>(Counter());
auto b = a;
a.increment();
// Verify: b.value == a.value (shared, not cloned)
```

### 2. Value Semantics Still Work (for primitive types)
```javascript
auto a = Counter();  // Script class - has reference semantics by default
auto b = a;
a.increment();
// Verify: b.value == a.value (script classes are already shared)
```

### 3. weak_ptr Lifetime Tracking
```javascript
auto obj = shared_ptr<LifetimeTest>(LifetimeTest("test"));
auto weak = weak_ptr<LifetimeTest>(obj);
obj = null;
auto locked = weak.lock();
// Verify: locked == null and weak.expired() == true
```

### 4. weak_ptr Circular References
```javascript
auto node1 = shared_ptr<Node>(Node());
auto node2 = shared_ptr<Node>(Node());
node1.next = node2;
node2.prev = weak_from_this();  // From node1's context
// Verify: No memory leak, nodes destroyed when out of scope
```

### 5. Existing Tests Pass
Run the full test suite, especially:
- `weak_ptr_script_class_lifetime`
- `weak_ptr_circular_reference_script`
- `weak_ptr_script_class_simplified`
- All method forwarding tests

## Expected Outcomes

1. **weak_ptr_script_class_lifetime test passes**: Objects are correctly destroyed when all strong references are gone

2. **Simplified codebase**: Removal of nested shared_ptr logic in method forwarding, weak_ptr.lock(), constructors

3. **Correct semantics**: `shared_ptr<T>` is truly just a type marker for "don't clone on copy"

4. **No behavior change**: Script classes still have reference semantics by default (this is preserved)

## Files to Modify

1. **source/implementation/value.cpp**: Update shared_ptr value creation logic
2. **source/implementation/interpreter.cpp**:
   - Update shared_ptr constructor (~lines 4165-4185)
   - Simplify weak_ptr.lock() (~lines 486-529)
   - Simplify weak_ptr constructor (~lines 4207-4227)
   - Remove/simplify method forwarding special cases (~lines 1450-1480)
   - Update clone/copy logic (search for clone operations)
3. **include/jaiscript/core/value.hpp**: Optionally remove unused variant type (line 913)

## Risks and Considerations

1. **Clone behavior for script classes**: The refactor should NOT change the fact that script classes already have reference semantics. Ensure clone logic respects this.

2. **Type info propagation**: Ensure the `jai_shared_ptr_type` marker is preserved through all value operations (assignment, function calls, returns)

3. **Backward compatibility**: Code using `shared_ptr<T>` should continue to work, just with simpler internals

4. **Performance**: Should be neutral or improved (one less indirection level)

## Summary

The refactor changes `shared_ptr<T>` from a **storage mechanism** to a **type marker** that affects **clone behavior only**. This eliminates the nested ownership problem and makes `weak_ptr<T>` work correctly.

**Before**: `shared_ptr<T>` = different storage + different cloning
**After**: `shared_ptr<T>` = same storage + different cloning
