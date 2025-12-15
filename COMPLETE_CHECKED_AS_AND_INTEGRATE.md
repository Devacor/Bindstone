# Complete checked_as<T>() Implementation and Try/Catch Removal

## Current Status

### What Was Completed
1. ✅ Created `bound_cpp_vector<T>` wrapper for zero-copy C++ vector access
2. ✅ Updated property getter in `class_builder.hpp` to detect `std::vector<T>` and wrap in `bound_cpp_vector`
3. ✅ Registered `bound_cpp_vector` with array methods including operator[]
4. ✅ Added operator[] method lookup to interpreter (checks instance methods before global operator)
5. ✅ Created initial `checked_as<T>()` method in `script_value` class
6. ✅ Updated operator[] handling in interpreter to use `checked_as<std::shared_ptr<class_instance>>()`

### Current Implementation Location

**File: `d:/git/Bindstone/Source/JaiScript/include/jaiscript/core/value.hpp`**
- Lines 825-863: `checked_as<T>()` method
- Currently implements checked version for `std::shared_ptr<class_instance>`
- Falls back to try/catch wrapper for other types (marked with TODO)

**File: `d:/git/Bindstone/Source/JaiScript/source/implementation/interpreter.cpp`**
- Lines 1908-1909: Uses `checked_as<std::shared_ptr<class_instance>>()` for operator[]
- This is the ONLY place currently using `checked_as` - everything else still uses `as<T>()` with try/catch

## What Needs To Be Done

### Phase 1: Complete checked_as<T>() Implementation

Implement checked versions for all types currently handled by `as<T>()`. Reference the existing `as<T>()` implementation (lines 402-822 in value.hpp) and convert each branch:

#### Types to Implement:
1. **Primitive types** (already optimized in as<T>):
   - `script_int`
   - `script_float` / `double`
   - `script_bool`
   - `script_char`
   - `script_string` / `std::string`

2. **Container types**:
   - `std::vector<script_value>` (script_array)
   - `std::map<script_value, script_value>` (script_map)
   - `bound_array<T>`
   - `bound_map<K, V>`

3. **Object types**:
   - `std::shared_ptr<class_instance>` ✅ (already done)
   - Other `std::shared_ptr<T>` types
   - Raw object pointers via custom converters

4. **Special types**:
   - `script_function`
   - `script_value` (self-reference)

#### Implementation Pattern:
```cpp
template<typename T>
checked_result<T> checked_as() const {
    if constexpr (std::is_same_v<T, SpecificType>) {
        // Check type
        if (type() != expected_type) {
            return checked_result<T>::error(
                make_error_code(runtime_error_code::type_mismatch),
                "Expected SpecificType but got " + type_name()
            );
        }

        // Extract value safely
        return checked_result<T>::success(/* extracted value */);
    }
    else if constexpr (...) {
        // Next type
    }
}
```

### Phase 2: Remove Try/Catch from Codebase

Search for all uses of `as<T>()` in the codebase and replace with `checked_as<T>()`.

#### Known Locations (from grep analysis):

**interpreter.cpp** - Multiple uses:
```bash
cd d:/git/Bindstone/Source/JaiScript
grep -n "\.as<" source/implementation/interpreter.cpp
```

Expected locations:
- Line 1909: ✅ Already converted to `checked_as`
- Line 5458: `cpp_obj.as<std::shared_ptr<class_instance>>()`
- Line 5640: `args[0].as<std::shared_ptr<class_instance>>()`
- Line 5654: `args[0].as<std::shared_ptr<class_instance>>()`
- Line 5684: `args[0].as<std::shared_ptr<class_instance>>()`
- Line 5698: `args[0].as<std::shared_ptr<class_instance>>()`

**Other files to check**:
- `engine.cpp` / `engine.hpp`
- `function_binder.hpp`
- `class_builder.hpp`
- Any test files using `as<T>()`

#### Conversion Pattern:

**Before (with try/catch):**
```cpp
try {
    auto instance = value.as<std::shared_ptr<class_instance>>();
    // use instance
} catch (...) {
    // handle error
}
```

**After (with checked_result):**
```cpp
auto instance_result = value.checked_as<std::shared_ptr<class_instance>>();
if (!instance_result) {
    return checked_result<void>(instance_result.error(), instance_result.message());
}
auto instance = instance_result.value();
// use instance
```

**Or using JAISCRIPT_TRY macro if available:**
```cpp
auto instance = JAISCRIPT_TRY(value.checked_as<std::shared_ptr<class_instance>>());
// use instance
```

### Phase 3: Testing

After implementation, verify all existing tests pass:

```bash
d:/git/Bindstone/Source/JaiScript/out/build/x64-Debug/bin/jaiscript_tests.exe
```

Pay special attention to:
1. **Container Property Tests** - Should now pass with `bound_cpp_vector` support
2. **Type conversion tests** - Ensure error handling is correct
3. **Operator overload tests** - Verify operator[] works on custom types

Expected to pass after completion:
- `basic_vector_access`
- `cpp_populate_script_access`
- `script_populate_and_access`

## Implementation Details

### File Structure

```
d:/git/Bindstone/Source/JaiScript/
├── include/jaiscript/core/
│   ├── value.hpp                          # Contains checked_as<T>() - NEEDS EXPANSION
│   ├── bound_cpp_vector.hpp               # ✅ Complete
│   ├── bound_cpp_vector_registration.hpp  # Helper (not currently used)
│   └── class_builder.hpp                  # ✅ Has vector detection and registration
└── source/implementation/
    └── interpreter.cpp                     # Has operator[] support - NEEDS MORE CONVERSIONS
```

### Key Code References

#### checked_as<T>() Template (value.hpp:825-863)
Current implementation supports:
- `std::shared_ptr<class_instance>` - fully implemented
- Everything else - wrapped in try/catch (NEEDS IMPLEMENTATION)

#### operator[] Support (interpreter.cpp:1905-1943)
- Lines 1908-1922: Checks for instance method `"[]"` using `checked_as`
- Lines 1924-1937: Falls back to global operator `"[]"`
- Lines 1939-1940: Returns error if neither found

#### bound_cpp_vector Registration (class_builder.hpp:1740-1762)
Auto-registers when property of type `std::vector<T>` is added:
- Detects vector properties via `is_specialization_v<P, std::vector>`
- Creates `bound_cpp_vector<element_type>` wrapper
- Registers methods: size, empty, clear, push/push_back, pop/pop_back, front, back, at, operator[]

### Error Codes to Use

When implementing `checked_as<T>()`, use these error codes from `runtime_error_code`:
- `type_mismatch` - When type doesn't match expected
- `invalid_operation` - When operation not supported for type
- `null_reference` - When dereferencing null/invalid reference

### Testing Checklist

- [ ] All primitive type conversions work with `checked_as`
- [ ] Container type conversions work with `checked_as`
- [ ] Object type conversions work with `checked_as`
- [ ] Error messages are clear and helpful
- [ ] No try/catch blocks remain in interpreter.cpp (except for external library calls)
- [ ] No try/catch blocks remain in engine.cpp
- [ ] All existing tests pass
- [ ] Container property tests pass
- [ ] Performance is not significantly impacted

## Migration Strategy

### Recommended Order:

1. **Implement all checked_as<T>() specializations** (value.hpp)
   - Start with primitive types (int, float, bool, string)
   - Then containers (array, map)
   - Then objects (shared_ptr variants)
   - Test each batch as you go

2. **Convert interpreter.cpp** (highest impact)
   - Convert all `.as<>()` calls to `.checked_as<>()`
   - Remove all try/catch blocks related to type conversion
   - Ensure error propagation uses checked_result

3. **Convert other implementation files**
   - engine.cpp
   - class_builder.cpp (if any runtime uses)
   - Other source files

4. **Audit and test**
   - Run full test suite
   - Check for any remaining try/catch related to type conversion
   - Profile to ensure no performance regression

## Notes

- The `checked_as<T>()` implementation should be in the header (template)
- Keep error messages consistent with existing `as<T>()` errors
- Consider adding `checked_as<T>()` overload for non-const (mutable) access if needed
- The TODO comment in value.hpp (line 847) marks where expansion is needed
- Reference the existing `as<T>()` implementation for type checking logic

## Questions to Consider

1. Should `checked_as<T>()` support the same reference type handling as `as<T>()`?
2. Do we need a non-const version of `checked_as<T>()`?
3. Should conversion registry fallback also use checked_result?
4. Can we deprecate `as<T>()` entirely once `checked_as<T>()` is complete?

## Success Criteria

✅ Implementation is complete when:
1. All type conversions supported by `as<T>()` are supported by `checked_as<T>()`
2. No try/catch blocks remain for type conversion (only for external libraries)
3. All tests pass
4. Error handling is consistent and uses checked_result throughout
5. Performance is comparable to previous implementation
