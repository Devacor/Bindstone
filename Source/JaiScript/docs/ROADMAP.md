# JaiScript Roadmap

## Type System Gaps

### High Priority

#### shared_ptr<T> Auto-Unwrap Assignment (COMPLETED)
**Current**: `shared_ptr<T>` doesn't delegate assignment to T's operator=
**Expected**: Value-like RHS should auto-unwrap to underlying object
```javascript
class Counter {
    int value = 0;
    auto operator=(int v) { value = v; }
}

auto a = shared_ptr<Counter>(5);
auto b = a;
a = 10;           // Should call Counter::operator=(int)
print(b.value);   // Should be 10 (shared mutation)
```

**Detection Rules**:
- `null` → Pointer op: nullify
- `shared_ptr<U>` where U : T → Pointer op: reassign
- Same type T → Value op: copy contents
- Different type → Value op: call operator= if exists

See TYPE_SYSTEM_DESIGN.md "Assignment Semantics" section for full specification.

#### Map Key Homogeneity
**Current**: Map keys can be mixed types with `auto`
**Expected**: `auto` maps should enforce homogeneous keys like arrays
```javascript
auto m = {{"a", 1}, {2, 3}};  // Should error: mixed key types
```

#### Smart Pointer Type Validation
**Current**: `weak_ptr<T>` and `shared_ptr<T>` don't validate T at runtime
**Expected**: Type parameter should be checked on assignment
```javascript
weak_ptr<Creature> ref = OtherClass();  // Should error
```

#### Deduced Type Tracking for array<auto>
**Current**: `array<auto>` validates homogeneity but doesn't track the deduced type
**Expected**: After declaration, type should be locked for push/insert operations
```javascript
array<auto> nums = [1, 2, 3];  // Deduced: array<int>
nums.push("x");                 // Should error: string into int array
```

### Medium Priority

#### Const Support
**Current**: No const keyword
**Expected**: Immutable declarations
```javascript
const int x = 5;
const array<int> nums = [1, 2, 3];
```

#### Explicit Truncation Warning/Error Option
**Current**: `int x = 3.14` silently truncates
**Option**: Configurable strictness level or require explicit cast

### Lower Priority

#### Union Types
```javascript
int | null maybe;
string | int flexible;
```

#### Type Narrowing
```javascript
if (x is int) {
    // x known as int in this scope
}
```

#### Generic Functions
```javascript
function<T> identity(T x) -> T { return x; }
```

## Standard Library Additions

### Box<T> Utility Type
**Purpose**: Enable shared mutation of primitives via `shared_ptr<Box<T>>`
```javascript
class Box<T> {
    T value;
    Box(T v) { value = v; }
    auto operator=(T v) { value = v; return this; }
}

auto x = shared_ptr<Box<int>>(5);
auto y = x;
y = 10;           // Box<int>::operator=(int)
print(x.value);   // 10 (shared mutation)
```

**Requirements**:
- Serialization support (serialize as wrapped value)
- All comparison operators delegating to T
- `weak_ptr<Box<T>>` works naturally

## Serialization System

### Pointer Deduplication
- Track `shared_ptr` instances across serialization
- Handle circular references
- Maintain object identity through save/load

### Binary Format
- Compact binary serialization option
- Versioning for schema evolution
- Zero-copy deserialization where possible

## Hot Reload Extensions

### Function State Preservation
- Maintain local variables during reload
- Preserve call stack
- Update function bodies in-place

## VM Backend

### Exception Handling
**Current**: try/catch/throw works in interpreter only
**Needed**: VM opcodes and compilation support

### Reference Semantics
**Design**: See VM_REFERENCE_SEMANTICS_DESIGN.md
- MAKE_REFERENCE, LOAD_REFERENCE, STORE_REFERENCE opcodes
- Lvalue/rvalue context detection in compiler

## Tooling

### Language Server Protocol (LSP)
- Autocomplete
- Go to definition
- Error diagnostics

### Syntax Highlighting
- VS Code extension
- Sublime Text package

### Debugger
- Breakpoints
- Step through
- Variable inspection

## Test Coverage Gaps

### Container Operations After Declaration
```javascript
// Test: push to array<auto> after type deduction
array<auto> nums = [1, 2];
nums.push(3);      // Should work
nums.push("x");    // Should error
```

### Map Key Type Validation
```javascript
// Test: mixed key types in auto map
auto m = {{"a", 1}, {2, 3}};  // Document expected behavior
```

### Smart Pointer Assignment Validation
```javascript
// Test: wrong type to typed weak_ptr
weak_ptr<A> ref;
ref = B();  // Document expected behavior
```

### Nested Container Modification
```javascript
// Test: modifying nested containers preserves type constraints
auto grid = [[1, 2], [3, 4]];
grid[0].push("x");  // Should error if homogeneity tracked
```

## Documentation Needs

- [ ] API reference for stdlib functions
- [ ] C++ integration cookbook with common patterns
- [ ] Migration guide from ChaiScript
- [ ] Performance tuning guide

## Priority Order

1. **Type System Correctness**
   - Map key validation
   - Smart pointer type params
   - array<auto> deduced type tracking

2. **Serialization** (enables save games)
   - Pointer deduplication
   - Binary format

3. **VM Completion** (enables performance)
   - Exception handling
   - Reference semantics

4. **Tooling** (enables adoption)
   - LSP
   - Syntax highlighting
