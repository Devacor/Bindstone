# Strong Types Design Document

## Overview

JaiScript implements a three-state type system that balances flexibility with performance optimization. Variables can be:
1. **Uninitialized** - awaiting first assignment to lock type
2. **Any-typed** (`var`) - dynamically typed, allows any value
3. **Locked** (`auto` with initializer, or explicit type) - statically typed, enforces type compatibility

## Motivation

**Performance**: By trusting locked types, we can skip runtime type checks in hot paths. Loop variables declared as `int i` use native integer operations without type dispatch.

**Safety**: Catch type errors at assignment time rather than when the value is used incorrectly later.

**Flexibility**: `var` provides JavaScript-like opt-out for dynamic typing when needed.

This pattern is proven successful in TypeScript (strict mode with `any` escape hatch) and Dart (`dynamic` keyword).

## Declaration Syntax and Behavior

| Declaration | Type State | First Assignment | Subsequent Assignments |
|-------------|------------|------------------|------------------------|
| `auto x;` | Uninitialized | Locks to assigned type | Must be compatible |
| `auto x = 5;` | Locked (`int`) | N/A | Must be int-compatible |
| `var x;` | Any | Stores value | Any type allowed |
| `var x = 5;` | Any | N/A | Any type allowed |
| `int x = 5;` | Locked (`int`) | N/A | Must be int-compatible |

## Type States

### 1. Uninitialized (`auto x;`)

Only valid for `auto` declarations without an initializer. The first assignment determines the variable's type permanently.

```jai
auto x;      // Uninitialized
x = 5;       // Locks to int
x = "hello"; // ERROR: int cannot accept string
```

### 2. Any Type (`var`)

Used by the `var` keyword. Provides JavaScript-like dynamic typing.

```jai
var x;       // Any type
x = 5;       // value is int 5
x = "hello"; // OK: value is now string "hello"
x = 3.14;    // OK: value is now float 3.14
```

### 3. Locked Type

Used by explicit type declarations and `auto` with initializers.

```jai
int x = 5;     // Locked to int
x = 10;        // OK: int to int
x = 3.14;      // OK: float converts to int (truncates to 3)
x = "hello";   // ERROR: string cannot convert to int

auto y = 5.0;  // Locked to float (inferred)
y = 10;        // OK: int converts to float
y = "hello";   // ERROR: string cannot convert to float
```

## Container Type Semantics

### Array Types

| Declaration | Element Behavior | Example |
|-------------|------------------|---------|
| `auto x = [1, 2, 3];` | Homogeneous required | All elements must be same type |
| `array<auto> x = [1, 2, 3];` | Homogeneous required | Type deduced from first element |
| `var x = [1, "two", 3.0];` | Heterogeneous allowed | Any element types |
| `array<var> x = [1, "two"];` | Heterogeneous allowed | Explicit dynamic elements |
| `array<int> x = [1, 2, 3];` | Typed elements | All elements must be int |

```jai
// Homogeneous arrays (auto)
auto nums = [1, 2, 3];           // OK: all ints
auto mixed = [1, "two"];         // ERROR: mixed types not allowed

// Heterogeneous arrays (var)
var mixed = [1, "two", 3.14];    // OK: var allows any types
array<var> items = [1, null];    // OK: explicit dynamic typing

// Typed arrays
array<int> ids = [1, 2, 3];      // OK: all ints
array<int> bad = [1, "two"];     // ERROR: string not int
```

### Map Types

| Declaration | Value Behavior | Example |
|-------------|----------------|---------|
| `auto m = {{"a", 1}, {"b", 2}};` | Homogeneous values | All values same type |
| `map<string, auto> m = {...};` | Homogeneous values | Value type from first |
| `var m = {{"a", 1}, {"b", "x"}};` | Heterogeneous allowed | Any value types |
| `map<string, var> m = {...};` | Heterogeneous allowed | Explicit dynamic values |
| `map<string, int> m = {...};` | Typed values | All values must be int |

```jai
// Homogeneous maps (auto)
auto scores = {{"alice", 100}, {"bob", 85}};     // OK: all int values
auto mixed = {{"name", "Alice"}, {"age", 30}};   // ERROR: mixed value types

// Heterogeneous maps (var)
var data = {{"name", "Alice"}, {"age", 30}};     // OK: var allows mixed
map<string, var> record = {...};                  // OK: explicit dynamic

// Typed maps
map<string, int> ages = {{"alice", 30}};         // OK: all int values
```

### Nested Container Validation

Homogeneity is validated recursively to arbitrary depth:

```jai
// 2D arrays
auto grid = [[1, 2], [3, 4]];              // OK: all inner arrays contain ints
auto bad = [[1, 2], [3, "x"]];             // ERROR: inner array has mixed types

// 3D arrays
auto cube = [[[1, 2]], [[3, 4]]];          // OK: homogeneous at all levels
auto bad = [[[1, 2]], [[3, "x"]]];         // ERROR: deepest level mixed

// Nested maps
auto nested = {{"a", {{"x", 1}}}};         // OK: inner maps have int values
auto bad = {{"a", {{"x", 1}, {"y", "z"}}}}; // ERROR: inner map mixed

// Mixed containers
auto data = [[{{"k", 1}}]];                // OK: homogeneous throughout
var flexible = [[{{"k", 1}, {"v", "x"}}]]; // OK: var allows any nesting
```

## Type Enforcement on Assignment

When assigning to a variable with a locked type:

1. **Same type**: Direct assignment (fast path)
2. **Compatible type**: Use registered type conversions
3. **Incompatible type**: Runtime error

## Fast Path Optimization

The primary motivation for this design is enabling fast paths in performance-critical code.

### Loop Counter Optimization

For common patterns like `for (int i = 0; i < n; i++)`:

1. `i` is declared with explicit `int` type → locked
2. Loop body can use unchecked int operations
3. No per-iteration type verification needed
4. Comparison and increment use native int operations

## Comparison with Other Languages

### TypeScript
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

TypeError: Array element type mismatch at index 1: expected int, got string
  at line 3: auto arr = [1, "two"];

TypeError: Variable 'x' is uninitialized, cannot read value
  at line 3: print(x);

Note: Use 'var' for dynamic typing: var x;
```

## Performance Impact Analysis

| Scenario | Impact |
|----------|--------|
| `var x = 5; x += 1;` | Standard - any_type check is O(1) |
| `int x = 5; x += 1;` | Faster - uses unchecked fast path |
| `auto x = 5; x += 1;` | Faster - type inferred and locked |
| `auto arr = [1,2,3];` | Small overhead - homogeneity check at declaration |
| `var arr = [1,2,3];` | No validation - heterogeneous allowed |
| Loop with `int i` | Significantly faster - skip type dispatch |

The design trades a single type comparison at assignment/declaration for eliminating repeated checks in loops and operations.

## Future Considerations

1. ✅ **Typed function parameters** — SHIPPED with C-style syntax: `int foo(int x) { }` (plus the
   shorthands `int: x` and `:x`; see `grammar.md`), not the speculated `fn foo(x: int)` form.
2. ✅ **Return type declarations** — SHIPPED: `int bar() { return 5; }` and trailing
   `auto bar() -> int { ... }`.
3. **Const declarations**: `const x = 5;` — still open. `const` currently parses ONLY on
   range-`for` bindings (`for (const auto& x : xs)`).
4. **Union types**: `var x: int | string;` — still open.

For compile-time enforcement of this ladder (off/warn/strict, narrowing diagnostics, jaibite
`checked_clean()`), see **`static_checking.md`** — the static companion to this runtime doc.
