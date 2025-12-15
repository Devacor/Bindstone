# Strong Types Design Document

## Overview

JaiScript implements a three-state type system that balances flexibility with performance optimization. Variables can be:
1. **Uninitialized** - awaiting first assignment to lock type
2. **Any-typed** - dynamically typed, allows any value
3. **Locked** - statically typed, enforces type compatibility

## Motivation

**Performance**: Current loop benchmarks show JaiScript at 28μs for 100 iterations while ChaiScript achieves 12μs. Per-iteration analysis shows ~15ns spent on type checks in `sum += i`. By trusting locked types, we can skip these checks entirely.

**Safety**: Catch type errors at assignment time rather than when the value is used incorrectly later.

**Flexibility**: `var` provides JavaScript-like opt-out for dynamic typing when needed.

This pattern is proven successful in TypeScript (strict mode with `any` escape hatch) and Dart (`dynamic` keyword).

## Declaration Syntax and Behavior

| Declaration | `type_info_` | First Assignment | Subsequent Assignments |
|-------------|--------------|------------------|------------------------|
| `auto x;` | `nullptr` | Locks to assigned type | Must be compatible |
| `auto x = 5;` | `int` | N/A (already set) | Must be compatible |
| `var x;` | `any_type` | Stores value | Any type allowed |
| `var x = 5;` | `any_type` | N/A (already set) | Any type allowed |
| `int x = 5;` | `int` | N/A (already set) | Must be int-compatible |

## Type States

### 1. Uninitialized (`type_info_ == nullptr`)

Only valid for `auto x;` declarations without an initializer.

```jai
auto x;      // type_info_ = nullptr
x = 5;       // type_info_ locks to int
x = "hello"; // ERROR: int cannot accept string
```

The first assignment determines the variable's type permanently.

### 2. Any Type (`type_info_->base_type == jai_any_type`)

Used by the `var` keyword. Provides JavaScript-like dynamic typing.

```jai
var x;       // type_info_ = any_type
x = 5;       // value is int 5, type_info_ remains any_type
x = "hello"; // OK: value is now string "hello"
x = 3.14;    // OK: value is now float 3.14
```

The `type_info_` never changes from `any_type`, but the underlying value maintains its own type for operations and casting.

### 3. Locked Type (`type_info_` is a concrete type)

Used by explicit type declarations and `auto` with initializers.

```jai
int x = 5;     // type_info_ = int, locked
x = 10;        // OK: int to int
x = 3.14;      // OK: float converts to int (truncates to 3)
x = "hello";   // ERROR: string cannot convert to int

auto y = 5.0;  // type_info_ = float, locked (inferred)
y = 10;        // OK: int converts to float
y = "hello";   // ERROR: string cannot convert to float
```

## Type Enforcement on Assignment

When assigning to a variable with a locked type:

1. **Same type**: Direct assignment (fast path)
2. **Compatible type**: Use registered type conversions
3. **Incompatible type**: Runtime error

```cpp
// Pseudo-code for assignment logic
void assign(script_value& target, const script_value& source) {
    auto* target_type = target.get_type_info();

    // Uninitialized: lock to source type
    if (target_type == nullptr) {
        target = source;  // Also copies type_info
        return;
    }

    // Any type: allow anything
    if (target_type->base_type == jai_any_type) {
        target.set_value(source);  // Keep any_type info
        return;
    }

    // Locked type: enforce compatibility
    auto source_type = source.get_base_type();
    if (target_type->base_type == source_type) {
        target.set_value(source);  // Fast path
    } else if (can_convert(source_type, target_type->base_type)) {
        target.set_value(convert(source, target_type));
    } else {
        throw type_error("Cannot assign {} to {}", source_type, target_type);
    }
}
```

## Fast Path Optimization

The primary motivation for this design is enabling fast paths in performance-critical code like loops.

### Before (current behavior)
```cpp
// Every iteration checks types
while (condition) {
    // Must verify i is still an int
    // Must verify i + 1 produces an int
    // Must verify assignment is valid
    i = i + 1;
}
```

### After (with strong types)
```cpp
// Type is locked at declaration
while (condition) {
    if (i.get_type_info()->base_type == jai_int_type) {
        // FAST PATH: Trust the type, skip all checks
        i.set_int_unchecked(i.get_int_unchecked() + 1);
    } else {
        // Slow path with full type checking
    }
}
```

### Loop Counter Optimization

For common patterns like `for (int i = 0; i < n; i++)`:

1. `i` is declared with explicit `int` type → `type_info_` locked to int
2. Loop body can use unchecked int operations
3. No per-iteration type verification needed
4. Comparison and increment use native int operations

## Implementation Checklist

### 1. Add `jai_any_type` to enum
```cpp
// In jaiscript_fwd.hpp
enum class script_value_type {
    jai_null_type,
    jai_any_type,     // NEW: for var keyword
    jai_int_type,
    // ... rest unchanged
};
```

### 2. Create any_type type_info
```cpp
// In type_info.hpp
static type_info* make_any() {
    static type_info any_info{script_value_type::jai_any_type, "any"};
    return &any_info;
}
```

### 3. Update `visit_variable_decl`
- `auto x;` → set `type_info_ = nullptr`
- `auto x = val;` → set `type_info_` from value's type
- `var x;` → set `type_info_ = make_any()`
- `int x;` → set `type_info_ = make_int()`

### 4. Update assignment operations
- Check target's `type_info_`
- Handle nullptr (lock type)
- Handle any_type (allow all)
- Handle locked types (enforce/convert)

### 5. Update fast paths
- Trust `type_info_->base_type` for locked variables
- Use unchecked accessors when type is guaranteed

## Implementation Notes (Critical Findings)

### Parser Must Distinguish `auto` from `var`

**Current problem**: Both `auto` and `var` return `nullptr` from `parse_type()`:
```cpp
// parser.cpp:530-531 - CURRENT (broken for this design)
if (match({token_type::auto_keyword, token_type::var_keyword, token_type::function_keyword})) {
    return type_info_ptr(nullptr);
}
```

**Required fix**:
```cpp
// parser.cpp - FIXED
if (match(token_type::auto_keyword)) {
    return type_info_ptr(nullptr);  // nullptr = inferred/uninitialized
}
if (match(token_type::var_keyword)) {
    return store_type_info(type_info::make_any(*symbolizer_));  // any_type
}
```

### Declared Type Must Be Propagated to script_value

**Current problem**: In `visit_variable_decl`, `decl->type` is never stored on the script_value:
```cpp
// Current code discards declared type
script_value value = make_value();
if (decl->initializer) {
    value = pop_value();  // Uses value's own type_info
}
environment_->define(decl->name_id, std::move(value));
// decl->type is LOST!
```

**Required fix**:
```cpp
// Propagate declared type
script_value value = make_value();
if (decl->initializer) {
    value = pop_value();
}
// Set declared type (for enforcement)
if (decl->type) {
    value.set_type_info(decl->type);  // Lock to declared type
}
// else: auto without init → keep nullptr for first-assignment locking
environment_->define(decl->name_id, std::move(value));
```

### Assignment Has Multiple Code Paths

Type enforcement must be added to ALL assignment paths in `visit_assignment_expr`:
1. Regular variable assignment (line ~3079-3089)
2. Reference assignment (line ~3021-3023)
3. C++ bound assignment (line ~3024-3026)
4. weak_ptr assignment (line ~3027-3058)
5. shared_ptr assignment (line ~3059-3078)
6. Member/property assignment
7. Subscript assignment

**Recommendation**: Create a central `enforce_type_compatibility()` function and call it from all paths.

### nullptr Already Has Multiple Meanings

Currently `type_info_ == nullptr` means:
- AST literals (parser creates without engine context)
- Default-constructed script_values (for std::map)
- Monostate/null values

**Decision**: Use nullptr for "uninitialized auto" is acceptable because:
- AST literals are never stored in environments
- Default-constructed values are immediately overwritten
- Null values (`make_value()`) will get explicit null type_info

## Comparison with Other Languages

### TypeScript
TypeScript uses type inference with locking:
```typescript
let x = 5;        // x: number (inferred, locked)
x = "hello";      // Error: Type 'string' is not assignable to type 'number'

let y: any = 5;   // y: any (explicit)
y = "hello";      // OK
```

JaiScript's `auto` mirrors TypeScript's `let` with inference, and `var` mirrors TypeScript's `any`.

### JavaScript
```javascript
let x = 5;        // x can be anything
x = "hello";      // OK
```

JaiScript's `var` provides JavaScript-like flexibility when needed.

### Jai (inspiration)
Jai uses strong static typing. JaiScript extends this with runtime flexibility options while maintaining the ability to optimize when types are known.

## Error Messages

```
TypeError: Cannot assign string to variable 'x' of type int
  at line 5: x = "hello";

TypeError: Variable 'x' is uninitialized, cannot read value
  at line 3: print(x);

Note: Use 'var' for dynamic typing: var x;
```

## Performance Impact Analysis

| Scenario | Expected Impact |
|----------|-----------------|
| `var x = 5; x += 1;` | **Same as today** - any_type check is O(1) |
| `int x = 5; x += 1;` | **Faster** - can use unchecked fast path |
| `auto x = 5; x += 1;` | **Faster** - type inferred and locked |
| Assignment to typed var | **Tiny overhead** - one type comparison |
| Loop with `int i` | **Potentially 2x faster** - skip type dispatch |

The design trades a single `if (type_info_->base_type == target_type)` check at assignment for eliminating repeated checks in loops and operations.

### Expected Loop Improvement

Current per-iteration cost (~25ns):
- condition: ~3ns
- body (sum += i): ~15ns (includes type checks)
- increment: ~7ns

With strong types (estimated ~15-18ns):
- condition: ~3ns
- body (sum += i): ~8-10ns (skip type dispatch, use unchecked)
- increment: ~4-5ns (skip type dispatch)

**Target**: Reduce loop overhead by 30-40%, bringing JaiScript closer to ChaiScript's loop performance while maintaining type safety.

## Future Considerations

1. **Type annotations in function parameters**: `fn foo(x: int) { }`
2. **Return type declarations**: `fn bar() -> int { return 5; }`
3. **Const declarations**: `const x = 5;` (locked and immutable)
4. **Union types**: `var x: int | string;`

## Implementation Order

1. ✅ Add `jai_any_type` to enum
2. ✅ Create `make_any()` factory
3. ✅ Fix parser to distinguish `auto` from `var`
4. ✅ Propagate `decl->type` to script_value in `visit_variable_decl`
5. ✅ Create `enforce_type_compatibility()` helper
6. ✅ Update simple assignment to use enforcement
7. ✅ Update compound assignments
8. ✅ Update member assignments
9. ✅ Add fast paths that trust locked types
10. ✅ Add tests for all type enforcement scenarios
