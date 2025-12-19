# JaiScript Type System Design

## Philosophy

JaiScript aims to provide **C++ value semantics** with **scripting flexibility**. The core principle is: assignments copy values (like C++), but you can opt into reference semantics when needed.

This differs from most scripting languages (JavaScript, Python, Lua) where objects are references by default. JaiScript's approach eliminates an entire class of aliasing bugs while maintaining familiar C++ mental models.

## Type Declaration Spectrum

```
More Static ◄─────────────────────────────────────► More Dynamic

int x = 5;     auto x = 5;     var x = 5;
array<int>     array<auto>     array<var>
│              │               │
│              │               └─ Any type, any time
│              └─ Type inferred, then locked
└─ Explicit type, enforced
```

### `auto` - Type Inference with Locking

```javascript
auto x = 5;           // Inferred as int, locked
x = 10;               // OK
x = "hello";          // ERROR: type mismatch

auto nums = [1, 2, 3];      // Homogeneous required
auto mixed = [1, "two"];    // ERROR: elements must match
```

**Strength**: Catches type errors early while reducing boilerplate.

### `var` - Dynamic Typing

```javascript
var x = 5;            // Any type
x = "hello";          // OK
x = [1, 2, 3];        // OK

var mixed = [1, "two", 3.14];  // OK: heterogeneous allowed
```

**Strength**: Escape hatch for truly dynamic scenarios (JSON parsing, configuration, prototyping).

## Value Semantics

### Deep Copy by Default

```javascript
auto a = [1, 2, 3];
auto b = a;           // Deep copy
b.push(4);            // Only b is modified
// a == [1, 2, 3], b == [1, 2, 3, 4]
```

**Strength**: No accidental aliasing. Reasoning about code is simpler.

**Trade-off**: Memory/performance cost for large containers. Mitigated by explicit references when needed.

### Explicit References with `&`

```javascript
void modify(array<int>& arr) {
    arr.push(42);     // Modifies original
}

auto nums = [1, 2, 3];
modify(nums);         // nums is now [1, 2, 3, 42]
```

**Strength**: Reference semantics are opt-in and explicit.

## Smart Pointer System

### Value vs Reference Semantics

```
┌─────────────────────────────────────────────────────────────────┐
│  VALUE SEMANTICS (default)                                      │
│  auto obj = MyClass();                                          │
│                                                                 │
│  auto a = obj;  // DEEP COPY - a and obj are independent        │
│  a.x = 10;      // Only affects a                               │
└─────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────┐
│  REFERENCE SEMANTICS (explicit shared_ptr)                      │
│  auto obj = shared_ptr<MyClass>();  // Args forwarded to ctor   │
│                                                                 │
│  auto a = obj;  // SHARED - a and obj point to same object      │
│  a.x = 10;      // Affects both a and obj                       │
└─────────────────────────────────────────────────────────────────┘
```

## Assignment Semantics

JaiScript distinguishes between **pointer operations** and **value operations** based on the right-hand side type. This applies to both regular objects and `shared_ptr<T>`.

### Regular Object Assignment

For value-semantic objects, assignment behavior depends on type compatibility:

```javascript
class Vector2 {
    float x = 0;
    float y = 0;
    Vector2(float x, float y) { this.x = x; this.y = y; }

    // Cross-type assignment operator
    auto operator=(float scalar) {
        x = scalar;
        y = scalar;
    }
}

auto v = Vector2(1, 2);

// Same type: deep copy (value semantics)
v = Vector2(3, 4);      // v is now a copy of new Vector2

// Different type with operator=: calls operator=
v = 5.0;                // Calls Vector2::operator=(float)
                        // v.x = 5.0, v.y = 5.0

// Different type without operator=: type error
v = "hello";            // ERROR: no operator=(string) defined
```

**Rule for regular objects**:
1. **Same type** → Deep copy (value semantics)
2. **Different type + operator= exists** → Call `operator=(T)`
3. **Different type + no operator=** → Type error

### shared_ptr<T> Assignment (Auto-Unwrap Semantics)

`shared_ptr<T>` is a **transparent wrapper**. Field access and method calls automatically delegate to the underlying object. Assignment follows the same pattern with special handling:

```
┌─────────────────────────────────────────────────────────────────┐
│  C++ ANALOGY                                                    │
│                                                                 │
│  std::shared_ptr<T> a = ...;                                    │
│  a = other_ptr;        // Reassign pointer                      │
│  *a = value;           // Call T::operator= (dereference)       │
│                                                                 │
│  JaiScript has no * operator, so we infer from RHS type:        │
│  - RHS is pointer-like → pointer operation                      │
│  - RHS is value-like   → auto-unwrap (*a = value)               │
└─────────────────────────────────────────────────────────────────┘
```

**Pointer-like RHS** (reassigns the shared_ptr itself):
```javascript
shared_ptr<Base> a = shared_ptr<Base>(5);
shared_ptr<Derived> b = shared_ptr<Derived>(10);

a = null;               // Nullify pointer
a = b;                  // Polymorphic assignment (Derived → Base)
a = shared_ptr<Base>(); // Same-type shared_ptr reassignment
```

**Value-like RHS** (auto-unwrap, delegate to underlying object):
```javascript
shared_ptr<Vector2> v = shared_ptr<Vector2>(1, 2);
shared_ptr<Vector2> w = v;  // w shares with v

v = 5.0;                // Auto-unwrap: (*v) = 5.0
                        // Calls Vector2::operator=(float)
                        // w.x is also 5.0 (shared mutation!)

v = Vector2(3, 4);      // Same underlying type: copy contents
                        // Equivalent to: (*v) = Vector2(3, 4)
                        // w also sees the change
```

### Detection Logic

The interpreter determines operation type based on RHS:

| RHS Type | Operation | Equivalent C++ |
|----------|-----------|----------------|
| `null` | Pointer: nullify | `a = nullptr` |
| `shared_ptr<U>` where U : T | Pointer: reassign | `a = other_ptr` |
| Same type T | Value: copy contents | `*a = T(...)` |
| Different type with operator= | Value: call operator= | `*a = value` |
| Different type, no operator= | Error | Error |

**C++ Bound Types**: C++ bound types support auto-unwrap when `"="` method is registered:

```cpp
class_builder<Counter>(eng, "Counter")
    .method("=", [](Counter& self, int v) { self.value = v; })
    .build();

// Now in script:
auto ptr = shared_ptr<Counter>(0);
ptr = 42;  // Calls the registered "=" method
```

### Complete Example

```javascript
class Counter {
    int value = 0;
    Counter(int v) { value = v; }

    auto operator=(int v) { value = v; }
    auto increment() { value = value + 1; }
}

// Create shared references
auto a = shared_ptr<Counter>(0);
auto b = a;                     // b shares with a

// Value operation: auto-unwrap
a = 10;                         // Calls Counter::operator=(int)
print(b.value);                 // 10 (shared mutation)

// Method calls: also shared
a.increment();
print(b.value);                 // 11

// Pointer operation: reassignment
a = shared_ptr<Counter>(100);   // a now points to NEW object
print(a.value);                 // 100
print(b.value);                 // 11 (b still points to old)

// Pointer operation: polymorphic
class SpecialCounter : Counter {
    SpecialCounter(int v) : Counter(v) {}
}
shared_ptr<Counter> c = shared_ptr<SpecialCounter>(50);  // OK: Derived → Base
```

### Box<T> Utility Pattern

For primitives that need shared mutation, use `Box<T>`:

```javascript
class Box<T> {
    T value;
    Box(T v) { value = v; }

    auto operator=(T v) { value = v; return this; }
    auto operator==(T other) { return value == other; }
    auto operator==(Box<T> other) { return value == other.value; }
}

// Shared primitive
auto x = shared_ptr<Box<int>>(5);
auto y = x;

y = 10;                 // Auto-unwrap: Box<int>::operator=(int)
print(x.value);         // 10 (shared mutation!)

// Works with weak_ptr
weak_ptr<Box<int>> weak = x;
auto locked = weak.lock();
if (locked != null) {
    locked = 20;        // Mutation through locked reference
    print(x.value);     // 20
}
```

### Why This Design?

1. **Transparent wrapper**: `shared_ptr<T>` should feel like using T directly
2. **No dereference syntax**: JaiScript is simpler without `*` and `->`
3. **Type-based inference**: RHS type clearly indicates intent
4. **C++ mental model**: Matches what C++ developers expect from smart pointers

### How It Works Internally

All objects are stored as `shared_ptr<object_holder>` internally (for memory management), but:
- **Default objects** have VALUE semantics - they clone on assignment
- **`shared_ptr<T>` marked objects** have REFERENCE semantics - no clone on assignment

```javascript
// Value type - clones on assignment
auto a = MyClass();
auto b = a;           // b is a deep copy
b.value = 99;         // Only b changes

// Reference type - shares on assignment
// shared_ptr<T>(args...) forwards args to T's constructor (like C++ make_shared)
auto a = shared_ptr<MyClass>();  // Calls MyClass()
auto b = a;           // b points to same object
b.value = 99;         // Both a and b see the change
```

### Creating `shared_ptr<T>` Objects

`shared_ptr<T>(args...)` forwards arguments directly to T's constructor (like C++ `std::make_shared`):

```javascript
class Player {
    string name;
    int level;
    Player(string n, int l) { name = n; level = l; }
}

// Arguments forwarded to Player(string, int)
auto p = shared_ptr<Player>("Hero", 42);

// No arguments - calls default constructor
auto obj = shared_ptr<MyClass>();

// Multiple shared_ptr variables share the same object
auto a = shared_ptr<Player>("Shared", 1);
auto b = a;           // b points to same Player
a.level = 99;         // Both a and b see the change
```

**Note**: You cannot wrap an existing variable in `shared_ptr`. The arguments are always forwarded to the constructor:
```javascript
auto val = Player("Original", 1);
auto ptr = shared_ptr<Player>(val);  // ERROR: passes 'val' as constructor arg
                                      // Player(Player) doesn't exist!
```

### `weak_ptr<T>` for Non-Owning References

`weak_ptr` ONLY accepts `shared_ptr`-marked objects (not value types):

```javascript
// CORRECT: weak_ptr from shared_ptr
auto obj = shared_ptr<Entity>();  // Args forwarded to Entity()
weak_ptr<Entity> weak = obj;

// ERROR: weak_ptr from value type
auto val = Entity();
weak_ptr<Entity> weak = val;  // Runtime error!
```

Why? Because `weak_ptr` needs reference counting to know when the object dies. Value-semantic objects are copied, so there's no single "object" to observe.

```javascript
class TreeNode {
    weak_ptr<TreeNode> parent;  // Won't prevent parent deletion
    array<shared_ptr<TreeNode>> children;

    void set_parent(shared_ptr<TreeNode> p) {
        parent = p;  // Store weak reference
    }

    auto get_parent_name() {
        auto p = parent.lock();
        if (p != null) {
            return p.name;
        }
        return "no parent";
    }
}
```

**Strength**: Breaks reference cycles, enables parent-child patterns without leaks.

### Current Gap: Type Parameter `<T>` Not Validated

```javascript
weak_ptr<Creature> ref;
auto obj = shared_ptr<OtherClass>();  // Args forwarded to OtherClass()
ref = obj;  // Currently allowed - should it check T?
```

**Decision needed**: Should `weak_ptr<T>` and `shared_ptr<T>` enforce that the stored object is actually of type `T` or a subclass?

## Container Type Safety

### Validated at Declaration

```javascript
auto homogeneous = [1, 2, 3];        // OK: all ints
auto heterogeneous = [1, "two"];     // ERROR at declaration

array<int> typed = [1, 2, 3];        // OK: all ints
array<int> bad = [1, "two"];         // ERROR: string not int
```

### Recursive Validation

Nested containers are validated to arbitrary depth:

```javascript
auto grid = [[1, 2], [3, 4]];              // OK
auto bad = [[1, 2], [3, "x"]];             // ERROR: inner mismatch

auto nested = {{"a", {{"x", 1}}}};         // OK
auto bad = {{"a", {{"x", 1}, {"y", "z"}}}}; // ERROR: inner map mixed
```

**Strength**: Catches structure errors immediately, not when accessed later.

### Current Gap: Map Key Homogeneity

```javascript
auto m = {{"a", 1}, {2, 3}};  // Mixed string/int keys - allowed?
```

**Current behavior**: Not validated. Keys can be mixed types.

**Decision needed**: Should `auto` maps enforce homogeneous keys?

## Numeric Conversions

### C++ Compatible Behavior

```javascript
int x = 3.14;    // Truncates to 3 (no warning)
float y = 5;     // Widens to 5.0

auto a = 5;      // int
a = 3.14;        // ERROR: float not assignable to int (type locked)

var b = 5;       // any
b = 3.14;        // OK: var allows any type
```

**Strength**: Consistent with C++ expectations.

**Trade-off**: Silent truncation can hide bugs. Consider: explicit `int(3.14)` cast requirement?

## Known Gaps and Future Considerations

### 1. Const Support (Not Implemented)

```javascript
// Desired:
const int x = 5;
x = 10;           // ERROR: const

const array<int> nums = [1, 2, 3];
nums.push(4);     // ERROR: mutating const
```

**Impact**: No way to express immutability intent.

### 2. Smart Pointer Type Parameters

```javascript
// Current: Type parameter not enforced at runtime
weak_ptr<Creature> ref = SomethingElse();  // Compiles

// Desired: Runtime type check
weak_ptr<Creature> ref = SomethingElse();  // ERROR: type mismatch
```

### 3. Union/Optional Types (Not Implemented)

```javascript
// Desired:
int | null maybe_int;
string | int flexible;
```

**Current workaround**: Use `var` for flexible types.

### 4. Type Narrowing in Control Flow

```javascript
var x = get_something();
if (x is int) {
    // x should be known as int here
    x + 5;  // Currently requires explicit cast
}
```

### 5. Generic Functions (Not Implemented)

```javascript
// Desired:
function<T> identity(T x) -> T { return x; }
```

**Current workaround**: Use `var` parameters with runtime type handling.

## Design Principles Summary

| Principle | Implementation |
|-----------|----------------|
| Values copy by default | Deep copy on assignment |
| References are explicit | `&` parameter syntax |
| Type inference with safety | `auto` locks inferred type |
| Dynamic escape hatch | `var` allows any type |
| Container homogeneity | Validated at declaration for `auto` |
| C++ numeric semantics | Implicit widening, truncation |
| Ownership clarity | `shared_ptr` owns, `weak_ptr` observes |

## Error Handling Philosophy

Type errors should fail fast and clearly:

```
TypeError: Array element type mismatch at index 1: expected int, got string
  at line 5: auto arr = [1, "two"];

TypeError: Cannot assign string to variable 'x' of type int
  at line 8: x = "hello";
```

**Principle**: Better to fail at declaration/assignment than at use site.

## Performance Implications

| Pattern | Performance | Notes |
|---------|-------------|-------|
| `int i` in loop | Fast | No type dispatch |
| `auto i = 0` in loop | Fast | Type locked after inference |
| `var i = 0` in loop | Slower | Type check every operation |
| `auto arr = [...]` | One-time validation | Recursive check at declaration |
| `var arr = [...]` | No validation | Fastest declaration |

**Guidance**: Use explicit types or `auto` for hot paths. Reserve `var` for truly dynamic scenarios.
