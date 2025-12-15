# JaiScript Type Safety Improvements

This document tracks the type safety audit findings, planned improvements, and test coverage for JaiScript's type system.

## Current Status: ~85% Type Safe

JaiScript has a sophisticated type system with excellent foundations, but several inconsistencies need addressing.

---

## Critical Issues (Priority 1)

### Issue 1: Equality vs Arithmetic Inconsistency

**Problem:**
```javascript
5 + 5.0       // -> 10.0 (implicit int->float conversion)
5 == 5.0      // -> false (NO conversion - different types!)
5 + 5.0 == 10.0  // -> true (works because left side is already float)
```

**Impact:** Extremely confusing. Arithmetic converts types implicitly, but equality does not.

**Location:** `interpreter_dispatch.cpp` - `handle_equal()` returns false for mismatched types

**Fix Options:**
- **Option A (Recommended):** Allow numeric type comparison in `==`
- **Option B:** Require explicit casts for mixed int+float arithmetic

**Test:** `type_safety_tests.cpp` - `equality_int_float_inconsistency`

---

### Issue 2: Logical Operators Return Non-Boolean

**Problem:**
```javascript
5 && 10       // -> 10 (returns right operand, NOT true!)
0 || "hello"  // -> "hello" (returns right operand, NOT false!)
(a && b) == true  // May fail unexpectedly
```

**Impact:** JavaScript-style short-circuit that returns values instead of booleans.

**Location:** `interpreter.cpp` lines 2331-2354

**Fix:** Change to return `make_value(bool)` instead of operand values

**Test:** `type_safety_tests.cpp` - `logical_and_returns_non_boolean`, `logical_or_returns_non_boolean`

---

### Issue 3: cpp_bound_ptr_ Has No Type Safety

**Problem:**
```cpp
void* cpp_bound_ptr_ = nullptr;  // NO TYPE INFO!
*static_cast<int*>(cpp_bound_ptr_) = value;  // Assumes correct type
```

**Impact:** Memory corruption if type mismatch with bound C++ values.

**Location:** `value.hpp` line 1228

**Fix:** Add `std::type_index cpp_bound_type_` alongside the pointer

**Test:** Requires C++ integration test

---

### Issue 4: Dual Conversion Pathways

**Problem:** Two separate conversion functions with different logic:
- `enforce_type_compatibility()` - for assignments
- `try_convert_for_parameter()` - for function calls

**Impact:** Inconsistent behavior - assignment may accept what function calls reject.

**Location:**
- `interpreter.cpp` lines 3763-3912 (assignments)
- `interpreter.cpp` lines 7874-8099 (parameters)

**Fix:** Unify into single `convert_value()` function

---

## High Priority Issues (Priority 2)

### Issue 5: Empty Arrays/Maps Are Truthy

**Problem:**
```javascript
if ([]) { print("runs!"); }   // Executes - empty array is truthy
if ({}) { print("runs!"); }   // Executes - empty map is truthy
```

**Impact:** Counter-intuitive. Most languages treat empty containers as falsy.

**Location:** `interpreter.hpp` lines 840-854 - `is_truthy()`

**Fix:**
```cpp
case 6: return !value.get_array_storage()->empty();  // Array
case 7: return !value.get_map_storage()->empty();    // Map
```

**Test:** `type_safety_tests.cpp` - `empty_array_is_truthy`, `empty_map_is_truthy`

---

### Issue 6: Object Equality Always False

**Problem:**
```javascript
auto a = Point(1, 2);
auto b = Point(1, 2);
a == b  // -> false (no operator== support)
a == a  // -> false (even comparing to itself!)
```

**Impact:** Cannot compare objects for equality.

**Location:** `interpreter_dispatch.cpp` - `handle_equal()` defaults to false for objects

**Fix:**
1. Check for `operator==` or `equals()` method on objects
2. Fall back to reference equality (same object_holder pointer)

**Test:** `type_safety_tests.cpp` - `object_equality_same_values`, `object_self_equality`

---

### Issue 7: Float->Int Truncation Is Silent

**Problem:**
```javascript
int x = 3.9;  // x = 3 (truncated, no warning)
auto y = 5;
y = 9.7;      // y = 9 (truncated, no warning)
```

**Impact:** Data loss without any indication.

**Test:** `type_safety_tests.cpp` - `float_to_int_truncation_silent`

---

## Medium Priority Issues (Priority 3)

### Issue 8: shared_ptr Type Stored in Multiple Places

**Problem:** Type info for `shared_ptr<T>` exists in both:
- `type_info->type_name`
- `class_instance->get_class_name()`

**Impact:** Potential inconsistency if one is updated but not the other.

**Fix:** Single source of truth - derive from type_info only

---

### Issue 9: Array/Map Element Types Not Validated

**Problem:**
```javascript
array<int> nums = [1, "hello", 3.14];  // Mixed types accepted!
```

**Impact:** Generic type parameters are not enforced during construction.

**Fix:** Validate element types match declared type during array/map initialization

**Test:** `type_safety_tests.cpp` - `typed_array_accepts_wrong_element_type`

---

### Issue 10: Inheritance Only Checks First Parent

**Problem:** Multiple inheritance chains only walk first parent.

**Location:** `interpreter.cpp` lines 3856-3869

**Fix:** Check all parent classes, not just first

---

## Implementation Plan

### Phase 1: Tests (Current)
- [x] Create `type_safety_tests.cpp` with failing tests for known issues
- [x] Create passing tests for working type safety features
- [ ] Integrate into CI/build system

### Phase 2: Critical Fixes
- [ ] Fix equality operator for numeric types
- [ ] Fix logical operators to return boolean
- [ ] Add cpp_bound_ptr_ type tracking
- [ ] Unify conversion pathways

### Phase 3: High Priority Fixes
- [ ] Fix empty container truthiness
- [ ] Add object equality support
- [ ] Add truncation warnings (optional/configurable)

### Phase 4: Medium Priority
- [ ] Single source of truth for shared_ptr types
- [ ] Element type validation for typed containers
- [ ] Full inheritance chain checking

---

## Test Coverage Summary

| Test File | Tests | Purpose |
|-----------|-------|---------|
| `type_conversion_tests.cpp` | 11 | Basic primitive conversions |
| `strong_types_tests.cpp` | 74 | auto/var type locking |
| `constructor_conversion_tests.cpp` | 32 | Implicit constructor conversions |
| `type_safety_tests.cpp` | NEW | Known issues + comprehensive safety |

---

## Design Decisions

### Why `auto` vs `var`?
- **`auto`**: Type inference with locking - safer, catches bugs
- **`var`**: Dynamic typing - flexible, JavaScript-like

### Why Allow int<->float Implicit Conversion?
- Common in game/graphics programming
- Matches C++ behavior
- Explicit casts would be tedious

### Why Not Fix Logical Operators?
- JavaScript compatibility may be desired
- Some users expect value-returning behavior
- **Decision needed:** Add `--strict-types` flag?

---

## Files Modified

- `Source/JaiScript/source/tests/semantics/type_safety_tests.cpp` - NEW
- `Source/JaiScript/TYPE_SAFETY_IMPROVEMENTS.md` - This file
- `Source/JaiScript/source/implementation/interpreter_dispatch.cpp` - Pending fixes
- `Source/JaiScript/source/implementation/interpreter.cpp` - Pending fixes

---

## References

- Type system architecture: `include/jaiscript/core/type_info.hpp`
- Value storage: `include/jaiscript/core/value.hpp`
- Operator dispatch: `source/implementation/interpreter_dispatch.cpp`
- Conversion logic: `source/implementation/interpreter.cpp`